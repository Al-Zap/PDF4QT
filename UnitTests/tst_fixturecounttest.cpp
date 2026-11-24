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

#include "fixturecount.h"
#include "fixturesymbolfinder.h"

#include "pdfaction.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfoptionalcontent.h"
#include "pdfoutline.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdfutils.h"

#include <QtTest>
#include <QBuffer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>

using namespace pdf;
using namespace pdfplugin;

class FixtureCountTest : public QObject
{
    Q_OBJECT

private slots:
    void scanEmptyDocument();
    void createAndCountMarkers();
    void markersOfAllShapes();
    void markerProperties();
    void findMarker();
    void removeMarker();
    void changeMarkerType();
    void ignoresOrdinaryAnnotations();
    void savedAndReopened();
    void sheetNamesFromBookmarks();
    void tableAndCsv();
    void csvEscaping();
    void defaultTypesDiffer();
    void shapePolygons();
    void changeMarkerTypeInPlace();
    void sheetNamesFromPageLabels();
    void csvFormulaInjection();
    void csvSeparatorOfLocale();
    void findSimilarSymbols();
    void findSimilarSymbolsWithoutRotations();
    void findSimilarSymbolsEmptySample();
    void findSimilarSymbolsAnyAngle();
    void findSymmetricSymbols();
    void findSymbolsByInkColor();
    void precompiledPageWithoutImagesAndText();
    void removeLargePaths();
    void removePathsOfOtherColors();
    void findOnPageRotatedAndMirrored();
    void findOnPageCrossedByLines();
    void findOnPageRejectsOtherSymbols();
    void findOnPageWithoutMirrors();
    void findOnPageSampleCrossedByLine();
    void findLargeSample();
    void realDocumentBenchmark();

private:
    static FixtureType type(const QString& name, FixtureMarkerShape shape = FixtureMarkerShape::Circle, QColor color = Qt::red, const QString& description = QString());
    static QByteArray write(const PDFDocument& document);
    static void drawFixtureSymbol(QPainter& painter, QPointF center, qreal angle);
    static void drawOtherSymbol(QPainter& painter, QPointF center);
    static QImage createSheetImage();
    static int countMatchesNear(const std::vector<FixtureSymbolFinder::Match>& matches, const std::vector<QPointF>& points, qreal tolerance);
    static std::vector<QPointF> getFixturePositions(bool rotated);
    static void addPoleSymbol(pdf::PDFPrecompiledPage& page, QPointF center, qreal angle, qreal scale = 1.0, bool mirrored = false);
    static void addLine(pdf::PDFPrecompiledPage& page, const std::vector<QPointF>& points, QColor color, qreal width, bool asSegments);
    static std::vector<FixtureSymbolFinder::Match> findOnPage(const pdf::PDFPrecompiledPage& compiledPage, QRectF sampleRect, const FixtureSymbolFinder::Settings& settings = FixtureSymbolFinder::Settings());
    static PDFDocument modify(const PDFDocument& document, const std::function<void(PDFDocumentBuilder*)>& function);
};

FixtureType FixtureCountTest::type(const QString& name, FixtureMarkerShape shape, QColor color, const QString& description)
{
    FixtureType result;
    result.name = name;
    result.shape = shape;
    result.color = color;
    result.description = description;
    return result;
}

QByteArray FixtureCountTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    writer.write(&buffer, &document);
    buffer.close();
    return buffer.data();
}

PDFDocument FixtureCountTest::modify(const PDFDocument& document, const std::function<void(PDFDocumentBuilder*)>& function)
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

void FixtureCountTest::scanEmptyDocument()
{
    FixtureCountData data = FixtureCountData::scan(nullptr);
    QVERIFY(data.getMarkers().empty());
    QCOMPARE(data.getTotalCount(), 0);

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    PDFDocument document = builder.build();
    data = FixtureCountData::scan(&document);
    QVERIFY(data.getMarkers().empty());
    QVERIFY(data.getTypes().empty());
    QCOMPARE(data.getCount("A1"), 0);
    QVERIFY(data.getPagesWithMarkers().empty());
}

void FixtureCountTest::createAndCountMarkers()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 2592, 1728));
    builder.appendPage(QRectF(0, 0, 2592, 1728));

    const FixtureType a1 = type("A1", FixtureMarkerShape::Circle, Qt::red, "2x4 LED troffer");
    const FixtureType b2 = type("B2", FixtureMarkerShape::Square, Qt::blue, "Downlight");

    for (int i = 0; i < 5; ++i)
    {
        FixtureCountEditor::createMarker(&builder, page1, QPointF(100 + 50 * i, 100), 12.0, a1);
    }
    for (int i = 0; i < 3; ++i)
    {
        FixtureCountEditor::createMarker(&builder, page1, QPointF(100 + 50 * i, 300), 12.0, b2);
    }
    for (int i = 0; i < 2; ++i)
    {
        FixtureCountEditor::createMarker(&builder, page2, QPointF(100 + 50 * i, 300), 12.0, a1);
    }

    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);

    QCOMPARE(data.getMarkers().size(), size_t(10));
    QCOMPARE(data.getCount("A1"), 7);
    QCOMPARE(data.getCount("A1", 0), 5);
    QCOMPARE(data.getCount("A1", 1), 2);
    QCOMPARE(data.getCount("A1", 2), 0);
    QCOMPARE(data.getCount("B2"), 3);
    QCOMPARE(data.getCount("B2", 0), 3);
    QCOMPARE(data.getCount("B2", 1), 0);
    QCOMPARE(data.getCount("C3"), 0);
    QCOMPARE(data.getTotalCount(), 10);
    QCOMPARE(data.getTotalCount(0), 8);
    QCOMPARE(data.getTotalCount(1), 2);
    QCOMPARE(data.getTotalCount(2), 0);
    QCOMPARE(data.getPagesWithMarkers(), (std::vector<PDFInteger>{ 0, 1 }));
    QCOMPARE(data.getMarkersOfType("B2").size(), size_t(3));

    // Types in the order of the first use, with their properties
    QCOMPARE(data.getTypes().size(), size_t(2));
    QCOMPARE(data.getTypes()[0].name, QString("A1"));
    QCOMPARE(data.getTypes()[0].description, QString("2x4 LED troffer"));
    QCOMPARE(data.getTypes()[0].shape, FixtureMarkerShape::Circle);
    QCOMPARE(data.getTypes()[0].color, QColor(Qt::red));
    QCOMPARE(data.getTypes()[1].name, QString("B2"));
    QCOMPARE(data.getTypes()[1].shape, FixtureMarkerShape::Square);
    QCOMPARE(data.getTypes()[1].color, QColor(Qt::blue));
}

void FixtureCountTest::markersOfAllShapes()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));

    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        const FixtureMarkerShape shape = FixtureMarkerShape(i);
        const PDFObjectReference annotation = FixtureCountEditor::createMarker(&builder, page, QPointF(100 + 100 * i, 100), 20.0, type(QString("S%1").arg(i), shape));
        QVERIFY(annotation.isValid());
    }

    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);
    QCOMPARE(data.getTypes().size(), size_t(FixtureMarkerShape::LastShape));

    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        QCOMPARE(data.getTypes()[i].shape, FixtureMarkerShape(i));

        // Center of the marker is the clicked point for all shapes
        const FixtureMarker& marker = data.getMarkers()[i];
        QVERIFY(qAbs(marker.getCenter().x() - (100 + 100 * i)) < 1.0);
        QVERIFY(qAbs(marker.getCenter().y() - 100) < 1.0);
        QVERIFY(marker.rect.width() >= 19.0);
    }

    PDFDocumentDataLoaderDecorator loader(&document);
    const std::vector<QByteArray> expectedSubtypes = { "Circle", "Square", "Polygon", "Polygon" };
    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(data.getMarkers()[i].annotation));
        QCOMPARE(loader.readNameFromDictionary(dictionary, "Subtype"), expectedSubtypes[i]);
    }
}

void FixtureCountTest::markerProperties()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference annotation = FixtureCountEditor::createMarker(&builder, page, QPointF(200, 200), 12.0, type("A1", FixtureMarkerShape::Circle, QColor(0, 128, 255), "Troffer"));
    const PDFDocument document = builder.build();

    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    QVERIFY(dictionary);

    // Other viewers show the type in the markup list
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "Subj", QString()), QString("Fixture A1"));
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "Contents", QString()), QString("A1 - Troffer"));
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, FixtureCountData::FIXTURE_TYPE_KEY, QString()), QString("A1"));
    QCOMPARE(loader.readNameFromDictionary(dictionary, FixtureCountData::FIXTURE_SHAPE_KEY), QByteArray("Circle"));

    // Transparent interior, so the symbol under the marker stays visible
    QCOMPARE(loader.readNumberFromDictionary(dictionary, "ca", 1.0), FixtureCountEditor::FILL_OPACITY);

    // Appearance stream is created, so other viewers display the marker
    QVERIFY(dictionary->hasKey("AP"));

    // Marker is in the annotations of the page
    QCOMPARE(document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(0)->getAnnotations().front(), annotation);
}

void FixtureCountTest::findMarker()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    FixtureCountEditor::createMarker(&builder, page1, QPointF(100, 100), 10.0, type("A1"));
    FixtureCountEditor::createMarker(&builder, page1, QPointF(108, 100), 10.0, type("B2"));
    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);

    const FixtureMarker* marker = data.findMarker(0, QPointF(100, 100), 0.0);
    QVERIFY(marker);
    QCOMPARE(marker->typeName, QString("A1"));

    // Overlapping markers - the nearest center wins
    marker = data.findMarker(0, QPointF(106, 100), 0.0);
    QVERIFY(marker);
    QCOMPARE(marker->typeName, QString("B2"));

    QVERIFY(!data.findMarker(0, QPointF(300, 300), 0.0));
    QVERIFY(!data.findMarker(1, QPointF(100, 100), 0.0));
    QVERIFY(!data.findMarker(0, QPointF(100, 112), 0.0));
    QVERIFY(data.findMarker(0, QPointF(100, 112), 10.0));
}

void FixtureCountTest::removeMarker()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    FixtureCountEditor::createMarker(&builder, page, QPointF(100, 100), 10.0, type("A1"));
    FixtureCountEditor::createMarker(&builder, page, QPointF(200, 100), 10.0, type("A1"));
    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);
    QCOMPARE(data.getCount("A1"), 2);

    const FixtureMarker marker = *data.findMarker(0, QPointF(200, 100), 0.0);
    const PDFDocument modified = modify(document, [&](PDFDocumentBuilder* builder) { QVERIFY(FixtureCountEditor::removeMarker(builder, &document, marker)); });
    const FixtureCountData modifiedData = FixtureCountData::scan(&modified);
    QCOMPARE(modifiedData.getCount("A1"), 1);
    QVERIFY(modifiedData.findMarker(0, QPointF(100, 100), 0.0));
    QVERIFY(!modifiedData.findMarker(0, QPointF(200, 100), 0.0));

    // Original document is not changed (the undo returns to it)
    QCOMPARE(FixtureCountData::scan(&document).getCount("A1"), 2);

    FixtureMarker invalidMarker = marker;
    invalidMarker.pageIndex = 5;
    PDFDocumentModifier modifier(&document);
    QVERIFY(!FixtureCountEditor::removeMarker(modifier.getBuilder(), &document, invalidMarker));
}

void FixtureCountTest::changeMarkerType()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    FixtureCountEditor::createMarker(&builder, page, QPointF(100, 100), 16.0, type("A1"));
    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);
    const FixtureMarker marker = data.getMarkers().front();

    const FixtureType b2 = type("B2", FixtureMarkerShape::Triangle, Qt::green, "Wall pack");
    PDFDocument modified = document;

    // Repeated change must keep the position and the size of the marker
    for (int i = 0; i < 3; ++i)
    {
        const FixtureCountData currentData = FixtureCountData::scan(&modified);
        const FixtureMarker currentMarker = currentData.getMarkers().front();
        modified = modify(modified, [&](PDFDocumentBuilder* builder) { QVERIFY(FixtureCountEditor::changeMarkerType(builder, &modified, currentMarker, i % 2 ? type("A1") : b2)); });
    }

    const FixtureCountData modifiedData = FixtureCountData::scan(&modified);
    QCOMPARE(modifiedData.getMarkers().size(), size_t(1));
    QCOMPARE(modifiedData.getCount("A1"), 0);
    QCOMPARE(modifiedData.getCount("B2"), 1);
    QCOMPARE(modifiedData.getTypes().front().shape, FixtureMarkerShape::Triangle);
    QCOMPARE(modifiedData.getTypes().front().description, QString("Wall pack"));

    const FixtureMarker& changedMarker = modifiedData.getMarkers().front();
    QVERIFY(qAbs(changedMarker.getCenter().x() - marker.getCenter().x()) < 0.5);
    QVERIFY(qAbs(changedMarker.getCenter().y() - marker.getCenter().y()) < 0.5);
    QVERIFY(qAbs(changedMarker.rect.width() - marker.rect.width()) < 2.5);
    QCOMPARE(changedMarker.size, 16.0);
    QCOMPARE(modified.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
}

void FixtureCountTest::ignoresOrdinaryAnnotations()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.createAnnotationCircle(page, QRectF(10, 10, 20, 20), 1.0, Qt::red, Qt::red, "Author", "Subject", "Contents");
    builder.createAnnotationSquare(page, QRectF(50, 10, 20, 20), 1.0, Qt::red, Qt::red, "Author", "Subject", "Contents");
    FixtureCountEditor::createMarker(&builder, page, QPointF(100, 100), 10.0, type("A1"));
    const PDFDocument document = builder.build();

    const FixtureCountData data = FixtureCountData::scan(&document);
    QCOMPARE(data.getMarkers().size(), size_t(1));
    QCOMPARE(data.getTotalCount(), 1);
}

void FixtureCountTest::savedAndReopened()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    FixtureCountEditor::createMarker(&builder, page, QPointF(100, 100), 10.0, type("A1", FixtureMarkerShape::Diamond, QColor(255, 0, 255), "Pendant ž"));
    FixtureCountEditor::createMarker(&builder, page, QPointF(200, 100), 10.0, type("A1", FixtureMarkerShape::Diamond, QColor(255, 0, 255), "Pendant ž"));
    FixtureCountEditor::createMarker(&builder, page, QPointF(300, 100), 10.0, type("EX-1", FixtureMarkerShape::Square, Qt::green));
    const PDFDocument document = builder.build();

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument reopened = reader.readFromBuffer(write(document));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const FixtureCountData data = FixtureCountData::scan(&reopened);
    QCOMPARE(data.getCount("A1"), 2);
    QCOMPARE(data.getCount("EX-1"), 1);
    QCOMPARE(data.getTypes()[0].description, QString("Pendant ž"));
    QCOMPARE(data.getTypes()[0].shape, FixtureMarkerShape::Diamond);
    QCOMPARE(data.getTypes()[0].color, QColor(255, 0, 255));
}

void FixtureCountTest::sheetNamesFromBookmarks()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference page3 = builder.appendPage(QRectF(0, 0, 612, 792));

    PDFOutlineItem root;
    QSharedPointer<PDFOutlineItem> electrical(new PDFOutlineItem());
    electrical->setTitle("Electrical");

    QSharedPointer<PDFOutlineItem> sheet1(new PDFOutlineItem());
    sheet1->setTitle("E-101  Lighting Plan - Level 1");
    sheet1->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(page1), PDFDestination())));

    QSharedPointer<PDFOutlineItem> sheet3(new PDFOutlineItem());
    sheet3->setTitle("E-102 Lighting Plan - Level 2");
    sheet3->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(page3), PDFDestination())));

    // Second bookmark of the same page does not replace the first one
    QSharedPointer<PDFOutlineItem> detail(new PDFOutlineItem());
    detail->setTitle("Detail 3");
    detail->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(page3), PDFDestination())));

    electrical->addChild(sheet1);
    electrical->addChild(sheet3);
    electrical->addChild(detail);
    root.addChild(electrical);
    builder.setOutline(&root);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(write(builder.build()));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const QStringList names = FixtureCountReport::getSheetNames(&document);
    QCOMPARE(names, (QStringList{ "E-101 Lighting Plan - Level 1", "Page 2", "E-102 Lighting Plan - Level 2" }));
    QVERIFY(FixtureCountReport::getSheetNames(nullptr).isEmpty());
}

void FixtureCountTest::tableAndCsv()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference page3 = builder.appendPage(QRectF(0, 0, 612, 792));
    const FixtureType a1 = type("A1", FixtureMarkerShape::Circle, Qt::red, "2x4 troffer");
    const FixtureType b2 = type("B2", FixtureMarkerShape::Square, Qt::blue, "Downlight, 6\"");
    const FixtureType c3 = type("C3", FixtureMarkerShape::Diamond, Qt::green, "Unused");

    FixtureCountEditor::createMarker(&builder, page1, QPointF(100, 100), 10.0, a1);
    FixtureCountEditor::createMarker(&builder, page1, QPointF(200, 100), 10.0, a1);
    FixtureCountEditor::createMarker(&builder, page3, QPointF(100, 100), 10.0, a1);
    FixtureCountEditor::createMarker(&builder, page3, QPointF(200, 100), 10.0, b2);
    FixtureCountEditor::createMarker(&builder, page3, QPointF(300, 100), 10.0, type("D4"));
    const PDFDocument document = builder.build();
    const FixtureCountData data = FixtureCountData::scan(&document);
    const QStringList sheetNames = { "E-101", "E-102", "E-103" };

    // Type C3 without markers is listed, type D4, which is only in the document, too.
    // Page without markers has no column.
    const std::vector<QStringList> table = FixtureCountReport::createTable({ a1, b2, c3 }, data, sheetNames);
    QCOMPARE(table.size(), size_t(6));
    QCOMPARE(table[0], (QStringList{ "Type", "Description", "E-101", "E-103", "Total" }));
    QCOMPARE(table[1], (QStringList{ "A1", "2x4 troffer", "2", "1", "3" }));
    QCOMPARE(table[2], (QStringList{ "B2", "Downlight, 6\"", "0", "1", "1" }));
    QCOMPARE(table[3], (QStringList{ "C3", "Unused", "0", "0", "0" }));
    QCOMPARE(table[4], (QStringList{ "D4", "", "0", "1", "1" }));
    QCOMPARE(table[5], (QStringList{ "Total", "", "2", "3", "5" }));

    const QString csv = FixtureCountReport::createCsv(table);
    const QStringList lines = csv.split("\r\n", Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 6);
    QCOMPARE(lines[0], QString("Type,Description,E-101,E-103,Total"));
    QCOMPARE(lines[2], QString("B2,\"Downlight, 6\"\"\",0,1,1"));
    QCOMPARE(lines[5], QString("Total,,2,3,5"));
}

void FixtureCountTest::csvEscaping()
{
    QCOMPARE(FixtureCountReport::escapeCsvField("A1"), QString("A1"));
    QCOMPARE(FixtureCountReport::escapeCsvField("a,b"), QString("\"a,b\""));
    QCOMPARE(FixtureCountReport::escapeCsvField("6\" can"), QString("\"6\"\" can\""));
    QCOMPARE(FixtureCountReport::escapeCsvField("line\nbreak"), QString("\"line\nbreak\""));
    QCOMPARE(FixtureCountReport::escapeCsvField(" padded"), QString("\" padded\""));
    QCOMPARE(FixtureCountReport::escapeCsvField(QString()), QString());
}

void FixtureCountTest::defaultTypesDiffer()
{
    // Consecutive default types must be distinguishable by the color or the shape
    for (int i = 0; i < 40; ++i)
    {
        const FixtureType first = FixtureType::createDefaultType(i);
        QVERIFY(first.isValid());
        QVERIFY(first.color.isValid());

        for (int j = i + 1; j < 40; ++j)
        {
            const FixtureType second = FixtureType::createDefaultType(j);
            QVERIFY(first.name != second.name);
            QVERIFY2(first.color != second.color || first.shape != second.shape, qPrintable(QString("%1 %2").arg(i).arg(j)));
        }
    }

    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        const FixtureMarkerShape shape = FixtureMarkerShape(i);
        QCOMPARE(FixtureType::getShapeFromKey(FixtureType::getShapeKey(shape)), shape);
        QVERIFY(!FixtureType::getShapeName(shape).isEmpty());
    }
    QCOMPARE(FixtureType::getShapeFromKey("Unknown"), FixtureMarkerShape::Circle);
}

void FixtureCountTest::shapePolygons()
{
    const QRectF rect(10, 20, 30, 30);
    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        const QPolygonF polygon = FixtureType::getShapePolygon(FixtureMarkerShape(i), rect);
        QVERIFY(polygon.size() >= 3);

        // All shapes fill the rectangle
        const QRectF boundingRect = polygon.boundingRect();
        QVERIFY(qAbs(boundingRect.left() - rect.left()) < 0.01);
        QVERIFY(qAbs(boundingRect.right() - rect.right()) < 0.01);
        QVERIFY(qAbs(boundingRect.top() - rect.top()) < 0.01);
        QVERIFY(qAbs(boundingRect.bottom() - rect.bottom()) < 0.01);
    }

    // Triangle points up on the page (y axis of the page goes up)
    const QPolygonF triangle = FixtureType::getShapePolygon(FixtureMarkerShape::Triangle, rect);
    QCOMPARE(triangle.size(), 3);
    QCOMPARE(triangle[2], QPointF(25, 50));
}

void FixtureCountTest::drawFixtureSymbol(QPainter& painter, QPointF center, qreal angle)
{
    // Asymmetric symbol (2x4 troffer with a tag line), so the rotations differ
    painter.save();
    painter.translate(center);
    painter.rotate(angle);
    painter.setPen(QPen(Qt::black, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(-20, -10, 40, 20));
    painter.drawLine(QPointF(-20, -10), QPointF(20, 10));
    painter.drawLine(QPointF(20, -10), QPointF(28, -18));
    painter.restore();
}

void FixtureCountTest::drawOtherSymbol(QPainter& painter, QPointF center)
{
    // Downlight - a circle with a cross, which must not be found
    painter.save();
    painter.setPen(QPen(Qt::black, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, 12, 12);
    painter.drawLine(center + QPointF(-12, 0), center + QPointF(12, 0));
    painter.drawLine(center + QPointF(0, -12), center + QPointF(0, 12));
    painter.restore();
}

std::vector<QPointF> FixtureCountTest::getFixturePositions(bool rotated)
{
    std::vector<QPointF> positions;
    for (int i = 0; i < 6; ++i)
    {
        positions.push_back(QPointF(100 + 150 * i, 100));
    }

    if (rotated)
    {
        for (int i = 0; i < 3; ++i)
        {
            positions.push_back(QPointF(100 + 150 * i, 400));
        }
    }
    return positions;
}

QImage FixtureCountTest::createSheetImage()
{
    QImage image(1000, 600, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // Walls of the plan
    painter.setPen(QPen(Qt::black, 4.0));
    painter.drawRect(QRect(20, 20, 960, 560));
    painter.drawLine(QPoint(20, 300), QPoint(980, 300));

    for (const QPointF& position : getFixturePositions(false))
    {
        drawFixtureSymbol(painter, position, 0.0);
    }

    // Rotated fixtures
    drawFixtureSymbol(painter, QPointF(100, 400), 90.0);
    drawFixtureSymbol(painter, QPointF(250, 400), 180.0);
    drawFixtureSymbol(painter, QPointF(400, 400), 270.0);

    // Other symbols and text
    for (int i = 0; i < 4; ++i)
    {
        drawOtherSymbol(painter, QPointF(600 + 100 * i, 400));
    }
    painter.setFont(QFont("Arial", 12));
    painter.drawText(QPointF(600, 500), "OFFICE 101");
    painter.drawText(QPointF(100, 220), "A1");
    painter.end();

    return image;
}

int FixtureCountTest::countMatchesNear(const std::vector<FixtureSymbolFinder::Match>& matches, const std::vector<QPointF>& points, qreal tolerance)
{
    int count = 0;
    for (const QPointF& point : points)
    {
        const bool found = std::any_of(matches.cbegin(), matches.cend(), [&](const FixtureSymbolFinder::Match& match)
        {
            return QLineF(match.center, point).length() < tolerance;
        });

        if (found)
        {
            ++count;
        }
    }
    return count;
}

void FixtureCountTest::changeMarkerTypeInPlace()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    FixtureCountEditor::createMarker(&builder, page, QPointF(100, 100), 16.0, type("A1", FixtureMarkerShape::Circle, Qt::red, "Troffer"));
    const PDFDocument document = builder.build();
    const FixtureMarker marker = FixtureCountData::scan(&document).getMarkers().front();

    // Type of the same shape (renamed, another color) - the annotation is kept,
    // so the replies to the marker and its geometry are not lost
    const FixtureType renamedType = type("A1E", FixtureMarkerShape::Circle, Qt::blue, QString());
    const PDFDocument modified = modify(document, [&](PDFDocumentBuilder* builder) { QVERIFY(FixtureCountEditor::changeMarkerType(builder, &document, marker, renamedType)); });

    const FixtureCountData modifiedData = FixtureCountData::scan(&modified);
    QCOMPARE(modifiedData.getMarkers().size(), size_t(1));
    QCOMPARE(modifiedData.getCount("A1"), 0);
    QCOMPARE(modifiedData.getCount("A1E"), 1);

    const FixtureMarker& changedMarker = modifiedData.getMarkers().front();
    QCOMPARE(changedMarker.annotation, marker.annotation);
    QCOMPARE(changedMarker.rect, marker.rect);
    QCOMPARE(changedMarker.size, marker.size);

    const FixtureType changedType = modifiedData.getTypes().front();
    QCOMPARE(changedType.name, QString("A1E"));
    QCOMPARE(changedType.color.rgb(), QColor(Qt::blue).rgb());
    QCOMPARE(changedType.shape, FixtureMarkerShape::Circle);
    QVERIFY(changedType.description.isEmpty());

    // Another shape needs another annotation
    const PDFDocument reshaped = modify(modified, [&](PDFDocumentBuilder* builder) { QVERIFY(FixtureCountEditor::changeMarkerType(builder, &modified, changedMarker, type("A1E", FixtureMarkerShape::Square))); });
    const FixtureCountData reshapedData = FixtureCountData::scan(&reshaped);
    QCOMPARE(reshapedData.getMarkers().size(), size_t(1));
    QCOMPARE(reshapedData.getTypes().front().shape, FixtureMarkerShape::Square);
    QVERIFY(reshapedData.getMarkers().front().annotation != marker.annotation);
}

void FixtureCountTest::sheetNamesFromPageLabels()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference page4 = builder.appendPage(QRectF(0, 0, 612, 792));

    // Labels as AutoCAD writes them, a plain sheet number and a plain page number
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("PageLabels");
    factory.beginDictionary();
    factory.beginDictionaryItem("Nums");
    factory.beginArray();
    factory << PDFInteger(0);
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << QString("[1] Light LT-1");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory << PDFInteger(1);
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << QString("E-102");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory << PDFInteger(2);
    factory.beginDictionary();
    factory.beginDictionaryItem("S");
    factory << WrapName("D");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), factory.takeObject());

    // Bookmark has the precedence over the label
    PDFOutlineItem root;
    QSharedPointer<PDFOutlineItem> sheet4(new PDFOutlineItem());
    sheet4->setTitle("E-501 Details");
    sheet4->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(page4), PDFDestination())));
    root.addChild(sheet4);
    builder.setOutline(&root);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(write(builder.build()));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const QStringList names = FixtureCountReport::getSheetNames(&document);
    QCOMPARE(names, (QStringList{ "Light LT-1", "E-102", "Page 3", "E-501 Details" }));
}

void FixtureCountTest::csvFormulaInjection()
{
    // Text from the document must not be evaluated as a formula by a spreadsheet
    QCOMPARE(FixtureCountReport::escapeCsvField("=HYPERLINK(\"http://x\")"), QString("\"'=HYPERLINK(\"\"http://x\"\")\""));
    QCOMPARE(FixtureCountReport::escapeCsvField("+1"), QString("'+1"));
    QCOMPARE(FixtureCountReport::escapeCsvField("-A"), QString("'-A"));
    QCOMPARE(FixtureCountReport::escapeCsvField("@SUM(A1)"), QString("'@SUM(A1)"));
    QCOMPARE(FixtureCountReport::escapeCsvField("E-101"), QString("E-101"));
    QCOMPARE(FixtureCountReport::escapeCsvField("12"), QString("12"));
}

void FixtureCountTest::csvSeparatorOfLocale()
{
    // Spreadsheets in the locales with the decimal comma expect a semicolon
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::English, QLocale::UnitedStates)), QChar(','));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::English, QLocale::UnitedKingdom)), QChar(','));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::German, QLocale::Germany)), QChar(';'));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::French, QLocale::France)), QChar(';'));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::Czech, QLocale::CzechRepublic)), QChar(';'));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale(QLocale::Spanish, QLocale::Spain)), QChar(';'));
    QCOMPARE(PDFSysUtils::getListSeparator(QLocale::c()), QChar(','));

    const QChar systemSeparator = PDFSysUtils::getSystemListSeparator();
    QVERIFY(!systemSeparator.isLetterOrNumber() && !systemSeparator.isSpace() && systemSeparator != QChar('"'));

    // Fields with any of the separators are quoted, so the file is read
    // correctly whichever separator the spreadsheet expects
    QCOMPARE(FixtureCountReport::escapeCsvField("a;b"), QString("\"a;b\""));
    QCOMPARE(FixtureCountReport::escapeCsvField("a\tb"), QString("\"a\tb\""));

    const std::vector<QStringList> table = { { "Type", "Description", "E-101", "Total" },
                                             { "B2", "Downlight, 6\"", "3", "3" },
                                             { "C3", "Wall pack; LED", "1", "1" },
                                             { "Total", "", "4", "4" } };
    const QStringList lines = FixtureCountReport::createCsv(table, QChar(';')).split("\r\n", Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines[0], QString("Type;Description;E-101;Total"));
    QCOMPARE(lines[1], QString("B2;\"Downlight, 6\"\"\";3;3"));
    QCOMPARE(lines[2], QString("C3;\"Wall pack; LED\";1;1"));
    QCOMPARE(lines[3], QString("Total;;4;4"));
}

void FixtureCountTest::findSimilarSymbols()
{
    const QImage image = createSheetImage();

    // Sample is the first fixture, with some whitespace around it
    const QRect sampleRect(70, 75, 70, 50);
    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(image, sampleRect, FixtureSymbolFinder::Settings());

    const std::vector<QPointF> expected = getFixturePositions(true);
    QCOMPARE(countMatchesNear(matches, expected, 8.0), int(expected.size()));
    QCOMPARE(matches.size(), expected.size());

    for (const FixtureSymbolFinder::Match& match : matches)
    {
        QVERIFY(match.score >= FixtureSymbolFinder::Settings().threshold);
    }
}

void FixtureCountTest::findSimilarSymbolsWithoutRotations()
{
    const QImage image = createSheetImage();

    FixtureSymbolFinder::Settings settings;
    settings.rotations = false;
    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(image, QRect(70, 75, 70, 50), settings);

    const std::vector<QPointF> expected = getFixturePositions(false);
    QCOMPARE(countMatchesNear(matches, expected, 8.0), int(expected.size()));

    // Symbols rotated by 90 and 270 degrees are not found. The symbol rotated
    // by 180 degrees differs only by the short tag line, so it can be found.
    QCOMPARE(countMatchesNear(matches, { QPointF(100, 400), QPointF(400, 400) }, 15.0), 0);
    QVERIFY(matches.size() <= expected.size() + 1);
}

void FixtureCountTest::findSimilarSymbolsEmptySample()
{
    const QImage image = createSheetImage();

    // Blank paper, the sample outside of the image and a tiny sample find nothing
    QVERIFY(FixtureSymbolFinder::find(image, QRect(500, 150, 40, 40), FixtureSymbolFinder::Settings()).empty());
    QVERIFY(FixtureSymbolFinder::find(image, QRect(2000, 2000, 40, 40), FixtureSymbolFinder::Settings()).empty());
    QVERIFY(FixtureSymbolFinder::find(QImage(), QRect(0, 0, 10, 10), FixtureSymbolFinder::Settings()).empty());

    // Ink bounding rectangle of the sample
    const QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
    const QRect inkRect = FixtureSymbolFinder::getInkBoundingRect(grayImage, QRect(70, 75, 70, 50));
    QVERIFY(inkRect.left() >= 77 && inkRect.left() <= 80);
    QVERIFY(inkRect.right() >= 120 && inkRect.right() <= 130);
    QVERIFY(FixtureSymbolFinder::getInkBoundingRect(grayImage, QRect(500, 150, 40, 40)).isEmpty());
}

void FixtureCountTest::findSimilarSymbolsAnyAngle()
{
    // Fixtures on site plans point in all directions
    const std::vector<std::pair<QPointF, qreal>> fixtures =
    {
        { QPointF(100, 100), 0.0 }, { QPointF(250, 100), 30.0 }, { QPointF(400, 100), 65.0 }, { QPointF(550, 100), 137.0 },
        { QPointF(700, 100), 200.0 }, { QPointF(850, 100), 311.0 }, { QPointF(100, 250), 7.0 }, { QPointF(250, 250), 352.0 }
    };

    QImage image(1000, 400, QImage::Format_RGB32);
    image.fill(Qt::white);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const auto& fixture : fixtures)
        {
            drawFixtureSymbol(painter, fixture.first, fixture.second);
        }
        for (int i = 0; i < 4; ++i)
        {
            drawOtherSymbol(painter, QPointF(500 + 100 * i, 250));
        }
    }

    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(image, QRect(70, 75, 70, 50), FixtureSymbolFinder::Settings());
    QCOMPARE(matches.size(), fixtures.size());

    for (const auto& fixture : fixtures)
    {
        auto it = std::find_if(matches.cbegin(), matches.cend(), [&](const FixtureSymbolFinder::Match& match) { return QLineF(match.center, fixture.first).length() < 8.0; });
        QVERIFY2(it != matches.cend(), qPrintable(QString("Fixture rotated by %1 degrees was not found.").arg(fixture.second)));

        qreal angleDifference = std::fmod(std::abs(it->angle - fixture.second), 360.0);
        angleDifference = std::min(angleDifference, 360.0 - angleDifference);
        QVERIFY2(angleDifference < 6.0, qPrintable(QString("Fixture rotated by %1 degrees was found with the angle %2.").arg(fixture.second).arg(it->angle)));
    }
}

void FixtureCountTest::findSymmetricSymbols()
{
    // Poles of site lighting plans are drawn as small filled dots
    std::vector<QPointF> dots;
    for (int i = 0; i < 12; ++i)
    {
        dots.push_back(QPointF(60 + 70 * i + (i % 3) * 0.4, 60 + (i % 4) * 45 + (i % 2) * 0.3));
    }

    QImage image(900, 400, QImage::Format_RGB32);
    image.fill(Qt::white);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (const QPointF& dot : dots)
        {
            painter.drawEllipse(dot, 5.0, 5.0);
        }

        // Shapes, which look like the dot only partially: filled rectangles
        // (labels), thick lines and their ends, a large dot, a ring
        painter.drawRect(QRectF(100, 300, 60, 10));
        painter.drawRect(QRectF(300, 280, 10, 60));
        painter.drawEllipse(QPointF(500, 320), 14.0, 14.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::black, 9.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(600, 300), QPointF(800, 350));
        painter.setPen(QPen(Qt::black, 1.5));
        painter.drawEllipse(QPointF(420, 320), 5.0, 5.0);
    }

    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(image, QRect(53, 53, 15, 15), FixtureSymbolFinder::Settings());
    QCOMPARE(countMatchesNear(matches, dots, 3.0), int(dots.size()));
    QCOMPARE(matches.size(), dots.size());
}

void FixtureCountTest::findSymbolsByInkColor()
{
    // Black fixtures crossed by thick colored lines (isolines of light levels)
    // on a colored background
    QImage image = createSheetImage();

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.fillRect(QRect(0, 0, 500, 600), QColor(200, 230, 200));
        painter.setPen(QPen(Qt::blue, 5.0));
        painter.drawLine(QPointF(40, 130), QPointF(960, 70));
        painter.setPen(QPen(Qt::red, 5.0));
        painter.drawLine(QPointF(60, 360), QPointF(460, 440));
        painter.drawEllipse(QPointF(250, 100), 30.0, 22.0);
    }

    const QRect sampleRect(70, 75, 70, 50);
    const QImage inkImage = FixtureSymbolFinder::createInkImage(image, sampleRect);
    QCOMPARE(inkImage.format(), QImage::Format_Grayscale8);
    QCOMPARE(inkImage.size(), image.size());

    // Black ink is kept, colored ink and the background are white
    QVERIFY(qGray(inkImage.pixel(80, 90)) < 64);        // edge of the fixture
    QVERIFY(qGray(inkImage.pixel(700, 87)) > 224);      // blue line
    QVERIFY(qGray(inkImage.pixel(250, 122)) > 192);     // red ellipse
    QVERIFY(qGray(inkImage.pixel(300, 200)) > 192);     // green background
    QVERIFY(qGray(inkImage.pixel(700, 200)) > 224);     // white paper

    // Colored lines leave gaps in the fixtures of the ink image (a raster image
    // can't be drawn without them as a page), so a lower threshold is used
    FixtureSymbolFinder::Settings settings;
    settings.threshold = 0.8;
    const std::vector<QPointF> expected = getFixturePositions(true);
    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(inkImage, sampleRect, settings);
    QCOMPARE(countMatchesNear(matches, expected, 8.0), int(expected.size()));
    QCOMPARE(matches.size(), expected.size());

    // An image without any ink in the sample is only converted
    const QImage plainImage = FixtureSymbolFinder::createInkImage(image, QRect(600, 150, 20, 20));
    QCOMPARE(plainImage, image.convertToFormat(QImage::Format_Grayscale8));
}

void FixtureCountTest::precompiledPageWithoutImagesAndText()
{
    // Photo under the drawing and text are removed from the page before the search
    QImage photo(10, 10, QImage::Format_RGB32);
    photo.fill(Qt::darkGreen);

    QPainterPath line;
    line.moveTo(10, 50);
    line.lineTo(90, 50);

    QPainterPath text;
    text.addRect(QRectF(20, 10, 20, 20));

    pdf::PDFPrecompiledPage page;
    page.addSaveGraphicState();
    page.addSetWorldMatrix(QTransform::fromScale(100, 100));
    page.addImage(photo);
    page.addRestoreGraphicState();
    page.addPath(QPen(Qt::black, 4.0), QBrush(Qt::NoBrush), line, false);
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), text, true);

    auto draw = [](const pdf::PDFPrecompiledPage& pageToDraw)
    {
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        pageToDraw.draw(&painter, QRectF(0, 0, 100, 100), QTransform(), pdf::PDFRenderer::None, 1.0);
        painter.end();
        return image;
    };

    const QImage fullImage = draw(page);
    QVERIFY(qGray(fullImage.pixel(50, 50)) < 64);       // line
    QVERIFY(qGray(fullImage.pixel(30, 20)) < 64);       // text
    QVERIFY(fullImage.pixel(80, 80) != qRgb(255, 255, 255));    // photo

    pdf::PDFPrecompiledPage vectorPage = page;
    vectorPage.removeImages();
    const QImage vectorImage = draw(vectorPage);
    QVERIFY(qGray(vectorImage.pixel(50, 50)) < 64);
    QVERIFY(qGray(vectorImage.pixel(30, 20)) < 64);
    QCOMPARE(vectorImage.pixel(80, 80), qRgb(255, 255, 255));

    vectorPage.removeText();
    const QImage reducedImage = draw(vectorPage);
    QVERIFY(qGray(reducedImage.pixel(50, 50)) < 64);
    QCOMPARE(reducedImage.pixel(30, 20), qRgb(255, 255, 255));
    QCOMPARE(reducedImage.pixel(80, 80), qRgb(255, 255, 255));

    // The original page is not changed
    QCOMPARE(draw(page), fullImage);
}

void FixtureCountTest::addPoleSymbol(pdf::PDFPrecompiledPage& page, QPointF center, qreal angle, qreal scale, bool mirrored)
{
    // Pole of a site plan (in points): a filled head, an L shaped arm and a dot,
    // so the rotations and the mirror image differ
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(angle);
    transform.scale(mirrored ? -scale : scale, scale);

    QPainterPath head;
    head.addRect(QRectF(-3.0, -2.0, 6.0, 4.0));
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), transform.map(head), false);

    QPainterPath arm;
    arm.moveTo(3.0, 0.0);
    arm.lineTo(5.5, 0.0);
    arm.lineTo(5.5, 2.0);
    page.addPath(QPen(Qt::black, 0.5), QBrush(Qt::NoBrush), transform.map(arm), false);

    QPainterPath dot;
    dot.addEllipse(QPointF(-4.5, 1.2), 0.8, 0.8);
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), transform.map(dot), false);

}

void FixtureCountTest::addLine(pdf::PDFPrecompiledPage& page, const std::vector<QPointF>& points, QColor color, qreal width, bool asSegments)
{
    // A polyline as one path, or as separate paths of the segments in a mixed
    // order and direction (as some CAD programs write isolines)
    if (!asSegments)
    {
        QPainterPath path;
        path.moveTo(points.front());
        for (size_t i = 1; i < points.size(); ++i)
        {
            path.lineTo(points[i]);
        }
        page.addPath(QPen(color, width), QBrush(Qt::NoBrush), path, false);
        return;
    }

    for (size_t i = 1; i < points.size(); i += 2)
    {
        QPainterPath path;
        path.moveTo(points[i]);
        path.lineTo(points[i - 1]);
        page.addPath(QPen(color, width), QBrush(Qt::NoBrush), path, false);
    }
    for (size_t i = 2; i < points.size(); i += 2)
    {
        QPainterPath path;
        path.moveTo(points[i - 1]);
        path.lineTo(points[i]);
        page.addPath(QPen(color, width), QBrush(Qt::NoBrush), path, false);
    }
}

std::vector<FixtureSymbolFinder::Match> FixtureCountTest::findOnPage(const pdf::PDFPrecompiledPage& compiledPage, QRectF sampleRect, const FixtureSymbolFinder::Settings& settings)
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 600, 400));
    const PDFDocument document = builder.build();
    const PDFPage* page = document.getCatalog()->getPage(0);
    return FixtureSymbolFinder::findOnPage(compiledPage, page, sampleRect, pdf::PDFRenderer::getDefaultFeatures(), settings);
}

void FixtureCountTest::removeLargePaths()
{
    auto draw = [](const pdf::PDFPrecompiledPage& page)
    {
        QImage image(600, 400, QImage::Format_Grayscale8);
        image.fill(Qt::white);
        QPainter painter(&image);
        page.draw(&painter, QRectF(0, 0, 600, 400), QTransform(), pdf::PDFRenderer::None, 1.0);
        painter.end();
        return image;
    };

    auto isInk = [](const QImage& image, int x, int y) { return qGray(image.pixel(x, y)) < 128; };

    pdf::PDFPrecompiledPage page;

    // Long line as a single path, as a chain of segments in a mixed order,
    // a small symbol drawn as a chain of segments
    addLine(page, { QPointF(10, 20), QPointF(590, 20) }, Qt::black, 3.0, false);
    std::vector<QPointF> chain;
    for (int i = 0; i <= 40; ++i)
    {
        chain.emplace_back(10 + 14.5 * i, 60 + 3.0 * std::sin(i * 0.5));
    }
    addLine(page, chain, Qt::black, 3.0, true);
    addLine(page, { QPointF(100, 100), QPointF(110, 100), QPointF(110, 110), QPointF(100, 110), QPointF(100, 100) }, Qt::black, 3.0, true);

    // Large path with a small subpath, large filled area with a small hole
    QPainterPath mixed;
    mixed.moveTo(10, 150);
    mixed.lineTo(590, 150);
    mixed.addRect(QRectF(200, 180, 10, 10));
    page.addPath(QPen(Qt::black, 3.0), QBrush(Qt::NoBrush), mixed, false);

    QPainterPath area;
    area.addRect(QRectF(300, 250, 200, 100));
    area.addRect(QRectF(390, 290, 10, 10));
    area.setFillRule(Qt::OddEvenFill);
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), area, false);

    // Text is not changed
    QPainterPath text;
    text.addRect(QRectF(20, 300, 200, 10));
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), text, true);

    const QImage before = draw(page);
    QVERIFY(isInk(before, 300, 20));
    QVERIFY(isInk(before, 300, 150));
    QVERIFY(isInk(before, 450, 300));

    pdf::PDFPrecompiledPage reduced = page;
    reduced.removeLargePaths(30.0);
    const QImage after = draw(reduced);

    QVERIFY(!isInk(after, 300, 20));      // long line
    for (int x = 20; x < 580; x += 10)
    {
        QVERIFY2(!isInk(after, x, 57) && !isInk(after, x, 60) && !isInk(after, x, 63), "chain of segments");
    }
    QVERIFY(isInk(after, 100, 105));      // small chain (symbol)
    QVERIFY(isInk(after, 105, 100));
    QVERIFY(!isInk(after, 300, 150));     // large subpath
    QVERIFY(isInk(after, 200, 185));      // small subpath of a large path
    QVERIFY(!isInk(after, 450, 300));     // large area
    QVERIFY(!isInk(after, 395, 295));     // hole of the large area does not become a filled square
    QVERIFY(isInk(after, 100, 305));      // text

    // The original page is not changed
    QCOMPARE(draw(page), before);
}

void FixtureCountTest::removePathsOfOtherColors()
{
    pdf::PDFPrecompiledPage page;
    addLine(page, { QPointF(10, 20), QPointF(590, 20) }, Qt::black, 3.0, false);
    addLine(page, { QPointF(10, 40), QPointF(590, 40) }, QColor(30, 30, 30), 3.0, false);
    addLine(page, { QPointF(10, 60), QPointF(590, 60) }, QColor(127, 127, 127), 3.0, false);
    addLine(page, { QPointF(10, 80), QPointF(590, 80) }, Qt::blue, 3.0, false);

    QPainterPath filled;
    filled.addRect(QRectF(10, 100, 100, 20));
    page.addPath(QPen(Qt::red, 1.0), QBrush(Qt::black), filled, false);

    QPainterPath patterned;
    patterned.addRect(QRectF(10, 150, 100, 20));
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::red, Qt::Dense3Pattern), patterned, false);

    QPainterPath text;
    text.addRect(QRectF(10, 200, 100, 20));
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::gray), text, true);

    page.removePathsOfOtherColors(Qt::black, 48);

    QImage image(600, 400, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    page.draw(&painter, QRectF(0, 0, 600, 400), QTransform(), pdf::PDFRenderer::None, 1.0);
    painter.end();

    QVERIFY(qGray(image.pixel(300, 20)) < 64);          // black
    QVERIFY(qGray(image.pixel(300, 40)) < 64);          // almost black
    QCOMPARE(image.pixel(300, 60), qRgb(255, 255, 255)); // gray
    QCOMPARE(image.pixel(300, 80), qRgb(255, 255, 255)); // blue
    QVERIFY(qGray(image.pixel(50, 110)) < 64);          // filled by black, red border
    QVERIFY(image.pixel(50, 160) != qRgb(255, 255, 255) || image.pixel(51, 160) != qRgb(255, 255, 255)); // pattern
    QCOMPARE(image.pixel(50, 210), qRgb(255, 255, 255)); // gray text
}

void FixtureCountTest::findOnPageRotatedAndMirrored()
{
    // Poles pointing in all directions, also mirrored and a little larger
    const std::vector<QPointF> fixtures = { QPointF(60, 60), QPointF(160, 60), QPointF(260, 60), QPointF(360, 60), QPointF(460, 60), QPointF(60, 160), QPointF(160, 160) };
    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    addPoleSymbol(page, fixtures[0], 0.0);
    addPoleSymbol(page, fixtures[1], 37.0);
    addPoleSymbol(page, fixtures[2], 90.0);
    addPoleSymbol(page, fixtures[3], 213.0);
    addPoleSymbol(page, fixtures[4], 0.0, 1.0, true);
    addPoleSymbol(page, fixtures[5], 128.0, 1.0, true);
    addPoleSymbol(page, fixtures[6], 300.0, 1.06);

    const std::vector<FixtureSymbolFinder::Match> matches = findOnPage(page, QRectF(52, 54, 16, 12));
    QCOMPARE(countMatchesNear(matches, fixtures, 1.5), int(fixtures.size()));
    QCOMPARE(matches.size(), fixtures.size());

    auto findMatch = [&](QPointF point)
    {
        return *std::find_if(matches.cbegin(), matches.cend(), [&](const FixtureSymbolFinder::Match& match) { return QLineF(match.center, point).length() < 1.5; });
    };

    // Angles of the page (y axis up) are clockwise in the image, the mirror image is reported
    const FixtureSymbolFinder::Match rotated = findMatch(fixtures[2]);
    QVERIFY(!rotated.mirrored);
    QVERIFY(qAbs(std::remainder(rotated.angle - 270.0, 360.0)) < 3.0);
    QVERIFY(findMatch(fixtures[4]).mirrored);
    QVERIFY(findMatch(fixtures[5]).mirrored);
    QVERIFY(!findMatch(fixtures[1]).mirrored);
    QVERIFY(qAbs(findMatch(fixtures[6]).scale - 1.06) < 0.04);
    QVERIFY(qAbs(findMatch(fixtures[0]).scale - 1.0) < 0.03);
    for (const FixtureSymbolFinder::Match& match : matches)
    {
        QVERIFY(match.score >= FixtureSymbolFinder::Settings().threshold);
    }
}

void FixtureCountTest::findOnPageCrossedByLines()
{
    // Poles crossed by lines of the drawing (a curb as a single path, an isoline
    // as a chain of segments, a colored isoline), by gray calculation points
    // and partially covered by a short line running into the symbol
    const std::vector<QPointF> fixtures = { QPointF(60, 60), QPointF(160, 100), QPointF(260, 140), QPointF(360, 180), QPointF(460, 220), QPointF(160, 300) };
    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    addPoleSymbol(page, fixtures[0], 0.0);
    addPoleSymbol(page, fixtures[1], 20.0);
    addPoleSymbol(page, fixtures[2], 75.0);
    addPoleSymbol(page, fixtures[3], 160.0);
    addPoleSymbol(page, fixtures[4], 250.0);
    addPoleSymbol(page, fixtures[5], 10.0);

    addLine(page, { QPointF(10, 95), QPointF(590, 105) }, Qt::black, 0.7, false);

    std::vector<QPointF> isoline;
    for (int i = 0; i <= 60; ++i)
    {
        const qreal angle = i * 2.0 * M_PI / 60.0;
        isoline.emplace_back(260 + 60 * std::cos(angle) - 60, 140 + 40 * std::sin(angle));
    }
    addLine(page, isoline, Qt::black, 0.5, true);

    addLine(page, { QPointF(300, 150), QPointF(420, 210) }, Qt::blue, 1.5, false);

    for (int i = -1; i <= 1; ++i)
    {
        QPainterPath plus;
        plus.moveTo(458 + 2 * i, 222);
        plus.lineTo(462 + 2 * i, 222);
        page.addPath(QPen(QColor(128, 128, 128), 0.4), QBrush(Qt::NoBrush), plus, false);
    }

    // Short line (not removed as a large path) running into the symbol
    addLine(page, { QPointF(150, 307), QPointF(162, 299) }, Qt::black, 0.6, false);

    const std::vector<FixtureSymbolFinder::Match> matches = findOnPage(page, QRectF(52, 54, 16, 12));
    QCOMPARE(countMatchesNear(matches, fixtures, 1.5), int(fixtures.size()));
    QCOMPARE(matches.size(), fixtures.size());
}

void FixtureCountTest::findOnPageRejectsOtherSymbols()
{
    // Symbols of the same shape, but of another size (other fixture types),
    // a round head and a square are not found
    const std::vector<QPointF> fixtures = { QPointF(60, 60), QPointF(160, 60), QPointF(260, 60) };
    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    addPoleSymbol(page, fixtures[0], 0.0);
    addPoleSymbol(page, fixtures[1], 45.0);
    addPoleSymbol(page, fixtures[2], 180.0);
    addPoleSymbol(page, QPointF(60, 160), 0.0, 1.35);
    addPoleSymbol(page, QPointF(160, 160), 90.0, 0.72);

    QPainterPath roundHead;
    roundHead.addEllipse(QPointF(360, 160), 3.0, 3.0);
    roundHead.addEllipse(QPointF(355.5, 161.2), 0.8, 0.8);
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), roundHead, false);
    QPainterPath roundHeadArm;
    roundHeadArm.moveTo(363.0, 160.0);
    roundHeadArm.lineTo(365.5, 160.0);
    roundHeadArm.lineTo(365.5, 162.0);
    page.addPath(QPen(Qt::black, 0.5), QBrush(Qt::NoBrush), roundHeadArm, false);

    QPainterPath square;
    square.addRect(QRectF(450, 150, 8, 8));
    page.addPath(QPen(Qt::NoPen), QBrush(Qt::black), square, false);

    const std::vector<FixtureSymbolFinder::Match> matches = findOnPage(page, QRectF(52, 54, 16, 12));
    QCOMPARE(countMatchesNear(matches, fixtures, 1.5), int(fixtures.size()));
    QCOMPARE(matches.size(), fixtures.size());
}

void FixtureCountTest::findOnPageWithoutMirrors()
{
    const std::vector<QPointF> fixtures = { QPointF(60, 60), QPointF(160, 60) };
    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    addPoleSymbol(page, fixtures[0], 0.0);
    addPoleSymbol(page, fixtures[1], 60.0);
    addPoleSymbol(page, QPointF(260, 60), 0.0, 1.0, true);
    addPoleSymbol(page, QPointF(360, 60), 200.0, 1.0, true);

    FixtureSymbolFinder::Settings settings;
    settings.mirrors = false;
    const std::vector<FixtureSymbolFinder::Match> matches = findOnPage(page, QRectF(52, 54, 16, 12), settings);
    QCOMPARE(countMatchesNear(matches, fixtures, 1.5), int(fixtures.size()));
    QCOMPARE(matches.size(), fixtures.size());

    // A symmetric symbol is found once, the mirror image is the symbol rotated
    pdf::PDFPrecompiledPage symmetricPage;
    symmetricPage.addSetWorldMatrix(QTransform());
    for (const QPointF& point : { QPointF(60, 60), QPointF(160, 60), QPointF(260, 60) })
    {
        QPainterPath head;
        head.addRect(QRectF(point.x() - 3.0, point.y() - 2.0, 6.0, 4.0));
        symmetricPage.addPath(QPen(Qt::NoPen), QBrush(Qt::black), head, false);
        QPainterPath arm;
        arm.moveTo(point + QPointF(3.0, 0.0));
        arm.lineTo(point + QPointF(6.0, 0.0));
        symmetricPage.addPath(QPen(Qt::black, 0.5), QBrush(Qt::NoBrush), arm, false);
    }

    const std::vector<FixtureSymbolFinder::Match> symmetricMatches = findOnPage(symmetricPage, QRectF(54, 55, 14, 10));
    QCOMPARE(symmetricMatches.size(), size_t(3));
}

void FixtureCountTest::findOnPageSampleCrossedByLine()
{
    // The sample itself lies on a line, the line is not a part of the sample
    const std::vector<QPointF> fixtures = { QPointF(60, 60), QPointF(160, 120), QPointF(260, 180), QPointF(360, 240) };
    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    for (size_t i = 0; i < fixtures.size(); ++i)
    {
        addPoleSymbol(page, fixtures[i], 33.0 * i);
    }
    addLine(page, { QPointF(10, 61), QPointF(590, 61) }, Qt::black, 0.7, false);

    const std::vector<FixtureSymbolFinder::Match> matches = findOnPage(page, QRectF(52, 54, 16, 12));
    QCOMPARE(countMatchesNear(matches, fixtures, 1.5), int(fixtures.size()));
    QCOMPARE(matches.size(), fixtures.size());
}

void FixtureCountTest::findLargeSample()
{
    // A large sample in an image is confirmed in a reduced image
    QImage image(1600, 900, QImage::Format_RGB32);
    image.fill(Qt::white);
    const std::vector<std::pair<QPointF, qreal>> fixtures = { { QPointF(250, 250), 0.0 }, { QPointF(800, 300), 40.0 }, { QPointF(1300, 600), 200.0 } };

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const auto& fixture : fixtures)
        {
            painter.save();
            painter.translate(fixture.first);
            painter.rotate(fixture.second);
            painter.scale(4.0, 4.0);
            drawFixtureSymbol(painter, QPointF(0, 0), 0.0);
            painter.restore();
        }
    }

    // Matches are reported at the center of the ink of the sample, which is
    // not the origin of the symbol
    const QRect sampleRect(150, 150, 240, 140);
    const QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
    const QPointF offset = QRectF(FixtureSymbolFinder::getInkBoundingRect(grayImage, sampleRect)).center() - fixtures.front().first;
    std::vector<QPointF> points;
    for (const auto& fixture : fixtures)
    {
        QTransform rotation;
        rotation.rotate(fixture.second);
        points.push_back(fixture.first + rotation.map(offset));
    }

    const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::find(image, sampleRect, FixtureSymbolFinder::Settings());
    QCOMPARE(countMatchesNear(matches, points, 12.0), int(points.size()));
    QCOMPARE(matches.size(), points.size());
}

void FixtureCountTest::realDocumentBenchmark()
{
    // Precision and recall of the search on real sheets. The cases are described
    // by a JSON file: { "cases": [ { "name", "file", "page" (from 0), "sample":
    // [x1, y1, x2, y2], "truth": [[x, y], ...], "ignore": [[x1, y1, x2, y2], ...],
    // "tolerance" } ] } in the page coordinates. Matches inside the ignored
    // rectangles (legends, schedules) are neither right nor wrong.
    const QString benchmarkFileName = qEnvironmentVariable("PDF4QT_FIXTURE_FINDER_BENCHMARK");
    if (benchmarkFileName.isEmpty())
    {
        QSKIP("Set PDF4QT_FIXTURE_FINDER_BENCHMARK to a JSON file with the cases");
    }

    QFile benchmarkFile(benchmarkFileName);
    QVERIFY(benchmarkFile.open(QIODevice::ReadOnly));
    const QJsonObject benchmark = QJsonDocument::fromJson(benchmarkFile.readAll()).object();
    const QString directory = QFileInfo(benchmarkFileName).absolutePath();

    auto toRect = [](const QJsonValue& value)
    {
        const QJsonArray array = value.toArray();
        return QRectF(QPointF(array.at(0).toDouble(), array.at(1).toDouble()), QPointF(array.at(2).toDouble(), array.at(3).toDouble())).normalized();
    };

    QJsonArray dump;
    int totalTruePositives = 0;
    int totalFalsePositives = 0;
    int totalFalseNegatives = 0;
    qint64 totalTime = 0;

    for (const QJsonValue& caseValue : benchmark.value("cases").toArray())
    {
        const QJsonObject caseObject = caseValue.toObject();
        const QString fileName = QDir(directory).absoluteFilePath(caseObject.value("file").toString());
        const PDFInteger pageIndex = caseObject.value("page").toInt();

        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(fileName);
        QVERIFY2(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK, qPrintable(fileName));

        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        pdf::PDFICCProfileCache iccProfileCache;
        pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
        pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(&document), &optionalContentActivity);
        fontCache.setDocument(modifiedDocument);
        iccProfileCache.setDocument(modifiedDocument);
        pdf::PDFCMSGeneric cms;

        pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures();
        features.setFlag(pdf::PDFRenderer::DisplayAnnotations, false);
        pdf::PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, features, pdf::PDFMeshQualitySettings());
        pdf::PDFPrecompiledPage compiledPage;
        renderer.compile(&compiledPage, size_t(pageIndex));
        const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
        QVERIFY(page);

        const QRectF sampleRect = toRect(caseObject.value("sample"));
        const qreal tolerance = caseObject.value("tolerance").toDouble(qMax(sampleRect.width(), sampleRect.height()) * 0.5);

        std::vector<QPointF> truth;
        for (const QJsonValue& value : caseObject.value("truth").toArray())
        {
            truth.emplace_back(value.toArray().at(0).toDouble(), value.toArray().at(1).toDouble());
        }

        std::vector<QRectF> ignoredRects;
        for (const QJsonValue& value : caseObject.value("ignore").toArray())
        {
            ignoredRects.push_back(toRect(value));
        }

        QElapsedTimer timer;
        timer.start();
        const std::vector<FixtureSymbolFinder::Match> matches = FixtureSymbolFinder::findOnPage(compiledPage, page, sampleRect, features, FixtureSymbolFinder::Settings());
        const qint64 time = timer.elapsed();

        // Each true symbol is matched with the nearest found symbol at most once
        std::vector<bool> isTruthFound(truth.size(), false);
        int truePositives = 0;
        int falsePositives = 0;
        QJsonArray dumpMatches;
        for (const FixtureSymbolFinder::Match& match : matches)
        {
            int nearest = -1;
            qreal nearestDistance = tolerance;
            for (size_t i = 0; i < truth.size(); ++i)
            {
                const qreal distance = QLineF(match.center, truth[i]).length();
                if (!isTruthFound[i] && distance <= nearestDistance)
                {
                    nearest = int(i);
                    nearestDistance = distance;
                }
            }

            QString verdict;
            if (nearest >= 0)
            {
                isTruthFound[nearest] = true;
                ++truePositives;
                verdict = "tp";
            }
            else if (std::any_of(ignoredRects.cbegin(), ignoredRects.cend(), [&](const QRectF& rect) { return rect.contains(match.center); }))
            {
                verdict = "ignored";
            }
            else
            {
                ++falsePositives;
                verdict = "fp";
            }

            dumpMatches.append(QJsonObject{ { "x", match.center.x() }, { "y", match.center.y() }, { "score", match.score }, { "angle", match.angle }, { "verdict", verdict } });
        }

        QJsonArray dumpMissed;
        for (size_t i = 0; i < truth.size(); ++i)
        {
            if (!isTruthFound[i])
            {
                dumpMissed.append(QJsonArray{ truth[i].x(), truth[i].y() });
            }
        }

        const int falseNegatives = int(dumpMissed.size());
        totalTruePositives += truePositives;
        totalFalsePositives += falsePositives;
        totalFalseNegatives += falseNegatives;
        totalTime += time;

        qInfo().noquote() << QString("%1: %2 symbols, found %3, wrong %4, missed %5, %6 ms").arg(caseObject.value("name").toString()).arg(truth.size()).arg(truePositives).arg(falsePositives).arg(falseNegatives).arg(time);
        dump.append(QJsonObject{ { "name", caseObject.value("name") }, { "matches", dumpMatches }, { "missed", dumpMissed }, { "time", time } });
    }

    const int found = totalTruePositives + totalFalsePositives;
    const int expected = totalTruePositives + totalFalseNegatives;
    qInfo().noquote() << QString("Total: precision %1 % (%2 of %3 found are right), recall %4 % (%2 of %5 symbols found), %6 ms")
                         .arg(found > 0 ? 100.0 * totalTruePositives / found : 100.0, 0, 'f', 1).arg(totalTruePositives).arg(found)
                         .arg(expected > 0 ? 100.0 * totalTruePositives / expected : 100.0, 0, 'f', 1).arg(expected).arg(totalTime);

    const QString dumpFileName = qEnvironmentVariable("PDF4QT_FIXTURE_FINDER_BENCHMARK_DUMP");
    if (!dumpFileName.isEmpty())
    {
        QFile dumpFile(dumpFileName);
        QVERIFY(dumpFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        dumpFile.write(QJsonDocument(dump).toJson());
    }
}

QTEST_MAIN(FixtureCountTest)

#include "tst_fixturecounttest.moc"
