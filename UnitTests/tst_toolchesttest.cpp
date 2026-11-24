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


#include "pdfcustomstamp.h"
#include "pdftoolchest.h"
#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdfplacemarkuptool.h"
#include "pdfcms.h"
#include "pdfprogress.h"
#include "pdftoolchestwidget.h"
#include "pdfcustomstampdialog.h"

#include <QtTest>
#include <QBuffer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QTemporaryDir>

#include <array>

using namespace pdf;

/// Tests of the tool chest (PDFToolChest, PDFToolChestWidget) and of the
/// custom stamps (PDFCustomStampLibrary, PDFCustomStampDialog)
class ToolChestTest : public QObject
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
    void stampText();
    void stampJson();
    void stampLoadAndSave();
    void stampAnnotation();
    void stampAnnotationOnRotatedPage();
    void customStampNames();
    void toolChestItemWithoutReplies();
    void toolChestPlaceItem();
    void toolChestRenderItem();
    void toolChestJson();
    void widgetSavesAndPlacesMarkups();
    void widgetPlacesStamp();
    void widgetEditsItems();
    void stampDialog();

private:
    /// Test document: two pages, a cloud with a reply and a popup, a note
    struct TestDocument
    {
        PDFDocument document;
        PDFObjectReference page1;
        PDFObjectReference page2;
        PDFObjectReference cloud;
        PDFObjectReference reply;
        PDFObjectReference note;
    };

    static TestDocument createTestDocument(PageRotation rotation = PageRotation::None);

    /// Merges a single entry into the dictionary of the object
    static void setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value);

    /// Returns the annotations of the page
    static std::vector<PDFObjectReference> getAnnotations(const PDFDocument& document, size_t pageIndex);

    /// Writes the document and reads it back
    static PDFDocument writeAndRead(const PDFDocument& document);

    /// Returns a stamp with all fields set (also a logo)
    static PDFCustomStamp createStamp();

    /// Returns the number of pixels, which are not transparent
    static int countOpaquePixels(const QImage& image);

    QTemporaryDir m_temporaryDirectory;
};

void ToolChestTest::initTestCase()
{
    QVERIFY(m_temporaryDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("ToolChest");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporaryDirectory.path());
    QStandardPaths::setTestModeEnabled(true);
}

void ToolChestTest::setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem(key);
    factory << value;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(reference, factory.takeObject());
}

std::vector<PDFObjectReference> ToolChestTest::getAnnotations(const PDFDocument& document, size_t pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    return page ? page->getAnnotations() : std::vector<PDFObjectReference>();
}

PDFDocument ToolChestTest::writeAndRead(const PDFDocument& document)
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

PDFCustomStamp ToolChestTest::createStamp()
{
    QImage logo(80, 40, QImage::Format_ARGB32);
    logo.fill(Qt::transparent);
    QPainter painter(&logo);
    painter.fillRect(QRect(10, 5, 60, 30), QColor(255, 160, 0));
    painter.end();
    QByteArray logoData;
    QBuffer buffer(&logoData);
    buffer.open(QBuffer::WriteOnly);
    logo.save(&buffer, "PNG");

    PDFCustomStamp stamp;
    stamp.id = QStringLiteral("TestReviewed");
    stamp.name = QStringLiteral("Reviewed (Test)");
    stamp.title = QStringLiteral("REVIEWED");
    stamp.text = QStringLiteral("{company}\nBy {author}\n{date}");
    stamp.company = QStringLiteral("Test Lighting Company");
    stamp.color = QColor(21, 101, 192);
    stamp.logo = logoData;
    stamp.width = 216.0;
    return stamp;
}

int ToolChestTest::countOpaquePixels(const QImage& image)
{
    int count = 0;
    const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < converted.height(); ++y)
    {
        for (int x = 0; x < converted.width(); ++x)
        {
            count += qAlpha(converted.pixel(x, y)) > 0 ? 1 : 0;
        }
    }
    return count;
}

ToolChestTest::TestDocument ToolChestTest::createTestDocument(PageRotation rotation)
{
    TestDocument result;

    PDFDocumentBuilder builder;
    result.page1 = builder.appendPage(QRectF(0, 0, 792, 612));
    result.page2 = builder.appendPage(QRectF(0, 0, 792, 612));
    builder.setPageRotation(result.page1, rotation);
    builder.setPageRotation(result.page2, rotation);

    result.cloud = builder.createAnnotationSquare(result.page1, QRectF(200, 200, 100, 60), 2.0, QColor(255, 255, 0), Qt::red, "Bob", "Pole Move", "Move the pole 5 ft north");
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("S");
    factory << WrapName("C");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("I");
    factory << 1.0;
    factory.endDictionaryItem();
    factory.endDictionary();
    setEntry(builder, result.cloud, "BE", factory.takeObject());
    builder.updateAnnotationAppearanceStreams(result.cloud);

    result.reply = builder.createAnnotationText(result.page1, QRectF(10, 10, 10, 10), TextAnnotationIcon::Comment, "Alice", QString(), "Agreed", false);
    setEntry(builder, result.reply, "IRT", PDFObject::createReference(result.cloud));

    result.note = builder.createAnnotationText(result.page1, QRectF(500, 400, 20, 20), TextAnnotationIcon::Comment, "Alice", "Question", "Is this pole height correct?", false);

    result.document = builder.build();
    return result;
}

void ToolChestTest::stampText()
{
    PDFCustomStamp stamp = createStamp();
    const QDateTime dateTime(QDate(2026, 3, 14), QTime(9, 30));

    QCOMPARE(PDFCustomStampLibrary::expandText("{author} - {company}", stamp, "Gremlin", dateTime), QString("Gremlin - Test Lighting Company"));
    QVERIFY(PDFCustomStampLibrary::expandText("{date}", stamp, "Gremlin", dateTime) == QLocale().toString(dateTime.date(), QLocale::ShortFormat));
    QVERIFY(PDFCustomStampLibrary::expandText("{time}", stamp, "Gremlin", dateTime) == QLocale().toString(dateTime.time(), QLocale::ShortFormat));

    // Line with the company is left out, when there is no company
    QCOMPARE(PDFCustomStampLibrary::getLines(stamp, "Gremlin", dateTime).size(), 3);
    stamp.company.clear();
    const QStringList lines = PDFCustomStampLibrary::getLines(stamp, "Gremlin", dateTime);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.front(), QString("By Gremlin"));

    // More lines make the stamp higher, width is the width of the stamp
    const QSizeF size2 = PDFCustomStampLibrary::getSize(stamp, "Gremlin", dateTime);
    stamp.company = QStringLiteral("Company");
    const QSizeF size3 = PDFCustomStampLibrary::getSize(stamp, "Gremlin", dateTime);
    QCOMPARE(size2.width(), 216.0);
    QVERIFY(size3.height() > size2.height());

    // Preview has the size of the stamp at the scale and something is drawn
    const QImage preview = PDFCustomStampLibrary::renderPreview(stamp, "Gremlin", dateTime, 2.0);
    QCOMPARE(preview.size(), (size3 * 2.0).toSize());
    QVERIFY(countOpaquePixels(preview) > preview.width() * preview.height() / 2);
}

void ToolChestTest::stampJson()
{
    PDFCustomStamps stamps = PDFCustomStampLibrary::getDefaultStamps();
    QVERIFY(stamps.size() >= 5);
    stamps.push_back(createStamp());

    bool ok = false;
    const PDFCustomStamps readStamps = PDFCustomStampLibrary::fromJson(PDFCustomStampLibrary::toJson(stamps), &ok);
    QVERIFY(ok);
    QVERIFY(readStamps == stamps);
    QVERIFY(!readStamps.back().getLogoImage().isNull());

    // Not a file of the stamps
    PDFCustomStampLibrary::fromJson("{\"type\":\"something\"}", &ok);
    QVERIFY(!ok);
    PDFCustomStampLibrary::fromJson("garbage", &ok);
    QVERIFY(!ok);

    // Invalid stamps are skipped, invalid logo is removed
    const QByteArray data = R"({"type":"PDF4QT-Stamps","version":1,"stamps":[{"id":"","name":"No id"},{"id":"A","name":"Valid","logo":"bm90IGFuIGltYWdl"}]})";
    const PDFCustomStamps filtered = PDFCustomStampLibrary::fromJson(data, &ok);
    QVERIFY(ok);
    QCOMPARE(filtered.size(), size_t(1));
    QCOMPARE(filtered.front().name, QString("Valid"));
    QVERIFY(filtered.front().logo.isEmpty());

    // Identifiers are unique
    QVERIFY(PDFCustomStampLibrary::createId() != PDFCustomStampLibrary::createId());
}

void ToolChestTest::stampLoadAndSave()
{
    const QString fileName = m_temporaryDirectory.filePath("stamps/stamps.json");

    // Default stamps, when nothing has been saved yet
    QVERIFY(PDFCustomStampLibrary::load(fileName) == PDFCustomStampLibrary::getDefaultStamps());

    PDFCustomStamps stamps = { createStamp() };
    QVERIFY(PDFCustomStampLibrary::save(stamps, fileName));
    QVERIFY(PDFCustomStampLibrary::load(fileName) == stamps);

    // The user can delete all stamps
    QVERIFY(PDFCustomStampLibrary::save({ }, fileName));
    QVERIFY(PDFCustomStampLibrary::load(fileName).empty());
}

void ToolChestTest::stampAnnotation()
{
    TestDocument test = createTestDocument();
    const PDFCustomStamp stamp = createStamp();
    const QDateTime dateTime(QDate(2026, 3, 14), QTime(9, 30));
    const QSizeF size = PDFCustomStampLibrary::getSize(stamp, "Gremlin", dateTime);

    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference annotationReference = PDFCustomStampLibrary::createAnnotation(&builder, test.page2, QPointF(400, 300), stamp, "Gremlin", dateTime);
    QVERIFY(annotationReference.isValid());

    // Appearance stream of the stamp is kept, when the appearance streams are updated
    const PDFDocument created = builder.build();
    const PDFDictionary* dictionary = created.getDictionaryFromObject(created.getObjectByReference(annotationReference));
    QVERIFY(dictionary);
    const PDFObject appearance = created.getObject(created.getDictionaryFromObject(dictionary->get("AP"))->get("N"));
    builder.updateAnnotationAppearanceStreams(annotationReference);
    PDFDocument document = writeAndRead(builder.build());
    QVERIFY(document.getCatalog());

    const std::vector<PDFObjectReference> annotations = getAnnotations(document, 1);
    QCOMPARE(annotations.size(), size_t(1));
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), annotations.front());
    QVERIFY(annotation);
    QCOMPARE(annotation->getType(), AnnotationType::Stamp);
    QVERIFY(annotation->getContents().contains("REVIEWED"));
    QVERIFY(annotation->getContents().contains("Test Lighting Company"));
    QCOMPARE(annotation->asMarkupAnnotation()->getWindowTitle(), QString("Gremlin"));
    QCOMPARE(annotation->asMarkupAnnotation()->getSubject(), stamp.name);

    const QRectF rectangle = annotation->getRectangle();
    QVERIFY(qAbs(rectangle.center().x() - 400) < 0.01);
    QVERIFY(qAbs(rectangle.center().y() - 300) < 0.01);
    QVERIFY(qAbs(rectangle.width() - size.width()) < 0.01);
    QVERIFY(qAbs(rectangle.height() - size.height()) < 0.01);

    const PDFDictionary* readDictionary = document.getDictionaryFromObject(document.getObjectByReference(annotations.front()));
    QCOMPARE(readDictionary->get("Name").getString(), QByteArray("PDF4QT_TestReviewed"));
    const PDFObject& readAppearance = document.getObject(document.getDictionaryFromObject(readDictionary->get("AP"))->get("N"));
    QVERIFY(readAppearance.isStream());
    QVERIFY(appearance.isStream());
    QCOMPARE(readAppearance.getStream()->getContent()->size(), appearance.getStream()->getContent()->size());

    // The stamp is drawn by the appearance stream (logo is an image of the form)
    PDFToolChestItem item = PDFToolChest::createItem(&document, annotations, QString());
    QVERIFY(item.isValid());
    QCOMPARE(item.name, stamp.name);
    const QImage image = PDFToolChest::renderItem(item, QSize(400, 200));
    QVERIFY(countOpaquePixels(image) > image.width() * image.height() / 2);
}

void ToolChestTest::stampAnnotationOnRotatedPage()
{
    TestDocument test = createTestDocument(PageRotation::Rotate90);
    const PDFCustomStamp stamp = createStamp();
    const QDateTime dateTime(QDate(2026, 3, 14), QTime(9, 30));
    const QSizeF size = PDFCustomStampLibrary::getSize(stamp, "Gremlin", dateTime);

    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference annotationReference = PDFCustomStampLibrary::createAnnotation(&builder, test.page2, QPointF(400, 300), stamp, "Gremlin", dateTime, 90);
    const PDFDocument document = builder.build();

    // The stamp is upright on the screen - its rectangle on the page is transposed
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), annotationReference);
    QVERIFY(annotation);
    QVERIFY(qAbs(annotation->getRectangle().width() - size.height()) < 0.01);
    QVERIFY(qAbs(annotation->getRectangle().height() - size.width()) < 0.01);
    QVERIFY(qAbs(annotation->getRectangle().center().x() - 400) < 0.01);
    QVERIFY(qAbs(annotation->getRectangle().center().y() - 300) < 0.01);
}

void ToolChestTest::customStampNames()
{
    QVERIFY(PDFCustomStampLibrary::isCustomStampName("PDF4QT_Reviewed"));
    QVERIFY(PDFCustomStampLibrary::isCustomStampName("#ApprovedByJohn"));
    QVERIFY(!PDFCustomStampLibrary::isCustomStampName("Approved"));
    QVERIFY(!PDFCustomStampLibrary::isCustomStampName("Draft"));
    QVERIFY(!PDFCustomStampLibrary::isCustomStampName("TopSecret"));
    QVERIFY(!PDFCustomStampLibrary::isCustomStampName(QByteArray()));
}

void ToolChestTest::toolChestItemWithoutReplies()
{
    TestDocument test = createTestDocument();

    PDFToolChestItem item = PDFToolChest::createItem(&test.document, { test.cloud }, QString());
    QVERIFY(item.isValid());
    QCOMPARE(item.name, QString("Pole Move"));

    // Reply is not saved
    PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(item.data);
    QVERIFY(serialized.isValid());
    QCOMPARE(serialized.annotations.size(), size_t(1));
    for (const PDFObjectReference& reference : serialized.document.getCatalog()->getPage(0)->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&serialized.document.getStorage(), reference);
        QVERIFY(annotation);
        QVERIFY(!annotation->asMarkupAnnotation() || !annotation->asMarkupAnnotation()->getInReplyTo().isValid());
    }
    QVERIFY(qAbs(serialized.boundingRectangle.width() - 100) < 5.0);
    QVERIFY(qAbs(PDFToolChest::getBoundingRectangle(item).center().x() - 250) < 2.5);

    // Names
    QCOMPARE(PDFToolChest::createItem(&test.document, { test.cloud }, "  Standard cloud ").name, QString("Standard cloud"));
    QCOMPARE(PDFToolChest::getDefaultName(&test.document, { test.cloud, test.note }), QString("Pole Move (+1)"));
    QVERIFY(!PDFToolChest::createItem(&test.document, { }, QString()).isValid());
    QVERIFY(!PDFToolChest::createItem(nullptr, { test.cloud }, QString()).isValid());
    QVERIFY(PDFToolChest::getBoundingRectangle(PDFToolChestItem()).isNull());
}

void ToolChestTest::toolChestPlaceItem()
{
    TestDocument test = createTestDocument();
    PDFToolChestItem item = PDFToolChest::createItem(&test.document, { test.cloud, test.note }, "Pair");
    QVERIFY(item.isValid());

    const QRectF boundingRectangle = PDFToolChest::getBoundingRectangle(item);
    PDFDocumentBuilder builder(&test.document);
    const std::vector<PDFObjectReference> placed = PDFToolChest::placeItem(&builder, test.page2, QPointF(100, 100), item, "Gremlin");
    QCOMPARE(placed.size(), size_t(2));
    PDFDocument document = writeAndRead(builder.build());

    // Placed on the second page around the point, with the author
    QRectF placedRectangle;
    int clouds = 0;
    for (const PDFObjectReference& reference : getAnnotations(document, 1))
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&document.getStorage(), reference);
        QVERIFY(annotation);
        if (annotation->getType() == AnnotationType::Popup)
        {
            continue;
        }

        placedRectangle = placedRectangle.united(annotation->getRectangle());
        QCOMPARE(annotation->asMarkupAnnotation()->getWindowTitle(), QString("Gremlin"));
        if (annotation->getType() == AnnotationType::Square)
        {
            ++clouds;
            QCOMPARE(annotation->getContents(), QString("Move the pole 5 ft north"));
        }
    }
    QCOMPARE(clouds, 1);
    QVERIFY(qAbs(placedRectangle.center().x() - 100) < 2.5);
    QVERIFY(qAbs(placedRectangle.center().y() - 100) < 2.5);
    QVERIFY(qAbs(placedRectangle.width() - boundingRectangle.width()) < 2.5);

    // Original annotations on the first page are not changed
    QCOMPARE(getAnnotations(document, 0).size(), getAnnotations(test.document, 0).size());

    // Invalid data
    QVERIFY(PDFToolChest::placeItem(&builder, test.page2, QPointF(), PDFToolChestItem(), "Gremlin").empty());
}

void ToolChestTest::toolChestRenderItem()
{
    TestDocument test = createTestDocument();
    PDFToolChestItem item = PDFToolChest::createItem(&test.document, { test.cloud }, QString());

    const QImage image = PDFToolChest::renderItem(item, QSize(100, 100));
    QVERIFY(!image.isNull());
    QVERIFY(image.width() <= 100 && image.height() <= 100);
    QVERIFY(image.width() == 100 || image.height() == 100);

    // Cloud has a red border and a yellow fill
    int red = 0;
    int yellow = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() > 200 && color.red() > 200 && color.green() < 80 && color.blue() < 80)
            {
                ++red;
            }
            if (color.alpha() > 200 && color.red() > 200 && color.green() > 200 && color.blue() < 80)
            {
                ++yellow;
            }
        }
    }
    QVERIFY(red > 50);
    QVERIFY(yellow > 500);

    QVERIFY(PDFToolChest::renderItem(PDFToolChestItem(), QSize(100, 100)).isNull());
    QVERIFY(PDFToolChest::renderItem(item, QSize()).isNull());
}

void ToolChestTest::toolChestJson()
{
    TestDocument test = createTestDocument();
    PDFToolChestItems items = { PDFToolChest::createItem(&test.document, { test.cloud }, "Cloud"),
                                PDFToolChest::createItem(&test.document, { test.note }, "Note") };

    bool ok = false;
    const PDFToolChestItems readItems = PDFToolChest::fromJson(PDFToolChest::toJson(items), &ok);
    QVERIFY(ok);
    QCOMPARE(readItems.size(), size_t(2));
    QCOMPARE(readItems[0].id, items[0].id);
    QCOMPARE(readItems[1].name, QString("Note"));
    QCOMPARE(readItems[1].data, items[1].data);

    // Items with data, which are not annotations, are skipped
    const QByteArray data = R"({"type":"PDF4QT-ToolChest","version":1,"items":[{"id":"x","name":"Bad","data":"bm90IGEgcGRm"}]})";
    QVERIFY(PDFToolChest::fromJson(data, &ok).empty());
    QVERIFY(ok);
    PDFToolChest::fromJson("{\"type\":\"PDF4QT-Stamps\"}", &ok);
    QVERIFY(!ok);

    // Files
    const QString fileName = m_temporaryDirectory.filePath("toolchest/toolchest.json");
    QVERIFY(PDFToolChest::load(fileName).empty());
    QVERIFY(PDFToolChest::save(items, fileName));
    QCOMPARE(PDFToolChest::load(fileName).size(), size_t(2));
}

/// Draw widget with the annotation manager and the tool manager, tool chest
/// with its own files
struct ToolChestFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    std::unique_ptr<pdfviewer::PDFToolChestWidget> toolChest;
    int modifications = 0;

    explicit ToolChestFixture(PDFDocument initialDocument, const QString& directory) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        toolChest.reset(new pdfviewer::PDFToolChestWidget(widget.getDrawWidgetProxy(), &tools, nullptr,
                                                          QDir(directory).filePath("toolchest.json"),
                                                          QDir(directory).filePath("stamps.json")));
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&tools, &PDFToolManager::documentModified, &tools, [this](PDFModifiedDocument modified)
        {
            ++modifications;
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
    }

    ~ToolChestFixture()
    {
        tools.setActiveTool(nullptr);
        toolChest.reset();
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setToolManager(nullptr);
        widget.setAnnotationManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        tools.setDocument(modified);
        toolChest->setDocument(modified);
    }
};

void ToolChestTest::widgetSavesAndPlacesMarkups()
{
    QTemporaryDir directory;
    TestDocument test = createTestDocument();
    const PDFObjectReference cloud = test.cloud;
    ToolChestFixture fixture(std::move(test.document), directory.path());
    pdfviewer::PDFToolChestWidget* toolChest = fixture.toolChest.get();

    // Nothing is selected
    QVERIFY(toolChest->getItems().empty());
    QVERIFY(!toolChest->getAddButton()->isEnabled());
    QVERIFY(!toolChest->addSelectedMarkups());

    // Selected cloud is saved (also to the file)
    fixture.annotations.setSelectedAnnotations({ cloud });
    QVERIFY(toolChest->getAddButton()->isEnabled());
    QVERIFY(toolChest->addSelectedMarkups());
    QCOMPARE(toolChest->getItems().size(), size_t(1));
    QCOMPARE(toolChest->getItemsListWidget()->count(), 1);
    QCOMPARE(toolChest->getItemsListWidget()->item(0)->text(), QString("Pole Move"));
    QVERIFY(!toolChest->getItemsListWidget()->item(0)->icon().isNull());
    QCOMPARE(PDFToolChest::load(QDir(directory.path()).filePath("toolchest.json")).size(), size_t(1));

    // Item is placed on the second page by the placing tool
    QVERIFY(toolChest->activateItem(0));
    QCOMPARE(fixture.tools.getActiveTool(), toolChest->getPlaceTool());
    QCOMPARE(toolChest->getPlaceTool()->getName(), QString("Pole Move"));
    const std::vector<PDFObjectReference> placed = toolChest->getPlaceTool()->placeMarkup(1, QPointF(400, 300));
    QVERIFY(!placed.empty());
    QCOMPARE(fixture.modifications, 1);

    const std::vector<PDFObjectReference> pageAnnotations = getAnnotations(*fixture.document, 1);
    QCOMPARE(pageAnnotations.size(), placed.size());
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&fixture.document->getStorage(), placed.front());
    QVERIFY(annotation);
    QCOMPARE(annotation->getType(), AnnotationType::Square);
    QVERIFY(qAbs(annotation->getRectangle().center().x() - 400) < 2.5);

    // A new tool chest reads the saved items
    pdfviewer::PDFToolChestWidget secondToolChest(fixture.widget.getDrawWidgetProxy(), nullptr, nullptr,
                                                  QDir(directory.path()).filePath("toolchest.json"),
                                                  QDir(directory.path()).filePath("stamps.json"));
    QCOMPARE(secondToolChest.getItems().size(), size_t(1));
    QVERIFY(!secondToolChest.activateItem(0)); // no document
}

void ToolChestTest::widgetPlacesStamp()
{
    QTemporaryDir directory;
    TestDocument test = createTestDocument();
    ToolChestFixture fixture(std::move(test.document), directory.path());
    pdfviewer::PDFToolChestWidget* toolChest = fixture.toolChest.get();

    // Default stamps are offered
    QCOMPARE(toolChest->getStamps().size(), PDFCustomStampLibrary::getDefaultStamps().size());
    QCOMPARE(toolChest->getStampsListWidget()->count(), int(toolChest->getStamps().size()));

    QSignalSpy stampsChanged(toolChest, &pdfviewer::PDFToolChestWidget::stampsChanged);
    toolChest->setStamps({ createStamp() });
    QCOMPARE(stampsChanged.count(), 1);
    QCOMPARE(toolChest->getStampsListWidget()->count(), 1);
    QVERIFY(PDFCustomStampLibrary::load(QDir(directory.path()).filePath("stamps.json")) == toolChest->getStamps());

    // Actions of the stamp menu
    QObject parent;
    const QList<QAction*> actions = toolChest->createStampActions(&parent);
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.front()->text(), QString("Reviewed (Test)"));
    QVERIFY(actions.front()->isEnabled());

    actions.front()->trigger();
    QCOMPARE(fixture.tools.getActiveTool(), toolChest->getPlaceTool());
    const std::vector<PDFObjectReference> placed = toolChest->getPlaceTool()->placeMarkup(0, QPointF(600, 100));
    QCOMPARE(placed.size(), size_t(1));

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&fixture.document->getStorage(), placed.front());
    QVERIFY(annotation);
    QCOMPARE(annotation->getType(), AnnotationType::Stamp);
    QCOMPARE(annotation->asMarkupAnnotation()->getSubject(), QString("Reviewed (Test)"));
    QVERIFY(qAbs(annotation->getRectangle().center().x() - 600) < 0.01);

    // Invalid index
    QVERIFY(!toolChest->activateStamp(5));
    QVERIFY(!toolChest->activateStamp(-1));
}

void ToolChestTest::widgetEditsItems()
{
    QTemporaryDir directory;
    TestDocument test = createTestDocument();
    const PDFObjectReference cloud = test.cloud;
    const PDFObjectReference note = test.note;
    ToolChestFixture fixture(std::move(test.document), directory.path());
    pdfviewer::PDFToolChestWidget* toolChest = fixture.toolChest.get();

    QVERIFY(toolChest->addMarkups({ cloud }, "Cloud"));
    QVERIFY(toolChest->addMarkups({ note }, "Note"));
    QVERIFY(!toolChest->addMarkups({ }, "Nothing"));
    QCOMPARE(toolChest->getItems().size(), size_t(2));

    toolChest->renameItem(0, "  Standard Cloud ");
    QCOMPARE(toolChest->getItems()[0].name, QString("Standard Cloud"));
    toolChest->renameItem(0, "   ");
    QCOMPARE(toolChest->getItems()[0].name, QString("Standard Cloud"));

    toolChest->moveItem(0, +1);
    QCOMPARE(toolChest->getItems()[0].name, QString("Note"));
    QCOMPARE(toolChest->getItemsListWidget()->item(1)->text(), QString("Standard Cloud"));
    toolChest->moveItem(0, -1);
    QCOMPARE(toolChest->getItems()[0].name, QString("Note"));

    // Export, removal and import (items, which exist, are not added twice)
    const QString exportFileName = QDir(directory.path()).filePath("exported.json");
    QVERIFY(toolChest->exportItems(exportFileName));
    toolChest->removeItem(0);
    QCOMPARE(toolChest->getItems().size(), size_t(1));
    QCOMPARE(toolChest->importItems(exportFileName), 1);
    QCOMPARE(toolChest->getItems().size(), size_t(2));
    QCOMPARE(toolChest->importItems(exportFileName), 0);
    QCOMPARE(toolChest->importItems(QDir(directory.path()).filePath("missing.json")), -1);
    QCOMPARE(PDFToolChest::load(QDir(directory.path()).filePath("toolchest.json")).size(), size_t(2));
}

void ToolChestTest::stampDialog()
{
    QTemporaryDir directory;
    pdfviewer::PDFCustomStampDialog dialog({ createStamp() }, nullptr);
    QCOMPARE(dialog.getCurrentIndex(), 0);
    QCOMPARE(dialog.getNameEdit()->text(), QString("Reviewed (Test)"));
    QCOMPARE(dialog.getWidthSpinBox()->value(), 3.0);
    QVERIFY(!dialog.getPreviewLabel()->pixmap().isNull());

    // Editing changes the stamp
    dialog.getTitleEdit()->setText("CHECKED");
    Q_EMIT dialog.getTitleEdit()->textEdited("CHECKED");
    dialog.getTextEdit()->setPlainText("{author}");
    dialog.getWidthSpinBox()->setValue(4.0);
    QCOMPARE(dialog.getStamps().front().title, QString("CHECKED"));
    QCOMPARE(dialog.getStamps().front().text, QString("{author}"));
    QCOMPARE(dialog.getStamps().front().width, 288.0);

    // New stamp takes the company and the logo of the current stamp
    dialog.addStamp();
    QCOMPARE(dialog.getStamps().size(), size_t(2));
    QCOMPARE(dialog.getCurrentIndex(), 1);
    QCOMPARE(dialog.getStamps()[1].company, QString("Test Lighting Company"));
    QVERIFY(!dialog.getStamps()[1].logo.isEmpty());
    QVERIFY(dialog.getStamps()[1].id != dialog.getStamps()[0].id);

    dialog.duplicateStamp();
    QCOMPARE(dialog.getStamps().size(), size_t(3));
    QCOMPARE(dialog.getListWidget()->count(), 3);
    dialog.moveStamp(-1);
    QCOMPARE(dialog.getCurrentIndex(), 1);
    dialog.removeStamp();
    dialog.removeStamp();
    QCOMPARE(dialog.getStamps().size(), size_t(1));

    // Logo: large images are scaled down, other files are refused
    dialog.removeLogo();
    QVERIFY(dialog.getStamps().front().logo.isEmpty());
    QImage logo(1920, 772, QImage::Format_ARGB32);
    logo.fill(QColor(0, 90, 160, 200));
    const QString logoFileName = QDir(directory.path()).filePath("logo.png");
    QVERIFY(logo.save(logoFileName));
    QVERIFY(dialog.setLogoFromFile(logoFileName));
    QCOMPARE(dialog.getStamps().front().getLogoImage().width(), 600);
    QVERIFY(!dialog.setLogoFromFile(QDir(directory.path()).filePath("missing.png")));

    dialog.setColor(QColor(Qt::darkGreen));
    QCOMPARE(dialog.getStamps().front().color, QColor(Qt::darkGreen));

    // Export and import (a stamp with the same identifier is replaced)
    const QString fileName = QDir(directory.path()).filePath("stamps.json");
    QVERIFY(dialog.exportStamps(fileName));
    QCOMPARE(dialog.importStamps(fileName), 1);
    QCOMPARE(dialog.getStamps().size(), size_t(1));
    PDFCustomStamps otherStamps = PDFCustomStampLibrary::getDefaultStamps();
    QVERIFY(PDFCustomStampLibrary::save(otherStamps, fileName));
    QCOMPARE(dialog.importStamps(fileName), int(otherStamps.size()));
    QCOMPARE(dialog.getStamps().size(), otherStamps.size() + 1);
    QCOMPARE(dialog.importStamps(QDir(directory.path()).filePath("missing.json")), -1);
}

QTEST_MAIN(ToolChestTest)

#include "tst_toolchesttest.moc"
