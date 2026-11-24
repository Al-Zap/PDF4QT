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

#include "pdfvisualcompare.h"
#include "pdfconstants.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfcolorspaces.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfprogress.h"
#include "pdfoperationcontrol.h"
#include "pdfexecutionpolicy.h"
#include "pdfdocumentbuilder.h"
#include "pdfstreamfilters.h"
#include "pdfoutline.h"
#include "pdfaction.h"

#include <QPainter>
#include <QThread>
#include <QLocale>

#include <algorithm>
#include <numeric>

namespace pdf
{

/// Binary mask of the ink of a page image
struct PDFVisualCompareMask
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;

    bool isEmpty() const { return width <= 0 || height <= 0; }
    uint8_t at(int x, int y) const { return data[size_t(y) * size_t(width) + size_t(x)]; }
};

class PDFVisualCompareHelper
{
public:
    PDFVisualCompareHelper() = delete;

    /// Gray level of the pixel (ITU-R BT.601 weights)
    static inline int getGray(QRgb rgb)
    {
        return (qRed(rgb) * 299 + qGreen(rgb) * 587 + qBlue(rgb) * 114 + 500) / 1000;
    }

    static inline int getColorDifference(QRgb a, QRgb b)
    {
        return qMax(qAbs(qRed(a) - qRed(b)), qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b))));
    }

    /// Mixes the paper (white) with the color, \p alpha is in the range [0, 255]
    static inline QRgb mix(QColor color, int alpha)
    {
        alpha = qBound(0, alpha, 255);
        const int r = 255 - ((255 - color.red()) * alpha + 127) / 255;
        const int g = 255 - ((255 - color.green()) * alpha + 127) / 255;
        const int b = 255 - ((255 - color.blue()) * alpha + 127) / 255;
        return qRgb(r, g, b);
    }

    /// Intensity of the ink used to draw the changes - the antialiased edges of
    /// thin lines are light, they are made stronger to be well visible
    static inline int getInkAlpha(int gray)
    {
        return qMin(255, (255 - gray) * 2);
    }

    /// Average color of the 3 x 3 pixels around the pixel
    static QRgb getAverageColor(const QImage& image, int x, int y)
    {
        int r = 0;
        int g = 0;
        int b = 0;
        int count = 0;
        for (int yy = qMax(0, y - 1); yy <= qMin(image.height() - 1, y + 1); ++yy)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(yy));
            for (int xx = qMax(0, x - 1); xx <= qMin(image.width() - 1, x + 1); ++xx)
            {
                r += qRed(line[xx]);
                g += qGreen(line[xx]);
                b += qBlue(line[xx]);
                ++count;
            }
        }
        return qRgb(r / count, g / count, b / count);
    }

    static QImage toRgb32(const QImage& image)
    {
        if (image.isNull() || image.format() == QImage::Format_RGB32)
        {
            return image;
        }

        // Transparent pixels are drawn onto the paper
        QImage result(image.size(), QImage::Format_RGB32);
        result.fill(Qt::white);
        QPainter painter(&result);
        painter.drawImage(0, 0, image);
        painter.end();
        return result;
    }

    static PDFVisualCompareMask createMask(const QImage& image, int threshold)
    {
        PDFVisualCompareMask mask;
        if (image.isNull())
        {
            return mask;
        }

        Q_ASSERT(image.format() == QImage::Format_RGB32);
        mask.width = image.width();
        mask.height = image.height();
        mask.data.resize(size_t(mask.width) * size_t(mask.height), 0);

        for (int y = 0; y < mask.height; ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            uint8_t* maskLine = mask.data.data() + size_t(y) * size_t(mask.width);
            for (int x = 0; x < mask.width; ++x)
            {
                maskLine[x] = getGray(line[x]) < threshold ? 1 : 0;
            }
        }

        return mask;
    }

    /// Mask downsampled by two, a cell is ink, if some of its pixels is ink
    static PDFVisualCompareMask downsample(const PDFVisualCompareMask& mask)
    {
        PDFVisualCompareMask result;
        result.width = (mask.width + 1) / 2;
        result.height = (mask.height + 1) / 2;
        result.data.resize(size_t(result.width) * size_t(result.height), 0);

        for (int y = 0; y < mask.height; ++y)
        {
            const uint8_t* line = mask.data.data() + size_t(y) * size_t(mask.width);
            uint8_t* resultLine = result.data.data() + size_t(y / 2) * size_t(result.width);
            for (int x = 0; x < mask.width; ++x)
            {
                resultLine[x / 2] |= line[x];
            }
        }

        return result;
    }

    /// Dilates the mask by a square of the given radius
    static PDFVisualCompareMask dilate(const PDFVisualCompareMask& mask, int radius)
    {
        if (radius <= 0 || mask.isEmpty())
        {
            return mask;
        }

        PDFVisualCompareMask horizontal = mask;
        for (int y = 0; y < mask.height; ++y)
        {
            const uint8_t* line = mask.data.data() + size_t(y) * size_t(mask.width);
            uint8_t* resultLine = horizontal.data.data() + size_t(y) * size_t(mask.width);

            // Distance to the last ink pixel on the left
            int lastInk = -radius - 1;
            for (int x = 0; x < mask.width; ++x)
            {
                if (line[x])
                {
                    lastInk = x;
                }
                resultLine[x] = (x - lastInk <= radius) ? 1 : 0;
            }

            lastInk = mask.width + radius + 1;
            for (int x = mask.width - 1; x >= 0; --x)
            {
                if (line[x])
                {
                    lastInk = x;
                }
                if (lastInk - x <= radius)
                {
                    resultLine[x] = 1;
                }
            }
        }

        PDFVisualCompareMask result = horizontal;
        std::vector<int> lastInk(size_t(mask.width), -radius - 1);
        for (int y = 0; y < mask.height; ++y)
        {
            const uint8_t* line = horizontal.data.data() + size_t(y) * size_t(mask.width);
            uint8_t* resultLine = result.data.data() + size_t(y) * size_t(mask.width);
            for (int x = 0; x < mask.width; ++x)
            {
                if (line[x])
                {
                    lastInk[x] = y;
                }
                resultLine[x] = (y - lastInk[x] <= radius) ? 1 : 0;
            }
        }

        std::fill(lastInk.begin(), lastInk.end(), mask.height + radius + 1);
        for (int y = mask.height - 1; y >= 0; --y)
        {
            const uint8_t* line = horizontal.data.data() + size_t(y) * size_t(mask.width);
            uint8_t* resultLine = result.data.data() + size_t(y) * size_t(mask.width);
            for (int x = 0; x < mask.width; ++x)
            {
                if (line[x])
                {
                    lastInk[x] = y;
                }
                if (lastInk[x] - y <= radius)
                {
                    resultLine[x] = 1;
                }
            }
        }

        return result;
    }

    /// Returns positions of the ink pixels, at most \p limit positions evenly
    /// distributed over all ink pixels
    static std::vector<QPoint> getInkPoints(const PDFVisualCompareMask& mask, size_t limit)
    {
        const size_t count = size_t(std::count(mask.data.cbegin(), mask.data.cend(), uint8_t(1)));
        const size_t stride = qMax<size_t>(1, (count + limit - 1) / qMax<size_t>(1, limit));

        std::vector<QPoint> points;
        points.reserve(count / stride + 1);

        size_t index = 0;
        for (int y = 0; y < mask.height; ++y)
        {
            const uint8_t* line = mask.data.data() + size_t(y) * size_t(mask.width);
            for (int x = 0; x < mask.width; ++x)
            {
                if (line[x] && (index++ % stride) == 0)
                {
                    points.emplace_back(x, y);
                }
            }
        }

        return points;
    }

    /// Number of the points, which hit the ink of the mask, when they are shifted by the offset
    static qint64 getScore(const std::vector<QPoint>& points, const PDFVisualCompareMask& mask, QPoint offset)
    {
        qint64 score = 0;
        for (const QPoint& point : points)
        {
            const int x = point.x() + offset.x();
            const int y = point.y() + offset.y();
            if (x >= 0 && y >= 0 && x < mask.width && y < mask.height && mask.at(x, y))
            {
                ++score;
            }
        }
        return score;
    }

    /// Finds the best offset in the range [center - radius, center + radius]. If more
    /// offsets have the same score, the one closest to the \p preferred offset is used.
    static QPoint findBestOffset(const std::vector<QPoint>& points,
                                 const PDFVisualCompareMask& mask,
                                 QPoint center,
                                 int radius,
                                 QPoint preferred,
                                 qint64* bestScore)
    {
        QPoint bestOffset = center;
        qint64 score = -1;
        int bestDistance = std::numeric_limits<int>::max();

        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const QPoint offset(center.x() + dx, center.y() + dy);
                const qint64 currentScore = getScore(points, mask, offset);
                const int distance = (offset - preferred).manhattanLength();
                if (currentScore > score || (currentScore == score && distance < bestDistance))
                {
                    score = currentScore;
                    bestOffset = offset;
                    bestDistance = distance;
                }
            }
        }

        if (bestScore)
        {
            *bestScore = score;
        }

        return bestOffset;
    }

    static QRectF toPageRect(const QRect& pixelRect, QSize imageSize, QSizeF pageSize)
    {
        const PDFReal sx = pageSize.width() / qMax(1, imageSize.width());
        const PDFReal sy = pageSize.height() / qMax(1, imageSize.height());
        return QRectF(pixelRect.left() * sx,
                      pageSize.height() - (pixelRect.top() + pixelRect.height()) * sy,
                      pixelRect.width() * sx,
                      pixelRect.height() * sy);
    }

    static QColor getChangeColor(const PDFVisualCompareSettings& settings, PDFVisualCompareChange::Type type)
    {
        switch (type)
        {
            case PDFVisualCompareChange::Type::Added:
                return settings.addedColor;
            case PDFVisualCompareChange::Type::Removed:
                return settings.removedColor;
            case PDFVisualCompareChange::Type::Changed:
                return settings.changedColor;
        }

        return settings.changedColor;
    }

    static PDFVisualCompareChange::Type getChangeType(int added, int removed, int changed)
    {
        if (changed == 0 && removed == 0)
        {
            return PDFVisualCompareChange::Type::Added;
        }
        if (changed == 0 && added == 0)
        {
            return PDFVisualCompareChange::Type::Removed;
        }
        return PDFVisualCompareChange::Type::Changed;
    }
};

int PDFVisualCompareSettings::toPixels(PDFReal mm) const
{
    return qMax(0, qRound(mm / 25.4 * resolution));
}

QString PDFVisualCompareChange::getTypeName(Type type)
{
    switch (type)
    {
        case Type::Added:
            return PDFTranslationContext::tr("Added");
        case Type::Removed:
            return PDFTranslationContext::tr("Removed");
        case Type::Changed:
            return PDFTranslationContext::tr("Changed");
    }

    return QString();
}

size_t PDFVisualComparePageResult::getChangeCount(PDFVisualCompareChange::Type type) const
{
    return size_t(std::count_if(changes.cbegin(), changes.cend(), [type](const PDFVisualCompareChange& change) { return change.type == type; }));
}

size_t PDFVisualCompareResult::getChangeCount() const
{
    size_t count = 0;
    for (const PDFVisualComparePageResult& page : pages)
    {
        count += page.changes.size();
    }
    return count;
}

size_t PDFVisualCompareResult::getChangedPageCount() const
{
    return size_t(std::count_if(pages.cbegin(), pages.cend(), [](const PDFVisualComparePageResult& page) { return page.hasChanges(); }));
}

/// Caches used to render the pages of a document
struct PDFVisualCompare::Context
{
    explicit Context(const PDFDocument* document) :
        document(document),
        fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT),
        optionalContentActivity(document, OCUsage::View, nullptr)
    {
        if (document)
        {
            PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
            fontCache.setDocument(modifiedDocument);
            iccProfileCache.setDocument(modifiedDocument);
        }

        // Pages are rendered in parallel, fonts must not be removed during rendering
        fontCache.setCacheShrinkEnabled(nullptr, false);
    }

    const PDFDocument* document;
    PDFFontCache fontCache;
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity;
};

PDFVisualCompare::PDFVisualCompare(const PDFDocument* oldDocument,
                                   const PDFDocument* newDocument,
                                   const PDFVisualCompareSettings& settings) :
    m_oldDocument(oldDocument),
    m_newDocument(newDocument),
    m_settings(settings),
    m_oldContext(std::make_unique<Context>(oldDocument)),
    m_newContext(std::make_unique<Context>(newDocument))
{
    m_settings.resolution = qBound(10.0, m_settings.resolution, 1200.0);
    m_settings.tolerancePixels = qBound(0, m_settings.tolerancePixels, 16);
}

PDFVisualCompare::~PDFVisualCompare() = default;

PDFVisualCompare::PagePairs PDFVisualCompare::createPagePairs(const std::vector<PDFInteger>& oldPages, const std::vector<PDFInteger>& newPages)
{
    PagePairs pairs;
    const size_t count = qMax(oldPages.size(), newPages.size());
    pairs.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        pairs.emplace_back(i < oldPages.size() ? oldPages[i] : -1, i < newPages.size() ? newPages[i] : -1);
    }

    return pairs;
}

bool PDFVisualCompare::parsePages(const QString& text, PDFInteger pageCount, std::vector<PDFInteger>& pages, QString* errorMessage)
{
    pages.clear();
    if (text.trimmed().isEmpty())
    {
        pages.resize(size_t(qMax(PDFInteger(0), pageCount)));
        std::iota(pages.begin(), pages.end(), PDFInteger(0));
        return true;
    }

    QString parseError;
    PDFClosedIntervalSet set = PDFClosedIntervalSet::parse(1, pageCount, text, &parseError);
    if (!parseError.isEmpty())
    {
        *errorMessage = parseError;
        return false;
    }

    pages = set.unfold();
    for (PDFInteger& page : pages)
    {
        if (page < 1 || page > pageCount)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 doesn't exist, the document has %2 pages.").arg(page).arg(pageCount);
            pages.clear();
            return false;
        }
        --page;
    }

    if (pages.empty())
    {
        *errorMessage = PDFTranslationContext::tr("No pages are selected.");
        return false;
    }

    return true;
}

PDFVisualCompare::PagePairs PDFVisualCompare::createPagePairs() const
{
    std::vector<PDFInteger> oldPages(m_oldDocument ? m_oldDocument->getCatalog()->getPageCount() : 0);
    std::vector<PDFInteger> newPages(m_newDocument ? m_newDocument->getCatalog()->getPageCount() : 0);
    std::iota(oldPages.begin(), oldPages.end(), 0);
    std::iota(newPages.begin(), newPages.end(), 0);
    return createPagePairs(oldPages, newPages);
}

QSizeF PDFVisualCompare::getPageSize(const PDFDocument* document, PDFInteger pageIndex)
{
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return QSizeF();
    }

    const PDFPage* page = document->getCatalog()->getPage(size_t(pageIndex));
    if (!page)
    {
        return QSizeF();
    }

    const QRectF cropBox = page->getCropBox().isValid() ? page->getCropBox() : page->getMediaBox();
    return PDFPage::getRotatedBox(cropBox, page->getPageRotation()).size();
}

QImage PDFVisualCompare::renderPage(const PDFDocument* document, PDFInteger pageIndex, PDFReal scale, QString* errorMessage) const
{
    const QSizeF pageSize = getPageSize(document, pageIndex);
    if (!pageSize.isValid() || pageSize.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1);
        }
        return QImage();
    }

    const PDFReal factor = m_settings.resolution / 72.0 * scale;
    const QSize imageSize(qMax(1, qRound(pageSize.width() * factor)), qMax(1, qRound(pageSize.height() * factor)));

    // 32-bit image larger than 2 GB can't be created
    constexpr qint64 maximalPixelCount = 400000000;
    if (qint64(imageSize.width()) * qint64(imageSize.height()) > maximalPixelCount)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 is too large for the resolution %2 ppi.").arg(pageIndex + 1).arg(m_settings.resolution);
        }
        return QImage();
    }

    QImage image(imageSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Not enough memory to render page %1.").arg(pageIndex + 1);
        }
        return QImage();
    }
    image.fill(Qt::white);

    const PDFPage* page = document->getCatalog()->getPage(size_t(pageIndex));
    const QRectF cropBox = page->getCropBox().isValid() ? page->getCropBox() : page->getMediaBox();
    const PageRotation rotation = page->getPageRotation();
    const QRectF rotatedBox = PDFPage::getRotatedBox(cropBox, rotation);
    const QTransform matrix = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, QRectF(QPointF(0.0, 0.0), QSizeF(imageSize)), rotation);

    const Context* context = (document == m_oldDocument) ? m_oldContext.get() : m_newContext.get();
    Q_ASSERT(context->document == document);

    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, m_settings.features.testFlag(PDFRenderer::Antialiasing));
    painter.setRenderHint(QPainter::TextAntialiasing, m_settings.features.testFlag(PDFRenderer::TextAntialiasing));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_settings.features.testFlag(PDFRenderer::SmoothImages));

    {
        PDFPainter processor(&painter, m_settings.features, matrix, page, document, &context->fontCache, &context->iccProfileCache, &cms, &context->optionalContentActivity, meshQualitySettings);
        processor.setOperationControl(m_operationControl);
        processor.processContents();
    }
    painter.end();

    return image;
}

QPoint PDFVisualCompare::findOffset(const QImage& oldImage, const QImage& newImage, const PDFVisualCompareSettings& settings)
{
    if (oldImage.isNull() || newImage.isNull())
    {
        return QPoint();
    }

    const QImage oldRgb = PDFVisualCompareHelper::toRgb32(oldImage);
    const QImage newRgb = PDFVisualCompareHelper::toRgb32(newImage);

    // Pyramid of the masks, the coarsest level has about 12 pixels per inch
    std::vector<PDFVisualCompareMask> oldMasks = { PDFVisualCompareHelper::createMask(oldRgb, settings.inkThreshold) };
    std::vector<PDFVisualCompareMask> newMasks = { PDFVisualCompareHelper::createMask(newRgb, settings.inkThreshold) };

    PDFReal resolution = settings.resolution;
    while (resolution > 24.0 && oldMasks.back().width > 16 && oldMasks.back().height > 16 && newMasks.back().width > 16 && newMasks.back().height > 16)
    {
        oldMasks.push_back(PDFVisualCompareHelper::downsample(oldMasks.back()));
        newMasks.push_back(PDFVisualCompareHelper::downsample(newMasks.back()));
        resolution *= 0.5;
    }

    constexpr size_t pointLimit = 250000;
    const int levels = int(oldMasks.size());
    const int coarseFactor = 1 << (levels - 1);
    const QPoint centerOffset((newRgb.width() - oldRgb.width()) / 2, (newRgb.height() - oldRgb.height()) / 2);
    const int radius = (settings.toPixels(settings.maximumShiftMM) + coarseFactor - 1) / coarseFactor;

    std::vector<QPoint> coarsePoints = PDFVisualCompareHelper::getInkPoints(oldMasks.back(), pointLimit);
    if (coarsePoints.empty())
    {
        return QPoint();
    }

    std::vector<QPoint> points = PDFVisualCompareHelper::getInkPoints(oldMasks.front(), pointLimit);
    const qint64 zeroScore = PDFVisualCompareHelper::getScore(points, newMasks.front(), QPoint());
    const qint64 centerScore = centerOffset.isNull() ? zeroScore : PDFVisualCompareHelper::getScore(points, newMasks.front(), centerOffset);
    const QPoint fallback = centerScore > zeroScore ? centerOffset : QPoint();
    const qint64 fallbackScore = qMax(zeroScore, centerScore);

    if (settings.toPixels(settings.maximumShiftMM) == 0)
    {
        return fallback;
    }

    // Search around the top left corner, and around the center, when page sizes differ
    std::vector<QPoint> starts = { QPoint() };
    if (!centerOffset.isNull())
    {
        starts.push_back(centerOffset);
    }

    QPoint best;
    qint64 bestScore = -1;
    for (const QPoint& start : starts)
    {
        qint64 score = 0;
        const QPoint coarseStart(start.x() / coarseFactor, start.y() / coarseFactor);
        const QPoint offset = PDFVisualCompareHelper::findBestOffset(coarsePoints, newMasks.back(), coarseStart, radius, QPoint(), &score);
        if (score > bestScore)
        {
            bestScore = score;
            best = offset;
        }
    }

    // Refine the offset on the finer levels
    for (int level = levels - 2; level >= 0; --level)
    {
        std::vector<QPoint> points = PDFVisualCompareHelper::getInkPoints(oldMasks[level], pointLimit);
        best = PDFVisualCompareHelper::findBestOffset(points, newMasks[level], best * 2, 2, QPoint(), &bestScore);
    }

    // Shift must be significantly better than no shift, otherwise the pages are
    // probably not shifted and the match is accidental (similar repeated content).
    if (bestScore <= fallbackScore + fallbackScore / 100)
    {
        return fallback;
    }

    return best;
}

PDFVisualComparePageResult PDFVisualCompare::compareImages(const QImage& oldImage,
                                                           const QImage& newImage,
                                                           const PDFVisualCompareSettings& settings,
                                                           QSizeF pageSize)
{
    PDFVisualComparePageResult result;
    result.pageSize = pageSize;

    if (oldImage.isNull() && newImage.isNull())
    {
        return result;
    }

    const QImage oldRgb = PDFVisualCompareHelper::toRgb32(oldImage);
    const QImage newRgb = PDFVisualCompareHelper::toRgb32(newImage);
    const QSize size = !newRgb.isNull() ? newRgb.size() : oldRgb.size();
    const int width = size.width();
    const int height = size.height();

    if (!pageSize.isValid() || pageSize.isEmpty())
    {
        result.pageSize = QSizeF(width * 72.0 / settings.resolution, height * 72.0 / settings.resolution);
    }

    // Align the old page to the new page
    QPoint offset;
    if (!oldRgb.isNull() && !newRgb.isNull())
    {
        switch (settings.alignment)
        {
            case PDFVisualCompareSettings::Alignment::PageOrigin:
                break;
            case PDFVisualCompareSettings::Alignment::PageCenter:
                offset = QPoint((newRgb.width() - oldRgb.width()) / 2, (newRgb.height() - oldRgb.height()) / 2);
                break;
            case PDFVisualCompareSettings::Alignment::Automatic:
                offset = findOffset(oldRgb, newRgb, settings);
                break;
        }
    }
    result.offset = offset;

    QImage alignedOld(size, QImage::Format_RGB32);
    alignedOld.fill(Qt::white);
    if (!oldRgb.isNull())
    {
        QPainter painter(&alignedOld);
        painter.drawImage(offset, oldRgb);
        painter.end();
    }

    QImage alignedNew = newRgb;
    if (alignedNew.isNull())
    {
        alignedNew = QImage(size, QImage::Format_RGB32);
        alignedNew.fill(Qt::white);
    }

    // Ink is compared with hysteresis: a pixel darker than the ink threshold is added
    // (removed), only when there is no trace of ink (any pixel, which is not white)
    // near it in the other page. Otherwise the faint lines and the antialiased edges,
    // whose gray level is close to the threshold, would flicker between the pages,
    // when the content is moved by a fraction of a pixel.
    const PDFVisualCompareMask oldMask = PDFVisualCompareHelper::createMask(alignedOld, settings.inkThreshold);
    const PDFVisualCompareMask newMask = PDFVisualCompareHelper::createMask(alignedNew, settings.inkThreshold);
    const int traceThreshold = 255;
    const int tolerance = qMax(0, settings.tolerancePixels);
    const PDFVisualCompareMask oldNear = PDFVisualCompareHelper::dilate(PDFVisualCompareHelper::createMask(alignedOld, traceThreshold), tolerance);
    const PDFVisualCompareMask newNear = PDFVisualCompareHelper::dilate(PDFVisualCompareHelper::createMask(alignedNew, traceThreshold), tolerance);

    // Classification of the pixels: 0 = no change, 1 = added, 2 = removed, 3 = changed color
    enum : uint8_t { ClassNone = 0, ClassAdded = 1, ClassRemoved = 2, ClassChanged = 3 };
    std::vector<uint8_t> classes(size_t(width) * size_t(height), ClassNone);

    // Ink drawn over other ink (for example, a new line over a hatched area or over
    // an aerial photo) is a change, when it is much darker or lighter, or when its
    // color is different.
    constexpr int darknessTolerance = 96;
    const bool checkColors = settings.colorTolerance > 0;

    // Returns true, if a pixel of the image near (x, y) has the same darkness and color
    // as the given pixel, or if the average colors of the neighbourhoods match (the edges
    // of the antialiased lines and glyphs differ, when the content is moved by a fraction
    // of a pixel, but the average colors are almost the same).
    auto isColorNear = [&](const QImage& image, int x, int y, QRgb color, QRgb averageColor)
    {
        auto isSame = [&](QRgb a, QRgb b, int divisor)
        {
            return qAbs(PDFVisualCompareHelper::getGray(a) - PDFVisualCompareHelper::getGray(b)) <= darknessTolerance / divisor &&
                   (!checkColors || PDFVisualCompareHelper::getColorDifference(a, b) <= settings.colorTolerance / divisor);
        };

        for (int yy = qMax(0, y - tolerance); yy <= qMin(height - 1, y + tolerance); ++yy)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(yy));
            for (int xx = qMax(0, x - tolerance); xx <= qMin(width - 1, x + tolerance); ++xx)
            {
                if (isSame(color, line[xx], 1) || isSame(averageColor, PDFVisualCompareHelper::getAverageColor(image, xx, yy), 2))
                {
                    return true;
                }
            }
        }

        return false;
    };

    for (int y = 0; y < height; ++y)
    {
        const QRgb* oldLine = reinterpret_cast<const QRgb*>(alignedOld.constScanLine(y));
        const QRgb* newLine = reinterpret_cast<const QRgb*>(alignedNew.constScanLine(y));
        const size_t lineOffset = size_t(y) * size_t(width);

        for (int x = 0; x < width; ++x)
        {
            const size_t index = lineOffset + size_t(x);
            const bool isOldInk = oldMask.data[index];
            const bool isNewInk = newMask.data[index];

            if (isNewInk && !oldNear.data[index])
            {
                classes[index] = ClassAdded;
            }
            else if (isOldInk && !newNear.data[index])
            {
                classes[index] = ClassRemoved;
            }
            else if (isOldInk || isNewInk)
            {
                const QRgb newColor = newLine[x];
                const QRgb oldColor = oldLine[x];
                const int newGray = PDFVisualCompareHelper::getGray(newColor);
                const int oldGray = PDFVisualCompareHelper::getGray(oldColor);
                const bool isDarknessChanged = qAbs(newGray - oldGray) > darknessTolerance;
                const bool isColorChanged = checkColors && PDFVisualCompareHelper::getColorDifference(newColor, oldColor) > settings.colorTolerance;
                if (!isDarknessChanged && !isColorChanged)
                {
                    continue;
                }

                if (newGray < oldGray - darknessTolerance)
                {
                    // Darker ink in the new page, isn't it in the old page nearby?
                    if (!isColorNear(alignedOld, x, y, newColor, PDFVisualCompareHelper::getAverageColor(alignedNew, x, y)))
                    {
                        classes[index] = ClassAdded;
                    }
                }
                else if (oldGray < newGray - darknessTolerance)
                {
                    // Darker ink in the old page, isn't it in the new page nearby?
                    if (!isColorNear(alignedNew, x, y, oldColor, PDFVisualCompareHelper::getAverageColor(alignedOld, x, y)))
                    {
                        classes[index] = ClassRemoved;
                    }
                }
                else if (!isColorNear(alignedOld, x, y, newColor, PDFVisualCompareHelper::getAverageColor(alignedNew, x, y)))
                {
                    classes[index] = ClassChanged;
                }
            }
        }
    }

    // Group the changed pixels into the changes using a grid of cells
    const int cellSize = qMax(1, settings.toPixels(settings.mergeDistanceMM));
    const int gridWidth = (width + cellSize - 1) / cellSize;
    const int gridHeight = (height + cellSize - 1) / cellSize;

    struct Cell
    {
        int counts[4] = { 0, 0, 0, 0 };
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = -1;
        int maxY = -1;
        int component = -1;

        bool isEmpty() const { return maxX < 0; }
    };

    std::vector<Cell> cells(size_t(gridWidth) * size_t(gridHeight));
    for (int y = 0; y < height; ++y)
    {
        const size_t lineOffset = size_t(y) * size_t(width);
        Cell* cellLine = cells.data() + size_t(y / cellSize) * size_t(gridWidth);
        for (int x = 0; x < width; ++x)
        {
            const uint8_t pixelClass = classes[lineOffset + size_t(x)];
            if (pixelClass != ClassNone)
            {
                Cell& cell = cellLine[x / cellSize];
                ++cell.counts[pixelClass];
                cell.minX = qMin(cell.minX, x);
                cell.minY = qMin(cell.minY, y);
                cell.maxX = qMax(cell.maxX, x);
                cell.maxY = qMax(cell.maxY, y);
            }
        }
    }

    struct Component
    {
        int counts[4] = { 0, 0, 0, 0 };
        QRect rect;
        bool kept = false;
    };

    std::vector<Component> components;
    std::vector<int> stack;
    for (int cy = 0; cy < gridHeight; ++cy)
    {
        for (int cx = 0; cx < gridWidth; ++cx)
        {
            Cell& seed = cells[size_t(cy) * size_t(gridWidth) + size_t(cx)];
            if (seed.isEmpty() || seed.component != -1)
            {
                continue;
            }

            const int componentIndex = int(components.size());
            Component component;
            int minX = std::numeric_limits<int>::max();
            int minY = std::numeric_limits<int>::max();
            int maxX = -1;
            int maxY = -1;

            seed.component = componentIndex;
            stack.push_back(cy * gridWidth + cx);
            while (!stack.empty())
            {
                const int cellIndex = stack.back();
                stack.pop_back();

                const Cell& cell = cells[size_t(cellIndex)];
                for (int i = 1; i < 4; ++i)
                {
                    component.counts[i] += cell.counts[i];
                }
                minX = qMin(minX, cell.minX);
                minY = qMin(minY, cell.minY);
                maxX = qMax(maxX, cell.maxX);
                maxY = qMax(maxY, cell.maxY);

                const int x = cellIndex % gridWidth;
                const int y = cellIndex / gridWidth;
                for (int ny = qMax(0, y - 1); ny <= qMin(gridHeight - 1, y + 1); ++ny)
                {
                    for (int nx = qMax(0, x - 1); nx <= qMin(gridWidth - 1, x + 1); ++nx)
                    {
                        Cell& neighbour = cells[size_t(ny) * size_t(gridWidth) + size_t(nx)];
                        if (!neighbour.isEmpty() && neighbour.component == -1)
                        {
                            neighbour.component = componentIndex;
                            stack.push_back(ny * gridWidth + nx);
                        }
                    }
                }
            }

            component.rect = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
            components.push_back(component);
        }
    }

    const int minimumSize = qMax(1, settings.toPixels(settings.minimumChangeSizeMM));
    for (Component& component : components)
    {
        component.kept = qMax(component.rect.width(), component.rect.height()) >= minimumSize;
    }

    const bool isWholePage = oldRgb.isNull() || newRgb.isNull();
    if (isWholePage)
    {
        // Added or removed page is a single change covering all its content
        Component wholePage;
        for (const Component& component : components)
        {
            if (component.kept)
            {
                for (int i = 1; i < 4; ++i)
                {
                    wholePage.counts[i] += component.counts[i];
                }
                wholePage.rect = wholePage.rect.united(component.rect);
            }
        }

        for (Component& component : components)
        {
            component.kept = false;
        }

        if (!wholePage.rect.isNull())
        {
            wholePage.kept = true;
            components.push_back(wholePage);
        }
    }

    for (const Component& component : components)
    {
        if (!component.kept)
        {
            continue;
        }

        PDFVisualCompareChange change;
        change.addedPixels = component.counts[ClassAdded];
        change.removedPixels = component.counts[ClassRemoved];
        change.changedPixels = component.counts[ClassChanged];
        change.type = PDFVisualCompareHelper::getChangeType(change.addedPixels, change.removedPixels, change.changedPixels);
        change.pixelRect = component.rect;
        change.pageRect = PDFVisualCompareHelper::toPageRect(component.rect, size, result.pageSize);
        result.addedPixels += change.addedPixels;
        result.removedPixels += change.removedPixels;
        result.changedPixels += change.changedPixels;
        result.changes.push_back(change);
    }

    // Reading order: top to bottom, left to right
    std::stable_sort(result.changes.begin(), result.changes.end(), [](const PDFVisualCompareChange& l, const PDFVisualCompareChange& r)
    {
        return std::make_pair(l.pixelRect.top(), l.pixelRect.left()) < std::make_pair(r.pixelRect.top(), r.pixelRect.left());
    });

    // Overlay image
    QImage overlay(size, QImage::Format_RGB32);
    const QColor colors[4] = { settings.unchangedColor, settings.addedColor, settings.removedColor, settings.changedColor };
    const bool keepAllChanges = isWholePage && !result.changes.empty();
    for (int y = 0; y < height; ++y)
    {
        const QRgb* oldLine = reinterpret_cast<const QRgb*>(alignedOld.constScanLine(y));
        const QRgb* newLine = reinterpret_cast<const QRgb*>(alignedNew.constScanLine(y));
        QRgb* overlayLine = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        const size_t lineOffset = size_t(y) * size_t(width);
        const Cell* cellLine = cells.data() + size_t(y / cellSize) * size_t(gridWidth);

        for (int x = 0; x < width; ++x)
        {
            const size_t index = lineOffset + size_t(x);
            const int oldGray = PDFVisualCompareHelper::getGray(oldLine[x]);
            const int newGray = PDFVisualCompareHelper::getGray(newLine[x]);
            uint8_t pixelClass = classes[index];

            if (pixelClass != ClassNone && !keepAllChanges)
            {
                const int component = cellLine[x / cellSize].component;
                if (component < 0 || !components[size_t(component)].kept)
                {
                    pixelClass = ClassNone;
                }
            }

            switch (pixelClass)
            {
                case ClassAdded:
                case ClassChanged:
                    overlayLine[x] = PDFVisualCompareHelper::mix(colors[pixelClass], PDFVisualCompareHelper::getInkAlpha(newGray));
                    break;
                case ClassRemoved:
                    overlayLine[x] = PDFVisualCompareHelper::mix(colors[pixelClass], PDFVisualCompareHelper::getInkAlpha(oldGray));
                    break;
                default:
                    overlayLine[x] = PDFVisualCompareHelper::mix(colors[ClassNone], 255 - qMin(oldGray, newGray));
                    break;
            }
        }
    }

    result.overlay = std::move(overlay);
    return result;
}

PDFVisualComparePageResult PDFVisualCompare::comparePages(PDFInteger oldPageIndex, PDFInteger newPageIndex) const
{
    PDFVisualComparePageResult result;

    const QSizeF oldSize = oldPageIndex >= 0 ? getPageSize(m_oldDocument, oldPageIndex) : QSizeF();
    const QSizeF newSize = newPageIndex >= 0 ? getPageSize(m_newDocument, newPageIndex) : QSizeF();

    PDFReal scale = 1.0;
    if (m_settings.scaleToFit && !oldSize.isEmpty() && !newSize.isEmpty())
    {
        scale = qMin(newSize.width() / oldSize.width(), newSize.height() / oldSize.height());
    }

    QString errorMessage;
    QImage oldImage;
    QImage newImage;

    if (oldPageIndex >= 0)
    {
        oldImage = renderPage(m_oldDocument, oldPageIndex, scale, &errorMessage);
    }

    if (newPageIndex >= 0 && errorMessage.isEmpty())
    {
        newImage = renderPage(m_newDocument, newPageIndex, 1.0, &errorMessage);
    }

    if (errorMessage.isEmpty() && !PDFOperationControl::isOperationCancelled(m_operationControl))
    {
        const QSizeF pageSize = !newSize.isEmpty() ? newSize : oldSize * scale;
        result = compareImages(oldImage, newImage, m_settings, pageSize);
    }
    else
    {
        result.pageSize = !newSize.isEmpty() ? newSize : oldSize;
    }

    result.oldPageIndex = oldPageIndex;
    result.newPageIndex = newPageIndex;
    result.oldPageScale = scale;
    result.errorMessage = errorMessage;
    return result;
}

PDFVisualCompareResult PDFVisualCompare::compare(const PagePairs& pairs, const PageCallback& callback) const
{
    PDFVisualCompareResult result;
    result.pages.reserve(pairs.size());

    if (m_progress)
    {
        ProgressStartupInfo info;
        info.showDialog = true;
        info.text = PDFTranslationContext::tr("Comparing pages...");
        m_progress->start(pairs.size(), std::move(info));
    }

    // Pages are compared in batches, so only a few page images are in the memory
    const size_t batchSize = size_t(qMax(1, QThread::idealThreadCount()));
    for (size_t batchStart = 0; batchStart < pairs.size(); batchStart += batchSize)
    {
        if (PDFOperationControl::isOperationCancelled(m_operationControl))
        {
            result.cancelled = true;
            break;
        }

        const size_t count = qMin(batchSize, pairs.size() - batchStart);
        std::vector<PDFVisualComparePageResult> batch(count);
        std::vector<size_t> indices(count);
        std::iota(indices.begin(), indices.end(), size_t(0));

        auto comparePair = [&](size_t index)
        {
            const PagePair& pair = pairs[batchStart + index];
            batch[index] = comparePages(pair.first, pair.second);

            if (m_progress)
            {
                m_progress->step();
            }
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, indices.begin(), indices.end(), comparePair);

        if (PDFOperationControl::isOperationCancelled(m_operationControl))
        {
            result.cancelled = true;
            break;
        }

        for (PDFVisualComparePageResult& page : batch)
        {
            if (callback)
            {
                callback(page);
                page.overlay = QImage();
            }
            result.pages.push_back(std::move(page));
        }
    }

    if (m_progress)
    {
        m_progress->finish();
    }

    return result;
}

struct PDFVisualCompareReport::Implementation
{
    PDFVisualCompareSettings settings;
    Information information;
    PDFDocumentBuilder builder;
    std::vector<PDFObjectReference> pages;
    std::vector<std::vector<PDFObjectReference>> annotations;
};

PDFVisualCompareReport::PDFVisualCompareReport(const PDFVisualCompareSettings& settings, const Information& information) :
    m_implementation(std::make_unique<Implementation>())
{
    m_implementation->settings = settings;
    m_implementation->information = information;
}

PDFVisualCompareReport::~PDFVisualCompareReport() = default;

QString PDFVisualCompareReport::getChangeSubject()
{
    return PDFTranslationContext::tr("Revision Change");
}

QString PDFVisualCompareReport::getPageTitle(const PDFVisualComparePageResult& page)
{
    if (page.isPageAdded())
    {
        return PDFTranslationContext::tr("New page %1 (added)").arg(page.newPageIndex + 1);
    }

    if (page.isPageRemoved())
    {
        return PDFTranslationContext::tr("Old page %1 (removed)").arg(page.oldPageIndex + 1);
    }

    if (page.oldPageIndex == page.newPageIndex)
    {
        return PDFTranslationContext::tr("Page %1").arg(page.newPageIndex + 1);
    }

    return PDFTranslationContext::tr("Old page %1 / new page %2").arg(page.oldPageIndex + 1).arg(page.newPageIndex + 1);
}

void PDFVisualCompareReport::addPage(const PDFVisualComparePageResult& page)
{
    PDFDocumentBuilder& builder = m_implementation->builder;
    const PDFVisualCompareSettings& settings = m_implementation->settings;

    QSizeF pageSize = page.pageSize;
    if (!pageSize.isValid() || pageSize.isEmpty())
    {
        pageSize = QSizeF(612.0, 792.0);
    }

    const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0.0, 0.0), pageSize));
    m_implementation->pages.push_back(pageReference);

    if (!page.overlay.isNull())
    {
        // The overlay is stored losslessly - thin lines of the drawings would be
        // damaged by the JPEG compression used for the images drawn by QPainter
        const QImage image = page.overlay.convertToFormat(QImage::Format_RGB888);
        QByteArray samples;
        samples.reserve(qsizetype(image.width()) * qsizetype(image.height()) * 3);
        for (int y = 0; y < image.height(); ++y)
        {
            samples.append(reinterpret_cast<const char*>(image.constScanLine(y)), qsizetype(image.width()) * 3);
        }
        QByteArray compressedData = PDFFlateDecodeFilter::compress(samples);

        PDFDictionaryBuilder imageDictionary;
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
        imageDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        const PDFObjectReference imageReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(imageDictionary), std::move(compressedData))));

        QByteArray content = QString("q %1 0 0 %2 0 0 cm /Im0 Do Q\n").arg(pageSize.width(), 0, 'f', 4).arg(pageSize.height(), 0, 'f', 4).toLatin1();
        QByteArray compressedContent = PDFFlateDecodeFilter::compress(content);
        PDFDictionaryBuilder contentDictionary;
        contentDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedContent.size()));
        contentDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), std::move(compressedContent))));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << contentReference;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory.beginDictionary();
        factory.beginDictionaryItem("XObject");
        factory.beginDictionary();
        factory.beginDictionaryItem("Im0");
        factory << imageReference;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(pageReference, factory.takeObject());
    }

    // Revision clouds around the changes
    std::vector<PDFObjectReference> annotations;
    const PDFReal margin = 4.0;
    const QRectF pageRect(QPointF(0.0, 0.0), pageSize);
    const size_t changeCount = page.changes.size();
    for (size_t i = 0; i < changeCount; ++i)
    {
        const PDFVisualCompareChange& change = page.changes[i];
        const QRectF rect = change.pageRect.adjusted(-margin, -margin, margin, margin).intersected(pageRect);
        if (!rect.isValid())
        {
            continue;
        }

        const QString contents = PDFTranslationContext::tr("Change %1 of %2: %3").arg(i + 1).arg(changeCount).arg(PDFVisualCompareChange::getTypeName(change.type));
        const PDFObjectReference annotation = builder.createAnnotationSquare(pageReference, rect, 1.5, QColor(), PDFVisualCompareHelper::getChangeColor(settings, change.type),
                                                                             PDFTranslationContext::tr("Compare"), getChangeSubject(), contents);
        builder.setAnnotationCloudyBorder(annotation, 1.0);
        builder.updateAnnotationAppearanceStreams(annotation);
        annotations.push_back(annotation);
    }
    m_implementation->annotations.push_back(std::move(annotations));
}

PDFDocument PDFVisualCompareReport::build(const PDFVisualCompareResult& result)
{
    PDFDocumentBuilder& builder = m_implementation->builder;
    const PDFVisualCompareSettings& settings = m_implementation->settings;
    const Information& information = m_implementation->information;
    QLocale locale;

    // Summary pages (US Letter)
    const QSizeF summarySize(612.0, 792.0);
    const PDFReal left = 54.0;
    const PDFReal right = summarySize.width() - 54.0;
    const PDFReal rowHeight = 16.0;
    const PDFReal bottom = summarySize.height() - 54.0;

    std::vector<PDFObjectReference> summaryPages;
    size_t row = 0;
    const size_t rowCount = result.pages.size();

    QFont titleFont("Helvetica");
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    QFont textFont("Helvetica");
    textFont.setPixelSize(10);
    QFont boldFont = textFont;
    boldFont.setBold(true);

    const PDFReal columns[] = { left, left + 190.0, left + 300.0, left + 360.0, left + 420.0, left + 480.0 };

    do
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0.0, 0.0), summarySize));
        summaryPages.push_back(pageReference);

        PDFPageContentStreamBuilder contentBuilder(&builder);
        QPainter* painter = contentBuilder.begin(pageReference);
        painter->setPen(Qt::black);

        PDFReal y = 54.0;
        if (summaryPages.size() == 1)
        {
            painter->setFont(titleFont);
            painter->drawText(QPointF(left, y + 20.0), PDFTranslationContext::tr("Document Comparison"));
            y += 44.0;

            painter->setFont(textFont);
            auto writeLine = [&](const QString& label, const QString& value)
            {
                painter->setFont(boldFont);
                painter->drawText(QPointF(left, y), label);
                painter->setFont(textFont);
                painter->drawText(QRectF(left + 90.0, y - 11.0, right - left - 90.0, 14.0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, value);
                y += rowHeight;
            };

            writeLine(PDFTranslationContext::tr("Old document"), information.oldDocumentName);
            writeLine(PDFTranslationContext::tr("New document"), information.newDocumentName);
            writeLine(PDFTranslationContext::tr("Compared"), locale.toString(information.dateTime, QLocale::LongFormat));
            writeLine(PDFTranslationContext::tr("Pages"), PDFTranslationContext::tr("%1 compared, %2 with changes").arg(result.pages.size()).arg(result.getChangedPageCount()));
            writeLine(PDFTranslationContext::tr("Changes"), locale.toString(qulonglong(result.getChangeCount())));
            writeLine(PDFTranslationContext::tr("Resolution"), PDFTranslationContext::tr("%1 ppi").arg(locale.toString(settings.resolution, 'f', 0)));
            y += 8.0;

            // Legend
            painter->setFont(boldFont);
            painter->drawText(QPointF(left, y), PDFTranslationContext::tr("Legend"));
            y += rowHeight;
            painter->setFont(textFont);
            const std::pair<QColor, QString> legend[] = {
                { settings.removedColor, PDFTranslationContext::tr("Removed - only in the old document") },
                { settings.addedColor, PDFTranslationContext::tr("Added - only in the new document") },
                { settings.changedColor, PDFTranslationContext::tr("Changed color") },
                { settings.unchangedColor, PDFTranslationContext::tr("Unchanged") },
            };
            for (const auto& item : legend)
            {
                painter->fillRect(QRectF(left, y - 9.0, 24.0, 10.0), item.first);
                painter->drawText(QPointF(left + 32.0, y), item.second);
                y += rowHeight;
            }
            y += 12.0;
        }

        // Table of the pages
        painter->setFont(boldFont);
        const QString headers[] = { PDFTranslationContext::tr("Page"), PDFTranslationContext::tr("Status"), PDFTranslationContext::tr("Added"),
                                    PDFTranslationContext::tr("Removed"), PDFTranslationContext::tr("Changed"), PDFTranslationContext::tr("Total") };
        for (int i = 0; i < 6; ++i)
        {
            painter->drawText(QPointF(columns[i], y), headers[i]);
        }
        painter->drawLine(QPointF(left, y + 4.0), QPointF(right, y + 4.0));
        y += rowHeight + 2.0;

        painter->setFont(textFont);
        while (row < rowCount && y < bottom)
        {
            const PDFVisualComparePageResult& page = result.pages[row];
            QString status;
            if (!page.errorMessage.isEmpty())
            {
                status = PDFTranslationContext::tr("Error");
            }
            else if (page.isPageAdded())
            {
                status = PDFTranslationContext::tr("Page added");
            }
            else if (page.isPageRemoved())
            {
                status = PDFTranslationContext::tr("Page removed");
            }
            else if (page.hasChanges())
            {
                status = PDFTranslationContext::tr("Changed");
            }
            else
            {
                status = PDFTranslationContext::tr("No changes");
            }

            painter->drawText(QRectF(columns[0], y - 11.0, columns[1] - columns[0] - 6.0, 14.0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, getPageTitle(page));
            painter->drawText(QPointF(columns[1], y), status);
            painter->drawText(QPointF(columns[2], y), locale.toString(qulonglong(page.getChangeCount(PDFVisualCompareChange::Type::Added))));
            painter->drawText(QPointF(columns[3], y), locale.toString(qulonglong(page.getChangeCount(PDFVisualCompareChange::Type::Removed))));
            painter->drawText(QPointF(columns[4], y), locale.toString(qulonglong(page.getChangeCount(PDFVisualCompareChange::Type::Changed))));
            painter->drawText(QPointF(columns[5], y), locale.toString(qulonglong(page.changes.size())));
            y += rowHeight;
            ++row;
        }

        contentBuilder.end(painter);
    }
    while (row < rowCount);

    // Summary first, then the overlay pages
    builder.flattenPageTree();
    std::vector<PDFObjectReference> pageOrder = summaryPages;
    pageOrder.insert(pageOrder.end(), m_implementation->pages.cbegin(), m_implementation->pages.cend());
    builder.setPages(pageOrder);

    // Outline: summary, pages and their changes
    PDFOutlineItem root;
    auto createItem = [](const QString& title, const PDFDestination& destination)
    {
        QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
        item->setTitle(title);
        item->setAction(PDFActionPtr(new PDFActionGoTo(destination, PDFDestination())));
        return item;
    };

    root.addChild(createItem(PDFTranslationContext::tr("Summary"), PDFDestination::createFit(summaryPages.front())));
    const size_t pageCount = qMin(result.pages.size(), m_implementation->pages.size());
    for (size_t i = 0; i < pageCount; ++i)
    {
        const PDFVisualComparePageResult& page = result.pages[i];
        const PDFObjectReference pageReference = m_implementation->pages[i];

        QString title = getPageTitle(page);
        if (!page.errorMessage.isEmpty())
        {
            title = PDFTranslationContext::tr("%1 - error: %2").arg(title, page.errorMessage);
        }
        else
        {
            title = PDFTranslationContext::tr("%1 - %2 changes").arg(title).arg(page.changes.size());
        }

        QSharedPointer<PDFOutlineItem> pageItem = createItem(title, PDFDestination::createFit(pageReference));
        for (size_t j = 0; j < page.changes.size(); ++j)
        {
            const PDFVisualCompareChange& change = page.changes[j];
            const QRectF rect = change.pageRect.adjusted(-36.0, -36.0, 36.0, 36.0);
            const QString changeTitle = PDFTranslationContext::tr("Change %1: %2").arg(j + 1).arg(PDFVisualCompareChange::getTypeName(change.type));
            QSharedPointer<PDFOutlineItem> changeItem = createItem(changeTitle, PDFDestination::createFitR(pageReference, rect.left(), rect.bottom(), rect.right(), rect.top()));
            changeItem->setTextColor(PDFVisualCompareHelper::getChangeColor(settings, change.type));
            pageItem->addChild(changeItem);
        }
        root.addChild(pageItem);
    }
    builder.setOutline(&root);

    builder.setDocumentTitle(PDFTranslationContext::tr("Comparison of %1 and %2").arg(information.oldDocumentName, information.newDocumentName));
    return builder.build();
}

}   // namespace pdf
