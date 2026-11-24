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


#include "pdfprintlayout.h"
#include "pdfprintdialog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfprogramcontroller.h"
#include "pdfcms.h"

#include <QtTest>
#include <QLabel>
#include <QPainter>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QPushButton>
#include <QToolButton>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QTemporaryDir>

using namespace pdf;

class PrintTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void scaleModes();
    void fitCentersPage();
    void autoRotate();
    void halfSizeOnTabloid();
    void tilingCoversPage();
    void tilingPicksLessSheets();
    void tileNames();
    void pagePointToDevice();

    void printActualSizeToPdf();
    void printTilesToPdf();
    void drawPageForPrintingFitsPaper();

    void dialogSheetsAndPreview();
    void dialogPageRangeErrors();
    void dialogWarnsAboutClippedPages();
    void dialogPrintsAndRemembersSettings();

private:
    /// Document with pages of the given size, each with a filled black square
    /// comment of 1 x 1 in, whose lower left corner is at (100, 100) of the page
    static PDFDocument createDocument(const QSizeF& pageSize, int pageCount);

    /// Reads the PDF file written by the printer
    static PDFDocument readDocument(const QString& fileName);

    /// Draws the page 1:1 (one pixel per point) into an image
    static QImage drawPage(PDFDocument* document, PDFInteger pageIndex);

    static QPrinter* createPdfPrinter(const QString& fileName, QPageSize::PageSizeId pageSize);

    QTemporaryDir m_directory;
};

struct ProxyFixture
{
    PDFCMSManager cms{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };

    explicit ProxyFixture(PDFDocument* document)
    {
        widget.setDocument(PDFModifiedDocument(document, nullptr), {});
    }

    const PDFDrawWidgetProxy* proxy() { return widget.getDrawWidgetProxy(); }
};

static constexpr qreal ARCH_D_WIDTH = 24 * 72.0;
static constexpr qreal ARCH_D_HEIGHT = 36 * 72.0;

void PrintTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Print Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.filePath("settings"));
    QSettings().clear();
}

PDFDocument PrintTest::createDocument(const QSizeF& pageSize, int pageCount)
{
    PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), pageSize));
        builder.createAnnotationSquare(page, QRectF(100, 100, 72, 72), 0.0, Qt::black, Qt::black, "Title", "Subject", "Contents");
    }
    return builder.build();
}

PDFDocument PrintTest::readDocument(const QString& fileName)
{
    PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    PDFDocument document = reader.readFromFile(fileName);
    return document;
}

QImage PrintTest::drawPage(PDFDocument* document, PDFInteger pageIndex)
{
    ProxyFixture fixture(document);
    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    const QSize size = page->getRotatedMediaBox().size().toSize();
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    pdfviewer::PDFProgramController::drawPageForPrinting(&painter, QRectF(image.rect()), document, pageIndex, fixture.proxy());
    painter.end();
    return image;
}

QPrinter* PrintTest::createPdfPrinter(const QString& fileName, QPageSize::PageSizeId pageSize)
{
    QPrinter* printer = new QPrinter(QPrinter::HighResolution);
    printer->setOutputFormat(QPrinter::PdfFormat);
    printer->setOutputFileName(fileName);
    printer->setPageSize(QPageSize(pageSize));
    printer->setPageOrientation(QPageLayout::Portrait);
    return printer;
}

void PrintTest::scaleModes()
{
    const QSizeF page(ARCH_D_WIDTH, ARCH_D_HEIGHT);
    const QSizeF letter(612, 792);
    const qreal fit = qMin(612 / ARCH_D_WIDTH, 792 / ARCH_D_HEIGHT);

    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::FitToPaper;
    QCOMPARE(settings.getScale(page, letter), fit);
    QCOMPARE(settings.getScale(QSizeF(100, 100), letter), 6.12);

    settings.scaleMode = PDFPrintSettings::ScaleMode::ShrinkToFit;
    QCOMPARE(settings.getScale(page, letter), fit);
    QCOMPARE(settings.getScale(QSizeF(100, 100), letter), 1.0);

    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    QCOMPARE(settings.getScale(page, letter), 1.0);

    settings.scaleMode = PDFPrintSettings::ScaleMode::CustomScale;
    settings.customScale = 0.5;
    QCOMPARE(settings.getScale(page, letter), 0.5);
    settings.customScale = 0.0;
    QCOMPARE(settings.getScale(page, letter), 1.0);

    // Tiles are not used, when the page is fitted to the paper
    settings.tiling = true;
    QVERIFY(settings.isTilingUsed());
    settings.scaleMode = PDFPrintSettings::ScaleMode::FitToPaper;
    QVERIFY(!settings.isTilingUsed());
}

void PrintTest::fitCentersPage()
{
    PDFPrintSettings settings;
    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(3, QSizeF(300, 300), QSizeF(600, 800), QRectF(0, 0, 600, 800), settings);
    QCOMPARE(sheets.size(), size_t(1));
    QCOMPARE(sheets[0].pageIndex, PDFInteger(3));
    QCOMPARE(sheets[0].scale, 2.0);
    QVERIFY(!sheets[0].rotated);
    QVERIFY(!sheets[0].isTile());
    QCOMPARE(sheets[0].getPageRectOnPaper(), QRectF(0, 100, 600, 600));

    // Printable area with margins
    const std::vector<PDFPrintSheet> marginSheets = PDFPrintLayout::layoutPage(0, QSizeF(300, 300), QSizeF(600, 800), QRectF(20, 20, 560, 760), settings);
    QCOMPARE(marginSheets[0].getPageRectOnPaper(), QRectF(20, 120, 560, 560));

    // Actual size larger than the paper is centered (edges are cut off)
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    settings.autoRotate = false;
    const std::vector<PDFPrintSheet> actualSheets = PDFPrintLayout::layoutPage(0, QSizeF(1000, 1000), QSizeF(600, 800), QRectF(0, 0, 600, 800), settings);
    QCOMPARE(actualSheets.size(), size_t(1));
    QCOMPARE(actualSheets[0].getPageRectOnPaper(), QRectF(-200, -100, 1000, 1000));

    // Empty sizes give no sheets
    QVERIFY(PDFPrintLayout::layoutPage(0, QSizeF(), QSizeF(600, 800), QRectF(0, 0, 600, 800), settings).empty());
    QVERIFY(PDFPrintLayout::layoutPage(0, QSizeF(100, 100), QSizeF(600, 800), QRectF(), settings).empty());
}

void PrintTest::autoRotate()
{
    // Landscape sheet on a portrait letter paper
    const QSizeF page(ARCH_D_HEIGHT, ARCH_D_WIDTH);
    const QRectF printable(18, 18, 576, 756);

    PDFPrintSettings settings;
    std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(0, page, QSizeF(612, 792), printable, settings);
    QCOMPARE(sheets.size(), size_t(1));
    const PDFPrintSheet& sheet = sheets[0];
    QVERIFY(sheet.rotated);
    QCOMPARE(sheet.scale, qMin(576 / ARCH_D_WIDTH, 756 / ARCH_D_HEIGHT));

    const QRectF rect = sheet.getPageRectOnPaper();
    QVERIFY(qAbs(rect.width() - ARCH_D_WIDTH * sheet.scale) < 0.001);
    QVERIFY(qAbs(rect.height() - ARCH_D_HEIGHT * sheet.scale) < 0.001);
    QVERIFY(printable.adjusted(-0.001, -0.001, 0.001, 0.001).contains(rect));
    QVERIFY(qAbs(rect.center().x() - printable.center().x()) < 0.001);
    QVERIFY(qAbs(rect.center().y() - printable.center().y()) < 0.001);

    // Rotation by 90 degrees clockwise: top left corner of the page is in the top right corner,
    // top right corner of the page is in the bottom right corner
    QVERIFY(QLineF(sheet.pageToPaper.map(QPointF(0, 0)), rect.topRight()).length() < 0.001);
    QVERIFY(QLineF(sheet.pageToPaper.map(QPointF(page.width(), 0)), rect.bottomRight()).length() < 0.001);
    QVERIFY(QLineF(sheet.pageToPaper.map(QPointF(0, page.height())), rect.topLeft()).length() < 0.001);

    // Without the rotation, the page is smaller
    settings.autoRotate = false;
    sheets = PDFPrintLayout::layoutPage(0, page, QSizeF(612, 792), printable, settings);
    QVERIFY(!sheets[0].rotated);
    QVERIFY(sheets[0].scale < sheet.scale);

    // Page of the same orientation as the paper is not rotated
    settings.autoRotate = true;
    sheets = PDFPrintLayout::layoutPage(0, page.transposed(), QSizeF(612, 792), printable, settings);
    QVERIFY(!sheets[0].rotated);
}

void PrintTest::halfSizeOnTabloid()
{
    // ANSI D (22 x 34 in) at half size is exactly 11 x 17 in
    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::CustomScale;
    settings.customScale = 0.5;
    settings.tiling = true;

    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(0, QSizeF(22 * 72, 34 * 72), QSizeF(11 * 72, 17 * 72), QRectF(0, 0, 11 * 72, 17 * 72), settings);
    QCOMPARE(sheets.size(), size_t(1));
    QCOMPARE(sheets[0].scale, 0.5);
    QVERIFY(!sheets[0].rotated);
    QCOMPARE(sheets[0].overlap, 0.0);
    QCOMPARE(sheets[0].getPageRectOnPaper(), QRectF(0, 0, 11 * 72, 17 * 72));
}

void PrintTest::tilingCoversPage()
{
    // ARCH D at 100 % on letter paper with margins of 1/4 in, overlap 1/2 in
    const QSizeF page(ARCH_D_WIDTH, ARCH_D_HEIGHT);
    const QRectF printable(18, 18, 576, 756);

    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    settings.tiling = true;
    settings.tileOverlap = 36;
    settings.autoRotate = false;

    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(0, page, QSizeF(612, 792), printable, settings);

    // ceil((1728 - 36) / 540) = 4 columns, ceil((2592 - 36) / 720) = 4 rows
    QCOMPARE(sheets.size(), size_t(16));
    QCOMPARE(sheets[0].columns, 4);
    QCOMPARE(sheets[0].rows, 4);
    QCOMPARE(sheets[0].overlap, 36.0);

    for (int i = 0; i < 16; ++i)
    {
        QCOMPARE(sheets[i].row, i / 4);
        QCOMPARE(sheets[i].column, i % 4);
        QCOMPARE(sheets[i].scale, 1.0);
        QVERIFY(sheets[i].isTile());
    }

    // Neighbouring tiles are shifted by the printable area minus the overlap
    const QPointF origin = sheets[0].pageToPaper.map(QPointF(0, 0));
    QCOMPARE(sheets[1].pageToPaper.map(QPointF(0, 0)), origin - QPointF(540, 0));
    QCOMPARE(sheets[4].pageToPaper.map(QPointF(0, 0)), origin - QPointF(0, 720));

    // The page is centered in the area covered by the tiles
    const qreal coveredWidth = 4 * 540 + 36;
    const qreal coveredHeight = 4 * 720 + 36;
    QCOMPARE(origin, QPointF(18 + (coveredWidth - ARCH_D_WIDTH) / 2, 18 + (coveredHeight - ARCH_D_HEIGHT) / 2));

    // Every point of the page is printed on some tile
    for (qreal x = 0; x <= page.width(); x += 12)
    {
        for (qreal y = 0; y <= page.height(); y += 12)
        {
            const bool printed = std::any_of(sheets.cbegin(), sheets.cend(), [x, y](const PDFPrintSheet& sheet)
            {
                return sheet.printableRect.adjusted(-0.001, -0.001, 0.001, 0.001).contains(sheet.pageToPaper.map(QPointF(x, y)));
            });

            if (!printed)
            {
                QFAIL(qPrintable(QString("Point %1, %2 of the page is not printed").arg(x).arg(y)));
            }
        }
    }

    // A page, which fits the paper, is a single sheet
    const std::vector<PDFPrintSheet> smallPage = PDFPrintLayout::layoutPage(0, QSizeF(500, 700), QSizeF(612, 792), printable, settings);
    QCOMPARE(smallPage.size(), size_t(1));
    QVERIFY(!smallPage[0].isTile());
    QCOMPARE(smallPage[0].overlap, 0.0);

    // Overlap is limited by the half of the printable area
    settings.tileOverlap = 10000;
    const std::vector<PDFPrintSheet> largeOverlap = PDFPrintLayout::layoutPage(0, page, QSizeF(612, 792), printable, settings);
    QCOMPARE(largeOverlap[0].overlap, 288.0);
}

void PrintTest::tilingPicksLessSheets()
{
    // ARCH D at 100 % on letter: 4 x 4 tiles in portrait, 5 x 3 tiles rotated
    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    settings.tiling = true;
    settings.tileOverlap = 36;
    settings.autoRotate = true;

    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(0, QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), QSizeF(612, 792), QRectF(18, 18, 576, 756), settings);
    QCOMPARE(sheets.size(), size_t(15));
    QVERIFY(sheets[0].rotated);
    QCOMPARE(sheets[0].columns, 5);
    QCOMPARE(sheets[0].rows, 3);

    // Tiling turned off: one sheet, the page is cut off
    settings.tiling = false;
    const std::vector<PDFPrintSheet> single = PDFPrintLayout::layoutPage(0, QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), QSizeF(612, 792), QRectF(18, 18, 576, 756), settings);
    QCOMPARE(single.size(), size_t(1));
    QVERIFY(!single[0].rotated);
}

void PrintTest::tileNames()
{
    PDFPrintSheet sheet;
    QCOMPARE(sheet.getTileName(), QString("A1"));
    sheet.row = 1;
    sheet.column = 2;
    QCOMPARE(sheet.getTileName(), QString("B3"));
    sheet.row = 25;
    QCOMPARE(sheet.getTileName(), QString("Z3"));
    sheet.row = 26;
    sheet.column = 0;
    QCOMPARE(sheet.getTileName(), QString("AA1"));

    sheet.pageIndex = 1;
    sheet.rows = 2;
    sheet.columns = 3;
    sheet.row = 1;
    sheet.column = 2;
    sheet.scale = 1.0;
    QCOMPARE(pdfviewer::PDFPrintRenderer::getSheetDescription(sheet), QString("Page 2, tile B3 of 6 (2 rows, 3 columns), 100 %"));
    sheet.rows = 1;
    sheet.columns = 1;
    sheet.scale = 0.5;
    QCOMPARE(pdfviewer::PDFPrintRenderer::getSheetDescription(sheet), QString("Page 2, 50 %"));
}

void PrintTest::pagePointToDevice()
{
    // PDF user space has the y axis going up, the paper has it going down
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 1);
    const PDFPage* page = document.getCatalog()->getPage(0);
    QCOMPARE(PDFPrintLayout::getPageSize(page), QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT));

    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutPage(0, PDFPrintLayout::getPageSize(page), QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), QRectF(0, 0, ARCH_D_WIDTH, ARCH_D_HEIGHT), settings);
    QCOMPARE(sheets.size(), size_t(1));

    // 600 dpi
    const QTransform matrix = sheets[0].createPagePointToDeviceMatrix(page, 600.0 / 72.0);
    QVERIFY(QLineF(matrix.map(QPointF(0, 0)), QPointF(0, ARCH_D_HEIGHT * 600 / 72)).length() < 0.001);
    QVERIFY(QLineF(matrix.map(QPointF(72, 72)), QPointF(600, (ARCH_D_HEIGHT - 72) * 600 / 72)).length() < 0.001);
}

void PrintTest::printActualSizeToPdf()
{
    // ARCH D sheet printed 1:1 on ARCH D paper: the comment is at the same place on the paper
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 1);
    ProxyFixture fixture(&document);

    const QString fileName = m_directory.filePath("actual-size.pdf");
    std::unique_ptr<QPrinter> printer(createPdfPrinter(fileName, QPageSize::ArchD));
    QCOMPARE(pdfviewer::PDFPrintDialog::getPaperSize(printer.get()), QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT));

    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutDocument(&document, { 0 }, pdfviewer::PDFPrintDialog::getPaperSize(printer.get()), pdfviewer::PDFPrintDialog::getPrintableRect(printer.get()), settings);
    QCOMPARE(sheets.size(), size_t(1));

    int progressCalls = 0;
    QVERIFY(pdfviewer::PDFPrintRenderer::print(printer.get(), sheets, &document, fixture.proxy(), true, [&progressCalls]() { ++progressCalls; return true; }));
    QCOMPARE(progressCalls, 1);
    printer.reset();

    PDFDocument printed = readDocument(fileName);
    QCOMPARE(printed.getCatalog()->getPageCount(), size_t(1));
    const QRectF mediaBox = printed.getCatalog()->getPage(0)->getRotatedMediaBox();
    QVERIFY(qAbs(mediaBox.width() - ARCH_D_WIDTH) < 1.0);
    QVERIFY(qAbs(mediaBox.height() - ARCH_D_HEIGHT) < 1.0);

    // Comment at x 100..172, y 100..172 of the page (y going up)
    const QImage image = drawPage(&printed, 0);
    QCOMPARE(image.pixelColor(136, int(ARCH_D_HEIGHT) - 136).rgb(), QColor(Qt::black).rgb());
    QCOMPARE(image.pixelColor(104, int(ARCH_D_HEIGHT) - 104).rgb(), QColor(Qt::black).rgb());
    QCOMPARE(image.pixelColor(168, int(ARCH_D_HEIGHT) - 168).rgb(), QColor(Qt::black).rgb());
    QCOMPARE(image.pixelColor(96, int(ARCH_D_HEIGHT) - 136).rgb(), QColor(Qt::white).rgb());
    QCOMPARE(image.pixelColor(176, int(ARCH_D_HEIGHT) - 136).rgb(), QColor(Qt::white).rgb());
    QCOMPARE(image.pixelColor(136, int(ARCH_D_HEIGHT) - 96).rgb(), QColor(Qt::white).rgb());
    QCOMPARE(image.pixelColor(136, int(ARCH_D_HEIGHT) - 176).rgb(), QColor(Qt::white).rgb());
}

void PrintTest::printTilesToPdf()
{
    // ARCH D sheet at 100 % on letter paper: tiles taped together give the sheet at its true size
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 1);
    ProxyFixture fixture(&document);

    const QString fileName = m_directory.filePath("tiles.pdf");
    std::unique_ptr<QPrinter> printer(createPdfPrinter(fileName, QPageSize::Letter));

    PDFPrintSettings settings;
    settings.scaleMode = PDFPrintSettings::ScaleMode::ActualSize;
    settings.tiling = true;
    settings.tileOverlap = 36;
    settings.autoRotate = false;
    const QRectF printableRect = pdfviewer::PDFPrintDialog::getPrintableRect(printer.get());
    const std::vector<PDFPrintSheet> sheets = PDFPrintLayout::layoutDocument(&document, { 0 }, pdfviewer::PDFPrintDialog::getPaperSize(printer.get()), printableRect, settings);

    // PDF output has no margins: ceil((1728 - 36) / 576) = 3 columns, ceil((2592 - 36) / 756) = 4 rows
    QCOMPARE(printableRect, QRectF(0, 0, 612, 792));
    QCOMPARE(sheets.size(), size_t(12));
    QVERIFY(pdfviewer::PDFPrintRenderer::print(printer.get(), sheets, &document, fixture.proxy(), true, {}));
    printer.reset();

    PDFDocument printed = readDocument(fileName);
    QCOMPARE(printed.getCatalog()->getPageCount(), size_t(12));

    // Center of the comment (page point 136, 136 with y going up) is on the tile at the expected place
    const QPointF center(136, ARCH_D_HEIGHT - 136);
    bool found = false;
    for (size_t i = 0; i < sheets.size(); ++i)
    {
        const QPointF paperPoint = sheets[i].pageToPaper.map(center);
        if (QRectF(10, 10, 592, 772).contains(paperPoint))
        {
            const QImage image = drawPage(&printed, PDFInteger(i));
            QCOMPARE(image.pixelColor(paperPoint.toPoint()).rgb(), QColor(Qt::black).rgb());
            QCOMPARE(image.pixelColor((paperPoint + QPointF(40, 0)).toPoint()).rgb(), QColor(Qt::white).rgb());
            found = true;
        }
    }
    QVERIFY(found);

    // Tile marks: tile B2 has the trim lines at the overlap
    const QImage tileB2 = drawPage(&printed, 4);
    int darkPixels = 0;
    for (int y = 100; y < 700; ++y)
    {
        if (qGray(tileB2.pixel(35, y)) < 240 || qGray(tileB2.pixel(36, y)) < 240)
        {
            ++darkPixels;
        }
    }
    QVERIFY(darkPixels > 100);
}

void PrintTest::drawPageForPrintingFitsPaper()
{
    // Page of 300 x 300 points fitted into a paper of 600 x 300 pixels is centered
    PDFDocument document = createDocument(QSizeF(300, 300), 1);
    ProxyFixture fixture(&document);

    QImage image(600, 300, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    pdfviewer::PDFProgramController::drawPageForPrinting(&painter, QRectF(image.rect()), &document, 0, fixture.proxy());
    painter.end();

    QCOMPARE(image.pixelColor(150 + 136, 300 - 136).rgb(), QColor(Qt::black).rgb());
    QCOMPARE(image.pixelColor(136, 300 - 136).rgb(), QColor(Qt::white).rgb());
}

void PrintTest::dialogSheetsAndPreview()
{
    QSettings().clear();
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 3);
    ProxyFixture fixture(&document);

    pdfviewer::PDFPrintDialog dialog(&document, 1, fixture.proxy(), QPrinter::HighResolution, nullptr);
    QComboBox* printerComboBox = dialog.findChild<QComboBox*>("printerComboBox");
    QComboBox* paperComboBox = dialog.findChild<QComboBox*>("paperComboBox");
    QComboBox* scaleComboBox = dialog.findChild<QComboBox*>("scaleComboBox");
    QCheckBox* tilingCheckBox = dialog.findChild<QCheckBox*>("tilingCheckBox");
    QCheckBox* autoRotateCheckBox = dialog.findChild<QCheckBox*>("autoRotateCheckBox");
    QLabel* sheetLabel = dialog.findChild<QLabel*>("sheetLabel");
    QToolButton* nextSheetButton = dialog.findChild<QToolButton*>("nextSheetButton");
    QToolButton* previousSheetButton = dialog.findChild<QToolButton*>("previousSheetButton");
    pdfviewer::PDFPrintPreviewWidget* preview = dialog.findChild<pdfviewer::PDFPrintPreviewWidget*>("printPreviewWidget");
    QVERIFY(printerComboBox && paperComboBox && scaleComboBox && tilingCheckBox && autoRotateCheckBox && sheetLabel && nextSheetButton && previousSheetButton && preview);

    printerComboBox->setCurrentIndex(printerComboBox->findData(QString()));
    paperComboBox->setCurrentIndex(paperComboBox->findData(QPageSize(QPageSize::Letter).name()));
    QCOMPARE(dialog.getPrinter()->pageLayout().pageSize().id(), QPageSize::Letter);

    // Fit to paper: one sheet per page, tiling disabled
    scaleComboBox->setCurrentIndex(scaleComboBox->findData(0));
    QCOMPARE(dialog.getSheets().size(), size_t(3));
    QVERIFY(!tilingCheckBox->isEnabled());
    QVERIFY(sheetLabel->text().startsWith("Sheet 1 of 3"));
    QVERIFY(!previousSheetButton->isEnabled());

    dialog.resize(900, 600);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    // Preview shows the paper with the black comment
    QImage paper = preview->getPaperImage();
    QVERIFY(!paper.isNull());
    QVERIFY(qAbs(qreal(paper.width()) / paper.height() - 612.0 / 792.0) < 0.01);
    int blackPixels = 0;
    for (int y = 0; y < paper.height(); ++y)
    {
        for (int x = 0; x < paper.width(); ++x)
        {
            if (qGray(paper.pixel(x, y)) < 64)
            {
                ++blackPixels;
            }
        }
    }
    QVERIFY(blackPixels > 0);

    // Actual size with tiles: 3 x 4 tiles for each page (PDF output has no margins, auto rotation off)
    autoRotateCheckBox->setChecked(false);
    scaleComboBox->setCurrentIndex(scaleComboBox->findData(2));
    QVERIFY(tilingCheckBox->isEnabled());
    tilingCheckBox->setChecked(true);
    QCOMPARE(dialog.getSheets().size(), size_t(3 * 12));
    QVERIFY(sheetLabel->text().startsWith("Sheet 1 of 36"));

    nextSheetButton->click();
    QCOMPARE(dialog.getCurrentSheet(), 1);
    QVERIFY(sheetLabel->text().contains("tile A2"));
    QVERIFY(previousSheetButton->isEnabled());
    QVERIFY(!preview->getPaperImage().isNull());

    dialog.setCurrentSheet(1000);
    QCOMPARE(dialog.getCurrentSheet(), 35);
    QVERIFY(!nextSheetButton->isEnabled());

    // Current page only
    dialog.findChild<QRadioButton*>("currentPageRadioButton")->setChecked(true);
    const std::vector<PDFPrintSheet> sheets = dialog.getSheets();
    QCOMPARE(sheets.size(), size_t(12));
    QCOMPARE(sheets.front().pageIndex, PDFInteger(1));
    QCOMPARE(dialog.getCurrentSheet(), 11);
}

void PrintTest::dialogPageRangeErrors()
{
    QSettings().clear();
    PDFDocument document = createDocument(QSizeF(612, 792), 3);
    ProxyFixture fixture(&document);

    pdfviewer::PDFPrintDialog dialog(&document, 0, fixture.proxy(), QPrinter::HighResolution, nullptr);
    QLineEdit* pageRangeEdit = dialog.findChild<QLineEdit*>("pageRangeEdit");
    QLabel* errorLabel = dialog.findChild<QLabel*>("errorLabel");
    QPushButton* printButton = dialog.findChild<QPushButton*>("printButton");
    QVERIFY(pageRangeEdit && errorLabel && printButton);

    pageRangeEdit->setText("1, 3");
    QVERIFY(dialog.findChild<QRadioButton*>("pageRangeRadioButton")->isChecked());
    QCOMPARE(dialog.getPageIndices(), (std::vector<PDFInteger>{ 0, 2 }));
    QVERIFY(errorLabel->text().isEmpty());
    QVERIFY(printButton->isEnabled());

    pageRangeEdit->setText("5");
    QVERIFY(dialog.getPageIndices().empty());
    QVERIFY(!errorLabel->text().isEmpty());
    QVERIFY(!printButton->isEnabled());

    pageRangeEdit->setText("abc");
    QVERIFY(dialog.getPageIndices().empty());
    QVERIFY(!errorLabel->text().isEmpty());

    dialog.findChild<QRadioButton*>("allPagesRadioButton")->setChecked(true);
    QCOMPARE(dialog.getPageIndices().size(), size_t(3));
    QVERIFY(errorLabel->text().isEmpty());
    QVERIFY(printButton->isEnabled());
}

void PrintTest::dialogWarnsAboutClippedPages()
{
    QSettings().clear();
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 1);
    ProxyFixture fixture(&document);

    pdfviewer::PDFPrintDialog dialog(&document, 0, fixture.proxy(), QPrinter::HighResolution, nullptr);
    QComboBox* printerComboBox = dialog.findChild<QComboBox*>("printerComboBox");
    QComboBox* paperComboBox = dialog.findChild<QComboBox*>("paperComboBox");
    QComboBox* scaleComboBox = dialog.findChild<QComboBox*>("scaleComboBox");
    QCheckBox* tilingCheckBox = dialog.findChild<QCheckBox*>("tilingCheckBox");
    QLabel* errorLabel = dialog.findChild<QLabel*>("errorLabel");
    QLabel* summaryLabel = dialog.findChild<QLabel*>("summaryLabel");

    printerComboBox->setCurrentIndex(printerComboBox->findData(QString()));
    paperComboBox->setCurrentIndex(paperComboBox->findData(QPageSize(QPageSize::Letter).name()));
    scaleComboBox->setCurrentIndex(scaleComboBox->findData(2));
    tilingCheckBox->setChecked(false);
    QVERIFY(errorLabel->text().contains("larger than the paper"));
    QVERIFY(summaryLabel->text().contains("100.0 %"));

    tilingCheckBox->setChecked(true);
    QVERIFY(errorLabel->text().isEmpty());

    // ARCH D paper at 100 % fits
    tilingCheckBox->setChecked(false);
    paperComboBox->setCurrentIndex(paperComboBox->findData(QPageSize(QPageSize::ArchD).name()));
    QVERIFY(errorLabel->text().isEmpty());
    QCOMPARE(dialog.getSheets().size(), size_t(1));

    // Half size on 11 x 17 doesn't fit a 24 x 36 in sheet
    paperComboBox->setCurrentIndex(paperComboBox->findData(QPageSize(QPageSize::Tabloid).name()));
    scaleComboBox->setCurrentIndex(scaleComboBox->findData(3));
    QCOMPARE(dialog.getPrintSettings().customScale, 0.5);
    QVERIFY(errorLabel->text().contains("larger than the paper"));

    // Shrink oversized pages
    scaleComboBox->setCurrentIndex(scaleComboBox->findData(1));
    QVERIFY(errorLabel->text().isEmpty());
}

void PrintTest::dialogPrintsAndRemembersSettings()
{
    QSettings().clear();
    PDFDocument document = createDocument(QSizeF(ARCH_D_WIDTH, ARCH_D_HEIGHT), 2);
    ProxyFixture fixture(&document);
    const QString fileName = m_directory.filePath("dialog.pdf");

    {
        pdfviewer::PDFPrintDialog dialog(&document, 0, fixture.proxy(), QPrinter::HighResolution, nullptr);
        QComboBox* printerComboBox = dialog.findChild<QComboBox*>("printerComboBox");
        QComboBox* paperComboBox = dialog.findChild<QComboBox*>("paperComboBox");
        QComboBox* scaleComboBox = dialog.findChild<QComboBox*>("scaleComboBox");
        QDoubleSpinBox* customScaleSpinBox = dialog.findChild<QDoubleSpinBox*>("customScaleSpinBox");
        QCheckBox* tilingCheckBox = dialog.findChild<QCheckBox*>("tilingCheckBox");

        printerComboBox->setCurrentIndex(printerComboBox->findData(QString()));
        paperComboBox->setCurrentIndex(paperComboBox->findData(QPageSize(QPageSize::Tabloid).name()));
        scaleComboBox->setCurrentIndex(scaleComboBox->findData(4));
        QVERIFY(customScaleSpinBox->isEnabled());
        customScaleSpinBox->setValue(25.0);
        tilingCheckBox->setChecked(true);
        QCOMPARE(dialog.getPrintSettings().customScale, 0.25);

        dialog.setOutputFileName(fileName);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.getPrinter()->outputFileName(), fileName);

        const std::vector<PDFPrintSheet> sheets = dialog.getSheets();
        QCOMPARE(sheets.size(), size_t(2));
        QVERIFY(pdfviewer::PDFPrintRenderer::print(dialog.getPrinter(), sheets, &document, fixture.proxy(), true, {}));
    }

    PDFDocument printed = readDocument(fileName);
    QCOMPARE(printed.getCatalog()->getPageCount(), size_t(2));
    const QRectF mediaBox = printed.getCatalog()->getPage(0)->getRotatedMediaBox();
    QVERIFY(qAbs(mediaBox.width() - 11 * 72) < 1.0);
    QVERIFY(qAbs(mediaBox.height() - 17 * 72) < 1.0);

    // Remembered settings
    pdfviewer::PDFPrintDialog dialog(&document, 0, fixture.proxy(), QPrinter::HighResolution, nullptr);
    QCOMPARE(dialog.findChild<QComboBox*>("printerComboBox")->currentData().toString(), QString());
    QCOMPARE(dialog.findChild<QComboBox*>("paperComboBox")->currentData().toString(), QPageSize(QPageSize::Tabloid).name());
    QCOMPARE(dialog.findChild<QComboBox*>("scaleComboBox")->currentData().toInt(), 4);
    QCOMPARE(dialog.findChild<QDoubleSpinBox*>("customScaleSpinBox")->value(), 25.0);
    QVERIFY(dialog.findChild<QCheckBox*>("tilingCheckBox")->isChecked());
    QCOMPARE(dialog.getPrintSettings().customScale, 0.25);
    QCOMPARE(dialog.getPrinter()->pageLayout().pageSize().id(), QPageSize::Tabloid);
}

QTEST_MAIN(PrintTest)

#include "tst_printtest.moc"
