#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>

namespace poptools {

// ============================================================
// UIA 精确取文（T-047-UIA）：中文上屏后从焦点控件读"光标附近文本"，
// 供 SnapshotOcrPipeline 以 UIA-first、截图+OCR 兜底的两级取文使用。
//
// 取文顺序：
//   1. TextPattern：GetSelection——选区非退化直接取；光标退化态取
//      [caret-2000, caret]（上屏文字就在光标前）；
//      GetSelection 失败/为空 → GetVisibleRanges 首段；
//   2. ValuePattern：GetValue 兜底（>2000 字符取尾部）；
//   3. 都取不到 → ok=false（调用方回落 OCR）。
// 密码判定：UIA_IsPasswordPropertyId 精确读取（password=true 时调用方
// 整链静默，且结果与 PasswordFieldGuard 共享同一前台窗口密码态缓存）。
//
// 线程纪律（P-025 前车之鉴）：全部 UIA COM 调用在专用 worker QThread
// 中执行（STA + CoInitializeEx/CoUninitialize 配对，UIA 元素引用先于
// CoUninitialize 释放——SmartRectDetector 同款模式）。worker 构造无
// parent（否则 moveToThread 静默失败），read() 内带线程身份运行期验证。
// 已知限制：UIA COM 调用无超时 API，目标进程挂起时可阻塞 worker 线程
// ——GUI 线程只经排队信号交互，永不被阻塞；调用方（管线）负责 busy
// 门卫（在途时新触发直接跳过）与 IsHungAppWindow 预检。
// ============================================================
class UiaTextReader : public QObject
{
    Q_OBJECT
public:
    struct Result
    {
        bool ok = false;        // 拿到非空文本
        bool password = false;  // 焦点控件是密码框（精确判定）
        QString text;           // 光标附近文本（≤2000 字符，行距统一 \n）
        quintptr topHwnd = 0;   // 焦点元素所属顶层窗口（0=不可知；供管线对号）
        quintptr elHwnd = 0;    // 焦点元素原生句柄（常为 0，如浏览器内嵌控件）
        quintptr elPid = 0;     // T-047-FIX(P3-1)：焦点元素进程号（进程对号
                                // 通过后带回；浏览器/Electron 场景 topHwnd
                                // 不可知时的权威防串窗校验依据）
    };

    explicit UiaTextReader(QObject *parent = nullptr);
    ~UiaTextReader() override;

    // 异步读焦点元素（任意线程；worker 串行处理，结果经 readFinished 回）。
    // expectedTopHwnd：触发时刻的前台窗口——worker 内比对焦点元素与该窗口
    // 的进程 ID（浏览器/Electron 内嵌控件无原生句柄，进程对号是唯一可靠
    // 的"读的就是目标窗口"校验），不一致按失败返回（调用方回落 OCR）
    void readAsync(quintptr expectedTopHwnd);

    // 文本规范化（纯函数，单测覆盖）：\r\n / \r → \n，剔除对象替换符
    // \uFFFC；超限取尾部 kMaxChars（保留最新输入）
    static QString normalize(const QString &raw);

    static constexpr int kMaxChars = 2000; // 单次取文限长

signals:
    void readFinished(const poptools::UiaTextReader::Result &result);

private:
    class Worker;

    Worker *m_worker = nullptr; // 无 parent；正常退出显式回收，超时路径泄漏
    QThread *m_thread = nullptr; // T-047-FIX(P2-2)：堆上无 parent，finished→
                                 // deleteLater 自清理；等待超时时整体泄漏
                                 //（绝不 delete 运行中的 QThread）
};

} // namespace poptools

Q_DECLARE_METATYPE(poptools::UiaTextReader::Result)
