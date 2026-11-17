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
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfoutline.h"

#include <QtTest>
#include <QBuffer>
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
    void findSimilarSymbols();
    void findSimilarSymbolsWithoutRotations();
    void findSimilarSymbolsEmptySample();

private:
    static FixtureType type(const QString& name, FixtureMarkerShape shape = FixtureMarkerShape::Circle, QColor color = Qt::red, const QString& description = QString());
    static QByteArray write(const PDFDocument& document);
    static void drawFixtureSymbol(QPainter& painter, QPointF center, qreal angle);
    static void drawOtherSymbol(QPainter& painter, QPointF center);
    static QImage createSheetImage();
    static int countMatchesNear(const std::vector<FixtureSymbolFinder::Match>& matches, const std::vector<QPointF>& points, qreal tolerance);
    static std::vector<QPointF> getFixturePositions(bool rotated);
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

QTEST_MAIN(FixtureCountTest)

#include "tst_fixturecounttest.moc"
