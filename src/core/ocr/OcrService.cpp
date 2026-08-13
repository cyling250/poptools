#include "OcrService.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>

#include <windows.h>

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

        // ---- libtesseract C API（动态加载，声明与本机 DLL 导出一致，见 capi.h）----
        // 仅声明本项目用到的函数；全部为 extern "C" 导出，GetProcAddress 按名字取。
        // 不透明句柄：只以指针使用，不需要完整定义（也避免引入 leptonica 头）
        typedef struct TessBaseAPI TessBaseAPI;
        typedef struct TessResultIterator TessResultIterator;
        typedef struct TessPageIterator TessPageIterator;

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
                // libtesseract 依赖同目录的 libleptonica 等 MSYS2 DLL：
                // 先把目录加入 DLL 搜索路径，再按完整路径加载主 DLL
                SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
                const QString dllPath = dir + QStringLiteral("/libtesseract-5.dll");
                dll = LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath.utf16()));
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

        // capi.h 常量（与本机 libtesseract-5.dll 保持一致）
        constexpr int OEM_LSTM_ONLY = 1;
        constexpr int PSM_SINGLE_BLOCK = 6; // 与原命令行 --psm 6 一致
        constexpr int RIL_TEXTLINE = 2;
        constexpr float kMinConfidence = 30.0f; // 与原 TSV 解析的置信度阈值一致

    } // namespace

    OcrService::OcrService(QObject *parent)
        : QObject(parent)
    {
    }

    OcrService::~OcrService()
    {
        // 等待后台识别线程结束，避免析构后悬垂访问
        m_future.waitForFinished();
    }

    bool OcrService::available() const
    {
        const QString dir = tesseractDir();
        return QFileInfo::exists(dir + QStringLiteral("/libtesseract-5.dll"))
               && (QFileInfo::exists(dir + QStringLiteral("/tessdata/chi_sim.traineddata"))
                   || QFileInfo::exists(dir + QStringLiteral("/tessdata/eng.traineddata")));
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
            const QString dir = tesseractDir();
            const QString dllPath = dir + QStringLiteral("/libtesseract-5.dll");

            TessApi api;
            if (!QFileInfo::exists(dllPath) || !api.load(dir))
            {
                m_busy.store(false, std::memory_order_relaxed);
                emit ocrFinished(false, QString(),
                                 QStringLiteral("未找到 OCR 引擎: %1").arg(dllPath));
                return;
            }

            // 引擎生命周期：Create → Init → SetImage → Recognize → 取结果 → Delete
            TessBaseAPI *handle = api.Create();
            if (!handle)
            {
                m_busy.store(false, std::memory_order_relaxed);
                emit ocrFinished(false, QString(), QStringLiteral("创建 OCR 引擎失败"));
                return;
            }

            const QString tessdata = dir + QStringLiteral("/tessdata");
            const QByteArray tessdata8 = QFileInfo::exists(tessdata)
                                             ? tessdata.toUtf8()
                                             : QByteArray();
            const QByteArray lang8 = "chi_sim+eng";
            // OEM_LSTM_ONLY：仅用 LSTM 引擎，与语言包（tessdata_fast）匹配
            const int initRet = api.Init2(handle,
                                          tessdata8.isEmpty() ? nullptr : tessdata8.constData(),
                                          lang8.constData(), OEM_LSTM_ONLY);
            if (initRet != 0)
            {
                api.Delete(handle);
                m_busy.store(false, std::memory_order_relaxed);
                emit ocrFinished(false, QString(),
                                 QStringLiteral("tesseract 初始化失败（tessdata 语言包缺失？）"));
                return;
            }

            // 灰度图直接喂给引擎（坐标为输入图像像素，与 OcrBox 语义一致）
            const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
            const int bpp = 1; // 8 位灰度
            const int stride = gray.bytesPerLine();
            api.SetImage(handle, gray.constBits(), gray.width(), gray.height(), bpp, stride);
            api.SetPageSegMode(handle, PSM_SINGLE_BLOCK);
            api.Recognize(handle, nullptr);

            QString error;
            if (boxes)
            {
                QVector<OcrBox> result;
                TessResultIterator *iter = api.GetIterator(handle);
                if (iter)
                {
                    // 遍历每个文本行：迭代器初始在最高层级，先 Next 到行级再取内容
                    // （与官方 examples/api_test.cpp 用法一致）
                    while (api.ResultIteratorNext(iter, RIL_TEXTLINE))
                    {
                        TessPageIterator *pageIter = api.ResultIteratorPageIterator(iter);
                        int left = 0, top = 0, right = 0, bottom = 0;
                        if (api.PageIteratorBoundingBox(pageIter, RIL_TEXTLINE,
                                                        &left, &top, &right, &bottom))
                        {
                            char *utf8 = api.ResultIteratorGetUTF8Text(iter, RIL_TEXTLINE);
                            const float conf = api.ResultIteratorConfidence(iter, RIL_TEXTLINE);
                            if (utf8)
                            {
                                const QString text = QString::fromUtf8(utf8).trimmed();
                                api.DeleteText(utf8);
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
                m_busy.store(false, std::memory_order_relaxed);
                api.Delete(handle);
                emit ocrBoxesFinished(!result.isEmpty(), result,
                                     result.isEmpty() ? error : QString());
                return;
            }

            char *utf8 = api.GetUTF8Text(handle);
            const QString text = utf8 ? QString::fromUtf8(utf8).trimmed() : QString();
            if (utf8)
                api.DeleteText(utf8);
            if (text.isEmpty())
                error = QStringLiteral("未识别到文字");
            m_busy.store(false, std::memory_order_relaxed);
            api.Delete(handle);
            emit ocrFinished(!text.isEmpty(), text, text.isEmpty() ? error : QString());
        });
    }

} // namespace snipaste
