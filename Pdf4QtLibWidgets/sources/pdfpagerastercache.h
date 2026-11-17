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

#ifndef PDFPAGERASTERCACHE_H
#define PDFPAGERASTERCACHE_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfrenderer.h"
#include "pdfpainter.h"

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>

#include <atomic>
#include <deque>
#include <map>
#include <memory>

namespace pdf
{

/// Cache of the rasterized pages for the page view. The pages are rasterized
/// by a background thread (using all cores, see PDFPrecompiledPage::drawParallel)
/// at the current zoom, and the page view then only copies the images onto the
/// screen. This keeps the page view responsive on heavy plan sheets, where the
/// replay of a precompiled page can take seconds: scrolling, panning and the
/// tools are fluent, because they do not draw the page content again.
///
/// When the zoom changes, the image of the previous zoom is drawn scaled as a
/// preview, until the sharp image at the new zoom is ready.
///
/// All functions except the worker thread are called from the main thread.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAsynchronousPageRasterCache : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAsynchronousPageRasterCache(QObject* parent);
    virtual ~PDFAsynchronousPageRasterCache() override;

    using PageConstPointer = std::shared_ptr<const PDFPrecompiledPage>;

    /// Parameters of drawing of a single page
    struct PageDrawParameters
    {
        PDFInteger pageIndex = -1;
        PageConstPointer page;              ///< Compiled page
        QRectF cropBox;                     ///< Crop box of the page
        QTransform pageToLocal;             ///< Maps page points to the logical pixels of the placed page (placed page starts at (0, 0))
        QSize pageSize;                     ///< Logical size of the placed page
        QPointF pageTopLeft;                ///< Top left corner of the placed page in the logical coordinates of the painter (world matrix is identity)
        QRect visibleRegion;                ///< Visible part of the placed page, in the logical pixels of the placed page
        qreal devicePixelRatio = 1.0;       ///< Device pixel ratio of the painter
        PDFRenderer::Features features;     ///< Features of the renderer
        QColor paperColor;                  ///< Color of the paper
    };

    /// Draws the page from the cache. If the cache does not contain an image of the page
    /// for the current parameters, the image is requested from the background thread and
    /// a preview (image of the page for other parameters, for example for a previous zoom)
    /// is drawn, if it exists. Signal \p imageReady is emitted, when the requested image
    /// is ready. Painter must have identity world matrix.
    /// \param painter Painter
    /// \param parameters Parameters of the drawn page
    /// \returns true, if something has been drawn (image or preview)
    bool drawPage(QPainter* painter, const PageDrawParameters& parameters);

    /// Removes images of all pages, which are not in the list of pages (list must be sorted)
    void retainPages(const std::vector<PDFInteger>& pages);

    /// Returns true, if an image of a page is being rasterized
    bool hasPendingRequests() const { return !m_pending.empty(); }

    /// Removes all images and cancels all pending requests
    void clear();

    /// Returns true, if the painter can use this cache (raster device, no scaling/rotation)
    static bool isPainterSupported(QPainter* painter);

    /// Maximal number of physical pixels of the image of a whole page. Larger pages are
    /// rasterized only in the neighbourhood of the visible area.
    static constexpr qint64 MAXIMAL_FULL_PAGE_PIXELS = 48LL * 1024 * 1024;

    /// Maximal number of physical pixels of the image of a region of a page
    static constexpr qint64 MAXIMAL_REGION_PIXELS = 24LL * 1024 * 1024;

signals:
    void imageReady();

    /// Image of the whole page has been rasterized
    /// \param pageIndex Page index
    /// \param image Image of the page
    /// \param pageToImage Maps page points to the pixels of the image
    void pageRasterized(pdf::PDFInteger pageIndex, const QImage& image, const QTransform& pageToImage);

private:
    using CancelFlag = std::shared_ptr<std::atomic_bool>;

    struct Key
    {
        bool isSameGeometry(const Key& other) const;

        QTransform pageToLocal;
        qreal devicePixelRatio = 1.0;
        PDFRenderer::Features features;
        QColor paperColor;
    };

    struct Task
    {
        quint64 id = 0;
        PDFInteger pageIndex = -1;
        PageConstPointer page;
        QRectF cropBox;
        Key key;
        QRect region;                       ///< Rasterized region in the logical pixels of the placed page
        bool isWholePage = false;           ///< Region is the whole page
        CancelFlag cancelFlag;
    };

    struct Entry
    {
        std::weak_ptr<const PDFPrecompiledPage> page;
        Key key;
        QRect region;
        QTransform pageToImage;             ///< Maps page points to the pixels of the image
        QImage image;                       ///< Image in the physical pixels (device pixel ratio is 1)
    };

    static QTransform createPageToImageMatrix(const Key& key, const QRect& region);
    QRect getRequestedRegion(const PageDrawParameters& parameters) const;
    void request(const PageDrawParameters& parameters, const Key& key);
    void runWorker();
    void onTaskFinished(quint64 taskId, PDFInteger pageIndex, QImage image);

    QThread* m_thread = nullptr;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_stopRequested = false;
    std::deque<Task> m_queue;               ///< Protected by the mutex
    quint64 m_nextTaskId = 1;

    std::map<PDFInteger, Task> m_pending;   ///< Requested tasks (main thread only)
    std::map<PDFInteger, Entry> m_entries;  ///< Rasterized pages (main thread only)
};

}   // namespace pdf

#endif // PDFPAGERASTERCACHE_H
