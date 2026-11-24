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

#include "pdfmarkupslist.h"
#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfcms.h"
#include "pdfprogress.h"
#include "pdfmarkupslistwidget.h"

#include <QtTest>
#include <QBuffer>
#include <QComboBox>
#include <QLineEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QItemSelectionModel>

using namespace pdf;

/// Tests of the markups list (PDFMarkupsList, PDFMarkupsListWidget)
class MarkupsListTest : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
    }

private slots:
    void initTestCase();
    void collectsMarkupsOfAllPages();
    void measurementsAndCounts();
    void repliesAndStatus();
    void statusIsWrittenAndRead();
    void statusRepliesAreNotListedAsComments();
    void summary();
    void csvExport();
    void pdfReport();
    void widgetFilters();
    void widgetSetsStatus();
    void widgetSelectionIsSynchronized();

private:
    /// Test document: two pages with labels, various markups
    struct TestDocument
    {
        PDFDocument document;
        PDFObjectReference page1;
        PDFObjectReference page2;
        PDFObjectReference note;
        PDFObjectReference cloud;
        PDFObjectReference length;
        PDFObjectReference area;
        PDFObjectReference countA1;
        PDFObjectReference countA1second;
        PDFObjectReference countB2;
        PDFObjectReference arrow;
        PDFObjectReference link;
    };

    static TestDocument createTestDocument();

    /// Merges a single entry into the dictionary of the object
    static void setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value);

    /// Measure dictionary with a scale, one unit of the user space is \p factor feet
    static PDFObject createMeasure(PDFReal factor);

    /// Adds a reply to the annotation
    static PDFObjectReference addReply(PDFDocumentBuilder& builder, PDFObjectReference page, PDFObjectReference annotation, const QString& author, const QString& text);

    /// Finds the item of the annotation
    static const PDFMarkupsListItem* findItem(const PDFMarkupsListItems& items, PDFObjectReference annotation);

    /// Writes the document and reads it back
    static PDFDocument writeAndRead(const PDFDocument& document);

    QTemporaryDir m_temporaryDirectory;
};

void MarkupsListTest::initTestCase()
{
    QVERIFY(m_temporaryDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("MarkupsList");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporaryDirectory.path());
}

void MarkupsListTest::setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem(key);
    factory << value;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(reference, factory.takeObject());
}

PDFObject MarkupsListTest::createMeasure(PDFReal factor)
{
    auto createFormat = [](PDFObjectFactory& factory, const char* unit, PDFReal conversion)
    {
        factory.beginArray();
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("NumberFormat");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("U");
        factory << QString::fromLatin1(unit);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << conversion;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endArray();
    };

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Measure");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("RL");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("R");
    factory << QString("1 in = 1 ft");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("X");
    createFormat(factory, "ft", factor);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("D");
    createFormat(factory, "ft", 1.0);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("A");
    createFormat(factory, "sq ft", 1.0);
    factory.endDictionaryItem();
    factory.endDictionary();
    return factory.takeObject();
}

PDFObjectReference MarkupsListTest::addReply(PDFDocumentBuilder& builder, PDFObjectReference page, PDFObjectReference annotation, const QString& author, const QString& text)
{
    const PDFObjectReference reply = builder.createAnnotationText(page, QRectF(10, 10, 10, 10), TextAnnotationIcon::Comment, author, QString(), text, false);
    setEntry(builder, reply, "IRT", PDFObject::createReference(annotation));
    return reply;
}

const PDFMarkupsListItem* MarkupsListTest::findItem(const PDFMarkupsListItems& items, PDFObjectReference annotation)
{
    auto it = std::find_if(items.cbegin(), items.cend(), [annotation](const PDFMarkupsListItem& item) { return item.annotation == annotation; });
    return it != items.cend() ? &*it : nullptr;
}

PDFDocument MarkupsListTest::writeAndRead(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QBuffer::ReadWrite);
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult result = writer.write(&buffer, &document);
    if (!result)
    {
        return PDFDocument();
    }
    buffer.close();

    PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    return reader.readFromBuffer(buffer.data());
}

MarkupsListTest::TestDocument MarkupsListTest::createTestDocument()
{
    TestDocument result;

    PDFDocumentBuilder builder;
    result.page1 = builder.appendPage(QRectF(0, 0, 792, 612));
    result.page2 = builder.appendPage(QRectF(0, 0, 792, 612));

    // Page labels: sheet numbers
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Nums");
    factory.beginArray();
    factory << PDFInteger(0);
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << QString("LT-1");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory << PDFInteger(1);
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << QString("LT-2");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    const PDFObjectReference pageLabels = builder.addObject(factory.takeObject());
    setEntry(builder, builder.getCatalogReference(), "PageLabels", PDFObject::createReference(pageLabels));

    result.note = builder.createAnnotationText(result.page1, QRectF(100, 100, 20, 20), TextAnnotationIcon::Comment, "Alice", "Question", "Is this pole height correct?", false);

    result.cloud = builder.createAnnotationSquare(result.page1, QRectF(200, 200, 100, 60), 1.0, QColor(), Qt::red, "Bob", "Revision", "Move the pole 5 ft north");
    factory.beginDictionary();
    factory.beginDictionaryItem("S");
    factory << WrapName("C");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("I");
    factory << 1.0;
    factory.endDictionaryItem();
    factory.endDictionary();
    setEntry(builder, result.cloud, "BE", factory.takeObject());

    // Length: polyline of 72 + 36 + 18 = 126 pt, one point is 1/72 ft (1.75 ft). Polyline
    // with three points would be an angle.
    result.length = builder.createAnnotationPolygon(result.page1, QPolygonF({ QPointF(300, 300), QPointF(372, 300), QPointF(372, 336), QPointF(390, 336) }), 1.0, QColor(), Qt::blue, "Alice", "Conduit", "1.75 ft");
    setEntry(builder, result.length, "Subtype", PDFObject::createName("PolyLine"));
    setEntry(builder, result.length, "IT", PDFObject::createName("PolyLineDimension"));
    setEntry(builder, result.length, "Measure", createMeasure(1.0 / 72.0));

    // Area: square 72 x 72 pt = 1 sq ft
    result.area = builder.createAnnotationPolygon(result.page2, QPolygonF({ QPointF(100, 100), QPointF(172, 100), QPointF(172, 172), QPointF(100, 172) }), 1.0, QColor(), Qt::green, "Alice", "Parking", "A = 1 sq ft");
    setEntry(builder, result.area, "IT", PDFObject::createName("PolygonDimension"));
    setEntry(builder, result.area, "Measure", createMeasure(1.0 / 72.0));

    // Counted fixtures
    auto createCount = [&](PDFObjectReference page, QRectF rect, const QString& type)
    {
        const PDFObjectReference marker = builder.createAnnotationCircle(page, rect, 1.0, Qt::yellow, Qt::black, "Alice", QString("Fixture %1").arg(type), type);
        PDFObjectFactory typeFactory;
        typeFactory << type;
        setEntry(builder, marker, "PDF4QT_FixtureType", typeFactory.takeObject());
        return marker;
    };
    result.countA1 = createCount(result.page1, QRectF(400, 400, 10, 10), "A1");
    result.countA1second = createCount(result.page2, QRectF(400, 400, 10, 10), "A1");
    result.countB2 = createCount(result.page2, QRectF(450, 400, 10, 10), "B2");

    result.arrow = builder.createAnnotationLine(result.page2, QRectF(0, 0, 792, 612), QPointF(50, 50), QPointF(150, 150), 1.0, QColor(), Qt::red, "Bob", QString(), "=SUM(A1:A2)", AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);

    // Not a markup
    result.link = builder.createAnnotationLink(result.page2, QRectF(10, 10, 50, 20), QString("https://example.com"), LinkHighlightMode::Invert);

    result.document = builder.build();
    return result;
}

void MarkupsListTest::collectsMarkupsOfAllPages()
{
    const TestDocument test = createTestDocument();
    const PDFMarkupsListItems items = PDFMarkupsList::collect(&test.document);

    // Link is not a markup
    QCOMPARE(items.size(), size_t(8));
    QVERIFY(!findItem(items, test.link));

    // Ordered by the pages
    QCOMPARE(items.front().annotation, test.note);
    QCOMPARE(items.front().pageIndex, PDFInteger(0));
    QCOMPARE(items.back().pageIndex, PDFInteger(1));

    const PDFMarkupsListItem* note = findItem(items, test.note);
    QVERIFY(note);
    QCOMPARE(note->kind, PDFMarkupKind::Note);
    QCOMPARE(note->sheet, QString("LT-1"));
    QCOMPARE(note->subject, QString("Question"));
    QCOMPARE(note->contents, QString("Is this pole height correct?"));
    QCOMPARE(note->author, QString("Alice"));
    QCOMPARE(note->status, PDFMarkupStatus::Open);
    QVERIFY(note->modified.isValid());

    const PDFMarkupsListItem* cloud = findItem(items, test.cloud);
    QVERIFY(cloud);
    QCOMPARE(cloud->kind, PDFMarkupKind::Cloud);
    QCOMPARE(cloud->color, QColor(Qt::red));

    const PDFMarkupsListItem* arrow = findItem(items, test.arrow);
    QVERIFY(arrow);
    QCOMPARE(arrow->kind, PDFMarkupKind::Arrow);
    QCOMPARE(arrow->sheet, QString("LT-2"));
    QCOMPARE(arrow->getLabel(), PDFMarkupsList::getKindName(PDFMarkupKind::Arrow));

    QVERIFY(PDFMarkupsList::collect(nullptr).empty());
    QCOMPARE(PDFMarkupsList::getSheetNames(&test.document), QStringList({ "LT-1", "LT-2" }));
}

void MarkupsListTest::measurementsAndCounts()
{
    const TestDocument test = createTestDocument();
    const PDFMarkupsListItems items = PDFMarkupsList::collect(&test.document);

    const PDFMarkupsListItem* length = findItem(items, test.length);
    QVERIFY(length);
    QCOMPARE(length->kind, PDFMarkupKind::Length);
    QVERIFY(length->hasLength());
    QVERIFY(!length->hasArea());
    QVERIFY(qAbs(length->length - 1.75) < 1e-9);
    QCOMPARE(length->lengthUnit, QString("ft"));
    QCOMPARE(PDFMarkupsList::getMeasurementText(*length), QString("1.75 ft"));

    const PDFMarkupsListItem* area = findItem(items, test.area);
    QVERIFY(area);
    QCOMPARE(area->kind, PDFMarkupKind::Area);
    QVERIFY(qAbs(area->area - 1.0) < 1e-9);
    QVERIFY(qAbs(area->length - 4.0) < 1e-9);
    QCOMPARE(area->areaUnit, QString("sq ft"));
    QCOMPARE(PDFMarkupsList::getMeasurementText(*area), QString("A = 1 sq ft, P = 4 ft"));

    // Fixture type replaces the subject of a count marker
    const PDFMarkupsListItem* count = findItem(items, test.countB2);
    QVERIFY(count);
    QCOMPARE(count->kind, PDFMarkupKind::Count);
    QCOMPARE(count->subject, QString("B2"));
    QVERIFY(!count->hasLength());
    QVERIFY(PDFMarkupsList::getMeasurementText(*count).isEmpty());
}

void MarkupsListTest::repliesAndStatus()
{
    TestDocument test = createTestDocument();

    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference reply = addReply(builder, test.page1, test.cloud, "Carol", "Agreed, moving it.");
    addReply(builder, test.page1, reply, "Bob", "Thanks");

    // Status set twice, the latest wins
    QVERIFY(PDFMarkupsList::setStatus(&builder, test.cloud, PDFMarkupStatus::Accepted, "Carol").isValid());
    PDFObjectReference done = PDFMarkupsList::setStatus(&builder, test.cloud, PDFMarkupStatus::Done, "Dave");
    QVERIFY(done.isValid());

    // Dates of the replies are equal (same second) - the order in the page decides
    const PDFDocument document = builder.build();
    const PDFMarkupsListItems items = PDFMarkupsList::collect(&document);

    // Replies and states are not listed
    QCOMPARE(items.size(), size_t(8));

    const PDFMarkupsListItem* cloud = findItem(items, test.cloud);
    QVERIFY(cloud);
    QCOMPARE(cloud->status, PDFMarkupStatus::Done);
    QCOMPARE(cloud->statusAuthor, QString("Dave"));
    QCOMPARE(cloud->replyCount, 2);
    QCOMPARE(cloud->replies, QStringList({ "Carol: Agreed, moving it.", "Bob: Thanks" }));

    // State reply is a hidden text annotation of the review model
    const PDFAnnotationPtr state = PDFAnnotation::parse(&document.getStorage(), done);
    const PDFTextAnnotation* textAnnotation = dynamic_cast<const PDFTextAnnotation*>(state.data());
    QVERIFY(textAnnotation);
    QCOMPARE(textAnnotation->getState(), QString("Completed"));
    QCOMPARE(textAnnotation->getStateModel(), QString("Review"));
    QCOMPARE(textAnnotation->getInReplyTo(), test.cloud);
    QVERIFY(textAnnotation->getFlags().testFlag(PDFAnnotation::Hidden));

    // Invalid annotation
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFMarkupsList::setStatus(&invalidBuilder, PDFObjectReference(9999, 0), PDFMarkupStatus::Done, "Dave").isValid());

    // Status names round trip
    for (int i = 0; i < int(PDFMarkupStatus::LastStatus); ++i)
    {
        const PDFMarkupStatus status = PDFMarkupStatus(i);
        QCOMPARE(PDFMarkupsList::getStatusFromState(QString::fromLatin1(PDFMarkupsList::getStateName(status))), status);
        QVERIFY(!PDFMarkupsList::getStatusName(status).isEmpty());
    }
    QCOMPARE(PDFMarkupsList::getStatusFromState("Marked"), PDFMarkupStatus::Open);
}

void MarkupsListTest::statusIsWrittenAndRead()
{
    TestDocument test = createTestDocument();

    PDFDocumentBuilder builder(&test.document);
    PDFMarkupsList::setStatus(&builder, test.length, PDFMarkupStatus::Rejected, "Erin");
    PDFMarkupsList::setStatus(&builder, test.countA1second, PDFMarkupStatus::Accepted, "Erin");

    const PDFDocument document = writeAndRead(builder.build());
    const PDFMarkupsListItems items = PDFMarkupsList::collect(&document);
    QCOMPARE(items.size(), size_t(8));

    // References are kept by the writer
    const PDFMarkupsListItem* length = findItem(items, test.length);
    QVERIFY(length);
    QCOMPARE(length->status, PDFMarkupStatus::Rejected);
    QCOMPARE(length->statusAuthor, QString("Erin"));
    QVERIFY(length->statusDate.isValid());

    const PDFMarkupsListItem* count = findItem(items, test.countA1second);
    QVERIFY(count);
    QCOMPARE(count->status, PDFMarkupStatus::Accepted);
    QCOMPARE(count->pageIndex, PDFInteger(1));
}

void MarkupsListTest::statusRepliesAreNotListedAsComments()
{
    TestDocument test = createTestDocument();
    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference state = PDFMarkupsList::setStatus(&builder, test.note, PDFMarkupStatus::Accepted, "Frank");
    const PDFDocument document = builder.build();

    // The state is a reply in the thread of the note (deleted, copied and moved with it)
    const std::vector<PDFObjectReference> replies = PDFAnnotationManipulator::getReplies(&document.getStorage(), test.page1, test.note);
    QVERIFY(std::find(replies.cbegin(), replies.cend(), state) != replies.cend());

    const PDFMarkupsListItem* note = findItem(PDFMarkupsList::collect(&document), test.note);
    QVERIFY(note);
    QCOMPARE(note->replyCount, 0);
    QCOMPARE(note->status, PDFMarkupStatus::Accepted);
}

void MarkupsListTest::summary()
{
    TestDocument test = createTestDocument();
    PDFDocumentBuilder builder(&test.document);
    PDFMarkupsList::setStatus(&builder, test.countA1, PDFMarkupStatus::Done, "Gina");
    const PDFDocument document = builder.build();

    const PDFMarkupsSummaryItems summary = PDFMarkupsList::summarize(PDFMarkupsList::collect(&document));

    // Counts first (A1, B2), then lengths, areas and the other markups
    QVERIFY(summary.size() >= 6);
    QCOMPARE(summary[0].label, QString("A1"));
    QCOMPARE(summary[0].kind, PDFMarkupKind::Count);
    QCOMPARE(summary[0].count, 2);
    QCOMPARE(summary[0].statusCounts[size_t(PDFMarkupStatus::Done)], 1);
    QCOMPARE(summary[0].statusCounts[size_t(PDFMarkupStatus::Open)], 1);
    QCOMPARE(summary[1].label, QString("B2"));
    QCOMPARE(summary[1].count, 1);

    QCOMPARE(summary[2].label, QString("Conduit"));
    QCOMPARE(summary[2].kind, PDFMarkupKind::Length);
    QVERIFY(summary[2].hasLength);
    QVERIFY(qAbs(summary[2].length - 1.75) < 1e-9);
    QCOMPARE(summary[2].lengthUnit, QString("ft"));

    QCOMPARE(summary[3].label, QString("Parking"));
    QCOMPARE(summary[3].kind, PDFMarkupKind::Area);
    QVERIFY(summary[3].hasArea);
    QVERIFY(qAbs(summary[3].area - 1.0) < 1e-9);

    // Two lengths with the same subject are summed
    PDFMarkupsListItems items = PDFMarkupsList::collect(&document);
    PDFMarkupsListItem second = *findItem(items, test.length);
    second.length = 2.25;
    items.push_back(second);
    const PDFMarkupsSummaryItems summed = PDFMarkupsList::summarize(items);
    QCOMPARE(summed[2].count, 2);
    QVERIFY(qAbs(summed[2].length - 4.0) < 1e-9);

    // Different units are not summed
    second.lengthUnit = "m";
    items.push_back(second);
    QCOMPARE(PDFMarkupsList::summarize(items).size(), summed.size() + 1);

    const std::vector<QStringList> table = PDFMarkupsList::createSummaryTable(summary);
    QCOMPARE(table.size(), summary.size() + 1);
    QCOMPARE(table[1][0], QString("A1"));
    QCOMPARE(table[1][2], QString("2"));
    QCOMPARE(table[3][3], QString("1.75 ft"));
}

void MarkupsListTest::csvExport()
{
    const TestDocument test = createTestDocument();
    const PDFMarkupsListItems items = PDFMarkupsList::collect(&test.document);
    const std::vector<QStringList> table = PDFMarkupsList::createTable(items);

    QCOMPARE(table.size(), items.size() + 1);
    QCOMPARE(table.front().size(), 16);
    for (const QStringList& row : table)
    {
        QCOMPARE(row.size(), table.front().size());
    }

    const QString csv = PDFMarkupsList::createCsv(table);
    QVERIFY(csv.startsWith(PDFMarkupsList::getColumnTitle(PDFMarkupsList::ColumnSheet)));
    QVERIFY(csv.contains("LT-1"));
    QVERIFY(csv.contains("1.75,ft"));

    // Formula injection: the comment of the arrow is not a formula
    QVERIFY(csv.contains("'=SUM(A1:A2)"));
    QVERIFY(!csv.contains(",=SUM"));

    QCOMPARE(PDFMarkupsList::escapeCsvField("a,b"), QString("\"a,b\""));
    QCOMPARE(PDFMarkupsList::escapeCsvField("say \"hi\""), QString("\"say \"\"hi\"\"\""));
    QCOMPARE(PDFMarkupsList::escapeCsvField("+1"), QString("'+1"));
    QCOMPARE(PDFMarkupsList::escapeCsvField("plain"), QString("plain"));
    QCOMPARE(PDFMarkupsList::formatValue(2.0), QString("2"));
    QCOMPARE(PDFMarkupsList::formatValue(2.125), QString("2.13"));
    QCOMPARE(PDFMarkupsList::formatValue(2.5), QString("2.5"));
}

void MarkupsListTest::pdfReport()
{
    const TestDocument test = createTestDocument();
    PDFMarkupsListItems items = PDFMarkupsList::collect(&test.document);

    // Many markups - the table continues on the next pages
    const PDFMarkupsListItems original = items;
    for (int i = 0; i < 20; ++i)
    {
        items.insert(items.end(), original.cbegin(), original.cend());
    }

    const QString fileName = m_temporaryDirectory.filePath("report.pdf");
    QString errorMessage;
    QVERIFY2(PDFMarkupsList::writePdfReport(fileName, "Lighting Plan", items, &errorMessage), qPrintable(errorMessage));

    PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    const PDFDocument report = reader.readFromFile(fileName);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(report.getCatalog()->getPageCount() >= 2);

    // Landscape letter
    const QRectF mediaBox = report.getCatalog()->getPage(0)->getMediaBox();
    QVERIFY(qAbs(mediaBox.width() - 792) < 1.0);
    QVERIFY(qAbs(mediaBox.height() - 612) < 1.0);

    QVERIFY(!PDFMarkupsList::writePdfReport(m_temporaryDirectory.filePath("missing/directory/report.pdf"), "x", items, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

/// Draw widget and annotation manager with the markups list
struct MarkupsListFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    std::unique_ptr<pdfviewer::PDFMarkupsListWidget> list;

    explicit MarkupsListFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        list.reset(new pdfviewer::PDFMarkupsListWidget(widget.getDrawWidgetProxy(), nullptr));
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(list.get(), &pdfviewer::PDFMarkupsListWidget::documentModified, list.get(), [this](PDFModifiedDocument modified)
        {
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
    }

    ~MarkupsListFixture()
    {
        list.reset();
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setAnnotationManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        list->setDocument(modified);
    }

    int rowCount() const { return list->getTableView()->model()->rowCount(); }
};

void MarkupsListTest::widgetFilters()
{
    TestDocument test = createTestDocument();
    MarkupsListFixture fixture(std::move(test.document));
    pdfviewer::PDFMarkupsListWidget* list = fixture.list.get();

    QCOMPARE(fixture.rowCount(), 8);
    QCOMPARE(list->getItems().size(), size_t(8));

    // Kind filter
    QComboBox* kind = list->getKindFilter();
    kind->setCurrentIndex(kind->findData(int(PDFMarkupKind::Count)));
    QCOMPARE(fixture.rowCount(), 3);
    QCOMPARE(list->getFilteredItems().size(), size_t(3));

    // Sheet filter together with the kind
    QComboBox* sheet = list->getSheetFilter();
    QCOMPARE(sheet->count(), 3);
    QCOMPARE(sheet->itemText(2), QString("LT-2"));
    sheet->setCurrentIndex(2);
    QCOMPARE(fixture.rowCount(), 2);
    kind->setCurrentIndex(0);
    QCOMPARE(fixture.rowCount(), 4);
    sheet->setCurrentIndex(0);

    // Search in all columns
    list->getSearchEdit()->setText("pole");
    QCOMPARE(fixture.rowCount(), 2);
    list->getSearchEdit()->setText("CONDUIT");
    QCOMPARE(fixture.rowCount(), 1);
    list->getSearchEdit()->clear();
    QCOMPARE(fixture.rowCount(), 8);

    // Status filter
    QComboBox* status = list->getStatusFilter();
    status->setCurrentIndex(status->findData(int(PDFMarkupStatus::Done)));
    QCOMPARE(fixture.rowCount(), 0);
    status->setCurrentIndex(status->findData(int(PDFMarkupStatus::Open)));
    QCOMPARE(fixture.rowCount(), 8);
    status->setCurrentIndex(0);

    // Summary follows the filter
    list->setSummaryVisible(true);
    QVERIFY(list->isSummaryVisible());
    kind->setCurrentIndex(kind->findData(int(PDFMarkupKind::Count)));
    QCOMPARE(list->getSummaryView()->model()->rowCount(), 2);
    list->setSummaryVisible(false);
    QVERIFY(!list->isSummaryVisible());

    // Exports of the filtered markups
    const QString csvFile = m_temporaryDirectory.filePath("counts.csv");
    QString errorMessage;
    QVERIFY(list->exportCsv(csvFile, false, &errorMessage));
    QFile file(csvFile);
    QVERIFY(file.open(QFile::ReadOnly));
    const QString csv = QString::fromUtf8(file.readAll());
    file.close();
    QCOMPARE(csv.count(QStringLiteral("\r\n")), 4);
    QVERIFY(list->exportCsv(m_temporaryDirectory.filePath("summary.csv"), true, &errorMessage));
    QVERIFY(list->exportPdf(m_temporaryDirectory.filePath("summary.pdf"), &errorMessage));
    QVERIFY(!list->exportCsv(m_temporaryDirectory.filePath("missing/x.csv"), false, &errorMessage));
}

void MarkupsListTest::widgetSetsStatus()
{
    TestDocument test = createTestDocument();
    const PDFObjectReference cloud = test.cloud;
    const PDFObjectReference note = test.note;
    MarkupsListFixture fixture(std::move(test.document));
    pdfviewer::PDFMarkupsListWidget* list = fixture.list.get();

    QSignalSpy spy(list, &pdfviewer::PDFMarkupsListWidget::documentModified);
    list->setStatus({ cloud, note }, PDFMarkupStatus::Accepted);
    QCOMPARE(spy.count(), 1);

    // The list is rebuilt from the modified document
    const PDFMarkupsListItems& items = list->getItems();
    QCOMPARE(findItem(items, cloud)->status, PDFMarkupStatus::Accepted);
    QCOMPARE(findItem(items, note)->status, PDFMarkupStatus::Accepted);
    QCOMPARE(findItem(items, cloud)->statusAuthor, PDFAuthorSettings::getAuthorName());

    // The same status again does not modify the document
    list->setStatus({ cloud }, PDFMarkupStatus::Accepted);
    QCOMPARE(spy.count(), 1);

    // Status of the selected rows
    QTableView* table = list->getTableView();
    table->selectRow(1);
    QCOMPARE(list->getSelectedItems().size(), size_t(1));
    const PDFObjectReference selected = list->getSelectedItems().front().annotation;
    list->setSelectedStatus(PDFMarkupStatus::Done);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(findItem(list->getItems(), selected)->status, PDFMarkupStatus::Done);

    // Selection is kept over the rebuild
    QCOMPARE(list->getSelectedItems().size(), size_t(1));
    QCOMPARE(list->getSelectedItems().front().annotation, selected);

    // Status column shows the status
    const QModelIndex statusIndex = table->model()->index(1, PDFMarkupsList::ColumnStatus);
    QCOMPARE(statusIndex.data().toString(), PDFMarkupsList::getStatusName(PDFMarkupStatus::Done));
}

void MarkupsListTest::widgetSelectionIsSynchronized()
{
    TestDocument test = createTestDocument();
    const PDFObjectReference cloud = test.cloud;
    const PDFObjectReference note = test.note;
    const PDFObjectReference arrow = test.arrow;
    MarkupsListFixture fixture(std::move(test.document));
    pdfviewer::PDFMarkupsListWidget* list = fixture.list.get();

    // Selection on the pages selects the rows
    fixture.annotations.setSelectedAnnotations({ cloud });
    QCOMPARE(list->getSelectedItems().size(), size_t(1));
    QCOMPARE(list->getSelectedItems().front().annotation, cloud);

    // Selection of the rows selects the annotations
    QTableView* table = list->getTableView();
    for (int row = 0; row < fixture.rowCount(); ++row)
    {
        const int itemIndex = table->model()->index(row, 0).data(Qt::UserRole).toInt();
        if (list->getItems()[itemIndex].annotation == arrow)
        {
            table->selectRow(row);
        }
    }
    const std::vector<PDFObjectReference> selected = fixture.annotations.getSelectedAnnotations();
    QCOMPARE(selected.size(), size_t(1));
    QCOMPARE(selected.front(), arrow);

    // Sorting by a column keeps the mapping of the rows
    table->sortByColumn(PDFMarkupsList::ColumnAuthor, Qt::DescendingOrder);
    fixture.annotations.setSelectedAnnotations({ note });
    QCOMPARE(list->getSelectedItems().size(), size_t(1));
    QCOMPARE(list->getSelectedItems().front().annotation, note);
}

QTEST_MAIN(MarkupsListTest)

#include "tst_markupslisttest.moc"
