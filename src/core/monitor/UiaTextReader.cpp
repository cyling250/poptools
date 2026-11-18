#include "UiaTextReader.h"

#include <QCoreApplication>
#include <QStringList>
#include <QThread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomationclient.h>
#endif

namespace poptools {

// ============================================================
// Worker：COM STA + IUIAutomation 会话常驻 worker 线程
//（SmartRectDetector::Worker 同款模式——P-025 修复后的既定先例）
// ============================================================
class UiaTextReader::Worker : public QObject
{
    Q_OBJECT
public:
    // 必须无 parent：moveToThread 对带 parent 对象静默失败（P-025）
    explicit Worker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

public slots:
    // 串行处理取文请求（排队投递；busy 门卫由调用方持有）
    void read(quintptr expectedTopHwnd)
    {
        // 线程身份运行期验证（P-025 教训：moveToThread 失败无断言，
        // 线程化必须验证"确实在别的线程跑"）
        const bool onWorker = QThread::currentThread() == this->thread()
                              && this->thread()
                                     != QCoreApplication::instance()->thread();
        if (!onWorker) {
            qWarning("UiaTextReader: UIA 调用落入 GUI 线程（moveToThread 失效，P-025）");
            Q_ASSERT(false);
            // T-047-FIX(P3-6)：release 下 Q_ASSERT 失效，必须就此打住——
            // 跨套间调 m_uia 属 UB。回空 Result 让调用方走 OCR 兜底
            emit readFinished(Result{});
            return;
        }
        emit readFinished(doRead(expectedTopHwnd));
    }

    void cleanup()
    {
#ifdef Q_OS_WIN
        // UIA 元素/模式引用均在请求内现取现放（doRead 末尾已 Release），
        // 此处只需在 COM STA 关闭前确认无遗漏引用（marshaled 代理在
        // 公寓关闭后释放属 UB——SmartRectDetector 先例）
        if (m_uia) {
            m_uia->Release();
            m_uia = nullptr;
        }
        if (m_comInit) {
            CoUninitialize();
            m_comInit = false;
        }
#endif
    }

signals:
    void readFinished(const poptools::UiaTextReader::Result &result);

private:
#ifdef Q_OS_WIN
    bool ensureSession()
    {
        // 惰性初始化（幂等）：线程已进入事件循环后才处理请求，STA 建立
        // 于此线程；S_FALSE（重复初始化）同样需要配对 CoUninitialize
        if (!m_comInit && SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            m_comInit = true;
        if (m_comInit && !m_uia) {
            CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                             __uuidof(IUIAutomation), reinterpret_cast<void **>(&m_uia));
        }
        return m_uia != nullptr;
    }

    // 按 BSTR 自带长度转 QString（不依赖 NUL 截断——BSTR 允许内嵌 NUL）
    static QString bstrToQString(BSTR b)
    {
        return QString::fromWCharArray(b, static_cast<int>(SysStringLen(b)));
    }

    // 焦点元素光标附近文本：退化选区取 [caret-kMaxChars, caret]
    QString readNearCaret(IUIAutomationTextPattern *tp, IUIAutomationTextRange *sel) const
    {
        // 选区非退化（用户有选区）→ 直接取选区文本
        int cmp = 0;
        const bool degenerate =
            SUCCEEDED(sel->CompareEndpoints(TextPatternRangeEndpoint_Start, sel,
                                            TextPatternRangeEndpoint_End, &cmp))
            && cmp == 0;
        if (!degenerate) {
            BSTR b = nullptr;
            if (SUCCEEDED(sel->GetText(kMaxChars, &b)) && b) {
                const QString t = bstrToQString(b);
                SysFreeString(b);
                if (!t.isEmpty())
                    return t;
            }
        }
        // 光标退化态：收拢到光标，再把 Start 端点左移 kMaxChars
        IUIAutomationTextRange *doc = nullptr;
        if (FAILED(tp->get_DocumentRange(&doc)) || !doc)
            return QString();
        IUIAutomationTextRange *window = nullptr;
        QString out;
        if (SUCCEEDED(doc->Clone(&window)) && window) {
            window->MoveEndpointByRange(TextPatternRangeEndpoint_Start, sel,
                                        TextPatternRangeEndpoint_Start);
            window->MoveEndpointByRange(TextPatternRangeEndpoint_End, sel,
                                        TextPatternRangeEndpoint_Start);
            int moved = 0;
            window->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                       TextUnit_Character, -kMaxChars, &moved);
            BSTR b = nullptr;
            if (SUCCEEDED(window->GetText(kMaxChars, &b)) && b) {
                out = bstrToQString(b);
                SysFreeString(b);
            }
            window->Release();
        }
        doc->Release();
        return out;
    }

    // TextPattern：GetSelection → GetVisibleRanges 首段
    QString readViaTextPattern(IUIAutomationElement *el)
    {
        IUIAutomationTextPattern *tp = nullptr;
        if (FAILED(el->GetCurrentPatternAs(UIA_TextPatternId,
                                           IID_IUIAutomationTextPattern,
                                           reinterpret_cast<void **>(&tp)))
            || !tp)
            return QString();

        QString text;
        IUIAutomationTextRangeArray *sel = nullptr;
        if (SUCCEEDED(tp->GetSelection(&sel)) && sel) {
            int n = 0;
            sel->get_Length(&n);
            if (n > 0) {
                IUIAutomationTextRange *r = nullptr;
                if (SUCCEEDED(sel->GetElement(0, &r)) && r) {
                    text = readNearCaret(tp, r);
                    r->Release();
                }
            }
            sel->Release();
        }
        if (text.isEmpty()) {
            // 选区拿不到（查看器/只读文档等）→ 首个可见范围
            IUIAutomationTextRangeArray *vis = nullptr;
            if (SUCCEEDED(tp->GetVisibleRanges(&vis)) && vis) {
                int n = 0;
                vis->get_Length(&n);
                if (n > 0) {
                    IUIAutomationTextRange *r = nullptr;
                    if (SUCCEEDED(vis->GetElement(0, &r)) && r) {
                        BSTR b = nullptr;
                        if (SUCCEEDED(r->GetText(kMaxChars, &b)) && b) {
                            text = bstrToQString(b);
                            SysFreeString(b);
                        }
                        r->Release();
                    }
                }
                vis->Release();
            }
        }
        tp->Release();
        return text;
    }

    // ValuePattern 兜底：整值（限长取尾部——最新输入在尾部，与
    // 滑动窗口 diff 语义一致）
    QString readViaValuePattern(IUIAutomationElement *el)
    {
        IUIAutomationValuePattern *vp = nullptr;
        if (FAILED(el->GetCurrentPatternAs(UIA_ValuePatternId,
                                           IID_IUIAutomationValuePattern,
                                           reinterpret_cast<void **>(&vp)))
            || !vp)
            return QString();
        BSTR b = nullptr;
        QString text;
        if (SUCCEEDED(vp->get_CurrentValue(&b)) && b) {
            text = bstrToQString(b);
            SysFreeString(b);
        }
        vp->Release();
        return text;
    }

    Result doRead(quintptr expectedTopHwnd)
    {
        Result res;
        if (!ensureSession())
            return res; // COM/UIA 不可用：ok=false → 调用方回落 OCR

        // 进程对号：焦点元素必须属于触发时刻的前台窗口所在进程——
        // 浏览器/Electron 内嵌控件的 NativeWindowHandle 常为 0，进程号
        // 是唯一可靠的"读的就是目标窗口"校验；换窗后到达的读请求在此
        // 被拒（结果作废，调用方回落 OCR 而不是串窗口 diff）
        DWORD fgPid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(expectedTopHwnd), &fgPid);
        if (!fgPid)
            return res;

        IUIAutomationElement *el = nullptr;
        if (FAILED(m_uia->GetFocusedElement(&el)) || !el)
            return res;

        // 焦点元素进程号经 UIA_ProcessIdPropertyId 读取（元素无直接
        // getter；VT_I4）
        DWORD elPid = 0;
        VARIANT pv;
        VariantInit(&pv);
        if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_ProcessIdPropertyId, &pv))) {
            if (pv.vt == VT_I4 && pv.lVal > 0)
                elPid = static_cast<DWORD>(pv.lVal);
            VariantClear(&pv);
        }
        if (!elPid || elPid != fgPid) {
            el->Release();
            return res;
        }
        // T-047-FIX(P3-1)：进程对号通过后把焦点元素 PID 带回调用方——
        // 浏览器/Electron 场景焦点元素 NativeWindowHandle 常为 0（topHwnd
        // 不可知），elPid 是管线侧防串窗的权威校验依据
        res.elPid = elPid;

        // 密码框精确判定优先：是密码 → 特殊结果，调用方整链静默
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_IsPasswordPropertyId, &v))) {
            if (v.vt == VT_BOOL && v.boolVal)
                res.password = true;
            VariantClear(&v);
        }

        UIA_HWND h = nullptr;
        el->get_CurrentNativeWindowHandle(&h);
        res.elHwnd = reinterpret_cast<quintptr>(h);
        // GetAncestor 可能返回 NULL（窗口已亡）→ topHwnd=0，语义同"不可知"
        res.topHwnd = h ? reinterpret_cast<quintptr>(GetAncestor(
                              reinterpret_cast<HWND>(h), GA_ROOT))
                        : 0;

        // T-066 文本镜像：RuntimeId（SAFEARRAY of LONG，调用方负责销毁）
        // 转点分十进制串——字段身份键；密码结果同样带回（调用方重置影子
        // 需要同一把键）
        SAFEARRAY *psa = nullptr;
        if (SUCCEEDED(el->GetRuntimeId(&psa)) && psa) {
            LONG lb = 0, ub = 0;
            if (SUCCEEDED(SafeArrayGetLBound(psa, 1, &lb))
                && SUCCEEDED(SafeArrayGetUBound(psa, 1, &ub))) {
                QStringList parts;
                for (LONG i = lb; i <= ub; ++i) {
                    LONG v = 0;
                    if (SUCCEEDED(SafeArrayGetElement(psa, &i, &v)))
                        parts << QString::number(v);
                }
                res.runtimeId = parts.join(QLatin1Char('.'));
            }
            SafeArrayDestroy(psa);
        }

        if (res.password) {
            el->Release();
            return res;
        }

        QString text = readViaTextPattern(el);
        if (text.isEmpty())
            text = readViaValuePattern(el);
        el->Release();

        text = UiaTextReader::normalize(text);
        if (!text.isEmpty()) {
            res.ok = true;
            res.text = text;
        }
        return res;
    }

    bool m_comInit = false;
    IUIAutomation *m_uia = nullptr; // 会话常驻；元素/模式按请求现取现放
#else
    Result doRead(quintptr) { return Result{}; }
#endif
};

UiaTextReader::UiaTextReader(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<poptools::UiaTextReader::Result>(
        "poptools::UiaTextReader::Result");
    // 无 parent：moveToThread 对带 parent 对象静默失败（P-025），
    // 整套 COM/UIA 取文会滞留 GUI 线程执行
    m_worker = new Worker();
    // T-047-FIX(P2-2)：QThread 改堆上无 parent——析构等待超时线程仍在跑
    // 时，值成员会在对象析构里连带销毁运行中的 QThread（崩溃）；改为
    // 泄漏自处。finished → deleteLater：正常退出路径由线程对象自清理
    m_thread = new QThread();
    m_worker->moveToThread(m_thread);
    // COM/UIA 会话在首个请求内惰性建立（避免 quit-before-exec 竞态吞掉
    // 启动任务——P-033），线程退出时 cleanup 关闭
    connect(m_thread, &QThread::finished, m_worker, &Worker::cleanup);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_worker, &Worker::readFinished, this, &UiaTextReader::readFinished);
    m_thread->start();
}

UiaTextReader::~UiaTextReader()
{
    m_thread->quit();
    // T-047-FIX(P2-2)：join 有上限（原 wait() 无限等）。worker 卡在跨进程
    // UIA COM 调用上时（目标进程恰在 IsHungAppWindow 预检后挂起），无限
    // join 会把退出整体挂死。超时即放弃 join 并泄漏 worker/QThread——
    // 绝不 delete 运行中的 QThread；cleanup（含 CoUninitialize）仍挂在
    // finished 上，线程真正结束时执行
    if (m_thread->wait(2000)) {
        delete m_worker; // 线程已停：cleanup 已执行完，无并发访问
    } else {
        qWarning("UiaTextReader: worker 线程 2s 未退出（UIA COM 被挂起进程阻塞），"
                 "放弃 join（有意泄漏）");
    }
    m_worker = nullptr;  // 正常路径已 delete；超时路径有意泄漏
    m_thread = nullptr;  // deleteLater 自清理或泄漏
}

void UiaTextReader::readAsync(quintptr expectedTopHwnd)
{
#ifdef Q_OS_WIN
    Worker *w = m_worker;
    QMetaObject::invokeMethod(
        w, [w, expectedTopHwnd] { w->read(expectedTopHwnd); }, Qt::QueuedConnection);
#else
    Q_UNUSED(expectedTopHwnd);
#endif
}

QString UiaTextReader::normalize(const QString &raw)
{
    if (raw.isEmpty())
        return raw;
    QString s = raw;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    s.remove(QChar(0xFFFC)); // 嵌入对象的替换符（按钮/图片占位）
    // 尾部对齐换行不影响 diff，去掉避免与 OCR 基线行为不一致
    while (s.endsWith(QLatin1Char('\n')))
        s.chop(1);
    if (s.size() > kMaxChars)
        s = s.right(kMaxChars);
    return s;
}

} // namespace poptools

#include "UiaTextReader.moc"
