#include "FsTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>

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
} // namespace

QList<ToolDef> makeFsTools()
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
        d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
            QString path = args.value(QStringLiteral("path")).toString();
            if (path.startsWith(QLatin1Char('~')))
                path = QDir::homePath() + path.mid(1);
            QDir dir(path);
            if (!dir.exists()) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("目录不存在：%1").arg(path)}});
                return;
            }
            const QFileInfoList entries =
                dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
            QJsonArray items;
            int total = 0;
            for (const QFileInfo &fi : entries) {
                ++total;
                if (items.size() >= kMaxListEntries)
                    break;
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
            done(r);
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
            QSaveFile f(path); // 原子写：不会出现半截文件
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                done(QJsonObject{{"ok", false},
                                 {"error", QStringLiteral("无法写入：%1").arg(path)}});
                return;
            }
            f.write(content.toUtf8());
            const bool ok = f.commit();
            done(QJsonObject{{"ok", ok},
                             {"bytes", double(content.toUtf8().size())},
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
            if (QFileInfo::exists(dst))
                QFile::remove(dst);
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
        d.description = QStringLiteral("把文件/目录移入回收站（非永久删除）。删除前必须向用户确认目标。");
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
            done(QJsonObject{{"ok", recyclePath(path)},
                             {"note", QStringLiteral("已尝试移入回收站")}});
        };
        tools << d;
    }

    return tools;
}

} // namespace poptools
