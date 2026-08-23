#include "OcrService.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>

#include <windows.h>

namespace snipaste
{

    // ---- tesseract 目录 ----
    QString tesseractDir()
    {
        return QCoreApplication::applicationDirPath() + "/tesseract";
    }

    // ---- libtesseract C API 不透明类型 ----
    typedef struct TessBaseAPI TessBaseAPI;
    typedef struct TessResultIterator TessResultIterator;
    typedef struct TessPageIterator TessPageIterator;

    // ---- 常量 ----
    constexpr int OEM_LSTM_ONLY = 1;
    constexpr int PSM_SINGLE_BLOCK = 6;
    constexpr int RIL_TEXTLINE = 2;
    constexpr float kMinConfidence = 30.0f;

    // ---- TessApi 定义（与头文件前向声明配套） ----
    struct TessApi
    {
        HMODULE dll = nullptr;

        TessBaseAPI *(*Create)() = nullptr;
        void (*Delete)(TessBaseAPI *) = nullptr;
        int (*Init2)(TessBaseAPI *, const char *, const char *, int) = nullptr;
        void (*SetImage)(TessBaseAPI *, const unsigned char *, int, int, int, int) = nullptr;
        void (*SetPageSegMode)(TessBaseAPI *, int) = nullptr;
        int (*Recognize)(TessBaseAPI *, void *) = nullptr;
        char *(*GetUTF8Text)(TessBaseAPI *) = nullptr;
        void (*DeleteText)(const char *) = nullptr;
        TessResultIterator *(*GetIterator)(TessBaseAPI *) = nullptr;
        TessPageIterator *(*ResultIteratorPageIterator)(TessResultIterator *) = nullptr;
        int (*ResultIteratorNext)(TessResultIterator *, int) = nullptr;
        char *(*ResultIteratorGetUTF8Text)(TessResultIterator *, int) = nullptr;
        float (*ResultIteratorConfidence)(TessResultIterator *, int) = nullptr;
        int (*PageIteratorBoundingBox)(TessPageIterator *, int,
                                       int *, int *, int *, int *) = nullptr;

        bool load(const QString &dir)
        {
            // 临时将 DLL 搜索目录设为 tesseract 目录（仅影响 LoadLibrary 调用）
            // 保存旧目录并在加载后恢复，避免影响进程中其他 DLL 加载行为
            WCHAR oldDir[MAX_PATH] = {};
            DWORD oldLen = GetDllDirectoryW(MAX_PATH, oldDir);
            SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
            const QString dllPath = dir + QStringLiteral("/libtesseract-5.dll");
            dll = LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath.utf16()));
            // 恢复旧的 DLL 搜索目录
            SetDllDirectoryW(oldLen > 0 ? oldDir : nullptr);
            if (!dll)
                return false;

            Create = (TessBaseAPI *(*)())GetProcAddress(dll, "TessBaseAPICreate");
            Delete = (void (*)(TessBaseAPI *))GetProcAddress(dll, "TessBaseAPIDelete");
            Init2 = (int (*)(TessBaseAPI *, const char *, const char *, int))GetProcAddress(
                dll, "TessBaseAPIInit2");
            SetImage = (void (*)(TessBaseAPI *, const unsigned char *, int, int, int, int))GetProcAddress(
                dll, "TessBaseAPISetImage");
            SetPageSegMode = (void (*)(TessBaseAPI *, int))GetProcAddress(
                dll, "TessBaseAPISetPageSegMode");
            Recognize = (int (*)(TessBaseAPI *, void *))GetProcAddress(dll, "TessBaseAPIRecognize");
            GetUTF8Text = (char *(*)(TessBaseAPI *))GetProcAddress(dll, "TessBaseAPIGetUTF8Text");
            DeleteText = (void (*)(const char *))GetProcAddress(dll, "TessDeleteText");
            GetIterator = (TessResultIterator *(*)(TessBaseAPI *))GetProcAddress(
                dll, "TessBaseAPIGetIterator");
            ResultIteratorPageIterator = (TessPageIterator *(*)(TessResultIterator *))GetProcAddress(
                dll, "TessResultIteratorGetPageIterator");
            ResultIteratorNext = (int (*)(TessResultIterator *, int))GetProcAddress(
                dll, "TessResultIteratorNext");
            ResultIteratorGetUTF8Text = (char *(*)(TessResultIterator *, int))GetProcAddress(
                dll, "TessResultIteratorGetUTF8Text");
            ResultIteratorConfidence = (float (*)(TessResultIterator *, int))GetProcAddress(
                dll, "TessResultIteratorConfidence");
            PageIteratorBoundingBox = (int (*)(TessPageIterator *, int,
                                               int *, int *, int *, int *))GetProcAddress(
                dll, "TessPageIteratorBoundingBox");
            return dll && Create && Delete && Init2 && SetImage && SetPageSegMode
                   && Recognize && GetUTF8Text && DeleteText && GetIterator
                   && ResultIteratorPageIterator && ResultIteratorNext
                   && ResultIteratorGetUTF8Text && ResultIteratorConfidence
                   && PageIteratorBoundingBox;
        }

        void unload()
        {
            if (dll)
            {
                FreeLibrary(dll);
                dll = nullptr;
            }
        }
    };

    // ---- OcrService 实现 ----

    OcrService::OcrService(QObject *parent)
        : QObject(parent)
    {
    }

    OcrService::~OcrService()
    {
        m_future.waitForFinished();
    }

    bool OcrService::available() const
    {
        const QString dir = tesseractDir();
        return QFileInfo::exists(dir + QStringLiteral("/libtesseract-5.dll"))
               && (QFileInfo::exists(dir + QStringLiteral("/tessdata/chi_sim.traineddata"))
                   || QFileInfo::exists(dir + QStringLiteral("/tessdata/eng.traineddata")));
    }

    TessApi *OcrService::ensureLoaded()
    {
        if (!m_tess)
        {
            auto t = std::make_unique<TessApi>();
            if (t->load(tesseractDir()))
                m_tess = std::move(t);
        }
        return m_tess.get();
    }

    void OcrService::recognize(const QImage &image)
    {
        runInBackground(image, false);
    }

    void OcrService::recognizeBoxes(const QImage &image)
    {
        runInBackground(image, true);
    }

    void OcrService::runInBackground(const QImage &image, bool boxes)
    {
        if (busy() || image.isNull())
            return;
        m_busy.store(true, std::memory_order_relaxed);
        m_future = QtConcurrent::run([this, image, boxes]() {
            // 首次调用时加载 DLL，之后复用
            TessApi *api = ensureLoaded();
            if (!api)
            {
                if (boxes)
                    emit ocrBoxesFinished(false, QVector<OcrBox>(),
                                          QStringLiteral("未找到 OCR 引擎（libtesseract-5.dll）"));
                else
                    emit ocrFinished(false, QString(),
                                     QStringLiteral("未找到 OCR 引擎（libtesseract-5.dll）"));
                m_busy.store(false, std::memory_order_relaxed);
                return;
            }

            // 引擎句柄生命周期：Create → Init → SetImage → Recognize → 取结果 → Delete
            TessBaseAPI *handle = api->Create();
            if (!handle)
            {
                if (boxes)
                    emit ocrBoxesFinished(false, QVector<OcrBox>(),
                                          QStringLiteral("创建 OCR 引擎失败"));
                else
                    emit ocrFinished(false, QString(), QStringLiteral("创建 OCR 引擎失败"));
                m_busy.store(false, std::memory_order_relaxed);
                return;
            }

            const QString dir = tesseractDir();
            const QString tessdata = dir + QStringLiteral("/tessdata");
            const QByteArray tessdata8 = QFileInfo::exists(tessdata)
                                             ? tessdata.toUtf8()
                                             : QByteArray();
            const QByteArray lang8 = "chi_sim+eng";
            const int initRet = api->Init2(handle,
                                           tessdata8.isEmpty() ? nullptr : tessdata8.constData(),
                                           lang8.constData(), OEM_LSTM_ONLY);
            if (initRet != 0)
            {
                api->Delete(handle);
                emit ocrFinished(false, QString(),
                                 QStringLiteral("tesseract 初始化失败（tessdata 语言包缺失？）"));
                m_busy.store(false, std::memory_order_relaxed);
                return;
            }

            const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
            const int bpp = 1;
            const int stride = gray.bytesPerLine();
            api->SetImage(handle, gray.constBits(), gray.width(), gray.height(), bpp, stride);
            api->SetPageSegMode(handle, PSM_SINGLE_BLOCK);
            api->Recognize(handle, nullptr);

            QString error;
            if (boxes)
            {
                QVector<OcrBox> result;
                TessResultIterator *iter = api->GetIterator(handle);
                if (iter)
                {
                    while (api->ResultIteratorNext(iter, RIL_TEXTLINE))
                    {
                        TessPageIterator *pageIter = api->ResultIteratorPageIterator(iter);
                        int left = 0, top = 0, right = 0, bottom = 0;
                        if (api->PageIteratorBoundingBox(pageIter, RIL_TEXTLINE,
                                                         &left, &top, &right, &bottom))
                        {
                            char *utf8 = api->ResultIteratorGetUTF8Text(iter, RIL_TEXTLINE);
                            const float conf = api->ResultIteratorConfidence(iter, RIL_TEXTLINE);
                            if (utf8)
                            {
                                const QString text = QString::fromUtf8(utf8).trimmed();
                                api->DeleteText(utf8);
                                if (!text.isEmpty() && conf >= kMinConfidence)
                                {
                                    OcrBox b;
                                    b.rect = QRect(left, top, right - left, bottom - top);
                                    b.text = text;
                                    b.conf = conf;
                                    result.append(b);
                                }
                            }
                        }
                    }
                }
                if (result.isEmpty())
                    error = QStringLiteral("未识别到文字");
                api->Delete(handle);
                emit ocrBoxesFinished(!result.isEmpty(), result,
                                     result.isEmpty() ? error : QString());
                m_busy.store(false, std::memory_order_relaxed);
                return;
            }

            char *utf8 = api->GetUTF8Text(handle);
            const QString text = utf8 ? QString::fromUtf8(utf8).trimmed() : QString();
            if (utf8)
                api->DeleteText(utf8);
            if (text.isEmpty())
                error = QStringLiteral("未识别到文字");
            api->Delete(handle);
            emit ocrFinished(!text.isEmpty(), text, text.isEmpty() ? error : QString());
            m_busy.store(false, std::memory_order_relaxed);
        });
    }

} // namespace snipaste