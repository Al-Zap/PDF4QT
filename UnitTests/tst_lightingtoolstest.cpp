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


#include "countlegend.h"
#include "fixtureschedule.h"
#include "footcandlestats.h"
#include "lightingdialogs.h"
#include "lightingshapetool.h"
#include "lightingtable.h"
#include "lightingtext.h"
#include "lightingtools.h"
#include "wiretakeoff.h"

#include "pdfannotation.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdfrenderer.h"
#include "pdftextlayoutgenerator.h"
#include "pdfwidgettool.h"

#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

#include <array>
#include <functional>

using namespace pdf;
using namespace pdfplugin;

class LightingToolsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void tableSize();
    void tableRotations();
    void tableContentStream();
    void tableAnnotationIsDrawn();
    void tableOnRotatedPageIsUpright();
    void tableScanAndUpdate();
    void tableSavedAndReopened();

    void legendTable();
    void legendsFollowTheCount();

    void parseFootcandleValues();
    void footcandleStatistics();
    void footcandleValuesFromText();
    void footcandleValuesOfRotatedText();
    void footcandleTable();

    void scheduleTable();
    void scheduleTypes();
    void scheduleWithoutHeader();

    void wireScale();
    void wireRunsAndTotals();
    void wireTakeoffTable();
    void wireScaleOfTheDrawing();
    void takeoffTablesFollowTheRuns();

    void tablePositionNextToArea();

    void shapeToolArea();
    void shapeToolPolygonAndPolyline();
    void shapeToolPoint();

    void placeLegendByClick();
    void footcandleToolPlacesTable();
    void scheduleToolCreatesTypes();
    void wireRunToolCreatesRun();

    void footcandleDialog();
    void scheduleDialog();
    void wireRunDialog();
    void wireTakeoffDialog();

private:
    /// Creates a PDF file with a single page with the given content stream,
    /// the font /F1 is Helvetica
    static QByteArray createPdf(const QByteArray& content, QSizeF size = QSizeF(1224, 792), int rotate = 0, const QByteArray& extraPageEntries = QByteArray());
    static PDFDocument readPdf(const QByteArray& data);
    static QByteArray write(const PDFDocument& document);
    static PDFTextLayout createTextLayout(const PDFDocument& document, PDFInteger pageIndex);
    static QImage renderPage(const PDFDocument& document, PDFInteger pageIndex);
    static PDFDocument modify(const PDFDocument& document, const std::function<void(PDFDocumentBuilder*)>& function);
    static FixtureType type(const QString& name, FixtureMarkerShape shape, QColor color, const QString& description = QString());
    static QString contents(const PDFDocument& document, PDFObjectReference annotation);
    static QPolygonF rect(qreal x, qreal y, qreal width, qreal height);

    /// Accepts the next modal dialog by the given function (called from a timer)
    static void handleNextDialog(const std::function<void(QDialog*)>& function);

    QTemporaryDir m_settingsDirectory;
};

/// Widget with the lighting tools and a document. Modified documents emitted
/// by the tools are applied the same way as the application does it.
struct LightingFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    LightingTools lighting{ nullptr };
    FixtureTypes types;
    FixtureTypes importedTypes;
    int modificationCount = 0;

    explicit LightingFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        lighting.initialize(&widget, nullptr);
        lighting.setTypesProvider([this]() { return types; });
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&tools, &PDFToolManager::documentModified, &tools, [this](PDFModifiedDocument modified)
        {
            ++modificationCount;
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
        QObject::connect(&lighting, &LightingTools::typesImported, &lighting, [this](const FixtureTypes& imported) { importedTypes = imported; });
    }

    ~LightingFixture()
    {
        tools.setActiveTool(nullptr);
        lighting.setDocument(nullptr);
        tools.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setToolManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        tools.setDocument(modified);
        lighting.setDocument(modified);
    }

    QAction* action(const char* name) const
    {
        for (QAction* item : lighting.getActions())
        {
            if (item->objectName() == QLatin1String(name))
            {
                return item;
            }
        }
        return nullptr;
    }

    LightingShapeTool* activeTool() const
    {
        return qobject_cast<LightingShapeTool*>(tools.getActiveTool());
    }

    QPoint device(const QPointF& pagePoint) const
    {
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        const QTransform matrix = snapshot.items.empty() ? QTransform() : snapshot.items.front().pageToDeviceMatrix;
        return matrix.map(pagePoint).toPoint();
    }

    void mouse(PDFWidgetTool* tool, QEvent::Type type, QPoint position, Qt::MouseButton button = Qt::LeftButton)
    {
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
        QMouseEvent event(type, position, widget.mapToGlobal(position), type == QEvent::MouseMove ? Qt::NoButton : button, buttons, Qt::NoModifier);
        event.ignore();
        switch (type)
        {
            case QEvent::MouseButtonPress:
                tool->mousePressEvent(&widget, &event);
                break;
            case QEvent::MouseButtonRelease:
                tool->mouseReleaseEvent(&widget, &event);
                break;
            case QEvent::MouseButtonDblClick:
                tool->mouseDoubleClickEvent(&widget, &event);
                break;
            default:
                tool->mouseMoveEvent(&widget, &event);
                break;
        }
    }

    void click(PDFWidgetTool* tool, const QPointF& pagePoint)
    {
        const QPoint position = device(pagePoint);
        mouse(tool, QEvent::MouseMove, position, Qt::NoButton);
        mouse(tool, QEvent::MouseButtonPress, position);
        mouse(tool, QEvent::MouseButtonRelease, position);
    }

    void drag(PDFWidgetTool* tool, const QPointF& from, const QPointF& to)
    {
        mouse(tool, QEvent::MouseButtonPress, device(from));
        mouse(tool, QEvent::MouseMove, device((from + to) * 0.5), Qt::LeftButton);
        mouse(tool, QEvent::MouseMove, device(to), Qt::LeftButton);
        mouse(tool, QEvent::MouseButtonRelease, device(to));
    }

    void key(PDFWidgetTool* tool, int key)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        event.ignore();
        tool->keyPressEvent(&widget, &event);
    }
};

void LightingToolsTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("LightingTools");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

QByteArray LightingToolsTest::createPdf(const QByteArray& content, QSizeF size, int rotate, const QByteArray& extraPageEntries)
{
    std::vector<QByteArray> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + QByteArray::number(size.width()) + " " + QByteArray::number(size.height()) + "]"
                      " /Rotate " + QByteArray::number(rotate) + " /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R " + extraPageEntries + " >>");
    objects.push_back("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");

    QByteArray data = "%PDF-1.7\n";
    std::vector<qsizetype> offsets;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(data.size());
        data += QByteArray::number(qulonglong(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const qsizetype xrefOffset = data.size();
    data += "xref\n0 " + QByteArray::number(qulonglong(objects.size() + 1)) + "\n0000000000 65535 f \n";
    for (qsizetype offset : offsets)
    {
        data += QByteArray::number(qlonglong(offset)).rightJustified(10, '0') + " 00000 n \n";
    }
    data += "trailer\n<< /Size " + QByteArray::number(qulonglong(objects.size() + 1)) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(qlonglong(xrefOffset)) + "\n%%EOF\n";
    return data;
}

PDFDocument LightingToolsTest::readPdf(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << "PDF not read:" << reader.getErrorMessage();
    }
    return document;
}

QByteArray LightingToolsTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    writer.write(&buffer, &document);
    buffer.close();
    return buffer.data();
}

PDFTextLayout LightingToolsTest::createTextLayout(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    PDFCMSPointer cms = cmsManager.getCurrentCMS();

    PDFTextLayoutGenerator generator(PDFRenderer::getDefaultFeatures(), document.getCatalog()->getPage(pageIndex), &document, &fontCache, &iccProfileCache,
                                     cms.data(), &optionalContentActivity, QTransform(), PDFMeshQualitySettings());
    generator.processContents();
    return generator.createTextLayout();
}

QImage LightingToolsTest::renderPage(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    PDFCMSPointer cms = cmsManager.getCurrentCMS();
    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, cms.data(), &optionalContentActivity, PDFRenderer::getDefaultFeatures(), PDFMeshQualitySettings());

    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const QSize size = page->getRotatedMediaBox().size().toSize();
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    QPainter painter(&image);
    renderer.render(&painter, QRectF(QPointF(0, 0), size), pageIndex);

    // The tables are annotations, they are drawn by the annotation manager
    PDFAnnotationManager annotationManager(&fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, PDFMeshQualitySettings(),
                                           PDFRenderer::getDefaultFeatures(), PDFAnnotationManager::Target::View, nullptr);
    annotationManager.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    PDFTextLayoutGetter layoutGetter(nullptr, pageIndex);
    QList<PDFRenderError> errors;
    const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), size));
    annotationManager.drawPage(&painter, pageIndex, nullptr, layoutGetter, matrix, PDFColorConvertor(), errors);
    painter.end();
    return image;
}

PDFDocument LightingToolsTest::modify(const PDFDocument& document, const std::function<void(PDFDocumentBuilder*)>& function)
{
    PDFDocumentModifier modifier(&document);
    function(modifier.getBuilder());
    modifier.markAnnotationsChanged();
    if (!modifier.finalize())
    {
        return document;
    }
    return *modifier.getDocument();
}

FixtureType LightingToolsTest::type(const QString& name, FixtureMarkerShape shape, QColor color, const QString& description)
{
    FixtureType result;
    result.name = name;
    result.shape = shape;
    result.color = color;
    result.description = description;
    return result;
}

QString LightingToolsTest::contents(const PDFDocument& document, PDFObjectReference annotation)
{
    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? loader.readTextStringFromDictionary(dictionary, "Contents", QString()) : QString();
}

QPolygonF LightingToolsTest::rect(qreal x, qreal y, qreal width, qreal height)
{
    return QPolygonF(QRectF(x, y, width, height)).mid(0, 4);
}

void LightingToolsTest::handleNextDialog(const std::function<void(QDialog*)>& function)
{
    QTimer::singleShot(0, [function]()
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog)
        {
            // The dialog is not shown yet
            handleNextDialog(function);
            return;
        }
        function(dialog);
    });
}

void LightingToolsTest::tableSize()
{
    LightingTable table;
    QCOMPARE(table.getColumnCount(), 0);

    // Empty table has a minimal size
    QSizeF size = LightingTableWriter::getTableSize(table, 10.0);
    QVERIFY(size.width() > 0.0 && size.height() > 0.0);

    LightingTableRow header;
    header.bold = true;
    header.cells << "Type" << "Description" << "Qty";
    LightingTableRow row;
    row.cells << "A1" << "2x4 LED troffer" << "12";
    table.rows = { header, row };
    QCOMPARE(table.getColumnCount(), 3);

    const QSizeF withoutTitle = LightingTableWriter::getTableSize(table, 10.0);
    QCOMPARE(withoutTitle.height(), 2 * 16.0);

    table.title = "FIXTURE COUNT";
    const QSizeF withTitle = LightingTableWriter::getTableSize(table, 10.0);
    QCOMPARE(withTitle.height(), 3 * 16.0);
    QCOMPARE(withTitle.width(), withoutTitle.width());

    // A long title widens the table
    table.title = "FIXTURE COUNT OF A VERY LONG SHEET NAME THAT IS WIDER THAN ALL COLUMNS";
    QVERIFY(LightingTableWriter::getTableSize(table, 10.0).width() > withoutTitle.width());

    // Symbol column adds a column, the size grows with the font
    table.title.clear();
    table.hasSymbolColumn = true;
    QCOMPARE(LightingTableWriter::getTableSize(table, 10.0).width(), withoutTitle.width() + 16.0);
    QCOMPARE(LightingTableWriter::getTableSize(table, 20.0).height(), 2 * 32.0);

    // Font size follows the size of the sheet
    QCOMPARE(LightingTableWriter::getDefaultFontSize(QSizeF(612, 792)), 8.0);
    QCOMPARE(LightingTableWriter::getDefaultFontSize(QSizeF(2592, 1728)), 12.0);
    QCOMPARE(LightingTableWriter::getDefaultFontSize(QSizeF(100000, 100000)), 24.0);
}

void LightingToolsTest::tableRotations()
{
    const QSizeF size(100, 40);
    const QPointF topLeft(500, 300);

    for (PageRotation rotation : { PageRotation::None, PageRotation::Rotate90, PageRotation::Rotate180, PageRotation::Rotate270 })
    {
        const QRectF annotationRect = LightingTableWriter::getAnnotationRect(topLeft, size, rotation);
        QCOMPARE(LightingTableWriter::getTopLeft(annotationRect, rotation), topLeft);

        // The rectangle is the box of the table rotated with the page
        const bool isQuarterTurn = rotation == PageRotation::Rotate90 || rotation == PageRotation::Rotate270;
        QCOMPARE(annotationRect.size(), isQuarterTurn ? size.transposed() : size);

        // The matrix maps the upright top left corner onto the corner of the rectangle
        const QTransform matrix = LightingTableWriter::getRotationMatrix(rotation);
        const QRectF box = matrix.mapRect(QRectF(QPointF(0, 0), size));
        QCOMPARE(matrix.map(QPointF(0, size.height())) - box.topLeft() + annotationRect.topLeft(), topLeft);
    }

    // Without rotation the table hangs down from the top left corner (y axis points up)
    QCOMPARE(LightingTableWriter::getAnnotationRect(topLeft, size, PageRotation::None), QRectF(500, 260, 100, 40));

    // Page displayed rotated by 90 degrees clockwise: the reading direction is up the page
    const QTransform matrix = LightingTableWriter::getRotationMatrix(PageRotation::Rotate90);
    QCOMPARE(matrix.map(QPointF(1, 0)), QPointF(0, 1));
    QCOMPARE(matrix.map(QPointF(0, 1)), QPointF(-1, 0));
}

void LightingToolsTest::tableContentStream()
{
    LightingTable table;
    table.title = "TITLE (1)";
    table.hasSymbolColumn = true;
    LightingTableRow header;
    header.bold = true;
    header.cells << "Type" << "Qty";
    LightingTableRow row1;
    row1.cells << "A1" << "12";
    row1.symbol = type("A1", FixtureMarkerShape::Circle, Qt::red);
    LightingTableRow row2;
    row2.cells << QString::fromUtf8("B\xE2\x80\x94\xE6\xBC\xA2") << "3";
    row2.symbol = type("B2", FixtureMarkerShape::Triangle, Qt::blue);
    table.rows = { header, row1, row2 };

    const QByteArray stream = LightingTableWriter::createContentStream(table, 10.0);
    QVERIFY(stream.startsWith("q\n"));
    QVERIFY(stream.endsWith("Q\n"));
    QVERIFY(stream.contains("/PDF4QT_HelvB 10 Tf"));
    QVERIFY(stream.contains("/PDF4QT_Helv 10 Tf"));

    // Texts are hex strings in WinAnsiEncoding (parentheses need no escaping,
    // characters outside of the encoding are replaced)
    QVERIFY(stream.contains("<" + QByteArray("TITLE (1)").toHex() + "> Tj"));
    QVERIFY(stream.contains("<" + QByteArray("A1").toHex() + "> Tj"));
    QVERIFY(stream.contains("<42973f> Tj"));

    // Circle by Bezier curves, triangle by lines, the interior is lighter than the outline
    QVERIFY(stream.contains(" c\n"));
    QVERIFY(stream.contains("1 0 0 RG"));
    QVERIFY(stream.contains("0 0 1 RG"));
    QVERIFY(stream.contains("1 0.65 0.65 rg"));

    QCOMPARE(LightingTableWriter::getText(table), QString("TITLE (1)\nType\tQty\nA1\t12\n") + row2.cells.join('\t'));
}

void LightingToolsTest::tableAnnotationIsDrawn()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 300));
    builder.build();

    LightingTable table;
    table.title = "CALCULATION SUMMARY";
    LightingTableRow row;
    row.cells << "Avg" << "2.5";
    table.rows = { row };

    const PDFObjectReference annotation = LightingTableWriter::createTable(&builder, page, PageRotation::None, QPointF(50, 250), table,
                                                                           LightingTableKind::FootcandleStats, LightingLegendScope::Sheet, 12.0);
    QVERIFY(annotation.isValid());
    const PDFDocument document = builder.build();

    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    QVERIFY(dictionary);
    PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readNameFromDictionary(dictionary, "Subtype"), QByteArray("Stamp"));
    QCOMPARE(loader.readNameFromDictionary(dictionary, LightingTableWriter::TABLE_KIND_KEY), QByteArray("FootcandleStats"));
    QCOMPARE(loader.readIntegerFromDictionary(dictionary, "F", 0), PDFInteger(4));
    QCOMPARE(contents(document, annotation), QString("CALCULATION SUMMARY\nAvg\t2.5"));

    const QSizeF size = LightingTableWriter::getTableSize(table, 12.0);
    const QRectF annotationRect = loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized();
    QCOMPARE(annotationRect, QRectF(50, 250 - size.height(), size.width(), size.height()));

    // The table is drawn: dark frame and text inside of the rectangle, nothing outside
    const QImage image = renderPage(document, 0);
    const QRect deviceRect(qRound(annotationRect.left()), qRound(300 - annotationRect.bottom()), qRound(annotationRect.width()), qRound(annotationRect.height()));
    int darkInside = 0;
    int darkOutside = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (qGray(image.pixel(x, y)) < 128)
            {
                (deviceRect.adjusted(-2, -2, 2, 2).contains(x, y) ? darkInside : darkOutside) += 1;
            }
        }
    }
    QVERIFY(darkInside > 100);
    QCOMPARE(darkOutside, 0);

    // Header row of the title is gray
    const QRgb titleBackground = image.pixel(deviceRect.right() - 3, deviceRect.top() + 4);
    QVERIFY(qGray(titleBackground) > 180 && qGray(titleBackground) < 240);
}

void LightingToolsTest::tableOnRotatedPageIsUpright()
{
    // Page displayed rotated by 90 degrees: the long side of the table must be horizontal on the screen
    PDFDocument document = readPdf(createPdf(QByteArray(), QSizeF(300, 500), 90));
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

    LightingTable table;
    LightingTableRow row;
    row.cells << "A LONG TEXT OF THE TABLE";
    table.rows = { row };
    const QSizeF size = LightingTableWriter::getTableSize(table, 10.0);
    QVERIFY(size.width() > 3 * size.height());

    PDFObjectReference annotation;
    document = modify(document, [&](PDFDocumentBuilder* builder)
    {
        const PDFPage* page = document.getCatalog()->getPage(0);
        annotation = LightingTableWriter::createTable(builder, page->getPageReference(), page->getPageRotation(), QPointF(100, 100), table,
                                                      LightingTableKind::WireTakeoff, LightingLegendScope::Sheet, 10.0);
    });

    const QImage image = renderPage(document, 0);
    QCOMPARE(image.size(), QSize(500, 300));

    QRect darkBox;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (qGray(image.pixel(x, y)) < 128)
            {
                darkBox = darkBox.united(QRect(x, y, 1, 1));
            }
        }
    }

    QVERIFY(!darkBox.isEmpty());
    QVERIFY(darkBox.width() > 3 * darkBox.height());

    // The upright top left corner is at the picked point: the page point (100, 100)
    // is displayed at (100, 100) of the image rotated by 90 degrees
    QVERIFY(qAbs(darkBox.left() - 100) <= 2);
    QVERIFY(qAbs(darkBox.top() - 100) <= 2);
}

void LightingToolsTest::tableScanAndUpdate()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.build();

    LightingTable small;
    LightingTableRow row;
    row.cells << "A" << "1";
    small.rows = { row };

    const PDFObjectReference legend = LightingTableWriter::createTable(&builder, page1, PageRotation::None, QPointF(10, 700), small,
                                                                       LightingTableKind::Legend, LightingLegendScope::Document, 9.0);
    const PDFObjectReference takeoff = LightingTableWriter::createTable(&builder, page2, PageRotation::None, QPointF(20, 600), small,
                                                                        LightingTableKind::WireTakeoff, LightingLegendScope::Sheet, 9.0);
    builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 50), 1.0, Qt::red, Qt::red, "author", "subject", "contents");
    PDFDocument document = builder.build();

    QVERIFY(LightingTableWriter::scan(nullptr).empty());
    const LightingTableAnnotations all = LightingTableWriter::scan(&document);
    QCOMPARE(all.size(), size_t(2));
    QCOMPARE(all[0].annotation, legend);
    QCOMPARE(all[0].pageIndex, PDFInteger(0));
    QCOMPARE(all[0].kind, LightingTableKind::Legend);
    QCOMPARE(all[0].scope, LightingLegendScope::Document);
    QCOMPARE(all[0].fontSize, 9.0);
    QCOMPARE(all[1].annotation, takeoff);
    QCOMPARE(all[1].pageIndex, PDFInteger(1));
    QCOMPARE(all[1].kind, LightingTableKind::WireTakeoff);

    const LightingTableAnnotations legends = LightingTableWriter::scan(&document, LightingTableKind::Legend);
    QCOMPARE(legends.size(), size_t(1));
    QVERIFY(LightingTableWriter::scan(&document, LightingTableKind::FootcandleStats).empty());

    // The table was moved by the user, the update keeps its new top left corner
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Rect");
        factory << legends.front().rect.translated(100, -50);
        factory.endDictionaryItem();
        factory.endDictionary();
        modifyBuilder->mergeTo(legend, factory.takeObject());
    });

    LightingTable larger = small;
    larger.title = "NEW TITLE";
    LightingTableRow row2;
    row2.cells << "B" << "22";
    larger.rows.push_back(row2);

    const LightingTableAnnotation moved = LightingTableWriter::scan(&document, LightingTableKind::Legend).front();
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        QVERIFY(LightingTableWriter::updateTable(modifyBuilder, &document, moved, larger));

        LightingTableAnnotation invalid = moved;
        invalid.pageIndex = 7;
        QVERIFY(!LightingTableWriter::updateTable(modifyBuilder, &document, invalid, larger));
    });

    const LightingTableAnnotation updated = LightingTableWriter::scan(&document, LightingTableKind::Legend).front();
    QCOMPARE(updated.annotation, legend);
    QCOMPARE(updated.scope, LightingLegendScope::Document);
    QCOMPARE(updated.fontSize, 9.0);
    QCOMPARE(LightingTableWriter::getTopLeft(updated.rect, PageRotation::None), QPointF(110, 650));
    QCOMPARE(updated.rect.size(), LightingTableWriter::getTableSize(larger, 9.0));
    QCOMPARE(contents(document, legend), QString("NEW TITLE\nA\t1\nB\t22"));

    // Kinds have persistent keys
    for (LightingTableKind kind : { LightingTableKind::Legend, LightingTableKind::FootcandleStats, LightingTableKind::WireTakeoff })
    {
        QCOMPARE(LightingTableWriter::getKindFromKey(LightingTableWriter::getKindKey(kind)), kind);
    }
    QCOMPARE(LightingTableWriter::getKindFromKey("Unknown"), LightingTableKind::Invalid);
    QVERIFY(LightingTableWriter::getKindKey(LightingTableKind::Invalid).isEmpty());
}

void LightingToolsTest::tableSavedAndReopened()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.build();

    LightingTable table;
    table.title = "FIXTURE COUNT";
    table.hasSymbolColumn = true;
    LightingTableRow row;
    row.cells << "A1" << "5";
    row.symbol = type("A1", FixtureMarkerShape::Square, Qt::red);
    table.rows = { row };
    LightingTableWriter::createTable(&builder, page, PageRotation::None, QPointF(10, 700), table, LightingTableKind::Legend, LightingLegendScope::Sheet, 8.0);

    const PDFDocument reopened = readPdf(write(builder.build()));
    const LightingTableAnnotations tables = LightingTableWriter::scan(&reopened);
    QCOMPARE(tables.size(), size_t(1));
    QCOMPARE(tables.front().kind, LightingTableKind::Legend);
    QCOMPARE(tables.front().scope, LightingLegendScope::Sheet);
    QCOMPARE(contents(reopened, tables.front().annotation), QString("FIXTURE COUNT\nA1\t5"));

    // The appearance survives the round trip: red symbol is drawn
    const QImage image = renderPage(reopened, 0);
    bool hasRed = false;
    for (int y = 792 - 700; y < 792 - 700 + 40 && !hasRed; ++y)
    {
        for (int x = 10; x < 40; ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 200 && color.green() < 80 && color.blue() < 80)
            {
                hasRed = true;
                break;
            }
        }
    }
    QVERIFY(hasRed);
}

void LightingToolsTest::legendTable()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    builder.build();

    const FixtureType a1 = type("A1", FixtureMarkerShape::Circle, Qt::red, "Area light");
    const FixtureType b2 = type("B2", FixtureMarkerShape::Square, Qt::blue, "Wall pack");
    const FixtureType c3 = type("C3", FixtureMarkerShape::Diamond, Qt::green, "Bollard");
    for (int i = 0; i < 4; ++i)
    {
        FixtureCountEditor::createMarker(&builder, page1, QPointF(100 + 40 * i, 100), 12.0, a1);
    }
    FixtureCountEditor::createMarker(&builder, page2, QPointF(100, 300), 12.0, a1);
    FixtureCountEditor::createMarker(&builder, page2, QPointF(200, 300), 12.0, b2);
    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);

    // Types of the count in their order, the type without fixtures (C3) is left out;
    // the remembered color of a type differs, the symbol of the document is used
    FixtureType rememberedB2 = b2;
    rememberedB2.color = Qt::yellow;
    rememberedB2.description.clear();
    const LightingTable sheetLegend = CountLegend::createTable({ c3, rememberedB2, a1 }, data, LightingLegendScope::Sheet, 1, "E-102 Site Plan");
    QVERIFY(sheetLegend.hasSymbolColumn);
    QCOMPARE(sheetLegend.title, QString("FIXTURE COUNT - E-102 SITE PLAN"));
    QCOMPARE(sheetLegend.rows.size(), size_t(4));
    QVERIFY(sheetLegend.rows[0].bold);
    QCOMPARE(sheetLegend.rows[0].cells, (QStringList{ "Type", "Description", "Qty" }));
    QCOMPARE(sheetLegend.rows[1].cells, (QStringList{ "B2", "Wall pack", "1" }));
    QCOMPARE(sheetLegend.rows[1].symbol.color, QColor(Qt::blue));
    QCOMPARE(sheetLegend.rows[1].symbol.shape, FixtureMarkerShape::Square);
    QCOMPARE(sheetLegend.rows[2].cells, (QStringList{ "A1", "Area light", "1" }));
    QCOMPARE(sheetLegend.rows[3].cells, (QStringList{ "Total", "", "2" }));
    QVERIFY(sheetLegend.rows[3].bold);
    QVERIFY(!sheetLegend.rows[3].symbol.isValid());

    // Whole set, types of the document missing in the list are appended
    const LightingTable setLegend = CountLegend::createTable({ b2 }, data, LightingLegendScope::Document, 0, "E-101");
    QCOMPARE(setLegend.title, QString("FIXTURE COUNT - ALL SHEETS"));
    QCOMPARE(setLegend.rows.size(), size_t(4));
    QCOMPARE(setLegend.rows[1].cells, (QStringList{ "B2", "Wall pack", "1" }));
    QCOMPARE(setLegend.rows[2].cells, (QStringList{ "A1", "Area light", "5" }));
    QCOMPARE(setLegend.rows[3].cells, (QStringList{ "Total", "", "6" }));

    // Sheet without fixtures, sheet without a name
    const LightingTable empty = CountLegend::createTable({ a1 }, FixtureCountData(), LightingLegendScope::Sheet, 0, QString());
    QCOMPARE(empty.title, QString("FIXTURE COUNT"));
    QCOMPARE(empty.rows.size(), size_t(2));
    QCOMPARE(empty.rows[1].cells, (QStringList{ "Total", "", "0" }));
}

void LightingToolsTest::legendsFollowTheCount()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    builder.build();

    const FixtureType a1 = type("A1", FixtureMarkerShape::Circle, Qt::red, "Area light");
    FixtureCountEditor::createMarker(&builder, page1, QPointF(100, 100), 12.0, a1);
    PDFDocument document = builder.build();

    // Legends: whole set on the first sheet, the second sheet on the second sheet
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        const FixtureCountData data = FixtureCountData::scan(&document);
        LightingTableWriter::createTable(modifyBuilder, page1, PageRotation::None, QPointF(2000, 1700),
                                         CountLegend::createTable({ a1 }, data, LightingLegendScope::Document, 0, "Page 1"),
                                         LightingTableKind::Legend, LightingLegendScope::Document, 12.0);
        LightingTableWriter::createTable(modifyBuilder, page2, PageRotation::None, QPointF(2000, 1700),
                                         CountLegend::createTable({ a1 }, data, LightingLegendScope::Sheet, 1, "Page 2"),
                                         LightingTableKind::Legend, LightingLegendScope::Sheet, 12.0);
    });

    LightingTableAnnotations legends = LightingTableWriter::scan(&document, LightingTableKind::Legend);
    QCOMPARE(legends.size(), size_t(2));
    QCOMPARE(contents(document, legends[0].annotation), QString("FIXTURE COUNT - ALL SHEETS\nType\tDescription\tQty\nA1\tArea light\t1\nTotal\t\t1"));
    QCOMPARE(contents(document, legends[1].annotation), QString("FIXTURE COUNT - PAGE 2\nType\tDescription\tQty\nTotal\t\t0"));

    // Fixtures are counted on the second sheet, the legends are rewritten in the same modification
    const FixtureType b2 = type("B2", FixtureMarkerShape::Triangle, Qt::blue, "Wall pack");
    int updated = 0;
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        FixtureCountEditor::createMarker(modifyBuilder, page2, QPointF(300, 300), 12.0, a1);
        FixtureCountEditor::createMarker(modifyBuilder, page2, QPointF(400, 300), 12.0, b2);
        FixtureCountEditor::createMarker(modifyBuilder, page2, QPointF(500, 300), 12.0, b2);
        updated = LightingTools::updateTables(modifyBuilder, { a1, b2 }, WireTakeoffSettings());
    });

    QCOMPARE(updated, 2);
    legends = LightingTableWriter::scan(&document, LightingTableKind::Legend);
    QCOMPARE(contents(document, legends[0].annotation), QString("FIXTURE COUNT - ALL SHEETS\nType\tDescription\tQty\nA1\tArea light\t2\nB2\tWall pack\t2\nTotal\t\t4"));
    QCOMPARE(contents(document, legends[1].annotation), QString("FIXTURE COUNT - PAGE 2\nType\tDescription\tQty\nA1\tArea light\t1\nB2\tWall pack\t2\nTotal\t\t3"));

    // The legend grew by the new rows, its top left corner stays
    QCOMPARE(LightingTableWriter::getTopLeft(legends[1].rect, PageRotation::None), QPointF(2000, 1700));

    // The legend function of the count legend does the same
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        FixtureCountEditor::removeMarker(modifyBuilder, &document, FixtureCountData::scan(&document).getMarkersOfType("B2").front());
        QCOMPARE(CountLegend::updateLegends(modifyBuilder, { a1, b2 }), 2);
    });
    legends = LightingTableWriter::scan(&document, LightingTableKind::Legend);
    QCOMPARE(contents(document, legends[1].annotation), QString("FIXTURE COUNT - PAGE 2\nType\tDescription\tQty\nA1\tArea light\t1\nB2\tWall pack\t1\nTotal\t\t2"));

    // Document without tables is not changed
    PDFDocumentBuilder emptyBuilder;
    emptyBuilder.appendPage(QRectF(0, 0, 100, 100));
    QCOMPARE(LightingTools::updateTables(&emptyBuilder, { a1 }, WireTakeoffSettings()), 0);
    QCOMPARE(CountLegend::updateLegends(&emptyBuilder, { a1 }), 0);
}

void LightingToolsTest::parseFootcandleValues()
{
    PDFReal value = -1.0;
    int decimals = -1;

    QVERIFY(FootcandleGrid::parseValue("2.4", &value, &decimals));
    QCOMPARE(value, 2.4);
    QCOMPARE(decimals, 1);
    QVERIFY(FootcandleGrid::parseValue("+0.65", &value, &decimals));
    QCOMPARE(value, 0.65);
    QCOMPARE(decimals, 2);
    QVERIFY(FootcandleGrid::parseValue(".5", &value, &decimals));
    QCOMPARE(value, 0.5);
    QCOMPARE(decimals, 1);
    QVERIFY(FootcandleGrid::parseValue("12", &value, &decimals));
    QCOMPARE(value, 12.0);
    QCOMPARE(decimals, 0);
    QVERIFY(FootcandleGrid::parseValue("0.0", &value, &decimals));
    QCOMPARE(value, 0.0);
    QVERIFY(FootcandleGrid::parseValue(" 3. ", &value, &decimals));
    QCOMPARE(value, 3.0);
    QVERIFY(FootcandleGrid::parseValue("7", nullptr, nullptr));

    // Labels, mounting heights, fixture tags, negative values are not calculation points
    for (const char* text : { "", "A1", "25'", "MH=25", "-1.0", "1.2.3", "1,5", "fc", "+", ".", "2.4fc", "1e5" })
    {
        QVERIFY2(!FootcandleGrid::parseValue(text, &value, &decimals), text);
    }
}

void LightingToolsTest::footcandleStatistics()
{
    auto values = [](std::initializer_list<std::pair<PDFReal, int>> list)
    {
        FootcandleValues result;
        for (const auto& item : list)
        {
            result.push_back(FootcandleValue{ item.first, QPointF(), item.second });
        }
        return result;
    };

    FootcandleStatistics statistics = FootcandleStatistics::compute(values({ { 1.0, 1 }, { 2.0, 1 }, { 3.0, 1 }, { 6.0, 1 } }), false);
    QVERIFY(statistics.isValid());
    QVERIFY(statistics.hasRatios());
    QCOMPARE(statistics.count, 4);
    QCOMPARE(statistics.average, 3.0);
    QCOMPARE(statistics.maximum, 6.0);
    QCOMPARE(statistics.minimum, 1.0);
    QCOMPARE(statistics.getAverageToMinimum(), 3.0);
    QCOMPARE(statistics.getMaximumToMinimum(), 6.0);
    QCOMPARE(statistics.decimals, 1);

    // Zero minimum: the ratios are not defined, unless the zeros are left out
    const FootcandleValues withZeros = values({ { 0.0, 1 }, { 0.5, 2 }, { 1.5, 2 } });
    statistics = FootcandleStatistics::compute(withZeros, false);
    QCOMPARE(statistics.count, 3);
    QCOMPARE(statistics.minimum, 0.0);
    QVERIFY(!statistics.hasRatios());
    QCOMPARE(statistics.getAverageToMinimum(), 0.0);
    QCOMPARE(statistics.decimals, 2);

    statistics = FootcandleStatistics::compute(withZeros, true);
    QCOMPARE(statistics.count, 2);
    QCOMPARE(statistics.average, 1.0);
    QCOMPARE(statistics.minimum, 0.5);
    QCOMPARE(statistics.getMaximumToMinimum(), 3.0);

    // Whole numbers are displayed with one decimal, more than two decimals with two
    QCOMPARE(FootcandleStatistics::compute(values({ { 4.0, 0 } }), false).decimals, 1);
    QCOMPARE(FootcandleStatistics::compute(values({ { 4.125, 3 } }), false).decimals, 2);

    // Nothing to compute
    QVERIFY(!FootcandleStatistics::compute(FootcandleValues(), false).isValid());
    QVERIFY(!FootcandleStatistics::compute(values({ { 0.0, 1 } }), true).isValid());
    QVERIFY(!FootcandleStatistics().hasRatios());

    QCOMPARE(FootcandleGrid::formatRatio(3.24, true), QString("3.2:1"));
    QCOMPARE(FootcandleGrid::formatRatio(3.26, true), QString("3.3:1"));
    QCOMPARE(FootcandleGrid::formatRatio(10.0, true), QString("10.0:1"));
    QCOMPARE(FootcandleGrid::formatRatio(3.0, false), QString("N/A"));
    QCOMPARE(FootcandleGrid::formatRatio(std::numeric_limits<PDFReal>::infinity(), true), QString("N/A"));
}

void LightingToolsTest::footcandleValuesFromText()
{
    // Grid of calculation points as AGi32 writes it: rows of values, some rows
    // as a single string with spaces, some as separate strings, some placed
    // with gaps by TJ; a label, a mounting height and a fixture tag are near
    QByteArray content;
    content += "BT /F1 8 Tf 100 500 Td (0.5   1.2   2.4   3.6) Tj ET\n";
    content += "BT /F1 8 Tf 100 480 Td (0.0) Tj ET\n";
    content += "BT /F1 8 Tf 130 480 Td (1.1) Tj ET\n";
    content += "BT /F1 8 Tf 160 480 Td (+2.6) Tj ET\n";
    content += "BT /F1 8 Tf 190 480 Td (4.0) Tj ET\n";
    content += "BT /F1 8 Tf 100 460 Td [(0.7) -2500 (1.5) -2500 (2.9) -2500 (5.2)] TJ ET\n";
    content += "BT /F1 8 Tf 100 440 Td (MH 25') Tj ET\n";
    content += "BT /F1 8 Tf 160 440 Td (A1) Tj ET\n";
    content += "BT /F1 8 Tf 400 500 Td (9.9) Tj ET\n";
    const PDFDocument document = readPdf(createPdf(content));
    const PDFTextLayout layout = createTextLayout(document, 0);

    FootcandleValues values = FootcandleGrid::collectValues(layout, rect(90, 450, 150, 60));
    QCOMPARE(values.size(), size_t(12));

    std::vector<PDFReal> numbers;
    for (const FootcandleValue& value : values)
    {
        numbers.push_back(value.value);
        QVERIFY(QRectF(90, 450, 150, 60).contains(value.position));
    }
    std::sort(numbers.begin(), numbers.end());
    QCOMPARE(numbers, (std::vector<PDFReal>{ 0.0, 0.5, 0.7, 1.1, 1.2, 1.5, 2.4, 2.6, 2.9, 3.6, 4.0, 5.2 }));

    const FootcandleStatistics statistics = FootcandleStatistics::compute(values, true);
    QCOMPARE(statistics.count, 11);
    QCOMPARE(statistics.maximum, 5.2);
    QCOMPARE(statistics.minimum, 0.5);
    QVERIFY(qAbs(statistics.average - 25.7 / 11.0) < 1e-9);

    // Polygon: the left triangle of the grid holds only some values
    QPolygonF triangle;
    triangle << QPointF(90, 520) << QPointF(150, 520) << QPointF(90, 440);
    values = FootcandleGrid::collectValues(layout, triangle);
    numbers.clear();
    for (const FootcandleValue& value : values)
    {
        numbers.push_back(value.value);
    }
    std::sort(numbers.begin(), numbers.end());
    QCOMPARE(numbers, (std::vector<PDFReal>{ 0.0, 0.5, 0.7, 1.2 }));

    // Area without values
    QVERIFY(FootcandleGrid::collectValues(layout, rect(600, 100, 50, 50)).empty());

    // Tokens of the text: words separated by large gaps
    const pdfplugin::LightingTextTokens tokens = LightingText::getTokens(layout, rect(90, 435, 150, 15), 1.2, false);
    QCOMPARE(tokens.size(), size_t(2));
    QCOMPARE(tokens[0].text, QString("MH 25'"));
    QCOMPARE(tokens[1].text, QString("A1"));
}

void LightingToolsTest::footcandleValuesOfRotatedText()
{
    // Site plan on a sheet rotated by 90 degrees, the values are written rotated (reading up the page)
    QByteArray content;
    content += "BT /F1 8 Tf 0 1 -1 0 300 100 Tm (1.0   2.0   3.0) Tj ET\n";
    content += "BT /F1 8 Tf 0 1 -1 0 320 100 Tm (4.0   5.0   6.0) Tj ET\n";
    const PDFDocument document = readPdf(createPdf(content, QSizeF(792, 1224), 90));
    const PDFTextLayout layout = createTextLayout(document, 0);

    const FootcandleValues values = FootcandleGrid::collectValues(layout, rect(250, 80, 100, 150));
    QCOMPARE(values.size(), size_t(6));
    const FootcandleStatistics statistics = FootcandleStatistics::compute(values, false);
    QCOMPARE(statistics.average, 3.5);
    QCOMPARE(statistics.maximum, 6.0);
    QCOMPARE(statistics.minimum, 1.0);

    // Centers of the values lie left of the baseline (above the rotated text)
    for (const FootcandleValue& value : values)
    {
        QVERIFY(value.position.x() < 320.0);
        QVERIFY(value.position.x() > 290.0);
    }
}

void LightingToolsTest::footcandleTable()
{
    FootcandleStatistics statistics;
    statistics.count = 48;
    statistics.average = 2.345;
    statistics.maximum = 6.1;
    statistics.minimum = 0.5;
    statistics.decimals = 1;

    LightingTable table = FootcandleGrid::createTable(statistics, "Parking Lot", "fc");
    QCOMPARE(table.title, QString("CALCULATION SUMMARY"));
    QCOMPARE(table.rows.size(), size_t(2));
    QCOMPARE(table.rows[0].cells, (QStringList{ "Area", "Avg (fc)", "Max (fc)", "Min (fc)", "Avg:Min", "Max:Min", "Points" }));
    QCOMPARE(table.rows[1].cells, (QStringList{ "Parking Lot", "2.3", "6.1", "0.5", "4.7:1", "12.2:1", "48" }));

    statistics.minimum = 0.0;
    table = FootcandleGrid::createTable(statistics, QString(), "lx");
    QCOMPARE(table.rows[0].cells[1], QString("Avg (lx)"));
    QCOMPARE(table.rows[1].cells, (QStringList{ "Selected Area", "2.3", "6.1", "0.0", "N/A", "N/A", "48" }));
}

void LightingToolsTest::scheduleTable()
{
    // Luminaire schedule: title spanning the columns, header, rows, a description
    // wrapped onto the next line; text outside of the box is not read
    QByteArray content;
    content += "BT /F1 10 Tf 100 700 Td (LUMINAIRE SCHEDULE - SITE LIGHTING AND BUILDING MOUNTED) Tj ET\n";
    content += "BT /F1 8 Tf 100 680 Td (TYPE) Tj ET\n";
    content += "BT /F1 8 Tf 150 680 Td (DESCRIPTION) Tj ET\n";
    content += "BT /F1 8 Tf 350 680 Td (VOLTS) Tj ET\n";
    content += "BT /F1 8 Tf 400 680 Td (WATTS) Tj ET\n";
    content += "BT /F1 8 Tf 100 665 Td (SA) Tj ET\n";
    content += "BT /F1 8 Tf 150 665 Td (LED AREA LIGHT TYPE IV) Tj ET\n";
    content += "BT /F1 8 Tf 350 665 Td (277) Tj ET\n";
    content += "BT /F1 8 Tf 400 665 Td (150) Tj ET\n";
    content += "BT /F1 8 Tf 150 655 Td (ON 25' POLE) Tj ET\n";
    content += "BT /F1 8 Tf 100 640 Td (WP) Tj ET\n";
    content += "BT /F1 8 Tf 150 640 Td (LED WALL PACK) Tj ET\n";
    content += "BT /F1 8 Tf 350 640 Td (120) Tj ET\n";
    content += "BT /F1 8 Tf 400 640 Td (40) Tj ET\n";
    content += "BT /F1 8 Tf 100 300 Td (GENERAL NOTES) Tj ET\n";
    const PDFDocument document = readPdf(createPdf(content));
    const PDFTextLayout layout = createTextLayout(document, 0);

    const FixtureScheduleTable table = FixtureSchedule::readTable(layout, rect(90, 630, 500, 85));
    QCOMPARE(table.size(), size_t(5));
    QCOMPARE(table[0].size(), qsizetype(4));
    QCOMPARE(table[0][0], QString("LUMINAIRE SCHEDULE - SITE LIGHTING AND BUILDING MOUNTED"));
    QCOMPARE(table[1], (QStringList{ "TYPE", "DESCRIPTION", "VOLTS", "WATTS" }));
    QCOMPARE(table[2], (QStringList{ "SA", "LED AREA LIGHT TYPE IV", "277", "150" }));
    QCOMPARE(table[3], (QStringList{ "", "ON 25' POLE", "", "" }));
    QCOMPARE(table[4], (QStringList{ "WP", "LED WALL PACK", "120", "40" }));

    const int headerRow = FixtureSchedule::findHeaderRow(table);
    QCOMPARE(headerRow, 1);
    QCOMPARE(FixtureSchedule::findTypeColumn(table, headerRow), 0);
    QCOMPARE(FixtureSchedule::findDescriptionColumn(table, headerRow, 0), 1);

    const FixtureScheduleTable joined = FixtureSchedule::joinWrappedRows(table, headerRow, 0);
    QCOMPARE(joined.size(), size_t(4));
    QCOMPARE(joined[2], (QStringList{ "SA", "LED AREA LIGHT TYPE IV ON 25' POLE", "277", "150" }));
    QCOMPARE(joined[3], (QStringList{ "WP", "LED WALL PACK", "120", "40" }));

    // CSV export of the schedule
    const QString csv = FixtureCountReport::createCsv(joined, QChar(','));
    QVERIFY(csv.contains("SA,LED AREA LIGHT TYPE IV ON 25' POLE,277,150"));

    // Nothing in the box
    QVERIFY(FixtureSchedule::readTable(layout, rect(600, 100, 50, 50)).empty());
}

void LightingToolsTest::scheduleTypes()
{
    const FixtureScheduleTable table = {
        { "LUMINAIRE SCHEDULE", "", "" },
        { "MARK", "MANUFACTURER", "CATALOG NUMBER" },
        { "A1", "Lithonia", "DSX1 LED P5 40K T4M" },
        { "", "", "ON 25' POLE" },
        { "B2", "Lithonia", "WPX2 LED 40K" },
        { "A1", "Duplicate", "Skipped" },
        { "THIS IS NOT A FIXTURE TYPE", "", "" },
        { "C3", "Cooper", "" },
        { "", "", "" }
    };

    const int headerRow = FixtureSchedule::findHeaderRow(table);
    QCOMPARE(headerRow, 1);
    const int typeColumn = FixtureSchedule::findTypeColumn(table, headerRow);
    QCOMPARE(typeColumn, 0);
    QCOMPARE(FixtureSchedule::findDescriptionColumn(table, headerRow, typeColumn), 2);

    const FixtureScheduleTable joined = FixtureSchedule::joinWrappedRows(table, headerRow, typeColumn);
    QCOMPARE(joined.size(), size_t(7));
    QCOMPARE(joined[2][2], QString("DSX1 LED P5 40K T4M ON 25' POLE"));

    // C3 already exists, A1 is a duplicate, the long text is not a type
    const FixtureType existing = type("c3", FixtureMarkerShape::Circle, Qt::red);
    const FixtureTypes types = FixtureSchedule::createTypes(joined, headerRow, typeColumn, 2, { existing });
    QCOMPARE(types.size(), size_t(2));
    QCOMPARE(types[0].name, QString("A1"));
    QCOMPARE(types[0].description, QString("DSX1 LED P5 40K T4M ON 25' POLE"));
    QCOMPARE(types[1].name, QString("B2"));
    QCOMPARE(types[1].description, QString("WPX2 LED 40K"));

    // Default symbols follow the existing types, so they differ from them
    QCOMPARE(types[0].color, FixtureType::createDefaultType(1).color);
    QCOMPARE(types[1].shape, FixtureType::createDefaultType(2).shape);

    // Without a description column, with an invalid type column
    QVERIFY(FixtureSchedule::createTypes(joined, headerRow, typeColumn, -1, {}).front().description.isEmpty());
    QVERIFY(FixtureSchedule::createTypes(joined, headerRow, 9, -1, {}).empty());
}

void LightingToolsTest::scheduleWithoutHeader()
{
    const FixtureScheduleTable table = {
        { "A", "Pole light with a long description", "1" },
        { "B", "Wall pack", "2" }
    };

    QCOMPARE(FixtureSchedule::findHeaderRow(table), -1);
    QCOMPARE(FixtureSchedule::findTypeColumn(table, -1), 0);
    QCOMPARE(FixtureSchedule::findDescriptionColumn(table, -1, 0), 1);
    QCOMPARE(FixtureSchedule::joinWrappedRows(table, -1, 0).size(), size_t(2));

    const FixtureTypes types = FixtureSchedule::createTypes(table, -1, 0, 1, {});
    QCOMPARE(types.size(), size_t(2));
    QCOMPARE(types[0].name, QString("A"));

    QCOMPARE(FixtureSchedule::findDescriptionColumn(FixtureScheduleTable(), -1, 0), -1);
}

void LightingToolsTest::wireScale()
{
    WireScale scale = WireScale::fromPaperScale(1.0, 20.0, "ft", 1.0);
    QVERIFY(scale.isValid());
    QCOMPARE(scale.unitsPerUserSpaceUnit, 20.0 / 72.0);
    QCOMPARE(scale.ratio, QString("1 in = 20 ft"));
    QCOMPARE(scale.feetPerUnit(), 1.0);

    // User unit enlarges the default user space
    QCOMPARE(WireScale::fromPaperScale(1.0, 20.0, "ft", 2.0).unitsPerUserSpaceUnit, 40.0 / 72.0);
    QVERIFY(!WireScale::fromPaperScale(0.0, 20.0, "ft", 1.0).isValid());
    QVERIFY(!WireScale::fromPaperScale(1.0, 0.0, "ft", 1.0).isValid());

    auto feet = [](const char* unit) { WireScale s; s.unit = unit; return s.feetPerUnit(); };
    QCOMPARE(feet("ft"), 1.0);
    QCOMPARE(feet("'"), 1.0);
    QCOMPARE(feet("in"), 1.0 / 12.0);
    QCOMPARE(feet("yd"), 3.0);
    QCOMPARE(feet("mi"), 5280.0);
    QCOMPARE(feet("m"), 1.0 / 0.3048);
    QCOMPARE(feet("cm"), 0.01 / 0.3048);
    QCOMPARE(feet("mm"), 0.001 / 0.3048);
    QCOMPARE(feet("km"), 1000.0 / 0.3048);
    QCOMPARE(feet("furlong"), 0.0);

    // Unknown unit makes the scale unusable
    WireScale unknown;
    unknown.unitsPerUserSpaceUnit = 1.0;
    unknown.unit = "parsec";
    QVERIFY(!unknown.isValid());

    WireRun run;
    run.points << QPointF(0, 0) << QPointF(72, 0) << QPointF(72, 144);
    QCOMPARE(run.getPaperLength(), 216.0);
    QCOMPARE(run.getLengthInFeet(), 0.0);
    run.scale = scale;
    QCOMPARE(run.getLengthInFeet(), 60.0);
    run.scale = WireScale::fromPaperScale(1.0, 10.0, "m", 1.0);
    QCOMPARE(run.getLengthInFeet(), 30.0 / 0.3048);
}

void LightingToolsTest::wireRunsAndTotals()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    builder.build();

    // 1 in = 20 ft: 72 units are 20 feet
    const WireScale scale = WireScale::fromPaperScale(1.0, 20.0, "ft", 1.0);
    const PDFObjectReference run1 = WireTakeoff::createRun(&builder, page1, QPolygonF({ QPointF(0, 0), QPointF(360, 0) }), "2", 3, 3, scale);       // 100 ft
    WireTakeoff::createRun(&builder, page2, QPolygonF({ QPointF(0, 0), QPointF(0, 180), QPointF(180, 180) }), "2", 2, 4, scale);                   // 100 ft
    WireTakeoff::createRun(&builder, page1, QPolygonF({ QPointF(0, 100), QPointF(720, 100) }), "10", 1, 3, scale);                                 // 200 ft
    WireTakeoff::createRun(&builder, page1, QPolygonF({ QPointF(0, 200), QPointF(72, 200) }), "B", 0, 0, scale);                                   // 20 ft, default conductors
    WireTakeoff::createRun(&builder, page2, QPolygonF({ QPointF(0, 300), QPointF(72, 300) }), "3", 1, 3, WireScale());                             // no scale

    // Measured polyline of the measuring tools (no circuit), angle measurement and a plain polyline are not runs
    const PDFObjectReference measured = builder.createAnnotationPolyline(page1, QPolygonF({ QPointF(0, 400), QPointF(36, 400), QPointF(36, 436) }), 1.0, QColor(), Qt::black,
                                                                         "author", "subject", "contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference angle = builder.createAnnotationPolyline(page1, QPolygonF({ QPointF(0, 500), QPointF(36, 500), QPointF(36, 536) }), 1.0, QColor(), Qt::black,
                                                                      "author", "subject", "contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    builder.createAnnotationPolyline(page1, QPolygonF({ QPointF(0, 600), QPointF(36, 600) }), 1.0, QColor(), Qt::black,
                                     "author", "subject", "contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    PDFDocument document = builder.build();

    const PDFObject measure = document.getDictionaryFromObject(document.getObjectByReference(run1))->get("Measure");
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        for (PDFObjectReference reference : { measured, angle })
        {
            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("Measure");
            factory << measure;
            factory.endDictionaryItem();
            if (reference == measured)
            {
                factory.beginDictionaryItem(PDFPolygonalGeometryAnnotation::MEASURED_QUANTITY_KEY);
                factory << WrapName(PDFPolygonalGeometryAnnotation::MEASURED_LENGTH);
                factory.endDictionaryItem();
            }
            factory.endDictionary();
            modifyBuilder->mergeTo(reference, factory.takeObject());
        }
    });

    QVERIFY(WireTakeoff::scan(nullptr).empty());
    const WireRuns runs = WireTakeoff::scan(&document);
    QCOMPARE(runs.size(), size_t(6));
    QCOMPARE(runs[0].annotation, run1);
    QCOMPARE(runs[0].circuit, QString("2"));
    QCOMPARE(runs[0].fixtures, 3);
    QCOMPARE(runs[0].conductors, 3);
    QCOMPARE(runs[0].pageIndex, PDFInteger(0));
    QCOMPARE(runs[0].scale.unit, QString("ft"));
    QVERIFY(qAbs(runs[0].getLengthInFeet() - 100.0) < 1e-9);
    QCOMPARE(runs[0].scale.ratio, QString("1 in = 20 ft"));

    const auto measuredRun = std::find_if(runs.cbegin(), runs.cend(), [&](const WireRun& run) { return run.annotation == measured; });
    QVERIFY(measuredRun != runs.cend());
    QVERIFY(measuredRun->circuit.isEmpty());
    QVERIFY(qAbs(measuredRun->getLengthInFeet() - 20.0) < 1e-9);
    QVERIFY(std::none_of(runs.cbegin(), runs.cend(), [&](const WireRun& run) { return run.annotation == angle; }));

    // The run annotation is a measured polyline, its contents describe it
    const QString runContents = contents(document, run1);
    QCOMPARE(runContents, QString("Circuit 2: 100.0 ft, 3 fixture(s)"));

    WireTakeoffSettings settings;
    settings.dropLength = 10.0;
    settings.wastePercent = 10.0;
    settings.defaultConductors = 2;

    const WireCircuitTotals totals = WireTakeoff::createTotals(runs, settings);
    QCOMPARE(totals.size(), size_t(4));

    // Natural order of the circuits, runs without a circuit are the last ones, runs without a scale are left out
    QCOMPARE(totals[0].circuit, QString("2"));
    QCOMPARE(totals[1].circuit, QString("10"));
    QCOMPARE(totals[2].circuit, QString("B"));
    QCOMPARE(totals[3].circuit, WireTakeoff::getUnassignedCircuitName());

    // Circuit 2: two runs of 100 ft, 5 fixtures, 50 ft of drops, 3 and 4 conductors
    QCOMPARE(totals[0].runs, 2);
    QCOMPARE(totals[0].fixtures, 5);
    QVERIFY(qAbs(totals[0].runLength - 200.0) < 1e-9);
    QVERIFY(qAbs(totals[0].dropLength - 50.0) < 1e-9);
    QVERIFY(qAbs(totals[0].conductorLength - 250.0 * 1.1) < 1e-9);
    QVERIFY(qAbs(totals[0].wireLength - (130.0 * 1.1 * 3 + 120.0 * 1.1 * 4)) < 1e-9);
    QCOMPARE(totals[0].conductors, 4);

    // Circuit B has no count of its own, the default count is used
    QCOMPARE(totals[2].conductors, 2);
    QVERIFY(qAbs(totals[2].wireLength - 20.0 * 1.1 * 2) < 1e-9);

    QCOMPARE(WireTakeoff::getCircuitColor("1"), WireTakeoff::getCircuitColor("9"));
    QVERIFY(WireTakeoff::getCircuitColor("1") != WireTakeoff::getCircuitColor("2"));
    QCOMPARE(WireTakeoff::getCircuitColor("LP-1"), WireTakeoff::getCircuitColor("lp-1"));
    QCOMPARE(WireTakeoff::getCircuitColor(QString()), QColor(96, 96, 96));
}

void LightingToolsTest::wireTakeoffTable()
{
    WireCircuitTotal total;
    total.circuit = "1";
    total.runs = 2;
    total.fixtures = 4;
    total.runLength = 300.0;
    total.dropLength = 40.0;
    total.conductorLength = 374.0;
    total.wireLength = 1122.0;
    total.conductors = 3;

    WireTakeoffSettings settings;
    settings.dropLength = 10.0;
    settings.wastePercent = 10.0;

    LightingTable table = WireTakeoff::createTable({ total, total }, settings);
    QCOMPARE(table.title, QString("WIRE TAKEOFF (DROP 10 ft PER FIXTURE, WASTE 10%)"));
    QCOMPARE(table.rows.size(), size_t(4));
    QCOMPARE(table.rows[0].cells, (QStringList{ "Circuit", "Fixtures", "Conduit (ft)", "Drops (ft)", "Per Cond. (ft)", "Cond.", "Total Wire (ft)" }));
    QCOMPARE(table.rows[1].cells, (QStringList{ "1", "4", "300", "40", "374", "3", "1122" }));
    QCOMPARE(table.rows[3].cells, (QStringList{ "Total", "8", "600", "80", "748", "", "2244" }));
    QVERIFY(table.rows[3].bold);

    settings.metric = true;
    table = WireTakeoff::createTable({ total }, settings);
    QCOMPARE(table.title, QString("WIRE TAKEOFF (DROP 3 m PER FIXTURE, WASTE 10%)"));
    QCOMPARE(table.rows[0].cells[2], QString("Conduit (m)"));
    QCOMPARE(table.rows[1].cells[2], QString("91"));

    table = WireTakeoff::createTable({}, WireTakeoffSettings());
    QCOMPARE(table.rows.size(), size_t(2));
    QCOMPARE(table.rows[1].cells[0], QString("Total"));
}

void LightingToolsTest::wireScaleOfTheDrawing()
{
    // Viewport of the drawing with its own scale (1 unit = 0.5 ft) in the lower half of the page
    const QByteArray viewport = "/VP [<< /Type /Viewport /BBox [0 0 600 400] /Measure << /Type /Measure /Subtype /RL /R (1 in = 36 ft)"
                                " /X [<< /Type /NumberFormat /U (ft) /C 0.5 >>] /D [<< /Type /NumberFormat /U (ft) /C 1 >>]"
                                " /A [<< /Type /NumberFormat /U (sq ft) /C 1 >>] >> >>]";
    PDFDocument document = readPdf(createPdf(QByteArray(), QSizeF(600, 800), 0, viewport));

    WireScale scale = WireTakeoff::findScale(&document, 0, QPointF(100, 100));
    QVERIFY(scale.isValid());
    QCOMPARE(scale.unitsPerUserSpaceUnit, 0.5);
    QCOMPARE(scale.unit, QString("ft"));
    QCOMPARE(scale.ratio, QString("1 in = 36 ft"));

    // Outside of the viewport nothing is known
    QVERIFY(!WireTakeoff::findScale(&document, 0, QPointF(100, 600)).isValid());
    QVERIFY(!WireTakeoff::findScale(&document, 5, QPointF(100, 600)).isValid());
    QVERIFY(!WireTakeoff::findScale(nullptr, 0, QPointF(100, 600)).isValid());

    // A run measured on the page gives the scale of the page
    const WireScale runScale = WireScale::fromPaperScale(1.0, 30.0, "ft", 1.0);
    document = modify(document, [&](PDFDocumentBuilder* builder)
    {
        WireTakeoff::createRun(builder, document.getCatalog()->getPage(0)->getPageReference(), QPolygonF({ QPointF(10, 600), QPointF(100, 600) }), "1", 1, 3, runScale);
    });
    scale = WireTakeoff::findScale(&document, 0, QPointF(100, 600));
    QVERIFY(scale.isValid());
    QCOMPARE(scale.unitsPerUserSpaceUnit, runScale.unitsPerUserSpaceUnit);

    // The viewport has priority
    QCOMPARE(WireTakeoff::findScale(&document, 0, QPointF(100, 100)).unitsPerUserSpaceUnit, 0.5);
}

void LightingToolsTest::takeoffTablesFollowTheRuns()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 2592, 1728));
    builder.build();

    const WireScale scale = WireScale::fromPaperScale(1.0, 20.0, "ft", 1.0);
    WireTakeoff::createRun(&builder, page, QPolygonF({ QPointF(0, 0), QPointF(360, 0) }), "1", 2, 3, scale);
    WireTakeoffSettings settings;
    settings.wastePercent = 0.0;
    settings.dropLength = 10.0;

    PDFDocument document = builder.build();
    PDFObjectReference table;
    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        table = LightingTableWriter::createTable(modifyBuilder, page, PageRotation::None, QPointF(1000, 1000),
                                                 WireTakeoff::createTable(WireTakeoff::createTotals(WireTakeoff::scan(&document), settings), settings),
                                                 LightingTableKind::WireTakeoff, LightingLegendScope::Sheet, 12.0);
    });
    QVERIFY(contents(document, table).contains("1\t2\t100\t20\t120\t3\t360"));

    document = modify(document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        WireTakeoff::createRun(modifyBuilder, page, QPolygonF({ QPointF(0, 100), QPointF(720, 100) }), "1", 1, 3, scale);
        QCOMPARE(LightingTools::updateTables(modifyBuilder, {}, settings), 1);
    });
    QVERIFY(contents(document, table).contains("1\t3\t300\t30\t330\t3\t990"));
}

void LightingToolsTest::tablePositionNextToArea()
{
    const QRectF pageBox(0, 0, 1000, 800);
    const QSizeF size(200, 100);
    const QPolygonF area = rect(100, 300, 200, 200);

    // Right of the area, top aligned with the area, gap of a half of the font size (8 points on this page)
    QCOMPARE(LightingTools::getTablePositionNextToArea(area, size, pageBox, PageRotation::None), QPointF(304, 500));

    // No space on the right: left of the area
    QCOMPARE(LightingTools::getTablePositionNextToArea(rect(700, 300, 250, 200), size, pageBox, PageRotation::None), QPointF(496, 500));

    // No space on either side: kept on the page
    const QPointF inside = LightingTools::getTablePositionNextToArea(rect(10, 300, 980, 200), size, pageBox, PageRotation::None);
    QVERIFY(pageBox.contains(LightingTableWriter::getAnnotationRect(inside, size, PageRotation::None)));

    // Area at the bottom: the table stays on the page
    const QPointF low = LightingTools::getTablePositionNextToArea(rect(100, 0, 100, 40), size, pageBox, PageRotation::None);
    QVERIFY(pageBox.contains(LightingTableWriter::getAnnotationRect(low, size, PageRotation::None)));

    // Rotated pages: the table is right of the area as displayed, and on the page
    for (PageRotation rotation : { PageRotation::Rotate90, PageRotation::Rotate180, PageRotation::Rotate270 })
    {
        const QPointF topLeft = LightingTools::getTablePositionNextToArea(area, size, pageBox, rotation);
        const QRectF tableRect = LightingTableWriter::getAnnotationRect(topLeft, size, rotation);
        QVERIFY(pageBox.contains(tableRect));
        QVERIFY(!tableRect.intersects(area.boundingRect()));
    }
}

void LightingToolsTest::shapeToolArea()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 400, 400));
    LightingFixture fixture(builder.build());

    QAction action;
    action.setCheckable(true);
    LightingShapeTool tool(fixture.widget.getDrawWidgetProxy(), LightingShapeTool::Mode::Area, &action, nullptr);
    QCOMPARE(tool.getMode(), LightingShapeTool::Mode::Area);
    QSignalSpy spy(&tool, &LightingShapeTool::shapePicked);

    // Dragged box
    fixture.drag(&tool, QPointF(100, 300), QPointF(200, 200));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).value<PDFInteger>(), PDFInteger(0));
    const QPolygonF box = spy.first().at(1).value<QPolygonF>();
    QCOMPARE(box.size(), qsizetype(4));
    const QRectF boxRect = box.boundingRect();
    QVERIFY(qAbs(boxRect.left() - 100) < 2 && qAbs(boxRect.right() - 200) < 2);
    QVERIFY(qAbs(boxRect.top() - 200) < 2 && qAbs(boxRect.bottom() - 300) < 2);

    // Polygon: three clicks and Enter, with a removed misplaced corner
    spy.clear();
    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 50));
    fixture.click(&tool, QPointF(390, 390));
    QCOMPARE(tool.getPoints().size(), qsizetype(3));
    QCOMPARE(tool.getPageIndex(), PDFInteger(0));
    fixture.key(&tool, Qt::Key_Backspace);
    QCOMPARE(tool.getPoints().size(), qsizetype(2));

    // Too few corners
    fixture.key(&tool, Qt::Key_Return);
    QCOMPARE(spy.count(), 0);

    fixture.click(&tool, QPointF(100, 150));
    fixture.key(&tool, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(1).value<QPolygonF>().size(), qsizetype(3));
    QVERIFY(tool.getPoints().isEmpty());

    // Esc cancels
    spy.clear();
    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 50));
    fixture.key(&tool, Qt::Key_Escape);
    QVERIFY(tool.getPoints().isEmpty());
    QCOMPARE(tool.getPageIndex(), PDFInteger(-1));
    QCOMPARE(spy.count(), 0);

    // Keys are not shortcuts only while a shape is being picked
    QKeyEvent enter(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
    tool.shortcutOverrideEvent(&fixture.widget, &enter);
    QVERIFY(!enter.isAccepted());
    fixture.click(&tool, QPointF(50, 50));
    QKeyEvent enter2(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
    tool.shortcutOverrideEvent(&fixture.widget, &enter2);
    QVERIFY(enter2.isAccepted());
    QKeyEvent other(QEvent::ShortcutOverride, Qt::Key_K, Qt::NoModifier);
    tool.shortcutOverrideEvent(&fixture.widget, &other);
    QVERIFY(!other.isAccepted());
    tool.cancelShape();
}

void LightingToolsTest::shapeToolPolygonAndPolyline()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 400, 400));
    LightingFixture fixture(builder.build());

    QAction action;
    action.setCheckable(true);
    LightingShapeTool tool(fixture.widget.getDrawWidgetProxy(), LightingShapeTool::Mode::Polyline, &action, nullptr);
    QSignalSpy spy(&tool, &LightingShapeTool::shapePicked);

    QStringList labels;
    tool.setLabelProvider([&labels](PDFInteger, const QPolygonF& shape) { labels << QString::number(shape.size()); return QString("label"); });
    tool.setColor(Qt::red);

    // Polyline finished by a double click (the first click of the double click adds the point)
    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 50));
    fixture.click(&tool, QPointF(150, 150));
    fixture.mouse(&tool, QEvent::MouseButtonDblClick, fixture.device(QPointF(150, 150)));
    QCOMPARE(spy.count(), 1);
    const QPolygonF polyline = spy.first().at(1).value<QPolygonF>();
    QCOMPARE(polyline.size(), qsizetype(3));
    QVERIFY(QLineF(polyline[1], QPointF(150, 50)).length() < 2.0);

    // A single point is not a run
    spy.clear();
    fixture.click(&tool, QPointF(50, 50));
    tool.finishShape();
    QCOMPARE(spy.count(), 0);

    // Right click finishes
    fixture.click(&tool, QPointF(250, 50));
    fixture.mouse(&tool, QEvent::MouseButtonPress, fixture.device(QPointF(250, 250)), Qt::RightButton);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(1).value<QPolygonF>().size(), qsizetype(2));

    // Right click without a shape is left to the context menu
    QMouseEvent rightPress(QEvent::MouseButtonPress, fixture.device(QPointF(50, 50)), fixture.widget.mapToGlobal(fixture.device(QPointF(50, 50))), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    rightPress.ignore();
    tool.mousePressEvent(&fixture.widget, &rightPress);
    QVERIFY(!rightPress.isAccepted());

    // Drawing of the live shape uses the label provider
    fixture.click(&tool, QPointF(50, 50));
    fixture.mouse(&tool, QEvent::MouseMove, fixture.device(QPointF(100, 100)), Qt::NoButton);
    QImage image(400, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    PDFTextLayoutGetter getter(nullptr, 0);
    QList<PDFRenderError> errors;
    tool.drawPage(&painter, 0, nullptr, getter, QTransform(), PDFColorConvertor(), errors);
    painter.end();
    QVERIFY(!labels.isEmpty());
    QCOMPARE(labels.back(), QString("2"));
    QVERIFY(image != QImage());

    // Clicking another sheet starts a new shape
    tool.removeLastPoint();
    QVERIFY(tool.getPoints().isEmpty());
}

void LightingToolsTest::shapeToolPoint()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 400, 400));
    LightingFixture fixture(builder.build());

    QAction action;
    action.setCheckable(true);
    LightingShapeTool tool(fixture.widget.getDrawWidgetProxy(), LightingShapeTool::Mode::Point, &action, nullptr);
    QSignalSpy spy(&tool, &LightingShapeTool::shapePicked);

    fixture.click(&tool, QPointF(120, 80));
    QCOMPARE(spy.count(), 1);
    const QPolygonF point = spy.first().at(1).value<QPolygonF>();
    QCOMPARE(point.size(), qsizetype(1));
    QVERIFY(QLineF(point.front(), QPointF(120, 80)).length() < 2.0);

    // Keys do nothing without a shape
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    event.ignore();
    tool.keyPressEvent(&fixture.widget, &event);
    QVERIFY(!event.isAccepted());

    // Click outside of the pages
    spy.clear();
    fixture.mouse(&tool, QEvent::MouseButtonPress, QPoint(-500, -500));
    fixture.mouse(&tool, QEvent::MouseButtonRelease, QPoint(-500, -500));
    QCOMPARE(spy.count(), 0);
}

void LightingToolsTest::placeLegendByClick()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    builder.build();
    const FixtureType a1 = type("A1", FixtureMarkerShape::Circle, Qt::red, "Area light");
    FixtureCountEditor::createMarker(&builder, page, QPointF(50, 50), 12.0, a1);
    FixtureCountEditor::createMarker(&builder, page, QPointF(80, 50), 12.0, a1);
    LightingFixture fixture(builder.build());
    fixture.types = { a1 };

    QVERIFY(fixture.action("lighting_PlaceSheetLegendAction")->isEnabled());
    fixture.action("lighting_PlaceSheetLegendAction")->trigger();
    LightingShapeTool* tool = fixture.activeTool();
    QVERIFY(tool);
    QCOMPARE(tool->getMode(), LightingShapeTool::Mode::Point);

    fixture.click(tool, QPointF(200, 350));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(!fixture.tools.getActiveTool());

    const LightingTableAnnotations legends = LightingTableWriter::scan(fixture.document.data(), LightingTableKind::Legend);
    QCOMPARE(legends.size(), size_t(1));
    QCOMPARE(legends.front().scope, LightingLegendScope::Sheet);
    QVERIFY(QLineF(LightingTableWriter::getTopLeft(legends.front().rect, PageRotation::None), QPointF(200, 350)).length() < 2.0);
    QCOMPARE(contents(*fixture.document, legends.front().annotation), QString("FIXTURE COUNT - PAGE 1\nType\tDescription\tQty\nA1\tArea light\t2\nTotal\t\t2"));

    // Set legend
    fixture.action("lighting_PlaceSetLegendAction")->trigger();
    fixture.click(fixture.activeTool(), QPointF(20, 350));
    QCOMPARE(LightingTableWriter::scan(fixture.document.data(), LightingTableKind::Legend).size(), size_t(2));
    QCOMPARE(LightingTableWriter::scan(fixture.document.data(), LightingTableKind::Legend).back().scope, LightingLegendScope::Document);

    // Update after a marker was deleted as an ordinary annotation
    PDFDocument changed = modify(*fixture.document, [&](PDFDocumentBuilder* modifyBuilder)
    {
        FixtureCountEditor::removeMarker(modifyBuilder, fixture.document.data(), FixtureCountData::scan(fixture.document.data()).getMarkers().front());
    });
    fixture.document = PDFDocumentPointer(new PDFDocument(changed));
    fixture.apply(PDFModifiedDocument(fixture.document, nullptr));
    fixture.action("lighting_UpdateTablesAction")->trigger();
    QCOMPARE(fixture.modificationCount, 3);
    QVERIFY(contents(*fixture.document, legends.front().annotation).endsWith("Total\t\t1"));

    // Actions are disabled without a document
    fixture.lighting.setDocument(nullptr);
    QVERIFY(!fixture.action("lighting_PlaceSheetLegendAction")->isEnabled());
    QCOMPARE(fixture.lighting.getActions().size(), size_t(7));
}

void LightingToolsTest::footcandleToolPlacesTable()
{
    QByteArray content;
    content += "BT /F1 8 Tf 100 500 Td (1.0   2.0   3.0) Tj ET\n";
    content += "BT /F1 8 Tf 100 480 Td (4.0   0.0   6.0) Tj ET\n";
    LightingFixture fixture(readPdf(createPdf(content, QSizeF(800, 800))));

    QAction* action = fixture.action("lighting_FootcandleStatsAction");
    QVERIFY(action->isCheckable());
    action->trigger();
    LightingShapeTool* tool = fixture.activeTool();
    QVERIFY(tool);
    QCOMPARE(tool->getMode(), LightingShapeTool::Mode::Area);

    // The dialog shows the statistics, the zeros are left out, the table is placed
    QString average;
    handleNextDialog([&average](QDialog* dialog)
    {
        dialog->findChild<QCheckBox*>("ignoreZerosCheckBox")->setChecked(true);
        dialog->findChild<QLineEdit*>("labelEdit")->setText("Lot A");
        average = dialog->findChild<QLabel*>("averageLabel")->text();
        dialog->findChild<QPushButton*>("placeButton")->click();
    });
    fixture.drag(tool, QPointF(90, 520), QPointF(200, 470));

    QCOMPARE(average, QString("3.2 fc"));
    QCOMPARE(fixture.modificationCount, 1);
    const LightingTableAnnotations tables = LightingTableWriter::scan(fixture.document.data(), LightingTableKind::FootcandleStats);
    QCOMPARE(tables.size(), size_t(1));
    QCOMPARE(contents(*fixture.document, tables.front().annotation),
             QString("CALCULATION SUMMARY\nArea\tAvg (fc)\tMax (fc)\tMin (fc)\tAvg:Min\tMax:Min\tPoints\nLot A\t3.2\t6.0\t1.0\t3.2:1\t6.0:1\t5"));
    QVERIFY(tables.front().rect.left() > 200.0);

    // The tool stays active for the next area; an area without values is reported
    QCOMPARE(fixture.activeTool(), tool);
    fixture.drag(tool, QPointF(500, 500), QPointF(600, 400));
    QCOMPARE(fixture.modificationCount, 1);
}

void LightingToolsTest::scheduleToolCreatesTypes()
{
    QByteArray content;
    content += "BT /F1 8 Tf 100 680 Td (TYPE) Tj ET\n";
    content += "BT /F1 8 Tf 150 680 Td (DESCRIPTION) Tj ET\n";
    content += "BT /F1 8 Tf 100 665 Td (SA) Tj ET\n";
    content += "BT /F1 8 Tf 150 665 Td (LED AREA LIGHT) Tj ET\n";
    content += "BT /F1 8 Tf 100 650 Td (WP) Tj ET\n";
    content += "BT /F1 8 Tf 150 650 Td (WALL PACK) Tj ET\n";
    LightingFixture fixture(readPdf(createPdf(content, QSizeF(800, 800))));
    fixture.types = { type("SA", FixtureMarkerShape::Circle, Qt::red) };

    fixture.action("lighting_ReadScheduleAction")->trigger();
    LightingShapeTool* tool = fixture.activeTool();
    QVERIFY(tool);

    QString typesText;
    handleNextDialog([&typesText](QDialog* dialog)
    {
        typesText = dialog->findChild<QLabel*>("typesLabel")->text();
        dialog->findChild<QPushButton*>("createTypesButton")->click();
    });
    fixture.drag(tool, QPointF(90, 690), QPointF(400, 640));

    QCOMPARE(typesText, QString("Fixture types: SA, WP"));
    QCOMPARE(fixture.importedTypes.size(), size_t(1));
    QCOMPARE(fixture.importedTypes.front().name, QString("WP"));
    QCOMPARE(fixture.importedTypes.front().description, QString("WALL PACK"));
    QVERIFY(!fixture.tools.getActiveTool());
    QCOMPARE(fixture.modificationCount, 0);
}

void LightingToolsTest::wireRunToolCreatesRun()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 720, 720));
    builder.build();
    const WireTakeoffSettings settings = LightingTools::loadTakeoffSettings();
    LightingTableWriter::createTable(&builder, page, PageRotation::None, QPointF(400, 700),
                                     WireTakeoff::createTable(WireCircuitTotals(), settings), LightingTableKind::WireTakeoff, LightingLegendScope::Sheet, 8.0);
    LightingFixture fixture(builder.build());

    fixture.action("lighting_WireRunAction")->trigger();
    LightingShapeTool* tool = fixture.activeTool();
    QVERIFY(tool);
    QCOMPARE(tool->getMode(), LightingShapeTool::Mode::Polyline);

    // The sheet has no scale, the scale is entered in the dialog (1 in = 10 ft)
    QString length;
    handleNextDialog([&length](QDialog* dialog)
    {
        dialog->findChild<QComboBox*>("circuitComboBox")->setCurrentText("LP-3");
        dialog->findChild<QSpinBox*>("fixturesSpinBox")->setValue(2);
        dialog->findChild<QSpinBox*>("conductorsSpinBox")->setValue(4);
        dialog->findChild<QDoubleSpinBox*>("paperInchesSpinBox")->setValue(1.0);
        dialog->findChild<QDoubleSpinBox*>("realLengthSpinBox")->setValue(10.0);
        length = dialog->findChild<QLabel*>("lengthLabel")->text();
        dialog->accept();
    });
    fixture.click(tool, QPointF(72, 72));
    fixture.click(tool, QPointF(72 + 288, 72));
    fixture.key(tool, Qt::Key_Return);

    QVERIFY(length.startsWith("40."));
    QCOMPARE(fixture.modificationCount, 1);
    const WireRuns runs = WireTakeoff::scan(fixture.document.data());
    QCOMPARE(runs.size(), size_t(1));
    QCOMPARE(runs.front().circuit, QString("LP-3"));
    QCOMPARE(runs.front().fixtures, 2);
    QCOMPARE(runs.front().conductors, 4);
    QVERIFY(qAbs(runs.front().getLengthInFeet() - 40.0) < 1.0);

    // The takeoff table on the sheet was rewritten in the same step
    const LightingTableAnnotations tables = LightingTableWriter::scan(fixture.document.data(), LightingTableKind::WireTakeoff);
    QCOMPARE(tables.size(), size_t(1));
    QVERIFY(contents(*fixture.document, tables.front().annotation).contains("\nLP-3\t2\t"));

    // The second run uses the scale of the first one, the dialog asks no scale
    bool hasScaleField = true;
    handleNextDialog([&hasScaleField](QDialog* dialog)
    {
        hasScaleField = dialog->findChild<QDoubleSpinBox*>("paperInchesSpinBox") != nullptr;
        QCOMPARE(dialog->findChild<QComboBox*>("circuitComboBox")->currentText(), QString("LP-3"));
        dialog->reject();
    });
    fixture.click(tool, QPointF(72, 300));
    fixture.click(tool, QPointF(300, 300));
    fixture.key(tool, Qt::Key_Return);
    QVERIFY(!hasScaleField);
    QCOMPARE(fixture.modificationCount, 1);

    // Takeoff dialog: place the table on the sheet by a click
    handleNextDialog([](QDialog* dialog)
    {
        dialog->findChild<QPushButton*>("placeButton")->click();
    });
    fixture.action("lighting_WireTakeoffAction")->trigger();
    QVERIFY(fixture.activeTool());
    QCOMPARE(fixture.activeTool()->getMode(), LightingShapeTool::Mode::Point);
    fixture.click(fixture.activeTool(), QPointF(50, 650));
    QCOMPARE(fixture.modificationCount, 2);
    QCOMPARE(LightingTableWriter::scan(fixture.document.data(), LightingTableKind::WireTakeoff).size(), size_t(2));
}

void LightingToolsTest::footcandleDialog()
{
    FootcandleValues values;
    for (PDFReal value : { 0.0, 1.0, 2.0, 5.0 })
    {
        values.push_back(FootcandleValue{ value, QPointF(), 1 });
    }

    FootcandleSettings settings;
    settings.label = "Drive";
    FootcandleStatsDialog dialog(values, settings, nullptr);
    QCOMPARE(dialog.findChild<QLabel*>("countLabel")->text(), QString("4"));
    QCOMPARE(dialog.findChild<QLabel*>("averageLabel")->text(), QString("2.0 fc"));
    QCOMPARE(dialog.findChild<QLabel*>("minimumLabel")->text(), QString("0.0 fc"));
    QCOMPARE(dialog.findChild<QLabel*>("averageToMinimumLabel")->text(), QString("N/A"));

    dialog.findChild<QCheckBox*>("ignoreZerosCheckBox")->setChecked(true);
    dialog.findChild<QComboBox*>("unitComboBox")->setCurrentIndex(1);
    QCOMPARE(dialog.findChild<QLabel*>("countLabel")->text(), QString("3"));
    QCOMPARE(dialog.findChild<QLabel*>("averageLabel")->text(), QString("2.7 lx"));
    QCOMPARE(dialog.findChild<QLabel*>("maximumToMinimumLabel")->text(), QString("5.0:1"));

    const FootcandleSettings result = dialog.getSettings();
    QVERIFY(result.ignoreZeros);
    QCOMPARE(result.unit, QString("lx"));
    QCOMPARE(result.label, QString("Drive"));
    QCOMPARE(dialog.getText(), QString("CALCULATION SUMMARY\nArea\tAvg (lx)\tMax (lx)\tMin (lx)\tAvg:Min\tMax:Min\tPoints\nDrive\t2.7\t5.0\t1.0\t2.7:1\t5.0:1\t3"));

    // Only zeros left out: nothing to place
    FootcandleStatsDialog zeros({ FootcandleValue{ 0.0, QPointF(), 1 } }, result, nullptr);
    QVERIFY(!zeros.findChild<QPushButton*>("placeButton")->isEnabled());
    QCOMPARE(zeros.findChild<QLabel*>("averageLabel")->text(), QString("-"));
}

void LightingToolsTest::scheduleDialog()
{
    const FixtureScheduleTable table = {
        { "SCHEDULE", "", "" },
        { "LAMPS", "TYPE", "DESCRIPTION" },
        { "2", "A", "Area light" },
        { "", "", "on pole" },
        { "1", "B", "Wall pack" }
    };

    FixtureScheduleDialog dialog(table, nullptr);
    QCOMPARE(dialog.getHeaderRow(), 1);
    QCOMPARE(dialog.getTypeColumn(), 1);
    QCOMPARE(dialog.getDescriptionColumn(), 2);
    QCOMPARE(dialog.findChild<QComboBox*>("typeColumnComboBox")->currentText(), QString("Column 2: TYPE"));
    QCOMPARE(dialog.findChild<QLabel*>("typesLabel")->text(), QString("Fixture types: A, B"));
    QCOMPARE(dialog.getTable().size(), size_t(4));
    QCOMPARE(dialog.findChild<QTableWidget*>("previewTable")->rowCount(), 4);

    dialog.findChild<QCheckBox*>("joinWrappedRowsCheckBox")->setChecked(false);
    QCOMPARE(dialog.getTable().size(), size_t(5));

    // Other type column: the numbers are no types... they are, but the header is skipped
    dialog.findChild<QComboBox*>("typeColumnComboBox")->setCurrentIndex(0);
    QCOMPARE(dialog.findChild<QLabel*>("typesLabel")->text(), QString("Fixture types: 2, 1"));

    QSignalSpy exportSpy(&dialog, &FixtureScheduleDialog::exportRequested);
    dialog.findChild<QPushButton*>("exportButton")->click();
    QCOMPARE(exportSpy.count(), 1);

    // No header: all rows are data
    dialog.findChild<QSpinBox*>("headerRowSpinBox")->setValue(0);
    QCOMPARE(dialog.getHeaderRow(), -1);
    QCOMPARE(dialog.findChild<QComboBox*>("typeColumnComboBox")->currentText(), QString("Column 1"));

    // Column without types
    FixtureScheduleDialog empty({ { "", "x" } }, nullptr);
    QVERIFY(!empty.findChild<QPushButton*>("createTypesButton")->isEnabled());
}

void LightingToolsTest::wireRunDialog()
{
    // Scale of the drawing is known: no scale fields
    const WireScale scale = WireScale::fromPaperScale(1.0, 20.0, "ft", 1.0);
    WireRunDialog known({ "1", "2" }, "2", 3, 360.0, scale, 1.0, nullptr);
    QVERIFY(!known.findChild<QDoubleSpinBox*>("paperInchesSpinBox"));
    QCOMPARE(known.findChild<QLabel*>("lengthLabel")->text(), QString("100.0 ft (scale 1 in = 20 ft)"));
    QCOMPARE(known.getCircuit(), QString("2"));
    QCOMPARE(known.getConductors(), 3);
    QCOMPARE(known.getFixtures(), 1);
    QCOMPARE(known.getScale().unitsPerUserSpaceUnit, scale.unitsPerUserSpaceUnit);
    QCOMPARE(known.getPaperInches(), 0.0);
    QCOMPARE(known.getRealUnit(), QString("ft"));
    known.setManualScale(2.0, 5.0, "m");

    // Unknown scale: entered by the user, remembered value
    WireRunDialog unknown({}, QString(), 2, 144.0, WireScale(), 1.0, nullptr);
    unknown.setManualScale(1.0, 50.0, "m");
    QCOMPARE(unknown.getRealUnit(), QString("m"));
    QCOMPARE(unknown.findChild<QLabel*>("lengthLabel")->text(), QString("100.0 m (scale 1 in = 50 m)"));
    QCOMPARE(unknown.getScale().unit, QString("m"));

    // A circuit is required
    handleNextDialog([](QDialog* messageBox) { messageBox->reject(); });
    unknown.accept();
    QCOMPARE(unknown.result(), int(QDialog::Rejected));
    unknown.findChild<QComboBox*>("circuitComboBox")->setCurrentText(" 4 ");
    unknown.accept();
    QCOMPARE(unknown.result(), int(QDialog::Accepted));
    QCOMPARE(unknown.getCircuit(), QString("4"));
}

void LightingToolsTest::wireTakeoffDialog()
{
    WireRun run;
    run.circuit = "1";
    run.fixtures = 2;
    run.conductors = 3;
    run.points << QPointF(0, 0) << QPointF(360, 0);
    run.scale = WireScale::fromPaperScale(1.0, 20.0, "ft", 1.0);
    WireRun unscaled = run;
    unscaled.scale = WireScale();

    WireTakeoffSettings settings;
    settings.dropLength = 10.0;
    settings.wastePercent = 0.0;
    WireTakeoffDialog dialog({ run, unscaled }, settings, nullptr);

    QTableWidget* preview = dialog.findChild<QTableWidget*>("previewTable");
    QCOMPARE(preview->rowCount(), 3);
    QCOMPARE(preview->item(1, 6)->text(), QString("360"));
    QVERIFY(dialog.findChild<QLabel*>("infoLabel")->text().contains("1 run(s) without a scale"));

    dialog.findChild<QDoubleSpinBox*>("wasteSpinBox")->setValue(10.0);
    QCOMPARE(preview->item(1, 6)->text(), QString("396"));

    // Meters: the drop length is converted, so the takeoff is the same length
    dialog.findChild<QComboBox*>("unitComboBox")->setCurrentIndex(1);
    QCOMPARE(dialog.findChild<QDoubleSpinBox*>("dropLengthSpinBox")->value(), 3.0);
    QCOMPARE(preview->item(0, 2)->text(), QString("Conduit (m)"));
    const WireTakeoffSettings result = dialog.getSettings();
    QVERIFY(result.metric);
    QVERIFY(qAbs(result.dropLength - 3.0 / 0.3048) < 1e-9);
    QCOMPARE(result.wastePercent, 10.0);

    QSignalSpy exportSpy(&dialog, &WireTakeoffDialog::exportRequested);
    dialog.findChild<QPushButton*>("exportButton")->click();
    QCOMPARE(exportSpy.count(), 1);
    QCOMPARE(exportSpy.first().at(0).value<LightingTable>().rows.size(), size_t(3));
}

QTEST_MAIN(LightingToolsTest)

#include "tst_lightingtoolstest.moc"
