#include "OcrService.h"
#include "Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QDateTime>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcessEnvironment>

OcrService::OcrService(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::finished,
            this, &OcrService::onFinished);
}

bool OcrService::available() const
{
    return QFileInfo::exists(Settings::instance().tesseractPath());
}

void OcrService::recognize(const QImage &image)
{
    if (busy() || image.isNull())
        return;

    m_lastImage = image;

    // 将图片保存为临时 PNG，供 tesseract 读取
    QTemporaryDir dir;
    if (!dir.isValid()) {
        emit ocrFinished(false, QString(), "无法创建临时目录");
        return;
    }
    const QString imgPath = dir.filePath("input.png");
    if (!image.save(imgPath, "PNG")) {
        emit ocrFinished(false, QString(), "图片保存失败");
        return;
    }

    const QString tessPath = Settings::instance().tesseractPath();
    if (!QFileInfo::exists(tessPath)) {
        emit ocrFinished(false, QString(),
            QString("未找到 tesseract: %1").arg(tessPath));
        return;
    }

    // tesseract input.png stdout -l chi_sim+eng
    QStringList args;
    args << imgPath << "stdout"
         << "-l" << "chi_sim+eng"
         << "--psm" << "6";

    // 优先使用 exe 旁 tessdata（项目自带中文语言包）；否则用 tesseract 默认 tessdata
    const QString bundledTessdata =
        QCoreApplication::applicationDirPath() + "/tessdata";
    if (QFileInfo::exists(bundledTessdata)) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("TESSDATA_PREFIX", bundledTessdata);
        m_process.setProcessEnvironment(env);
    }

    m_process.start(tessPath, args);
}

void OcrService::onFinished(int exitCode, QProcess::ExitStatus status)
{
    const QString text = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();

    bool ok = (status == QProcess::NormalExit && exitCode == 0 && !text.isEmpty());
    if (!ok && err.isEmpty())
        err = QString("tesseract 退出码 %1").arg(exitCode);

    emit ocrFinished(ok, text, ok ? QString() : err);
}
