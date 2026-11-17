// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#ifndef PDFPAGEPREVIEWSTORE_H
#define PDFPAGEPREVIEWSTORE_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfrenderer.h"
#include "pdfdocument.h"
#include "pdfcms.h"
#include "pdfoperationcontrol.h"
#include "pdfpattern.h"

#include <QObject>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <map>
#include <set>
#include <vector>

namespace pdf
{
class PDFDrawWidgetProxy;

/// Store of the rough images (previews) of the pages of the document. The previews
/// are created by a background thread of the lowest priority for all pages of the
/// document, starting at the pages nearest to the current page. The page view draws
/// a preview, when the page is not compiled or rasterized yet, so a heavy page (for
/// example a plan sheet, which is compiled and rasterized for seconds) is displayed
/// immediately and it is replaced by the sharp image later.
///
/// Previews of an unmodified document are stored also on the disk (in the cache
/// directory of the application), so they are available immediately, when the
/// document is opened again. Encrypted documents and documents without the
/// identifier are never stored on the disk.
///
/// All functions except the worker thread are called from the main thread.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPagePreviewStore : public QObject, private PDFOperationControl
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFPagePreviewStore(PDFDrawWidgetProxy* proxy);
    virtual ~PDFPagePreviewStore() override;

    struct Preview
    {
        bool isValid() const { return !image.isNull(); }

        QImage image;
        QTransform pageToImage;     ///< Maps page points to the pixels of the image
    };

    /// Sets the shared pointer to the current document of the proxy. The worker thread
    /// works with the document for minutes, so the previews are created only for a
    /// document, which is kept alive by this pointer - the owner of a document passed
    /// as a raw pointer can destroy or replace it at any time.
    void setDocumentPointer(PDFDocumentPointer document);

    /// Starts creation of the previews of the current document of the proxy
    /// \param useDiskCache Previews can be stored on the disk (the document is
    ///        not modified and it is displayed with its default settings)
    void start(bool useDiskCache);

    /// Stops creation of the previews
    /// \param clear Remove also the previews from the memory
    void stop(bool clear);

    /// Returns the preview of the page, or invalid preview, if it is not created yet
    Preview getPreview(PDFInteger pageIndex);

    /// Sets the current page, pages nearest to it are created first
    void setCurrentPage(PDFInteger pageIndex);

    /// Adds an image of the whole page, which has been rasterized by the page view. The preview
    /// is created from this image, if it does not exist, so the page is not compiled again.
    /// \param pageIndex Page index
    /// \param image Image of the page
    /// \param pageToImage Maps page points to the pixels of the image
    void addPageImage(PDFInteger pageIndex, const QImage& image, const QTransform& pageToImage);

    /// Pauses creation of the previews. It is paused, while the page view waits for
    /// the displayed pages, so the previews do not take the processor from them.
    void setPaused(bool paused);

    /// Returns true, if the previews are enabled (they can be
    /// disabled by the environment variable PDF4QT_DISABLE_PAGE_PREVIEWS)
    static bool isEnabled();

    /// Length of the longer side of the preview in pixels
    static constexpr int PREVIEW_SIZE = 2048;

    /// Maximal resolution of the preview (pixels per point), for small pages
    static constexpr PDFReal MAXIMAL_PREVIEW_SCALE = 2.0;

    /// Maximal number of the images of the pages waiting for the worker thread
    static constexpr size_t MAXIMAL_PAGE_IMAGE_COUNT = 4;

    /// Maximal size of the previews in the memory (they are compressed)
    static constexpr qint64 MAXIMAL_MEMORY_SIZE = 256LL * 1024 * 1024;

    /// Maximal size of the previews of all documents on the disk
    static constexpr qint64 MAXIMAL_DISK_SIZE = 1024LL * 1024 * 1024;

signals:
    void previewReady(pdf::PDFInteger pageIndex);

private:
    virtual bool isOperationCancelled() const override;

    /// Data of the worker thread, they are constant while the thread is running
    struct Context
    {
        const PDFDocument* document = nullptr;
        PDFFontCache* fontCache = nullptr;
        const PDFICCProfileCache* iccProfileCache = nullptr;
        const PDFOptionalContentActivity* optionalContentActivity = nullptr;
        PDFCMSPointer cms;
        PDFCMSSettings cmsSettings;
        PDFRenderer::Features features;
        PDFMeshQualitySettings meshQualitySettings;
        QColor paperColor;
        QString directory;          ///< Directory with the previews on the disk, empty, if the disk is not used
        bool isDiskWritable = false;    ///< New previews are written to the disk
    };

    enum class PageState : char
    {
        Waiting,
        Ready,
        Failed
    };

    void runWorker();
    void waitWhilePaused() const;
    PDFInteger pickPage();
    PageState createPreview(PDFInteger pageIndex);
    PageState storePreview(PDFInteger pageIndex, const QString& fileName, const QImage& image);
    void processPageImages();
    QString getPreviewFileName(const PDFPage* page, PDFInteger pageIndex) const;
    void pruneDiskCache() const;

    static QTransform createPageToImageMatrix(const PDFPage* page, QSize* imageSize);
    static QString getCacheRootDirectory();
    static QString getDocumentDirectoryName(const Context& context);
    static QString getPageFileName(const PDFDocument* document, const PDFPage* page, PDFInteger pageIndex);

    PDFDrawWidgetProxy* m_proxy;
    PDFDocumentPointer m_documentPointer;
    QThread* m_thread = nullptr;
    Context m_context;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_paused{false};

    mutable QMutex m_mutex;
    mutable QWaitCondition m_waitCondition;
    std::vector<PageState> m_states;                ///< Protected by the mutex
    std::map<PDFInteger, QByteArray> m_previews;    ///< Compressed previews in the memory, protected by the mutex
    qint64 m_memorySize = 0;                        ///< Protected by the mutex
    PDFInteger m_currentPage = 0;                   ///< Protected by the mutex

    struct PageImage
    {
        PDFInteger pageIndex = -1;
        QImage image;
        QTransform pageToImage;
    };

    std::vector<PageImage> m_pageImages;            ///< Images of the pages from the page view, protected by the mutex

    QCache<PDFInteger, Preview> m_decodedPreviews;  ///< Main thread only
    std::set<PDFInteger> m_pagesMissingOnDisk;      ///< Pages, which were not found on the disk, main thread only
};

}   // namespace pdf

#endif // PDFPAGEPREVIEWSTORE_H
