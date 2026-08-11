#include "OcrService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QUuid>

namespace snipaste
{

    namespace
    {

        // tesseract 统一存放目录（exe 旁的 tesseract\，开发 / 安装同布局）：
        // 由 CMake POST_BUILD 从 tools/tesseract 复制而来，随安装包一起发放
        QString tesseractDir()
        {
            return QCoreApplication::applicationDirPath() + "/tesseract";
        }

        // 判断文本是否包含中日韩字符（决定行文本拼接时是否加空格）
        bool containsCjk(const QString &s)
        {
            for (QChar ch : s)
            {
                const ushort u = ch.unicode();
                if ((u >= 0x3400 && u <= 0x4DBF)     // CJK 扩展 A
                    || (u >= 0x4E00 && u <= 0x9FFF)  // CJK 统一表意
                    || (u >= 0x3000 && u <= 0x30FF)  // 标点 / 假名
                    || (u >= 0xF900 && u <= 0xFAFF)) // CJK 兼容表意
                    return true;
            }
            return false;
        }

        // 行内词拼接：中英混排时 CJK 之间不加空格，ASCII 词之间加空格
        QString joinWords(const QStringList &words)
        {
            QString out;
            for (int i = 0; i < words.size(); ++i)
            {
                if (i > 0)
                {
                    const bool prevCjk = containsCjk(words[i - 1]);
                    const bool curCjk = containsCjk(words[i]);
                    if (!prevCjk && !curCjk)
                        out += QLatin1Char(' ');
                }
                out += words[i];
            }
            return out;
        }

        // 解析 tesseract TSV（stdout）：按 (block, par, line) 把 word 行聚合为行级文本框，
        // 保证"连成一段的连续文字"处于同一个文本框而非逐字拆分。
        QVector<OcrBox> parseOcrTsv(const QByteArray &raw)
        {
            QVector<OcrBox> boxes;
            // Windows 下 tesseract TSV 行尾为 \r\n：先移除 \r，否则 header 末列 "text" 变成
            // "text\r"，indexOf 匹配失败导致解析直接返回空（曾表现为识别结果永远为空）
            const QStringList lines =
                QString::fromUtf8(raw).remove(QLatin1Char('\r')).split(QLatin1Char('\n'));
            if (lines.size() < 2)
                return boxes;

            const QStringList header = lines[0].split(QLatin1Char('\t'));
            const int iLevel = header.indexOf(QLatin1String("level"));
            const int iBlock = header.indexOf(QLatin1String("block_num"));
            const int iPar = header.indexOf(QLatin1String("par_num"));
            const int iLine = header.indexOf(QLatin1String("line_num"));
            const int iLeft = header.indexOf(QLatin1String("left"));
            const int iTop = header.indexOf(QLatin1String("top"));
            const int iW = header.indexOf(QLatin1String("width"));
            const int iH = header.indexOf(QLatin1String("height"));
            const int iConf = header.indexOf(QLatin1String("conf"));
            const int iText = header.indexOf(QLatin1String("text"));
            if (iLevel < 0 || iText < 0 || iBlock < 0 || iLine < 0)
                return boxes;

            struct LineAgg
            {
                QRect rect;
                QStringList words;
                double confSum = 0;
                int confN = 0;
            };
            QMap<QString, LineAgg> groups; // key: "block/par/line"
            QStringList order;             // 保持组出现顺序（TSV 本身即阅读顺序）

            for (int i = 1; i < lines.size(); ++i)
            {
                const QStringList c = lines[i].split(QLatin1Char('\t'));
                if (c.size() <= iText || c[iLevel].trimmed() != QLatin1String("5"))
                    continue; // 仅取 word 级行
                const QString text = c[iText].trimmed();
                const double conf = c[iConf].toDouble();
                if (text.isEmpty() || conf < 30.0)
                    continue; // 跳过空词 / 低置信度词

                const QString key = QStringLiteral("%1/%2/%3")
                                        .arg(c[iBlock], c[iPar], c[iLine]);
                if (!groups.contains(key))
                    order.append(key);
                LineAgg &g = groups[key];
                const QRect w(c[iLeft].toInt(), c[iTop].toInt(), c[iW].toInt(), c[iH].toInt());
                g.rect = g.rect.isNull() ? w : g.rect.united(w);
                g.words << text;
                g.confSum += conf;
                g.confN++;
            }

            for (const QString &key : order)
            {
                const LineAgg &g = groups[key];
                if (g.confN == 0 || g.rect.isNull())
                    continue;
                OcrBox b;
                b.rect = g.rect;
                b.text = joinWords(g.words);
                b.conf = g.confSum / g.confN;
                boxes.append(b);
            }
            return boxes;
        }

    } // namespace

    OcrService::OcrService(QObject *parent)
        : QObject(parent)
    {
        connect(&m_process, &QProcess::finished,
                this, &OcrService::onFinished);
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            cleanupWorkDir();
        });
    }

    void OcrService::cleanupWorkDir()
    {
        if (!m_workDir.isEmpty()) {
            const QString parent = QFileInfo(m_workDir).absolutePath();
            QDir(m_workDir).removeRecursively();
            // 顺手删除空壳父目录（ocr_tmp/），非空（异常残留）则保留
            if (QDir(parent).exists())
                QDir(parent).rmdir(parent);
            m_workDir.clear();
        }
    }

    bool OcrService::available() const
    {
        return QFileInfo::exists(tesseractDir() + "/tesseract.exe");
    }

    void OcrService::recognize(const QImage &image)
    {
        start(image, false);
    }

    void OcrService::recognizeBoxes(const QImage &image)
    {
        start(image, true);
    }

    void OcrService::start(const QImage &image, bool boxes)
    {
        if (busy() || image.isNull())
            return;

        m_lastImage = image;
        m_boxesMode = boxes;

        // OCR 工作目录放在软件目录（exe 旁）而非系统临时目录：
        // ① 此前用 QTemporaryDir 局部变量，start() 返回即被析构删除，而 QProcess 异步运行，
        //    tesseract 读取 input.png 时文件已不存在 → "Can not read input file"；
        // ② 中间文件统一落盘在软件目录，识别结束后（onFinished / errorOccurred）清理。
        const QString base = QCoreApplication::applicationDirPath();
        m_workDir = base + "/ocr_tmp/"
                    + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        if (!QDir().mkpath(m_workDir))
        {
            emit ocrFinished(false, QString(), "无法创建 OCR 工作目录");
            return;
        }
        const QString imgPath = m_workDir + "/input.png";
        if (!image.save(imgPath, "PNG"))
        {
            cleanupWorkDir();
            emit ocrFinished(false, QString(), "图片保存失败");
            return;
        }

        // 引擎固定在 exe 旁统一目录（tesseract\），不允许用户修改
        const QString tessPath = tesseractDir() + "/tesseract.exe";
        if (!QFileInfo::exists(tessPath))
        {
            cleanupWorkDir();
            emit ocrFinished(false, QString(),
                             QString("未找到 tesseract: %1").arg(tessPath));
            return;
        }

        // tesseract input.png stdout -l chi_sim+eng --psm 6 [tsv]
        QStringList args;
        args << imgPath << "stdout"
             << "-l" << "chi_sim+eng"
             << "--psm" << "6";
        if (boxes)
            args << "tsv"; // 输出 TSV（含行级包围盒）

        // tessdata 优先级：统一目录 tessdata > exe 旁 tessdata > tesseract 默认
        const QString appTessdata = tesseractDir() + "/tessdata";
        const QString sidecarTessdata =
            QCoreApplication::applicationDirPath() + "/tessdata";
        const QString tessdata =
            QFileInfo::exists(appTessdata)       ? appTessdata
            : QFileInfo::exists(sidecarTessdata) ? sidecarTessdata
                                                 : QString();
        if (!tessdata.isEmpty())
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("TESSDATA_PREFIX", tessdata);
            m_process.setProcessEnvironment(env);
        }

        m_process.start(tessPath, args);
    }

    void OcrService::onFinished(int exitCode, QProcess::ExitStatus status)
    {
        const bool normal = (status == QProcess::NormalExit && exitCode == 0);

        if (m_boxesMode)
        {
            const QByteArray raw = m_process.readAllStandardOutput();
            QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
            const QVector<OcrBox> boxes = parseOcrTsv(raw);
            const bool ok = normal && !boxes.isEmpty();
            if (!ok && err.isEmpty())
                err = QString("tesseract 退出码 %1").arg(exitCode);
            emit ocrBoxesFinished(ok, boxes, ok ? QString() : err);
            cleanupWorkDir();
            return;
        }

        const QString text = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
        QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();

        const bool ok = normal && !text.isEmpty();
        if (!ok && err.isEmpty())
            err = QString("tesseract 退出码 %1").arg(exitCode);

        emit ocrFinished(ok, text, ok ? QString() : err);
        cleanupWorkDir();
    }

} // namespace snipaste
