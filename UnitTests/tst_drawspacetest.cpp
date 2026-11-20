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

#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfpagerastercache.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QLayout>
#include <QRandomGenerator>
#include <QScrollBar>

class DrawSpaceTest : public QObject
{
    Q_OBJECT

private slots:
    void fitWidthFillsViewportWidth();
    void fitHeightFillsViewportHeight();
    void fitPageFillsViewport();
    void fitIsIndependentOfLogicalSizeZooming();
    void fitDoesNotOscillate();
    void rasterCacheMatrixHasUniformScale();
    void rasterCacheDrawsImageWithoutCompiledPage();
    void rasterCacheKeepsSharpTaskWhileScrolling();
    void heavyPageBecomesSharpAfterZoomAndScroll();
};

struct DrawSpaceFixture
{
    pdf::PDFDocument document;
    pdf::PDFCMSManager cms{nullptr};
    pdf::PDFProgress progress{nullptr};
    pdf::PDFWidget widget{&cms, pdf::RendererEngine::QPainter, nullptr};

    explicit DrawSpaceFixture(pdf::PDFDocument doc, pdf::PageLayout pageLayout, QSize size) :
        document(std::move(doc))
    {
        widget.resize(size);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        widget.setDocument(pdf::PDFModifiedDocument(&document, nullptr), {});
        widget.getDrawWidgetProxy()->setPageLayout(pageLayout);
        settle();
    }

    ~DrawSpaceFixture()
    {
        widget.setDocument(pdf::PDFModifiedDocument(), {});
    }

    /// The widget is never shown, so the grid layout must be activated by hand.
    /// Showing or hiding a scrollbar resizes the draw widget, which recalculates
    /// the draw space again - a few rounds are needed before the sizes settle.
    void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            widget.layout()->activate();
            widget.getDrawWidgetProxy()->update();
        }
    }

    void performOperation(pdf::PDFDrawWidgetProxy::Operation operation)
    {
        widget.getDrawWidgetProxy()->performOperation(operation);
        settle();
    }

    /// Returns the area which is really available for the pages, i.e. the widget
    /// decreased by the scrollbars which are visible.
    QSize getAvailableSize() const
    {
        QSize size = widget.size();

        if (!widget.getVerticalScrollbar()->isHidden())
        {
            size.rwidth() -= widget.getVerticalScrollbar()->sizeHint().width();
        }

        if (!widget.getHorizontalScrollbar()->isHidden())
        {
            size.rheight() -= widget.getHorizontalScrollbar()->sizeHint().height();
        }

        return size;
    }

    /// Returns the size of the first page in pixels, as it is displayed now
    QSizeF getPageSizeInPixels() const
    {
        const pdf::PDFDrawWidgetProxy* proxy = widget.getDrawWidgetProxy();
        const QSizeF pageSizeMM = document.getCatalog()->getPage(0)->getRotatedMediaBoxMM().size();

        return QSizeF(proxy->transformDeviceSpaceToPixel(pageSizeMM.width()),
                      proxy->transformDeviceSpaceToPixel(pageSizeMM.height()));
    }
};

static pdf::PDFDocument createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;

    for (int i = 0; i < pageCount; ++i)
    {
        // A4 portrait, in the default user space units (1/72 inch)
        builder.appendPage(QRectF(0, 0, 595, 842));
    }

    return builder.build();
}

// The fit must use the whole available area. A tolerance of a few pixels covers
// the margin which keeps the rounding of the page rectangles from making a
// scrollbar appear.
static constexpr qreal FIT_TOLERANCE = 3.0;

#define QVERIFY_FITS(actual, available) \
    do { \
        QVERIFY2((actual) <= (available), \
                 qPrintable(QString("%1 does not fit into %2").arg(actual).arg(available))); \
        QVERIFY2((actual) >= (available) - FIT_TOLERANCE, \
                 qPrintable(QString("%1 wastes too much of %2").arg(actual).arg(available))); \
    } while (false)

void DrawSpaceTest::fitWidthFillsViewportWidth()
{
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);

    QVERIFY_FITS(fixture.getPageSizeInPixels().width(), fixture.getAvailableSize().width());
}

void DrawSpaceTest::fitHeightFillsViewportHeight()
{
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);

    QVERIFY_FITS(fixture.getPageSizeInPixels().height(), fixture.getAvailableSize().height());
}

void DrawSpaceTest::fitPageFillsViewport()
{
    // Single page layout uses the vertical scrollbar to switch the pages, so its
    // space must be reserved by the fit even though nothing overflows vertically.
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::SinglePage, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);

    const QSizeF pageSize = fixture.getPageSizeInPixels();
    const QSize availableSize = fixture.getAvailableSize();

    QVERIFY(pageSize.width() <= availableSize.width());
    QVERIFY(pageSize.height() <= availableSize.height());

    // The page is higher than wide, so the height is the limiting dimension
    QVERIFY_FITS(pageSize.height(), availableSize.height());

    // No page of the document may be clipped horizontally
    QVERIFY(fixture.widget.getHorizontalScrollbar()->isHidden());
}

void DrawSpaceTest::fitIsIndependentOfLogicalSizeZooming()
{
    // The zoom hint and the page layout must derive the pixel per millimeter ratio
    // from the same place. When they do not (issue #391 - the layout used the primary
    // screen, while the hint used the screen of the widget), the fitted page does not
    // fill the viewport. Logical size zooming changes that ratio, so it detects the
    // two sources of truth without a second monitor.
    for (bool logicalSizeZooming : { false, true })
    {
        DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));

        pdf::PDFRenderer::Features features = fixture.widget.getDrawWidgetProxy()->getFeatures();
        features.setFlag(pdf::PDFRenderer::LogicalSizeZooming, logicalSizeZooming);
        fixture.widget.getDrawWidgetProxy()->setFeatures(features);
        fixture.settle();

        fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);

        QVERIFY_FITS(fixture.getPageSizeInPixels().width(), fixture.getAvailableSize().width());
    }
}

void DrawSpaceTest::fitDoesNotOscillate()
{
    // The fit reserves the space of the scrollbars which will become visible. The
    // reservation must not be derived from the scrollbars which are visible right
    // now - the zoom would keep switching between the two states forever.
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::SinglePage, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);

    const pdf::PDFReal zoom = fixture.widget.getDrawWidgetProxy()->getZoom();

    for (int i = 0; i < 5; ++i)
    {
        fixture.widget.layout()->activate();
        fixture.widget.getDrawWidgetProxy()->update();
        QCOMPARE(fixture.widget.getDrawWidgetProxy()->getZoom(), zoom);
    }
}

void DrawSpaceTest::rasterCacheMatrixHasUniformScale()
{
    using Cache = pdf::PDFAsynchronousPageRasterCache;

    // Page of 2592 x 1728 points placed into 1618 x 1079 pixels - the size is rounded
    // to whole pixels, so the scales differ a little. They are unified and the page is
    // shrunk around the center of the placed page by less than a pixel.
    const QSize pageSize(1618, 1079);
    const QTransform matrix(1618.0 / 2592.0, 0.0, 0.0, -1079.0 / 1728.0, 0.0, 1079.0);
    const QTransform uniform = Cache::createUniformScaleMatrix(matrix, pageSize);

    QCOMPARE(uniform.type(), QTransform::TxScale);
    QCOMPARE(uniform.m11(), -uniform.m22());
    QCOMPARE(uniform.m11(), qMin(matrix.m11(), -matrix.m22()));

    const QRectF placedPage(QPointF(0.0, 0.0), QSizeF(pageSize));
    const QRectF mappedPage = uniform.mapRect(QRectF(0.0, 0.0, 2592.0, 1728.0));
    QVERIFY(placedPage.adjusted(-1.0e-6, -1.0e-6, 1.0e-6, 1.0e-6).contains(mappedPage));
    QVERIFY(placedPage.width() - mappedPage.width() < 1.0);
    QVERIFY(placedPage.height() - mappedPage.height() < 1.0);
    QVERIFY(qAbs(mappedPage.center().x() - placedPage.center().x()) < 1.0e-6);
    QVERIFY(qAbs(mappedPage.center().y() - placedPage.center().y()) < 1.0e-6);

    // Page rotated by a quarter turn
    const QSize rotatedPageSize(1079, 1618);
    const QTransform rotatedMatrix(0.0, 1618.0 / 2592.0, 1079.0 / 1728.0, 0.0, 0.0, 0.0);
    const QTransform rotatedUniform = Cache::createUniformScaleMatrix(rotatedMatrix, rotatedPageSize);

    QCOMPARE(rotatedUniform.m11(), 0.0);
    QCOMPARE(rotatedUniform.m22(), 0.0);
    QCOMPARE(rotatedUniform.m12(), rotatedUniform.m21());
    QCOMPARE(rotatedUniform.m12(), qMin(rotatedMatrix.m12(), rotatedMatrix.m21()));

    const QRectF rotatedPlacedPage(QPointF(0.0, 0.0), QSizeF(rotatedPageSize));
    const QRectF rotatedMappedPage = rotatedUniform.mapRect(QRectF(0.0, 0.0, 2592.0, 1728.0));
    QVERIFY(rotatedPlacedPage.adjusted(-1.0e-6, -1.0e-6, 1.0e-6, 1.0e-6).contains(rotatedMappedPage));
    QVERIFY(rotatedPlacedPage.height() - rotatedMappedPage.height() < 1.0);

    // Uniform matrix, really different scales and a general rotation are kept
    const QTransform alreadyUniform(0.5, 0.0, 0.0, -0.5, 0.0, 864.0);
    QCOMPARE(Cache::createUniformScaleMatrix(alreadyUniform, QSize(1296, 864)), alreadyUniform);

    const QTransform stretched(0.5, 0.0, 0.0, -0.75, 0.0, 1296.0);
    QCOMPARE(Cache::createUniformScaleMatrix(stretched, QSize(1296, 1296)), stretched);

    QTransform rotated;
    rotated.rotate(30.0);
    QCOMPARE(Cache::createUniformScaleMatrix(rotated, QSize(1000, 1000)), rotated);
}

/// Synthetic heavy plan sheet and helpers of the tests of the sharp page images
class HeavyPageTestHelper
{
public:
    HeavyPageTestHelper() = delete;

    /// Creates a document of three Architect D sheets (2592 x 1728 points). The middle
    /// sheet is heavy, similar to a lighting plan with photos: large images with soft
    /// masks in the upper part, page transparency group, vector schedule at the bottom.
    /// The other sheets are light - they are prefetched, when the heavy sheet is displayed.
    static pdf::PDFDocument createDocument()
    {
        QByteArray file = "%PDF-1.7\n";
        std::vector<qint64> offsets;
        QRandomGenerator random(20261120);

        auto addObject = [&](const QByteArray& dictionary, const QByteArray& streamData)
        {
            offsets.push_back(file.size());
            file += QByteArray::number(qint64(offsets.size())) + " 0 obj\n" + dictionary;
            if (!streamData.isNull())
            {
                file += "\nstream\n" + streamData + "\nendstream";
            }
            file += "\nendobj\n";
            return qint64(offsets.size());
        };

        // Image of random cells of 3 x 3 pixels - any rescaled preview differs from it a lot
        auto createImageData = [&](int width, int height, int components)
        {
            QByteArray data(width * height * components, Qt::Uninitialized);
            std::vector<quint8> cells(((width + 2) / 3) * components);
            for (int y = 0; y < height; ++y)
            {
                if (y % 3 == 0)
                {
                    for (quint8& value : cells)
                    {
                        value = quint8(random.bounded(256));
                    }
                }

                for (int x = 0; x < width; ++x)
                {
                    for (int c = 0; c < components; ++c)
                    {
                        data[(y * width + x) * components + c] = char(cells[(x / 3) * components + c]);
                    }
                }
            }
            return data;
        };

        auto addImage = [&](int width, int height)
        {
            QByteArray maskData(width * height, Qt::Uninitialized);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    maskData[y * width + x] = char(160 + (95 * x) / width);
                }
            }

            const QByteArray size = "/Width " + QByteArray::number(width) + " /Height " + QByteArray::number(height);
            const qint64 mask = addObject("<< /Type /XObject /Subtype /Image " + size + " /ColorSpace /DeviceGray /BitsPerComponent 8 /Length " + QByteArray::number(maskData.size()) + " >>", maskData);
            const QByteArray imageData = createImageData(width, height, 3);
            return addObject("<< /Type /XObject /Subtype /Image " + size + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask " + QByteArray::number(mask) + " 0 R /Length " + QByteArray::number(imageData.size()) + " >>", imageData);
        };

        auto addPage = [&](const QByteArray& content, const std::vector<qint64>& images)
        {
            QByteArray xobjects;
            for (size_t i = 0; i < images.size(); ++i)
            {
                xobjects += "/Im" + QByteArray::number(qint64(i)) + " " + QByteArray::number(images[i]) + " 0 R ";
            }

            const qint64 contents = addObject("<< /Length " + QByteArray::number(content.size()) + " >>", content);
            return addObject("<< /Type /Page /Parent 1 0 R /MediaBox [0 0 2592 1728] /Group << /S /Transparency /CS /DeviceRGB >> /Resources << /XObject << " + xobjects + ">> >> /Contents " + QByteArray::number(contents) + " 0 R >>", QByteArray());
        };

        // Object 1 is the page tree, it is written at the end
        offsets.push_back(0);

        // Vector schedule - grid of thin lines at the bottom of the sheet
        QByteArray schedule = "q 0 0 0 RG 0.5 w\n";
        for (int row = 0; row <= 20; ++row)
        {
            schedule += "100 " + QByteArray::number(60 + row * 16) + " m 2492 " + QByteArray::number(60 + row * 16) + " l\n";
        }
        for (int column = 0; column <= 24; ++column)
        {
            schedule += QByteArray::number(100 + column * 99.6, 'f', 1) + " 60 m " + QByteArray::number(100 + column * 99.6, 'f', 1) + " 380 l\n";
        }
        schedule += "S Q\n";

        // Heavy sheet - strips of images over the whole width of the sheet
        std::vector<qint64> heavyImages;
        QByteArray heavyContent;
        for (int i = 0; i < 6; ++i)
        {
            heavyImages.push_back(addImage(1728, 160));
            heavyContent += "q 2592 0 0 200 0 " + QByteArray::number(1500 - i * 200) + " cm /Im" + QByteArray::number(i) + " Do Q\n";
        }
        heavyContent += schedule;

        std::vector<qint64> lightImages = { addImage(120, 120) };
        const QByteArray lightContent = "q 600 0 0 600 996 800 cm /Im0 Do Q\n" + schedule;

        const qint64 page0 = addPage(lightContent, lightImages);
        const qint64 page1 = addPage(heavyContent, heavyImages);
        const qint64 page2 = addPage(lightContent, lightImages);

        offsets[0] = file.size();
        file += "1 0 obj\n<< /Type /Pages /Count 3 /Kids [" + QByteArray::number(page0) + " 0 R " + QByteArray::number(page1) + " 0 R " + QByteArray::number(page2) + " 0 R] >>\nendobj\n";
        const qint64 catalog = addObject("<< /Type /Catalog /Pages 1 0 R >>", QByteArray());

        const qint64 xrefOffset = file.size();
        file += "xref\n0 " + QByteArray::number(qint64(offsets.size() + 1)) + "\n0000000000 65535 f \n";
        for (qint64 offset : offsets)
        {
            file += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        }
        file += "trailer\n<< /Size " + QByteArray::number(qint64(offsets.size() + 1)) + " /Root " + QByteArray::number(catalog) + " 0 R >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";

        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument document = reader.readFromBuffer(file);
        Q_ASSERT(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK);
        return document;
    }

    /// Mean absolute difference of the color channels of two images of the same size
    static double getMeanDifference(const QImage& left, const QImage& right)
    {
        Q_ASSERT(left.size() == right.size());

        const QImage leftImage = left.convertToFormat(QImage::Format_RGB32);
        const QImage rightImage = right.convertToFormat(QImage::Format_RGB32);

        qint64 sum = 0;
        for (int y = 0; y < leftImage.height(); ++y)
        {
            const QRgb* leftLine = reinterpret_cast<const QRgb*>(leftImage.constScanLine(y));
            const QRgb* rightLine = reinterpret_cast<const QRgb*>(rightImage.constScanLine(y));
            for (int x = 0; x < leftImage.width(); ++x)
            {
                sum += qAbs(qRed(leftLine[x]) - qRed(rightLine[x])) +
                       qAbs(qGreen(leftLine[x]) - qGreen(rightLine[x])) +
                       qAbs(qBlue(leftLine[x]) - qBlue(rightLine[x]));
            }
        }

        return double(sum) / double(3LL * leftImage.width() * leftImage.height());
    }

    /// Processes events until the condition holds, returns false on a timeout
    template<typename Condition>
    static bool waitFor(Condition condition, int timeout = 30000)
    {
        QElapsedTimer timer;
        timer.start();

        while (!condition())
        {
            if (timer.elapsed() > timeout)
            {
                return false;
            }

            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }

        return true;
    }
};

/// Page view over the synthetic heavy sheet. The page view is painted into an image,
/// with the painter of the page view (raster cache path), as the draw widget does it.
struct HeavyPageFixture
{
    static constexpr pdf::PDFInteger HEAVY_PAGE = 1;

    DrawSpaceFixture fixture;
    pdf::PDFDrawWidgetProxy* proxy;
    pdf::PDFAsynchronousPageCompiler* compiler;
    pdf::PDFAsynchronousPageRasterCache* rasterCache;

    HeavyPageFixture() :
        fixture(HeavyPageTestHelper::createDocument(), pdf::PageLayout::SinglePage, QSize(1000, 800)),
        proxy(fixture.widget.getDrawWidgetProxy()),
        compiler(proxy->getCompiler()),
        rasterCache(proxy->getPageRasterCache())
    {

    }

    QRect getViewRect() const { return fixture.widget.getDrawWidget()->getWidget()->rect(); }

    /// Paints the page view as the draw widget does
    QImage paint()
    {
        QImage image(getViewRect().size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::gray);
        QPainter painter(&image);
        proxy->draw(&painter, getViewRect());
        painter.end();
        return image;
    }

    /// Paints the page view directly from the compiled pages, without the raster
    /// cache (as a magnifier does it) - this is the sharp reference image.
    QImage paintReference()
    {
        QImage image(getViewRect().size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::gray);
        QPainter painter(&image);
        painter.setWorldTransform(QTransform::fromScale(1.0, 1.0 + 1.0e-9));
        proxy->draw(&painter, getViewRect());
        painter.end();
        return image;
    }

    /// Prefetches the neighbouring pages, as the main window does after
    /// each change of the visible area.
    void prefetch()
    {
        std::vector<pdf::PDFInteger> currentPages = proxy->getPagesIntersectingRect(getViewRect());
        if (!currentPages.empty())
        {
            proxy->prefetchPages(currentPages.back());
        }
    }

    /// Paints the page view and prefetches the neighbouring pages again and again, whenever
    /// the page view asks for a repaint (a page has been compiled, an image has been
    /// rasterized), as the draw widget and the main window do it, until all the work is
    /// done. Returns the last painted image, or a null image, if the work never ends.
    QImage paintUntilIdle()
    {
        bool isDirty = true;
        QMetaObject::Connection repaintConnection = QObject::connect(proxy, &pdf::PDFDrawWidgetProxy::repaintNeeded, [&isDirty]() { isDirty = true; });
        QMetaObject::Connection pageConnection = QObject::connect(proxy, &pdf::PDFDrawWidgetProxy::pageImageChanged, [&isDirty]() { isDirty = true; });

        QImage image;
        QElapsedTimer timer;
        timer.start();

        while (true)
        {
            if (timer.elapsed() > 60000)
            {
                image = QImage();
                break;
            }

            if (isDirty)
            {
                isDirty = false;
                image = paint();
                prefetch();
            }

            if (!compiler->hasPendingTasks() && !rasterCache->hasPendingRequests())
            {
                // Results of the work are delivered by queued signals
                QCoreApplication::processEvents();
                if (!isDirty)
                {
                    break;
                }
            }

            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }

        QObject::disconnect(repaintConnection);
        QObject::disconnect(pageConnection);
        return image;
    }
};

void DrawSpaceTest::heavyPageBecomesSharpAfterZoomAndScroll()
{
    using DrawResult = pdf::PDFAsynchronousPageRasterCache::DrawResult;
    using CompileMode = pdf::PDFAsynchronousPageCompiler::CompileMode;

    HeavyPageFixture heavy;
    heavy.fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);
    heavy.proxy->goToPage(HeavyPageFixture::HEAVY_PAGE);
    heavy.fixture.settle();
    QCOMPARE(heavy.proxy->getPagesIntersectingRect(heavy.getViewRect()), std::vector<pdf::PDFInteger>{ HeavyPageFixture::HEAVY_PAGE });

    // Measure the compiled pages - the cache of the compiler is then made smaller than
    // the heavy page, but larger than the light pages, as with the 512 MB cache and a
    // real sheet with hundreds of megabytes of images.
    QVERIFY(!heavy.paintUntilIdle().isNull());
    QVERIFY(HeavyPageTestHelper::waitFor([&heavy]()
    {
        heavy.compiler->getCompiledPage(0, CompileMode::Persistent);
        heavy.compiler->getCompiledPage(HeavyPageFixture::HEAVY_PAGE, CompileMode::Persistent);
        return !heavy.compiler->hasPendingTasks() &&
               heavy.compiler->getCompiledPage(0, CompileMode::None) &&
               heavy.compiler->getCompiledPage(HeavyPageFixture::HEAVY_PAGE, CompileMode::None);
    }));

    const qint64 heavyCost = heavy.compiler->getCompiledPage(HeavyPageFixture::HEAVY_PAGE, CompileMode::None)->getMemoryConsumptionEstimate();
    const qint64 lightCost = heavy.compiler->getCompiledPage(0, CompileMode::None)->getMemoryConsumptionEstimate();
    const qint64 cacheLimit = heavyCost * 3 / 4;
    QVERIFY2(lightCost < cacheLimit / 2, qPrintable(QString("heavy %1, light %2").arg(heavyCost).arg(lightCost)));

    heavy.compiler->setCacheLimit(cacheLimit);
    heavy.compiler->reset();
    heavy.rasterCache->clear();

    // Initial zoom - the sharp image is drawn
    QImage image = heavy.paintUntilIdle();
    QVERIFY(!image.isNull());
    QCOMPARE(heavy.rasterCache->getLastDrawResult(HeavyPageFixture::HEAVY_PAGE), DrawResult::Exact);

    auto verifySharp = [&heavy](const QImage& painted, const QImage& preview, const char* step)
    {
        const QImage reference = heavy.paintReference();
        const double sharpDifference = HeavyPageTestHelper::getMeanDifference(painted, reference);
        const double previewDifference = preview.isNull() ? 0.0 : HeavyPageTestHelper::getMeanDifference(preview, reference);
        const QString message = QString("%1: difference of the painted image %2, of the preview %3").arg(step).arg(sharpDifference).arg(previewDifference);
        QVERIFY2(heavy.rasterCache->getLastDrawResult(HeavyPageFixture::HEAVY_PAGE) == DrawResult::Exact, qPrintable(message));
        QVERIFY2(preview.isNull() || sharpDifference * 4.0 < previewDifference, qPrintable(message));
        QVERIFY2(sharpDifference < 3.0, qPrintable(message));

        // Heavy page stays compiled, prefetch of the neighbouring pages never removes it
        QVERIFY2(heavy.compiler->getCompiledPage(HeavyPageFixture::HEAVY_PAGE, CompileMode::None), qPrintable(message));
    };

    // Zoom to 113 % - the first painting is the scaled preview of the previous zoom,
    // then the sharp image replaces it and stays there, also after the prefetch.
    heavy.proxy->zoom(1.13);
    heavy.fixture.settle();
    const QImage zoomPreview = heavy.paint();
    QCOMPARE(heavy.rasterCache->getLastDrawResult(HeavyPageFixture::HEAVY_PAGE), DrawResult::Preview);

    image = heavy.paintUntilIdle();
    QVERIFY(!image.isNull());
    verifySharp(image, zoomPreview, "zoom");

    // Neighbouring pages have been compiled, but they were not kept, because they
    // don't fit into the cache together with the heavy page. They are not compiled again.
    QVERIFY(!heavy.compiler->getCompiledPage(0, CompileMode::None));
    QVERIFY(!heavy.compiler->getCompiledPage(2, CompileMode::None));
    heavy.prefetch();
    QVERIFY(!heavy.compiler->hasPendingTasks());

    // Scroll - the image of the whole page is reused, it is never replaced by a preview
    for (const QPoint offset : { QPoint(0, -300), QPoint(-400, 0), QPoint(250, 120) })
    {
        const QPoint scrolled = heavy.proxy->scrollByPixels(offset);
        QVERIFY(scrolled != QPoint());
        heavy.paint();
        QCOMPARE(heavy.rasterCache->getLastDrawResult(HeavyPageFixture::HEAVY_PAGE), DrawResult::Exact);

        image = heavy.paintUntilIdle();
        QVERIFY(!image.isNull());
        verifySharp(image, QImage(), "scroll");
    }
}

void DrawSpaceTest::rasterCacheDrawsImageWithoutCompiledPage()
{
    using Cache = pdf::PDFAsynchronousPageRasterCache;

    HeavyPageFixture heavy;
    QVERIFY(HeavyPageTestHelper::waitFor([&heavy]() { heavy.compiler->getCompiledPage(0, pdf::PDFAsynchronousPageCompiler::CompileMode::Persistent); return !heavy.compiler->hasPendingTasks(); }));
    QCoreApplication::processEvents();

    Cache cache(nullptr);
    Cache::PageDrawParameters parameters;
    parameters.pageIndex = 0;
    parameters.page = heavy.compiler->getCompiledPageShared(0, pdf::PDFAsynchronousPageCompiler::CompileMode::None);
    QVERIFY(parameters.page);
    parameters.contentGeneration = heavy.compiler->getCacheGeneration();
    parameters.cropBox = QRectF(0, 0, 2592, 1728);
    parameters.pageSize = QSize(1296, 864);
    parameters.pageToLocal = QTransform(0.5, 0.0, 0.0, -0.5, 0.0, 864.0);
    parameters.visibleRegion = QRect(0, 0, 1296, 600);
    parameters.paperColor = Qt::white;

    QImage image(1296, 864, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);

    QSignalSpy spy(&cache, &Cache::imageReady);
    QVERIFY(!cache.drawPage(&painter, parameters));
    QVERIFY(cache.hasPendingRequests());
    QVERIFY(spy.wait(30000));

    // Compiled page has been removed from the cache of the compiler - the
    // image of the same generation is still exact and it is drawn.
    parameters.page.reset();
    QVERIFY(cache.drawPage(&painter, parameters));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Exact);
    QVERIFY(!cache.hasPendingRequests());

    // Other zoom without the compiled page - the image is drawn as a preview,
    // nothing can be rasterized until the page is compiled again.
    Cache::PageDrawParameters zoomed = parameters;
    zoomed.pageToLocal = QTransform(0.565, 0.0, 0.0, -0.565, 0.0, 976.32);
    zoomed.pageSize = QSize(1464, 976);
    QVERIFY(cache.drawPage(&painter, zoomed));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Preview);
    QVERIFY(!cache.hasPendingRequests());

    // Content of other generation is only a preview
    Cache::PageDrawParameters changed = parameters;
    changed.contentGeneration = parameters.contentGeneration + 1;
    QVERIFY(cache.drawPage(&painter, changed));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Preview);

    painter.end();
}

void DrawSpaceTest::rasterCacheKeepsSharpTaskWhileScrolling()
{
    using Cache = pdf::PDFAsynchronousPageRasterCache;

    HeavyPageFixture heavy;
    QVERIFY(HeavyPageTestHelper::waitFor([&heavy]() { heavy.compiler->getCompiledPage(0, pdf::PDFAsynchronousPageCompiler::CompileMode::Persistent); return !heavy.compiler->hasPendingTasks(); }));
    QCoreApplication::processEvents();

    // Page larger than the limit of the image of a whole page - only
    // the neighbourhood of the visible area is rasterized.
    const QSize pageSize(9072, 6048);
    QVERIFY(qint64(pageSize.width()) * pageSize.height() > Cache::MAXIMAL_FULL_PAGE_PIXELS);

    Cache cache(nullptr);
    Cache::PageDrawParameters parameters;
    parameters.pageIndex = 0;
    parameters.page = heavy.compiler->getCompiledPageShared(0, pdf::PDFAsynchronousPageCompiler::CompileMode::None);
    QVERIFY(parameters.page);
    parameters.contentGeneration = heavy.compiler->getCacheGeneration();
    parameters.cropBox = QRectF(0, 0, 2592, 1728);
    parameters.pageSize = pageSize;
    parameters.pageToLocal = QTransform(3.5, 0.0, 0.0, -3.5, 0.0, 6048.0);
    parameters.visibleRegion = QRect(3000, 2000, 1000, 800);
    parameters.paperColor = Qt::white;

    QImage image(1000, 800, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);

    QSignalSpy spy(&cache, &Cache::imageReady);
    cache.drawPage(&painter, parameters);
    QVERIFY(cache.hasPendingRequests());

    // Scroll during the rasterization - the visible area still intersects the
    // rasterized area, so the sharp image of the current zoom is not cancelled.
    Cache::PageDrawParameters scrolledParameters = parameters;
    scrolledParameters.visibleRegion.translate(700, 300);
    cache.drawPage(&painter, scrolledParameters);

    QVERIFY(spy.wait(30000));

    // Image of the first request is finished - it covers the area of the first request
    QVERIFY(cache.drawPage(&painter, parameters));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Exact);

    // Scrolled area is only partially covered, the rest is requested now
    QVERIFY(cache.drawPage(&painter, scrolledParameters));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Preview);
    QVERIFY(cache.hasPendingRequests());
    QVERIFY(spy.wait(30000));
    QVERIFY(cache.drawPage(&painter, scrolledParameters));
    QCOMPARE(cache.getLastDrawResult(0), Cache::DrawResult::Exact);

    painter.end();
}

QTEST_MAIN(DrawSpaceTest)

#include "tst_drawspacetest.moc"
