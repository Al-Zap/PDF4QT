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

#include "pdfpagerastercache.h"
#include "pdfexecutionpolicy.h"

#include <QPainter>
#include <QtMath>

#include "pdfdbgheap.h"

namespace pdf
{

PDFAsynchronousPageRasterCache::PDFAsynchronousPageRasterCache(QObject* parent) :
    BaseClass(parent)
{
    m_thread = QThread::create([this]() { runWorker(); });
    m_thread->setObjectName(QStringLiteral("PDFAsynchronousPageRasterCache"));
    m_thread->start(QThread::LowPriority);
}

PDFAsynchronousPageRasterCache::~PDFAsynchronousPageRasterCache()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;

        for (Task& task : m_queue)
        {
            task.cancelFlag->store(true, std::memory_order_relaxed);
        }

        m_queue.clear();
        m_waitCondition.wakeAll();
    }

    for (auto& item : m_pending)
    {
        item.second.cancelFlag->store(true, std::memory_order_relaxed);
    }

    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
}

bool PDFAsynchronousPageRasterCache::Key::isSameGeometry(const Key& other) const
{
    return qFuzzyCompare(pageToLocal, other.pageToLocal) &&
           qFuzzyCompare(devicePixelRatio, other.devicePixelRatio) &&
           features == other.features &&
           paperColor == other.paperColor;
}

bool PDFAsynchronousPageRasterCache::isPainterSupported(QPainter* painter)
{
    const QPaintDevice* device = painter->device();
    if (!device || painter->worldTransform().type() != QTransform::TxNone)
    {
        return false;
    }

    switch (device->devType())
    {
        case QInternal::Widget:
        case QInternal::Pixmap:
        case QInternal::Image:
        case QInternal::CustomRaster:
            return true;

        default:
            break;
    }

    return false;
}

QTransform PDFAsynchronousPageRasterCache::createPageToImageMatrix(const Key& key, const QRect& region)
{
    return key.pageToLocal * QTransform::fromTranslate(-region.left(), -region.top()) * QTransform::fromScale(key.devicePixelRatio, key.devicePixelRatio);
}

QTransform PDFAsynchronousPageRasterCache::createUniformScaleMatrix(const QTransform& pageToLocal, const QSize& pageSize)
{
    // Size of the placed page is rounded to whole pixels, so the horizontal scale
    // of the matrix differs slightly from the vertical one. Paint engine of Qt
    // draws thin lines by a fast algorithm only for a uniform scale, otherwise each
    // line is converted to an outline, which is several times slower. Plan sheets
    // consist of thin lines, so the scales are unified here - the page is then
    // smaller than the placed page by less than a pixel.
    const bool isStraight = qFuzzyIsNull(pageToLocal.m12()) && qFuzzyIsNull(pageToLocal.m21());
    const bool isQuarterTurn = qFuzzyIsNull(pageToLocal.m11()) && qFuzzyIsNull(pageToLocal.m22());

    if (pageToLocal.type() > QTransform::TxRotate || (!isStraight && !isQuarterTurn))
    {
        return pageToLocal;
    }

    // Elements, which scale the horizontal and the vertical axis of the image
    const qreal elementX = isStraight ? pageToLocal.m11() : pageToLocal.m21();
    const qreal elementY = isStraight ? pageToLocal.m22() : pageToLocal.m12();
    const qreal scaleX = qAbs(elementX);
    const qreal scaleY = qAbs(elementY);

    if (scaleX == scaleY || qFuzzyIsNull(scaleX) || qFuzzyIsNull(scaleY))
    {
        return pageToLocal;
    }

    const qreal scale = qMin(scaleX, scaleY);
    const qreal factorX = scale / scaleX;
    const qreal factorY = scale / scaleY;

    if (pageSize.width() * (1.0 - factorX) > 1.0 || pageSize.height() * (1.0 - factorY) > 1.0)
    {
        // Scales are really different
        return pageToLocal;
    }

    // Page is shrunk around the center of the placed page
    const qreal centerX = 0.5 * pageSize.width();
    const qreal centerY = 0.5 * pageSize.height();
    const qreal dx = (pageToLocal.dx() - centerX) * factorX + centerX;
    const qreal dy = (pageToLocal.dy() - centerY) * factorY + centerY;
    const qreal uniformX = elementX < 0.0 ? -scale : scale;
    const qreal uniformY = elementY < 0.0 ? -scale : scale;

    if (isStraight)
    {
        return QTransform(uniformX, 0.0, 0.0, uniformY, dx, dy);
    }

    return QTransform(0.0, uniformY, uniformX, 0.0, dx, dy);
}

QRect PDFAsynchronousPageRasterCache::getRequestedRegion(const PageDrawParameters& parameters) const
{
    const QRect fullPage(QPoint(0, 0), parameters.pageSize);
    const qreal pixelRatioSquared = parameters.devicePixelRatio * parameters.devicePixelRatio;
    const qreal fullPagePixels = qreal(fullPage.width()) * qreal(fullPage.height()) * pixelRatioSquared;

    if (fullPagePixels <= MAXIMAL_FULL_PAGE_PIXELS)
    {
        // Whole page - scrolling and panning then never needs a new image
        return fullPage;
    }

    // Visible area with a margin of a half of its size in each direction, but
    // the image is never larger than the limit.
    QRect region = parameters.visibleRegion;
    for (qreal marginFactor : { 0.5, 0.25, 0.1, 0.0 })
    {
        const int marginX = qCeil(parameters.visibleRegion.width() * marginFactor);
        const int marginY = qCeil(parameters.visibleRegion.height() * marginFactor);
        region = parameters.visibleRegion.adjusted(-marginX, -marginY, marginX, marginY).intersected(fullPage);

        if (qreal(region.width()) * qreal(region.height()) * pixelRatioSquared <= MAXIMAL_REGION_PIXELS)
        {
            break;
        }
    }

    return region;
}

bool PDFAsynchronousPageRasterCache::drawPage(QPainter* painter, const PageDrawParameters& parameters)
{
    Q_ASSERT(painter->worldTransform().type() == QTransform::TxNone);

    if (!parameters.page || parameters.visibleRegion.isEmpty())
    {
        return false;
    }

    Key key;
    key.pageToLocal = parameters.pageToLocal;
    key.devicePixelRatio = parameters.devicePixelRatio;
    key.features = parameters.features;
    key.paperColor = parameters.paperColor;

    bool isDrawn = false;
    bool isExact = false;

    auto it = m_entries.find(parameters.pageIndex);
    if (it != m_entries.end())
    {
        const Entry& entry = it->second;
        const bool isSameContent = entry.page.lock() == parameters.page;
        const bool isSameGeometry = entry.key.isSameGeometry(key);

        painter->save();

        if (isSameContent && isSameGeometry && entry.region.contains(parameters.visibleRegion))
        {
            // Exact image - it is copied pixel by pixel. The physical position is rounded,
            // so the image is never resampled (it can be moved by less than a pixel).
            const qreal ratio = parameters.devicePixelRatio;
            const QPointF physicalTopLeft = (parameters.pageTopLeft + QPointF(entry.region.topLeft())) * ratio;
            painter->setWorldTransform(QTransform::fromScale(1.0 / ratio, 1.0 / ratio));
            painter->drawImage(QPoint(qRound(physicalTopLeft.x()), qRound(physicalTopLeft.y())), entry.image);
            isExact = true;
        }
        else
        {
            // Preview - image of the previous zoom (or of the previous version of the page),
            // scaled to the current zoom. It is replaced by the sharp image, when it is ready.
            bool isInvertible = false;
            const QTransform imageToPage = entry.pageToImage.inverted(&isInvertible);

            if (isInvertible)
            {
                const QTransform pageToPainter = parameters.pageToLocal * QTransform::fromTranslate(parameters.pageTopLeft.x(), parameters.pageTopLeft.y());
                painter->setWorldTransform(imageToPage * pageToPainter);
                painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter->drawImage(QPointF(0.0, 0.0), entry.image);
            }
        }

        painter->restore();
        isDrawn = true;
    }

    if (!isExact)
    {
        request(parameters, key);
    }

    return isDrawn;
}

void PDFAsynchronousPageRasterCache::request(const PageDrawParameters& parameters, const Key& key)
{
    auto pendingIt = m_pending.find(parameters.pageIndex);
    if (pendingIt != m_pending.end())
    {
        const Task& pendingTask = pendingIt->second;
        if (pendingTask.page == parameters.page &&
            pendingTask.key.isSameGeometry(key) &&
            pendingTask.region.contains(parameters.visibleRegion))
        {
            // Same image is already being rasterized
            return;
        }

        // Pending task is obsolete, it is cancelled (the worker thread
        // stops drawing of the page, if it is drawing it right now).
        pendingTask.cancelFlag->store(true, std::memory_order_relaxed);
        m_pending.erase(pendingIt);
    }

    Task task;
    task.id = m_nextTaskId++;
    task.pageIndex = parameters.pageIndex;
    task.page = parameters.page;
    task.cropBox = parameters.cropBox;
    task.key = key;
    task.region = getRequestedRegion(parameters);
    task.isWholePage = task.region == QRect(QPoint(0, 0), parameters.pageSize);
    task.cancelFlag = std::make_shared<std::atomic_bool>(false);

    if (task.region.isEmpty())
    {
        return;
    }

    m_pending[task.pageIndex] = task;

    QMutexLocker locker(&m_mutex);

    // Remove cancelled tasks from the queue, they would be skipped anyway
    std::erase_if(m_queue, [](const Task& queuedTask) { return queuedTask.cancelFlag->load(std::memory_order_relaxed); });

    m_queue.push_back(std::move(task));
    m_waitCondition.wakeOne();
}

void PDFAsynchronousPageRasterCache::runWorker()
{
    QMutexLocker locker(&m_mutex);

    while (!m_stopRequested)
    {
        if (m_queue.empty())
        {
            m_waitCondition.wait(&m_mutex);
            continue;
        }

        Task task = std::move(m_queue.front());
        m_queue.pop_front();

        if (task.cancelFlag->load(std::memory_order_relaxed))
        {
            continue;
        }

        locker.unlock();

        const QTransform pageToImage = createPageToImageMatrix(task.key, task.region);
        const QSize imageSize(qCeil(task.region.width() * task.key.devicePixelRatio), qCeil(task.region.height() * task.key.devicePixelRatio));

        QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
        if (!image.isNull())
        {
            image.fill(task.key.paperColor);

            const int bandCount = qMax(1, PDFExecutionPolicy::getIdealThreadCount(PDFExecutionPolicy::Scope::Unknown));
            task.page->drawParallel(&image, task.cropBox, pageToImage, task.key.features, 1.0, bandCount, task.cancelFlag.get());
        }

        // Page is released here, by the worker thread, so the main thread
        // never waits for the destruction of a large page, which has been
        // removed from the cache of the compiler meanwhile.
        task.page.reset();

        if (!task.cancelFlag->load(std::memory_order_relaxed) && !image.isNull())
        {
            const quint64 taskId = task.id;
            const PDFInteger pageIndex = task.pageIndex;
            QMetaObject::invokeMethod(this, [this, taskId, pageIndex, image = std::move(image)]() mutable { onTaskFinished(taskId, pageIndex, std::move(image)); }, Qt::QueuedConnection);
        }

        locker.relock();
    }
}

void PDFAsynchronousPageRasterCache::onTaskFinished(quint64 taskId, PDFInteger pageIndex, QImage image)
{
    auto it = m_pending.find(pageIndex);
    if (it == m_pending.end() || it->second.id != taskId)
    {
        // Task has been cancelled or replaced meanwhile
        return;
    }

    const Task& task = it->second;

    Entry entry;
    entry.page = task.page;
    entry.key = task.key;
    entry.region = task.region;
    entry.pageToImage = createPageToImageMatrix(task.key, task.region);
    entry.image = std::move(image);

    const bool isWholePage = task.isWholePage;
    const QImage pageImage = entry.image;
    const QTransform pageToImage = entry.pageToImage;

    m_entries[pageIndex] = std::move(entry);
    m_pending.erase(it);

    Q_EMIT imageReady();

    if (isWholePage)
    {
        Q_EMIT pageRasterized(pageIndex, pageImage, pageToImage);
    }
}

void PDFAsynchronousPageRasterCache::retainPages(const std::vector<PDFInteger>& pages)
{
    Q_ASSERT(std::is_sorted(pages.cbegin(), pages.cend()));

    auto isRetained = [&pages](PDFInteger pageIndex) { return std::binary_search(pages.cbegin(), pages.cend(), pageIndex); };

    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (isRetained(it->first))
        {
            ++it;
        }
        else
        {
            it = m_entries.erase(it);
        }
    }

    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        if (isRetained(it->first))
        {
            ++it;
        }
        else
        {
            it->second.cancelFlag->store(true, std::memory_order_relaxed);
            it = m_pending.erase(it);
        }
    }
}

void PDFAsynchronousPageRasterCache::clear()
{
    retainPages({ });
}

}   // namespace pdf
