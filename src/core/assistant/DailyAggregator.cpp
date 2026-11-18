#include "DailyAggregator.h"

#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace poptools {

namespace {
constexpr int kExcerptChars = 300; // 每窗口键入/OCR 摘录上限
constexpr int kMaxTimelineRows = 120;
constexpr int kMaxGroupRows = 24; // 键入/OCR 分组条数上限

QDateTime tsToDateTime(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms);
}

// 单条键入/OCR 分组
struct TextGroup
{
    QString app, title;
    QStringList chunks; // 各次事件文本
    int totalChars = 0;
    int deletedChars = 0; // 仅文本镜像组使用（T-066：退格删除的原文统计）
};

QString groupExcerpt(const TextGroup &g)
{
    QString joined = g.chunks.join(QStringLiteral(" "));
    if (joined.size() > kExcerptChars)
        joined = joined.left(kExcerptChars) + QStringLiteral("…");
    // 删除统计仅文本镜像组非零（keys/OCR 组恒 0，格式退化为原样）
    const QString del = g.deletedChars > 0
                            ? QStringLiteral("，删%1字").arg(g.deletedChars)
                            : QString();
    // 多参单次 arg：一次替换 %1-%5，键入/OCR 文本里的 %N 不被后续 arg
    // 二次展开（链式 arg 曾把字数注入含 %3 的标题段）
    return QStringLiteral("[%1 — %2] 共%3字%4：%5")
        .arg(g.app, g.title.left(60), QString::number(g.totalChars), del,
             joined.replace(QLatin1Char('\n'), QStringLiteral("⏎")));
}
} // namespace

void DailyAggregator::buildDigestAsync(const QString &dayDir, int maxChars, QObject *ctx,
                                       const std::function<void(bool, QString)> &done)
{
    const QPointer<QObject> anchor(ctx);
    // 注：池线程里读 QPointer（下方 if (anchor)）与 GUI 线程析构存在极窄
    // 竞争窗口——前提是锚点为模块级长命对象（GoodyModule 与进程同寿）；
    // 保持现状，注释到位防误改（对齐 FsTool/MonitorQueryTool 口径）
    QThreadPool::globalInstance()->start([dayDir, maxChars, anchor, done] {
        // ---- timeline.jsonl：合并相邻同窗段 ----
        struct Row
        {
            QString app, title;
            qint64 start = 0, end = 0;
            int activeSec = 0, keystrokes = 0, clicks = 0;
        };
        QVector<Row> rows;
        QFile tf(dayDir + QStringLiteral("/timeline.jsonl"));
        if (tf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!tf.atEnd()) {
                const QJsonObject o =
                    QJsonDocument::fromJson(tf.readLine()).object();
                if (o.isEmpty())
                    continue;
                const QString app = o.value(QStringLiteral("app")).toString();
                const QString title = o.value(QStringLiteral("title")).toString();
                const qint64 start = static_cast<qint64>(
                    o.value(QStringLiteral("start")).toDouble());
                const qint64 end = static_cast<qint64>(o.value(QStringLiteral("end")).toDouble());
                if (!rows.isEmpty() && rows.last().app == app
                    && rows.last().title == title && start - rows.last().end < 90 * 1000) {
                    Row &last = rows.last(); // 90s 内同窗续段合并（切片还原）
                    last.end = qMax(last.end, end);
                    last.activeSec += o.value(QStringLiteral("activeSec")).toInt();
                    last.keystrokes += o.value(QStringLiteral("keystrokes")).toInt();
                    last.clicks += o.value(QStringLiteral("clicks")).toInt();
                } else {
                    rows.append({app, title, start, end,
                                 o.value(QStringLiteral("activeSec")).toInt(),
                                 o.value(QStringLiteral("keystrokes")).toInt(),
                                 o.value(QStringLiteral("clicks")).toInt()});
                }
                if (rows.size() > kMaxTimelineRows * 3)
                    break; // 超大日志防炸内存（后续截断）
            }
        }

        // ---- keys.jsonl / ocrtext.jsonl：按窗口分组摘录 ----
        QVector<TextGroup> keyGroups, ocrGroups;
        auto readGroups = [&dayDir](const QString &stream, QVector<TextGroup> &out) {
            QFile f(dayDir + QLatin1Char('/') + stream + QStringLiteral(".jsonl"));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return;
            while (!f.atEnd()) {
                const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
                const QString text = o.value(QStringLiteral("text")).toString();
                if (text.trimmed().isEmpty())
                    continue;
                const QString app = o.value(QStringLiteral("app")).toString();
                const QString title = o.value(QStringLiteral("title")).toString();
                if (!out.isEmpty() && out.last().app == app && out.last().title == title)
                    out.last().chunks << text;
                else
                    out.append(TextGroup{app, title, QStringList{text}, 0});
                if (out.size() > 200)
                    break;
            }
            for (TextGroup &g : out)
                for (const QString &c : g.chunks)
                    g.totalChars += c.size();
        };
        readGroups(QStringLiteral("keys"), keyGroups);
        readGroups(QStringLiteral("ocrtext"), ocrGroups);

        // ---- textlog.jsonl：文本镜像摘录（T-066，按窗口分组） ----
        // insert/replace 的 text 是最终落进输入框的文本（含 IME 上屏后的
        // 中文），是日志里最接近"用户写了什么"的信号；delete 只知被删原文
        // （removed），计入删除字数、不进正文摘录
        QVector<TextGroup> mirrorGroups;
        {
            QFile mf(dayDir + QStringLiteral("/textlog.jsonl"));
            if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!mf.atEnd()) {
                    const QJsonObject o =
                        QJsonDocument::fromJson(mf.readLine()).object();
                    const QString text = o.value(QStringLiteral("text")).toString();
                    const QString removed =
                        o.value(QStringLiteral("removed")).toString();
                    if (text.isEmpty() && removed.isEmpty())
                        continue;
                    const QString app = o.value(QStringLiteral("app")).toString();
                    const QString title = o.value(QStringLiteral("title")).toString();
                    if (mirrorGroups.isEmpty() || mirrorGroups.last().app != app
                        || mirrorGroups.last().title != title) {
                        if (mirrorGroups.size() > 200)
                            break;
                        mirrorGroups.append(TextGroup{app, title, QStringList{}, 0, 0});
                    }
                    TextGroup &g = mirrorGroups.last();
                    if (!text.isEmpty()) {
                        g.chunks << text;
                        g.totalChars += text.size();
                    }
                    if (!removed.isEmpty())
                        g.deletedChars += removed.size();
                }
            }
        }

        // ---- clipboard.jsonl：复制文本预览按窗口分组（T-047-CLIP） ----
        // 只取 kind=="text" 的条目（files/image 无摘要价值）；head/tail
        // 拼为预览；repeat 行不计（count 已隐含在复制行为里）
        struct ClipGroup
        {
            QString app;
            QStringList previews;
            // T-062: 删死字段 totalChars——输出格式只用 previews.size() 与
            // join 后文本，无任何读点（与 TextGroup::totalChars 区分，后者
            // groupExcerpt 真正使用）
        };
        QVector<ClipGroup> clipGroups;
        {
            QFile cf(dayDir + QStringLiteral("/clipboard.jsonl"));
            if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!cf.atEnd()) {
                    const QJsonObject o = QJsonDocument::fromJson(cf.readLine()).object();
                    if (o.value(QStringLiteral("kind")).toString()
                        != QLatin1String("text"))
                        continue;
                    QString preview =
                        o.value(QStringLiteral("head")).toString();
                    const QString tail = o.value(QStringLiteral("tail")).toString();
                    if (!tail.isEmpty())
                        preview += QStringLiteral("…") + tail;
                    if (preview.trimmed().isEmpty())
                        continue;
                    const QString app = o.value(QStringLiteral("app")).toString();
                    if (!clipGroups.isEmpty() && clipGroups.last().app == app)
                        clipGroups.last().previews << preview;
                    else
                        clipGroups.append(ClipGroup{app, QStringList{preview}});
                    if (clipGroups.size() > 200)
                        break;
                }
            }
        }

        // ---- 组装摘要 ----
        QString digest;
        if (rows.isEmpty() && keyGroups.isEmpty() && ocrGroups.isEmpty()
            && clipGroups.isEmpty() && mirrorGroups.isEmpty()) {
            if (anchor) {
                QMetaObject::invokeMethod(anchor, [done] { done(false, QString()); },
                                          Qt::QueuedConnection);
            }
            return;
        }

        digest += QStringLiteral("【时间线】\n");
        int shown = 0;
        for (const Row &r : rows) {
            if (shown >= kMaxTimelineRows)
                break;
            const QDateTime st = tsToDateTime(r.start);
            const int mins = qMax(0, int((r.end - r.start) / 60000));
            const int activeMin = qMax(0, r.activeSec / 60);
            digest += QStringLiteral("%1–%2 约%3分(活跃%4分 键入%5 点击%6) %7 — %8\n")
                          .arg(st.toString("HH:mm"),
                               tsToDateTime(r.end).toString("HH:mm"))
                          .arg(mins)
                          .arg(activeMin)
                          .arg(r.keystrokes)
                          .arg(r.clicks)
                          .arg(r.app, r.title.left(80));
            ++shown;
        }

        if (!keyGroups.isEmpty()) {
            digest += QStringLiteral("\n【键入摘录】（键盘输入的文本，按窗口）\n");
            for (int i = 0; i < keyGroups.size() && i < kMaxGroupRows; ++i)
                digest += groupExcerpt(keyGroups.at(i)) + QLatin1Char('\n');
        }
        if (!ocrGroups.isEmpty()) {
            digest += QStringLiteral("\n【中文上屏摘录】（OCR 识别的输入法上屏文字）\n");
            for (int i = 0; i < ocrGroups.size() && i < kMaxGroupRows; ++i)
                digest += groupExcerpt(ocrGroups.at(i)) + QLatin1Char('\n');
        }
        if (!mirrorGroups.isEmpty()) {
            digest += QStringLiteral(
                "\n【输入原文摘录】（文本镜像：各输入框最终输入的原文，含删除统计）\n");
            for (int i = 0; i < mirrorGroups.size() && i < kMaxGroupRows; ++i)
                digest += groupExcerpt(mirrorGroups.at(i)) + QLatin1Char('\n');
        }
        if (!clipGroups.isEmpty()) {
            digest += QStringLiteral("\n【剪贴板摘录】（复制的文本预览，按窗口）\n");
            int shownClips = 0;
            for (int i = 0; i < clipGroups.size() && shownClips < kMaxGroupRows; ++i) {
                const ClipGroup &g = clipGroups.at(i);
                QString joined = g.previews.join(QStringLiteral(" "));
                if (joined.size() > kExcerptChars)
                    joined = joined.left(kExcerptChars) + QStringLiteral("…");
                digest += QStringLiteral("[%1] 共%2次复制：%3\n")
                              .arg(g.app, QString::number(g.previews.size()),
                                   joined.replace(QLatin1Char('\n'),
                                                  QStringLiteral("⏎")));
                ++shownClips;
            }
        }

        // 总量截断（保头部：时间线与早期摘录优先）
        if (digest.size() > maxChars)
            digest = digest.left(maxChars) + QStringLiteral("\n…（超出预算已截断）");

        if (anchor) {
            QMetaObject::invokeMethod(anchor, [done, digest] { done(true, digest); },
                                      Qt::QueuedConnection);
        }
    });
}

} // namespace poptools
