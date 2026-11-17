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

#include "pdfpageorganizer.h"
#include "pdfannotation.h"
#include "pdfaction.h"
#include "pdfoutline.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QBuffer>

using namespace pdf;

/// Tests of PDFPageOrganizer (organizing of the pages in the main window)
/// and of the revision clouds (cloudy border effect of the annotations)
class PageOrganizerTest : public QObject
{
    Q_OBJECT

private slots:
    void rotatePages();
    void deletePages();
    void movePages();
    void movePagesIdentity();
    void insertDocument();
    void insertDocumentKeepsOutlines();
    void insertBlankPages();
    void extractPages();
    void splitPages();
    void annotationsFollowMovedPages();
    void organizedDocumentRoundTrip();
    void cloudyPathSurroundsPolygon();
    void cloudyPolygonAnnotation();
    void cloudySquareAnnotation();

private:
    /// Creates a document, whose page i has width 100 + 10 * i, so the pages can be identified
    static PDFDocument createDocument(int pageCount, int firstWidth = 100);

    /// Returns the identifiers (widths) of the pages of the document
    static std::vector<int> getPageWidths(const PDFDocument& document);

    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
};

PDFDocument PageOrganizerTest::createDocument(int pageCount, int firstWidth)
{
    PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, firstWidth + 10 * i, 500));
    }
    return builder.build();
}

std::vector<int> PageOrganizerTest::getPageWidths(const PDFDocument& document)
{
    std::vector<int> widths;
    for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
    {
        widths.push_back(qRound(document.getCatalog()->getPage(i)->getMediaBox().width()));
    }
    return widths;
}

QByteArray PageOrganizerTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument PageOrganizerTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
    }
    return document;
}

void PageOrganizerTest::rotatePages()
{
    PDFDocument document = createDocument(3);

    QVERIFY(PDFPageOrganizer::rotatePages(&document, { 0, 2, 2, 7, -1 }, true));
    QCOMPARE(document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(document.getCatalog()->getPage(1)->getPageRotation(), PageRotation::None);
    QCOMPARE(document.getCatalog()->getPage(2)->getPageRotation(), PageRotation::Rotate90);

    QVERIFY(PDFPageOrganizer::rotatePages(&document, { 0, 1 }, false));
    QCOMPARE(document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::None);
    QCOMPARE(document.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate270);

    // Four rotations give the original rotation
    for (int i = 0; i < 4; ++i)
    {
        QVERIFY(PDFPageOrganizer::rotatePages(&document, { 2 }, true));
    }
    QCOMPARE(document.getCatalog()->getPage(2)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 110, 120 }));

    // Nothing selected - document is not changed
    const PDFDocument original = document;
    QVERIFY(PDFPageOrganizer::rotatePages(&document, { }, true));
    QVERIFY(document == original);
}

void PageOrganizerTest::deletePages()
{
    PDFDocument document = createDocument(5);
    QVERIFY(PDFPageOrganizer::deletePages(&document, { 3, 1, 1, 10 }));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 120, 140 }));

    // All pages can't be deleted, document stays unchanged
    const PDFDocument beforeDelete = document;
    PDFOperationResult result = PDFPageOrganizer::deletePages(&document, { 0, 1, 2 });
    QVERIFY(!result);
    QVERIFY(!result.getErrorMessage().isEmpty());
    QVERIFY(document == beforeDelete);

    QVERIFY(!PDFPageOrganizer::deletePages(nullptr, { 0 }));
}

void PageOrganizerTest::movePages()
{
    struct TestCase
    {
        std::vector<PDFInteger> pages;
        PDFInteger insertPosition;
        std::vector<int> expected;
    };

    const std::vector<TestCase> testCases = {
        { { 0 }, 5, { 110, 120, 130, 140, 100 } },          // first page to the end
        { { 4 }, 0, { 140, 100, 110, 120, 130 } },          // last page to the beginning
        { { 3, 4 }, 1, { 100, 130, 140, 110, 120 } },       // block moved up
        { { 0, 1 }, 4, { 120, 130, 100, 110, 140 } },       // block moved down
        { { 0, 2, 4 }, 2, { 110, 100, 120, 140, 130 } },    // scattered pages, dropped before the third page
        { { 4, 0 }, 99, { 110, 120, 130, 100, 140 } },      // order of the selection doesn't matter, position is clamped
        { { 1 }, -5, { 110, 100, 120, 130, 140 } },         // negative position means the beginning
    };

    for (const TestCase& testCase : testCases)
    {
        PDFDocument document = createDocument(5);
        QVERIFY(PDFPageOrganizer::movePages(&document, testCase.pages, testCase.insertPosition));
        QCOMPARE(getPageWidths(document), testCase.expected);
    }
}

void PageOrganizerTest::movePagesIdentity()
{
    // Pages dropped at their own position don't change the document
    const PDFDocument original = createDocument(4);

    for (const auto& [pages, position] : std::vector<std::pair<std::vector<PDFInteger>, PDFInteger>>{ { { 1 }, 1 }, { { 1 }, 2 }, { { 1, 2 }, 3 }, { { 3 }, 4 }, { { }, 0 } })
    {
        PDFDocument document = original;
        QVERIFY(PDFPageOrganizer::movePages(&document, pages, position));
        QVERIFY(document == original);
    }
}

void PageOrganizerTest::insertDocument()
{
    PDFDocument document = createDocument(3);
    const PDFDocument inserted = createDocument(2, 500);

    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 1));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 500, 510, 110, 120 }));

    // Selected pages of the inserted document in the given order, at the end
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 100, { 1, 0, 1 }));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 500, 510, 110, 120, 510, 500 }));

    // Inserted document is a copy - the same document can be inserted into itself
    const PDFDocument copy = document;
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &copy, 0, { 6 }));
    QCOMPARE(getPageWidths(document).front(), 500);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(8));

    // Invalid pages only - error, document is unchanged
    const PDFDocument beforeInsert = document;
    QVERIFY(!PDFPageOrganizer::insertDocument(&document, &inserted, 0, { 5 }));
    QVERIFY(document == beforeInsert);
}

void PageOrganizerTest::insertDocumentKeepsOutlines()
{
    auto createDocumentWithOutline = [](int firstWidth, const QString& title)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, firstWidth, 500));
        builder.appendPage(QRectF(0, 0, firstWidth + 10, 500));

        PDFOutlineItem root;
        QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
        item->setTitle(title);
        item->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(page), PDFDestination())));
        root.addChild(item);
        builder.setOutline(&root);
        return builder.build();
    };

    auto getTopLevelTitles = [](const PDFDocument& document)
    {
        QStringList titles;
        if (QSharedPointer<PDFOutlineItem> root = document.getCatalog()->getOutlineRootPtr())
        {
            for (size_t i = 0; i < root->getChildCount(); ++i)
            {
                titles << root->getChild(i)->getTitle();
            }
        }
        return titles;
    };

    // Bookmarks of both documents are kept, no extra item is created for each document
    PDFDocument document = createDocumentWithOutline(100, "Sheet A");
    const PDFDocument inserted = createDocumentWithOutline(500, "Sheet B");
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 2));
    QCOMPARE(getTopLevelTitles(document), QStringList({ "Sheet A", "Sheet B" }));

    // Documents without bookmarks get no bookmarks
    PDFDocument plainDocument = createDocument(2);
    const PDFDocument plainInserted = createDocument(1, 500);
    QVERIFY(PDFPageOrganizer::insertDocument(&plainDocument, &plainInserted, 1));
    QVERIFY(getTopLevelTitles(plainDocument).isEmpty());

    // Bookmarks survive reordering of the pages
    QVERIFY(PDFPageOrganizer::movePages(&document, { 0 }, 4));
    QCOMPARE(getTopLevelTitles(document), QStringList({ "Sheet A", "Sheet B" }));
}

void PageOrganizerTest::insertBlankPages()
{
    PDFDocument document = createDocument(2);
    QVERIFY(PDFPageOrganizer::rotatePages(&document, { 1 }, true));

    QVERIFY(PDFPageOrganizer::insertBlankPages(&document, 1, 2));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 110, 110, 110 }));

    // Blank pages take the size and the rotation of the page, before which they are inserted
    QCOMPARE(document.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(qRound(document.getCatalog()->getPage(1)->getMediaBox().height()), 500);

    // At the end, the last page is used
    QVERIFY(PDFPageOrganizer::insertBlankPages(&document, 4, 1));
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(5));
    QCOMPARE(getPageWidths(document).back(), 110);

    const PDFDocument beforeInsert = document;
    QVERIFY(PDFPageOrganizer::insertBlankPages(&document, 0, 0));
    QVERIFY(document == beforeInsert);
}

void PageOrganizerTest::extractPages()
{
    const PDFDocument document = createDocument(5);

    PDFDocument extracted;
    QVERIFY(PDFPageOrganizer::extractPages(&document, { 3, 0, 3, 9 }, &extracted));
    QCOMPARE(getPageWidths(extracted), std::vector<int>({ 130, 100 }));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 110, 120, 130, 140 }));

    PDFDocument empty;
    QVERIFY(!PDFPageOrganizer::extractPages(&document, { }, &empty));
    QVERIFY(!PDFPageOrganizer::extractPages(&document, { 0 }, nullptr));
}

void PageOrganizerTest::splitPages()
{
    using Parts = std::vector<std::vector<PDFInteger>>;
    QCOMPARE(PDFPageOrganizer::splitPages(5, 2), Parts({ { 0, 1 }, { 2, 3 }, { 4 } }));
    QCOMPARE(PDFPageOrganizer::splitPages(3, 1), Parts({ { 0 }, { 1 }, { 2 } }));
    QCOMPARE(PDFPageOrganizer::splitPages(3, 10), Parts({ { 0, 1, 2 } }));
    QCOMPARE(PDFPageOrganizer::splitPages(0, 1), Parts());
    QCOMPARE(PDFPageOrganizer::splitPages(3, 0), Parts());
}

void PageOrganizerTest::annotationsFollowMovedPages()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 500));
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 110, 500));
    builder.createAnnotationSquare(page, QRectF(10, 10, 50, 50), 1.0, QColor(), Qt::red, "Author", QString(), "Note");
    PDFDocument document = builder.build();

    QVERIFY(PDFPageOrganizer::movePages(&document, { 1 }, 0));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 110, 100 }));
    QCOMPARE(document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(1)->getAnnotations().size(), size_t(0));

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), document.getCatalog()->getPage(0)->getAnnotations().front());
    QVERIFY(annotation);
    QCOMPARE(annotation->getType(), AnnotationType::Square);
    QCOMPARE(annotation->getContents(), QString("Note"));
}

void PageOrganizerTest::organizedDocumentRoundTrip()
{
    PDFDocument document = createDocument(4);
    const PDFDocument inserted = createDocument(1, 700);

    QVERIFY(PDFPageOrganizer::movePages(&document, { 3 }, 0));
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 2));
    QVERIFY(PDFPageOrganizer::deletePages(&document, { 4 }));
    QVERIFY(PDFPageOrganizer::rotatePages(&document, { 0 }, true));

    const PDFDocument reread = read(write(document));
    QCOMPARE(getPageWidths(reread), std::vector<int>({ 130, 100, 700, 110 }));
    QCOMPARE(reread.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate90);
}

void PageOrganizerTest::cloudyPathSurroundsPolygon()
{
    const QRectF rectangle(100, 100, 200, 100);
    const PDFReal radius = PDFAnnotationBorderEffect::getCloudyArcRadius(1.0);
    QVERIFY(radius > 0.0);
    QVERIFY(PDFAnnotationBorderEffect::getCloudyArcRadius(0.0) < radius);
    QVERIFY(PDFAnnotationBorderEffect::getCloudyArcRadius(2.0) > radius);
    QCOMPARE(PDFAnnotationBorderEffect::getCloudyArcRadius(5.0), PDFAnnotationBorderEffect::getCloudyArcRadius(2.0));
    QCOMPARE(PDFAnnotationBorderEffect::getCloudyBorderMargin(1.0, 2.0), radius + 1.0);

    // Arcs bulge outward regardless of the orientation of the polygon
    QPolygonF clockwise = QPolygonF(rectangle);
    QPolygonF counterClockwise;
    for (auto it = clockwise.crbegin(); it != clockwise.crend(); ++it)
    {
        counterClockwise << *it;
    }

    for (const QPolygonF& polygon : { clockwise, counterClockwise })
    {
        const QPainterPath path = PDFAnnotationBorderEffect::createCloudyPath(polygon, 1.0);
        const QRectF bounds = path.boundingRect();

        QVERIFY(bounds.contains(rectangle));
        QVERIFY(bounds.left() < rectangle.left() - 0.5 * radius);
        QVERIFY(bounds.top() < rectangle.top() - 0.5 * radius);
        QVERIFY(bounds.right() > rectangle.right() + 0.5 * radius);
        QVERIFY(bounds.bottom() > rectangle.bottom() + 0.5 * radius);
        QVERIFY(rectangle.adjusted(-radius - 0.01, -radius - 0.01, radius + 0.01, radius + 0.01).contains(bounds));

        // Center of the polygon lies inside of the cloud
        QVERIFY(path.contains(rectangle.center()));

        // Each edge is divided into several arcs (two cubic curves per arc)
        QVERIFY(path.elementCount() > 2 * 3 * 8);
    }

    // Degenerated polygon gives a plain path
    const QPainterPath linePath = PDFAnnotationBorderEffect::createCloudyPath(QPolygonF({ QPointF(0, 0), QPointF(10, 10) }), 1.0);
    QCOMPARE(linePath.elementCount(), 2);
}

void PageOrganizerTest::cloudyPolygonAnnotation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 600, 600));

    QPolygonF polygon;
    polygon << QPointF(100, 100) << QPointF(300, 100) << QPointF(300, 250) << QPointF(100, 250) << QPointF(100, 100);
    const PDFObjectReference annotationReference = builder.createAnnotationPolygon(page, polygon, 1.0, QColor(), Qt::red, "Author", QString(), QString());
    builder.setAnnotationCloudyBorder(annotationReference, 1.0);
    builder.updateAnnotationAppearanceStreams(annotationReference);

    const PDFDocument document = read(write(builder.build()));
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), document.getCatalog()->getPage(0)->getAnnotations().front());
    const PDFPolygonalGeometryAnnotation* polygonAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation.data());
    QVERIFY(polygonAnnotation);
    QVERIFY(polygonAnnotation->getBorderEffect().isCloudy());
    QCOMPARE(polygonAnnotation->getBorderEffect().getIntensity(), 1.0);
    QCOMPARE(polygonAnnotation->getIntent(), PDFPolygonalGeometryAnnotation::Intent::Cloud);

    // The annotation rectangle contains the arcs, the appearance stream is generated
    const PDFReal margin = PDFAnnotationBorderEffect::getCloudyBorderMargin(1.0, 1.0);
    const QRectF rectangle = annotation->getRectangle();
    QVERIFY(rectangle.contains(QRectF(100 - margin + 0.1, 100 - margin + 0.1, 200 + 2 * margin - 0.2, 150 + 2 * margin - 0.2)));
    QVERIFY(!annotation->getAppearanceStreams().getAppearanceKeys().empty());

    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotationReference));
    QVERIFY(dictionary);
    const PDFDictionary* borderEffect = document.getDictionaryFromObject(dictionary->get("BE"));
    QVERIFY(borderEffect);
    QCOMPARE(borderEffect->get("S").getString(), QByteArray("C"));
}

void PageOrganizerTest::cloudySquareAnnotation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 600, 600));

    const QRectF cloudRectangle(100, 100, 200, 150);
    const PDFObjectReference annotationReference = builder.createAnnotationSquare(page, cloudRectangle, 2.0, QColor(), Qt::red, "Author", QString(), QString());
    builder.setAnnotationCloudyBorder(annotationReference, 2.0);
    builder.updateAnnotationAppearanceStreams(annotationReference);

    const PDFDocument document = builder.build();
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), annotationReference);
    const PDFSimpleGeometryAnnotation* squareAnnotation = dynamic_cast<const PDFSimpleGeometryAnnotation*>(annotation.data());
    QVERIFY(squareAnnotation);
    QVERIFY(squareAnnotation->getBorderEffect().isCloudy());

    // Rectangle differences keep the original rectangle as the geometry of the cloud,
    // the annotation rectangle is enlarged by the margin of the cloud
    const PDFReal margin = PDFAnnotationBorderEffect::getCloudyBorderMargin(2.0, 2.0);
    const QRectF geometry = squareAnnotation->getGeometryRectangle();
    QVERIFY(qAbs(geometry.left() - cloudRectangle.left()) < 0.01);
    QVERIFY(qAbs(geometry.width() - cloudRectangle.width()) < 0.01);
    QVERIFY(qAbs(annotation->getRectangle().width() - (cloudRectangle.width() + 2 * margin)) < 0.01);
    QVERIFY(!annotation->getAppearanceStreams().getAppearanceKeys().empty());
}

QTEST_MAIN(PageOrganizerTest)

#include "tst_pageorganizertest.moc"
