#include "FsTool.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace poptools {

namespace {
constexpr int kMaxListEntries = 500;
constexpr int kMaxReadBytes = 64 * 1024;
constexpr int kMaxContentChars = 48 * 1024;

// 移入回收站（比直接删除安全；SHFileOperation 需要双 NUL 结尾的路径列表）
bool recyclePath(const QString &path)
{
#ifdef Q_OS_WIN
    const QString abs = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    std::wstring w = abs.toStdWString();
    w.push_back(L'\0');
    w.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = w.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    const int rc = SHFileOperationW(&op);
    return rc == 0 && !op.fAnyOperationsAborted;
#else
    Q_UNUSED(path);
    return false;
#endif
}

// 回收站可用性预检（按目标所在驱动器，P2-4）：无回收站的卷（U 盘/网络
// 位置等）上 FOF_ALLOWUNDO 会静默退化为永久删除——与工具"非永久删除"
// 的描述相悖，预检失败即拒绝执行。UNC 路径保守拒绝
bool recycleBinAvailable(const QString &path)
{
#ifdef Q_OS_WIN
    const QString abs = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (abs.startsWith(QLatin1String("\\\\")))
        return false;
    const QString root = abs.left(3); // "C:\"
    if (!abs.contains(QLatin1Char(':')) || !root.endsWith(QLatin1Char('\\')))
        return false;
    SHQUERYRBINFO rb = {};
    rb.cbSize = sizeof(rb);
    return SUCCEEDED(SHQueryRecycleBin(reinterpret_cast<const wchar_t *>(root.utf16()), &rb));
#else
    Q_UNUSED(path);
    return false;
#endif
}
} // namespace

QList<ToolDef> makeFsTools(QObject *ctx)
{
    QList<ToolDef> tools;

    {
        ToolDef d;
        d.name = QStringLiteral("fs.list");
        d.readOnly = true;
        d.description = QStringLiteral("列出目录内容（名称/类型/大小/修改时间，最多 500 项）。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"path", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("目录路径（~ 为主目录）")}}}}},
            {"required", QJsonArray{QStringLiteral("path")}},
        };
        d.fn = [ctx](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString path = args.value(QStringLiteral("path")).toString();
            if (path.startsWith(QLatin1Char('~')))
                path = QDir::homePath() + path.mid(1);
            // R6-C1: 枚举挪线程池交付（照 MonitorQueryTool 先例，done 支持异步）。
            // 原实现在 GUI 线程 entryInfoList 全量物化（LLM 列 System32/
            // node_modules 类巨目录会卡死主线程），且 readOnly 免确认放行，
            // 触发门槛极低。枚举改 QDirIterator 有界取样：前 500 项构造
            // QFileInfo 取属性，之后只迭代文件名计数不 stat；entries/total/
            // note 响应字段形状不变（代价：条目顺序由名称排序变为目录序）
            const QPointer<QObject> anchor(ctx);
            // 注：池线程里读 QPointer（if (anchor)）与 GUI 线程析构存在极窄
            // 竞争窗口——前提是锚点为模块级长命对象（GoodyModule 与进程同寿，
            // 析构只在退出瞬间）；invokeMethod 排队后对象若亡，事件投递被 Qt
            // 安全丢弃。保持现状，注释到位防误改
            QThreadPool::globalInstance()->start([path, anchor, done] {
                QDir dir(path);
                if (!dir.exists()) {
                    if (anchor)
                        QMetaObject::invokeMethod(
                            anchor.data(),
                            [done, path] {
                                done(QJsonObject{{"ok", false},
                                                 {"error", QStringLiteral("目录不存在：%1").arg(path)}});
                            },
                            Qt::QueuedConnection);
                    return;
                }
                QJsonArray items;
                int total = 0;
                QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot);
                while (it.hasNext()) {
                    it.next();
                    ++total;
                    if (items.size() >= kMaxListEntries)
                        continue; // 取满后只计数，不再 stat
                    const QFileInfo fi(it.filePath()); // 属性惰性 stat，仅前 500 项发生
                    QJsonObject o{{"name", fi.fileName()},
                                  {"type", fi.isDir() ? QStringLiteral("dir")
                                                      : QStringLiteral("file")}};
                    if (fi.isFile())
                        o.insert("size", double(fi.size()));
                    o.insert("mtime", fi.lastModified().toMSecsSinceEpoch());
                    items.append(o);
                }
                QJsonObject r{{"ok", true}, {"entries", items}, {"total", total}};
                if (total > kMaxListEntries)
                    r.insert("note", QStringLiteral("仅返回前 %1 项").arg(kMaxListEntries));
                // 回主线程交付；锚点（GoodyModule）已销毁则在途结果整体丢弃
                if (anchor)
                    QMetaObject::invokeMethod(anchor.data(), [done, r] { done(r); },
                                              Qt::QueuedConnection);
            });
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("fs.read");
        d.readOnly = true;
        d.description = QStringLiteral(
            "读取文本文件内容（上限 64KB，二进制文件拒绝）。适合查看配置、日志、代码。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"path", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("文件路径")}}}}},
            {"required", QJsonArray{QStringLiteral("path")}},
        };
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString path = args.value(QStringLiteral("path")).toString();
            if (path.startsWith(QLatin1Char('~')))
                path = QDir::homePath() + path.mid(1);
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("无法打开：%1").arg(f.errorString())}});
                return;
            }
            const QByteArray head = f.peek(8192);
            if (head.contains('\0')) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("疑似二进制文件，拒绝读取：%1").arg(path)}});
                return;
            }
            const QByteArray data = f.read(kMaxReadBytes);
            QJsonObject r{{"ok", true},
                          {"content", QString::fromUtf8(data)},
                          {"truncated", !f.atEnd()}};
            done(r);
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("fs.write");
        d.readOnly = false;
        d.description = QStringLiteral(
            "写文本文件（覆盖或追加，原子写入）。写前须向用户说明目标路径与内容概要；"
            "覆盖已有文件尤其要谨慎。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"path", QJsonObject{{"type", QStringLiteral("string")},
                                              {"description", QStringLiteral("文件路径")}}},
                         {"content", QJsonObject{{"type", QStringLiteral("string")},
                                                 {"description", QStringLiteral("写入的文本内容")}}},
                         {"append", QJsonObject{{"type", QStringLiteral("boolean")},
                                                {"description", QStringLiteral("true=追加到末尾，默认覆盖")}}}}},
            {"required", QJsonArray{QStringLiteral("path"), QStringLiteral("content")}},
        };
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString path = args.value(QStringLiteral("path")).toString();
            if (path.startsWith(QLatin1Char('~')))
                path = QDir::homePath() + path.mid(1);
            const QString content = args.value(QStringLiteral("content")).toString();
            const bool append = args.value(QStringLiteral("append")).toBool(false);
            // 目录目标明确拒绝（P2-5）：往下走只会得到误导性的打开失败文案
            if (QFileInfo(path).isDir()) {
                done(QJsonObject{{"ok", false},
                                 {"error",
                                  QStringLiteral("目标是目录，无法作为文件写入：%1").arg(path)}});
                return;
            }
            // 写前确保父目录存在（LLM 常直接给深层新路径）；append 分支同样受益
            QDir().mkpath(QFileInfo(path).absolutePath());
            if (append) {
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                    done(QJsonObject{{"ok", false},
                                     {"error", QStringLiteral("无法打开：%1").arg(f.errorString())}});
                    return;
                }
                const qint64 n = f.write(content.toUtf8());
                done(QJsonObject{{"ok", n >= 0}, {"bytes", double(n)}});
                return;
            }
            // T-062: 一次编码复用——原先 write 与 bytes 计数各调一次 toUtf8
            const QByteArray payload = content.toUtf8();
            QSaveFile f(path); // 原子写：不会出现半截文件
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("无法写入：%1").arg(path)}});
                return;
            }
            f.write(payload);
            const bool ok = f.commit();
            done(QJsonObject{{"ok", ok},
                             {"bytes", double(payload.size())},
                             {"error", ok ? QString() : QStringLiteral("提交失败（目录不可写？）")}});
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("fs.move");
        d.readOnly = false;
        d.description = QStringLiteral("移动/重命名文件或目录；目标已存在时拒绝（可传 overwrite）。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"src", QJsonObject{{"type", QStringLiteral("string")}}},
                         {"dst", QJsonObject{{"type", QStringLiteral("string")}}},
                         {"overwrite", QJsonObject{{"type", QStringLiteral("boolean")}}}}},
            {"required", QJsonArray{QStringLiteral("src"), QStringLiteral("dst")}},
        };
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString src = args.value(QStringLiteral("src")).toString();
            QString dst = args.value(QStringLiteral("dst")).toString();
            if (src.startsWith(QLatin1Char('~')))
                src = QDir::homePath() + src.mid(1);
            if (dst.startsWith(QLatin1Char('~')))
                dst = QDir::homePath() + dst.mid(1);
            if (!QFileInfo::exists(src)) {
                done(QJsonObject{{"ok", false}, {"error", QStringLiteral("源不存在：%1").arg(src)}});
                return;
            }
            if (QFileInfo::exists(dst) && !args.value(QStringLiteral("overwrite")).toBool(false)) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("目标已存在（传 overwrite=true 覆盖）：%1").arg(dst)}});
                return;
            }
            if (QFileInfo::exists(dst)) {
                // 目录目标明确拒绝（P2-5）：remove 对目录恒失败，随后 rename
                // 失败只剩"跨盘/被占用？"的误导文案
                if (QFileInfo(dst).isDir()) {
                    done(QJsonObject{{"ok", false},
                                     {"error",
                                      QStringLiteral("目标是目录，拒绝覆盖：%1").arg(dst)}});
                    return;
                }
                QFile::remove(dst);
            }
            const bool moved = QFile::rename(src, dst);
            done(QJsonObject{{"ok", moved},
                             {"error", moved ? QString()
                                             : QStringLiteral("移动失败（跨盘/被占用？）")}});
        };
        tools << d;
    }

    {
        ToolDef d;
        d.name = QStringLiteral("fs.delete");
        d.readOnly = false;
        d.description = QStringLiteral(
            "把文件/目录移入回收站（非永久删除）。删除前必须向用户确认目标。"
            "所在卷没有可用回收站时拒绝执行（不会退化为永久删除）。");
        d.parameters = QJsonObject{
            {"type", QStringLiteral("object")},
            {"properties",
             QJsonObject{{"path", QJsonObject{{"type", QStringLiteral("string")}}}}},
            {"required", QJsonArray{QStringLiteral("path")}},
        };
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString path = args.value(QStringLiteral("path")).toString();
            if (path.startsWith(QLatin1Char('~')))
                path = QDir::homePath() + path.mid(1);
            if (!QFileInfo::exists(path)) {
                done(QJsonObject{{"ok", false}, {"error", QStringLiteral("不存在：%1").arg(path)}});
                return;
            }
            // 回收站预检（P2-4）：无回收站的卷上 FO_DELETE 会静默永久删除，
            // 与"非永久删除"承诺相悖——拒绝执行并回错误
            if (!recycleBinAvailable(path)) {
                done(QJsonObject{
                    {"ok", false},
                    {"error",
                     QStringLiteral("目标所在卷没有可用回收站，执行将变为永久删除，已拒绝：")
                         + path}});
                return;
            }
            done(QJsonObject{{"ok", recyclePath(path)},
                             {"note", QStringLiteral("已尝试移入回收站")}});
        };
        tools << d;
    }

    return tools;
}

} // namespace poptools
