#include "ShellTool.h"

#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>

#include <memory>

namespace poptools {

namespace {
// 采集上限：边读边计数，累计达上限后只排空管道不再缓存——不再全量
// 缓冲后截断（Get-Content huge.log 曾无界吃内存）。结果侧仍按 16KB/8KB
// 截断回传（LLM 上下文体量约束）
constexpr qint64 kMaxCaptureBytes = 2 * 1024 * 1024;

// 排空指定通道进 buf（循环读，单块 64KB）；超限后继续排空但不缓存
//（管道滞留会卡死子进程）
void drainChannel(QProcess *proc, QProcess::ProcessChannel channel, QByteArray *buf,
                  bool *cut, qint64 cap)
{
    proc->setReadChannel(channel);
    forever {
        const QByteArray chunk = proc->read(64 * 1024);
        if (chunk.isEmpty())
            break;
        if (buf->size() >= cap) {
            *cut = true;
            continue;
        }
        buf->append(chunk.left(cap - buf->size()));
        if (buf->size() >= cap)
            *cut = true;
    }
}
} // namespace

ToolDef makeShellRunTool()
{
    ToolDef d;
    d.name = QStringLiteral("shell.run");
    d.readOnly = false;
    d.description = QStringLiteral(
        "在 Windows PowerShell 中执行命令并返回输出。适合：查询系统信息、管理文件、"
        "运行脚本等电脑整理与排查任务。默认在用户主目录执行，超时默认 30 秒。"
        "该工具可实际修改系统——调用前先向用户说明将要做什么；宁可多次小步执行，"
        "不要一次跑长命令链。");
    d.parameters = QJsonObject{
        {"type", QStringLiteral("object")},
        {"properties",
         QJsonObject{
             {"command",
              QJsonObject{{"type", QStringLiteral("string")},
                          {"description", QStringLiteral("PowerShell 命令内容（不含 powershell.exe 本身）")}}},
             {"timeout_ms",
              QJsonObject{{"type", QStringLiteral("integer")},
                          {"description", QStringLiteral("超时毫秒，默认 30000，上限 120000")}}},
         }},
        {"required", QJsonArray{QStringLiteral("command")}},
    };
    d.fn = [](const QJsonObject &args, const std::function<void(QJsonObject)> &done) {
        const QString command = args.value(QStringLiteral("command")).toString();
        if (command.trimmed().isEmpty()) {
            done(QJsonObject{{"ok", false}, {"error", QStringLiteral("命令为空")}});
            return;
        }
        const int timeout = qBound(1000, args.value(QStringLiteral("timeout_ms")).toInt(30000), 120000);

        // 双路回调只允许一次（errorOccurred 与 finished 可能先后都来）
        auto fired = std::make_shared<bool>(false);
        auto once = [fired, done](QJsonObject r) {
            if (!*fired) {
                *fired = true;
                done(std::move(r));
            }
        };

        // 流式采集缓冲与截断标记（生命周期跨 readyRead/finished，用 shared_ptr 挂住）
        auto outBuf = std::make_shared<QByteArray>();
        auto errBuf = std::make_shared<QByteArray>();
        auto outCut = std::make_shared<bool>(false);
        auto errCut = std::make_shared<bool>(false);

        auto *proc = new QProcess(); // 无父对象：完成后 deleteLater 自清
        proc->setWorkingDirectory(QDir::homePath());
        // 输出编码：控制台代码页不可控（中文 Windows 常为 GBK），先在会话内
        // 强制 UTF-8 再解码，避免中文输出乱码
        const QString fullCmd = QStringLiteral(
            "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ")
            + command;
        // 边读边计数：readyRead 循环排空，累计达 2MB 上限即停止缓存
        QObject::connect(proc, &QProcess::readyReadStandardOutput, proc,
                         [proc, outBuf, outCut] {
                             drainChannel(proc, QProcess::StandardOutput, outBuf.get(),
                                          outCut.get(), kMaxCaptureBytes);
                         });
        QObject::connect(proc, &QProcess::readyReadStandardError, proc,
                         [proc, errBuf, errCut] {
                             drainChannel(proc, QProcess::StandardError, errBuf.get(),
                                          errCut.get(), kMaxCaptureBytes);
                         });
        QObject::connect(proc, &QProcess::finished, proc,
                         [proc, once, outBuf, errBuf, outCut, errCut](int exitCode,
                                                                      QProcess::ExitStatus st) {
                             // 收尾再排一次：进程终止后管道内的残余数据可能晚于
                             // 最后一次 readyRead 到达
                             drainChannel(proc, QProcess::StandardOutput, outBuf.get(),
                                          outCut.get(), kMaxCaptureBytes);
                             drainChannel(proc, QProcess::StandardError, errBuf.get(),
                                          errCut.get(), kMaxCaptureBytes);
                             QJsonObject r;
                             const QString out = QString::fromUtf8(*outBuf);
                             const QString err = QString::fromUtf8(*errBuf);
                             r.insert(QStringLiteral("ok"),
                                      st == QProcess::NormalExit && exitCode == 0);
                             r.insert(QStringLiteral("exitCode"), exitCode);
                             if (!out.isEmpty())
                                 r.insert(QStringLiteral("output"), out.left(16 * 1024));
                             if (!err.isEmpty())
                                 r.insert(QStringLiteral("stderr"), err.left(8 * 1024));
                             if (*outCut || *errCut)
                                 r.insert(QStringLiteral("note"),
                                          QStringLiteral("输出超过 2MB，超出部分已丢弃"));
                             if (st == QProcess::CrashExit)
                                 r.insert(QStringLiteral("error"),
                                          QStringLiteral("进程被终止（超时或崩溃）"));
                             once(r);
                             proc->deleteLater();
                         });
        QObject::connect(proc, &QProcess::errorOccurred, proc,
                         [once, proc](QProcess::ProcessError e) {
                             if (e == QProcess::FailedToStart)
                                 once(QJsonObject{{"ok", false},
                                                  {"error", QStringLiteral("无法启动 PowerShell 进程")}});
                             proc->deleteLater();
                         });
        auto *timer = new QTimer(proc);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, proc, [proc] { proc->kill(); });
        timer->start(timeout);
        proc->start(QStringLiteral("powershell.exe"),
                    {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                     QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                     QStringLiteral("-Command"), fullCmd});
    };
    return d;
}

} // namespace poptools
