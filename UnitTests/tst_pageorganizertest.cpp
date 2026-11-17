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
#include "pdfform.h"
#include "pdfnumbertreeloader.h"

#include <QtTest>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

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
    void insertDocumentInTheMiddleKeepsOutlines();
    void insertDocumentKeepsFormsAndNamedDestinations();
    void insertDocumentKeepsDocumentInformation();
    void removedPagesAreNotKept();
    void outlineWithNamedDestinationsSurvivesDelete();
    void extractedDocumentHasOnlyItsFields();
    void pageLabelsFollowPages();
    void realDocument();
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

    /// Description of a document created by createRichDocument
    struct RichDocument
    {
        int pageCount = 0;
        int firstWidth = 100;
        QString title;
        std::vector<std::pair<QString, int>> fields;                ///< Name of the field and index of the page of its widget
        std::vector<std::pair<QByteArray, int>> namedDestinations;  ///< Name of the destination and index of its page
        std::vector<std::pair<QString, int>> pageLabels;            ///< Prefix and index of the first page of the label range
    };

    /// Creates a document with form fields, named destinations (in the name tree), page labels and a title
    static PDFDocument createRichDocument(const RichDocument& description);

    /// Returns the names of the form fields of the document
    static QStringList getFieldNames(const PDFDocument& document);

    /// Returns the named destinations of the document with the widths of their pages (-1 for a page, which is not in the document)
    static std::map<QByteArray, int> getNamedDestinationPageWidths(const PDFDocument& document);

    /// Returns the labels of the pages of the document
    static QStringList getPageLabels(const PDFDocument& document);

    /// Returns the number of the page objects in the storage of the document
    static int getPageObjectCount(const PDFDocument& document);

    /// Returns the titles of the top level outline items
    static QStringList getTopLevelOutlineTitles(const PDFDocument& document);

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

PDFDocument PageOrganizerTest::createRichDocument(const RichDocument& description)
{
    PDFDocumentBuilder builder;

    std::vector<PDFObjectReference> pages;
    for (int i = 0; i < description.pageCount; ++i)
    {
        pages.push_back(builder.appendPage(QRectF(0, 0, description.firstWidth + 10 * i, 500)));
    }

    if (!description.title.isEmpty())
    {
        builder.setDocumentTitle(description.title);
    }

    if (!description.fields.empty())
    {
        PDFObjectReferenceVector fields;
        for (const auto& field : description.fields)
        {
            const PDFObjectReference fieldReference = builder.createFormFieldSignature(field.first, { }, PDFObjectReference());
            builder.createFormFieldWidget(fieldReference, pages.at(field.second), PDFObjectReference(), QRectF(10, 10, 50, 20));
            fields.push_back(fieldReference);
        }
        builder.createAcroForm(fields);
    }

    if (!description.namedDestinations.empty())
    {
        std::map<QByteArray, int> destinations(description.namedDestinations.cbegin(), description.namedDestinations.cend());

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Dests");
        factory.beginDictionary();
        factory.beginDictionaryItem("Names");
        factory.beginArray();
        for (const auto& destination : destinations)
        {
            factory << WrapString(destination.first);
            factory << PDFDestination::createFit(pages.at(destination.second));
        }
        factory.endArray();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.setCatalogNames(builder.addObject(factory.takeObject()));
    }

    if (!description.pageLabels.empty())
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("PageLabels");
        factory.beginDictionary();
        factory.beginDictionaryItem("Nums");
        factory.beginArray();
        for (const auto& pageLabel : description.pageLabels)
        {
            factory << PDFInteger(pageLabel.second);
            factory.beginDictionary();
            factory.beginDictionaryItem("S");
            factory << WrapName("D");
            factory.endDictionaryItem();
            factory.beginDictionaryItem("P");
            factory << pageLabel.first;
            factory.endDictionaryItem();
            factory.endDictionary();
        }
        factory.endArray();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.getCatalogReference(), factory.takeObject());
    }

    // Documents are tested as they are read from a file
    return read(write(builder.build()));
}

QStringList PageOrganizerTest::getFieldNames(const PDFDocument& document)
{
    QStringList names;
    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    for (const PDFFormFieldPointer& field : form.getFormFields())
    {
        names << field->getName(PDFFormField::NameType::FullyQualified);
    }
    names.sort();
    return names;
}

std::map<QByteArray, int> PageOrganizerTest::getNamedDestinationPageWidths(const PDFDocument& document)
{
    std::map<QByteArray, int> result;
    for (const auto& item : document.getCatalog()->getNamedDestinations())
    {
        const size_t pageIndex = document.getCatalog()->getPageIndexFromPageReference(item.second.getPageReference());
        result[item.first] = pageIndex != PDFCatalog::INVALID_PAGE_INDEX ? qRound(document.getCatalog()->getPage(pageIndex)->getMediaBox().width()) : -1;
    }
    return result;
}

QStringList PageOrganizerTest::getPageLabels(const PDFDocument& document)
{
    QStringList result;

    const PDFDictionary* catalogDictionary = document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"));
    std::vector<PDFPageLabel> pageLabels = PDFNumberTreeLoader<PDFPageLabel>::parse(&document.getStorage(), catalogDictionary->get("PageLabels"));
    std::stable_sort(pageLabels.begin(), pageLabels.end());

    for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
    {
        QString label;
        for (const PDFPageLabel& pageLabel : pageLabels)
        {
            if (pageLabel.getPageIndex() <= PDFInteger(i))
            {
                label = pageLabel.getPrefix() + QString::number(pageLabel.getPageStartNumber() + PDFInteger(i) - pageLabel.getPageIndex());
            }
        }
        result << label;
    }

    return result;
}

int PageOrganizerTest::getPageObjectCount(const PDFDocument& document)
{
    int count = 0;
    for (const PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
    {
        if (entry.object.isDictionary())
        {
            const PDFObject& type = entry.object.getDictionary()->get("Type");
            if (type.isName() && type.getString() == "Page")
            {
                ++count;
            }
        }
    }
    return count;
}

QStringList PageOrganizerTest::getTopLevelOutlineTitles(const PDFDocument& document)
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

void PageOrganizerTest::insertDocumentInTheMiddleKeepsOutlines()
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
        return read(write(builder.build()));
    };

    // Pages of the document are split by the inserted pages, bookmarks of both documents are kept
    PDFDocument document = createDocumentWithOutline(100, "Sheet A");
    const PDFDocument inserted = createDocumentWithOutline(500, "Sheet B");
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 1));
    QCOMPARE(getPageWidths(document), std::vector<int>({ 100, 500, 510, 110 }));
    QCOMPARE(getTopLevelOutlineTitles(document), QStringList({ "Sheet A", "Sheet B" }));

    // Only a part of the inserted document: bookmark of the page, which is not inserted, is omitted
    PDFDocument documentWithPart = createDocumentWithOutline(100, "Sheet A");
    QVERIFY(PDFPageOrganizer::insertDocument(&documentWithPart, &inserted, 1, { 1 }));
    QCOMPARE(getPageWidths(documentWithPart), std::vector<int>({ 100, 510, 110 }));
    QCOMPARE(getTopLevelOutlineTitles(documentWithPart), QStringList({ "Sheet A" }));
    QCOMPARE(getPageObjectCount(documentWithPart), 3);

    PDFDocument documentWithFirstPage = createDocumentWithOutline(100, "Sheet A");
    QVERIFY(PDFPageOrganizer::insertDocument(&documentWithFirstPage, &inserted, 0, { 0 }));
    QCOMPARE(getTopLevelOutlineTitles(documentWithFirstPage), QStringList({ "Sheet B", "Sheet A" }));

    // Bookmarks point to their pages also in the written file
    const PDFDocument reread = read(write(document));
    QSharedPointer<PDFOutlineItem> root = reread.getCatalog()->getOutlineRootPtr();
    QVERIFY(root);
    QCOMPARE(root->getChildCount(), size_t(2));
    const PDFActionGoTo* action = dynamic_cast<const PDFActionGoTo*>(root->getChild(1)->getAction());
    QVERIFY(action);
    QCOMPARE(reread.getCatalog()->getPageIndexFromPageReference(action->getDestination().getPageReference()), size_t(1));
}

void PageOrganizerTest::insertDocumentKeepsFormsAndNamedDestinations()
{
    RichDocument descriptionA;
    descriptionA.pageCount = 2;
    descriptionA.fields = { { "fieldA", 0 } };
    descriptionA.namedDestinations = { { "destinationA", 1 } };

    RichDocument descriptionB;
    descriptionB.pageCount = 2;
    descriptionB.firstWidth = 500;
    descriptionB.fields = { { "fieldB", 1 } };
    descriptionB.namedDestinations = { { "destinationB", 0 } };

    const PDFDocument inserted = createRichDocument(descriptionB);
    const std::map<QByteArray, int> expectedDestinations = { { "destinationA", 110 }, { "destinationB", 500 } };

    // At the end, at the beginning and in the middle
    for (const PDFInteger position : { 2, 0, 1 })
    {
        PDFDocument document = createRichDocument(descriptionA);
        QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, position));
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
        QCOMPARE(getFieldNames(document), QStringList({ "fieldA", "fieldB" }));
        QCOMPARE(getNamedDestinationPageWidths(document), expectedDestinations);

        const PDFDocument reread = read(write(document));
        QCOMPARE(getFieldNames(reread), QStringList({ "fieldA", "fieldB" }));
        QCOMPARE(getNamedDestinationPageWidths(reread), expectedDestinations);
        QCOMPARE(getPageObjectCount(reread), 4);
    }

    // Document without a form and named destinations gets them from the inserted document
    PDFDocument plainDocument = createDocument(2);
    QVERIFY(PDFPageOrganizer::insertDocument(&plainDocument, &inserted, 1));
    QCOMPARE(getFieldNames(plainDocument), QStringList({ "fieldB" }));
    QCOMPARE(getNamedDestinationPageWidths(plainDocument), (std::map<QByteArray, int>{ { "destinationB", 500 } }));

    // ... and keeps its own, when a plain document is inserted
    PDFDocument document = createRichDocument(descriptionA);
    const PDFDocument plainInserted = createDocument(1, 700);
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &plainInserted, 1));
    QCOMPARE(getFieldNames(document), QStringList({ "fieldA" }));
    QCOMPARE(getNamedDestinationPageWidths(document), (std::map<QByteArray, int>{ { "destinationA", 110 } }));

    // Only the fields and the destinations of the inserted pages are added
    PDFDocument documentWithPart = createRichDocument(descriptionA);
    QVERIFY(PDFPageOrganizer::insertDocument(&documentWithPart, &inserted, 1, { 0 }));
    QCOMPARE(getPageWidths(documentWithPart), std::vector<int>({ 100, 500, 110 }));
    QCOMPARE(getFieldNames(documentWithPart), QStringList({ "fieldA" }));
    QCOMPARE(getNamedDestinationPageWidths(documentWithPart), expectedDestinations);
    QCOMPARE(getPageObjectCount(read(write(documentWithPart))), 3);
}

void PageOrganizerTest::insertDocumentKeepsDocumentInformation()
{
    RichDocument descriptionA;
    descriptionA.pageCount = 2;
    descriptionA.title = "Plan set A";

    RichDocument descriptionB;
    descriptionB.pageCount = 1;
    descriptionB.firstWidth = 500;
    descriptionB.title = "Plan set B";

    PDFDocument document = createRichDocument(descriptionA);
    const PDFDocument inserted = createRichDocument(descriptionB);
    QVERIFY(PDFPageOrganizer::insertDocument(&document, &inserted, 1));
    QCOMPARE(document.getInfo()->title, QString("Plan set A"));
    QCOMPARE(read(write(document)).getInfo()->title, QString("Plan set A"));

    QVERIFY(PDFPageOrganizer::insertBlankPages(&document, 0, 1));
    QVERIFY(PDFPageOrganizer::deletePages(&document, { 1 }));
    QVERIFY(PDFPageOrganizer::movePages(&document, { 0 }, 3));
    QCOMPARE(document.getInfo()->title, QString("Plan set A"));
}

void PageOrganizerTest::removedPagesAreNotKept()
{
    RichDocument description;
    description.pageCount = 3;
    description.fields = { { "field0", 0 }, { "field1", 1 }, { "field2", 2 } };
    description.namedDestinations = { { "destination0", 0 }, { "destination1", 1 }, { "destination2", 2 } };

    // Nested bookmarks: the bookmark of the page 0 has the bookmarks of the pages 1 and 2 as its children
    PDFDocument document = createRichDocument(description);
    {
        PDFDocumentBuilder builder(&document);
        const std::vector<PDFObjectReference> pages = builder.getPages();

        auto createItem = [&pages](const QString& title, size_t pageIndex)
        {
            QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
            item->setTitle(title);
            item->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(pages.at(pageIndex)), PDFDestination())));
            return item;
        };

        PDFOutlineItem root;
        QSharedPointer<PDFOutlineItem> item0 = createItem("Page 0", 0);
        item0->addChild(createItem("Page 1", 1));
        item0->addChild(createItem("Page 2", 2));
        root.addChild(item0);
        root.addChild(createItem("Page 1 again", 1));
        builder.setOutline(&root);
        document = read(write(builder.build()));
    }
    QCOMPARE(getPageObjectCount(document), 3);

    // Page 1 is deleted: its field, named destination and bookmarks are removed, the page is not in the file
    PDFDocument deleted = document;
    QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 1 }));
    for (const PDFDocument& result : { deleted, read(write(deleted)) })
    {
        QCOMPARE(getPageWidths(result), std::vector<int>({ 100, 120 }));
        QCOMPARE(getPageObjectCount(result), 2);
        QCOMPARE(getFieldNames(result), QStringList({ "field0", "field2" }));
        QCOMPARE(getNamedDestinationPageWidths(result), (std::map<QByteArray, int>{ { "destination0", 100 }, { "destination2", 120 } }));
        QCOMPARE(getTopLevelOutlineTitles(result), QStringList({ "Page 0" }));

        QSharedPointer<PDFOutlineItem> root = result.getCatalog()->getOutlineRootPtr();
        QCOMPARE(root->getChild(0)->getChildCount(), size_t(1));
        QCOMPARE(root->getChild(0)->getChild(0)->getTitle(), QString("Page 2"));
    }

    // Page 0 is deleted: its bookmark stays as a heading of the remaining bookmark
    deleted = document;
    QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 0 }));
    QCOMPARE(getPageObjectCount(read(write(deleted))), 2);
    QCOMPARE(getFieldNames(deleted), QStringList({ "field1", "field2" }));
    QCOMPARE(getTopLevelOutlineTitles(deleted), QStringList({ "Page 0", "Page 1 again" }));
    QSharedPointer<PDFOutlineItem> root = deleted.getCatalog()->getOutlineRootPtr();
    QVERIFY(!dynamic_cast<const PDFActionGoTo*>(root->getChild(0)->getAction()));
    QCOMPARE(root->getChild(0)->getChildCount(), size_t(2));

    // Moved pages keep everything
    PDFDocument moved = document;
    QVERIFY(PDFPageOrganizer::movePages(&moved, { 0 }, 3));
    QCOMPARE(getPageObjectCount(moved), 3);
    QCOMPARE(getFieldNames(moved), QStringList({ "field0", "field1", "field2" }));
    QCOMPARE(getNamedDestinationPageWidths(moved).size(), size_t(3));
    QCOMPARE(getTopLevelOutlineTitles(moved), QStringList({ "Page 0", "Page 1 again" }));
}

void PageOrganizerTest::outlineWithNamedDestinationsSurvivesDelete()
{
    RichDocument description;
    description.pageCount = 3;
    description.namedDestinations = { { "destination0", 0 }, { "destination1", 1 } };

    PDFDocument document = createRichDocument(description);
    {
        PDFDocumentBuilder builder(&document);

        auto createItem = [](const QString& title, const QByteArray& name)
        {
            QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
            item->setTitle(title);
            item->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createNamed(name), PDFDestination())));
            return item;
        };

        PDFOutlineItem root;
        root.addChild(createItem("Page 0", "destination0"));
        root.addChild(createItem("Page 1", "destination1"));
        builder.setOutline(&root);
        document = read(write(builder.build()));
    }
    QCOMPARE(getTopLevelOutlineTitles(document), QStringList({ "Page 0", "Page 1" }));

    // Bookmarks with the named destinations are not removed with an unrelated page
    PDFDocument deleted = document;
    QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 2 }));
    QCOMPARE(getTopLevelOutlineTitles(deleted), QStringList({ "Page 0", "Page 1" }));

    // ... they are removed with their page
    deleted = document;
    QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 1 }));
    QCOMPARE(getTopLevelOutlineTitles(deleted), QStringList({ "Page 0" }));
    QCOMPARE(getNamedDestinationPageWidths(deleted), (std::map<QByteArray, int>{ { "destination0", 100 } }));
}

void PageOrganizerTest::extractedDocumentHasOnlyItsFields()
{
    RichDocument description;
    description.pageCount = 4;
    description.title = "Plan set";
    description.fields = { { "field0", 0 }, { "field1", 1 }, { "field2", 2 }, { "field3", 3 } };
    description.namedDestinations = { { "destination0", 0 }, { "destination3", 3 } };

    const PDFDocument document = createRichDocument(description);

    PDFDocument extracted;
    QVERIFY(PDFPageOrganizer::extractPages(&document, { 3, 1 }, &extracted));

    const PDFDocument reread = read(write(extracted));
    QCOMPARE(getPageWidths(reread), std::vector<int>({ 130, 110 }));
    QCOMPARE(getPageObjectCount(reread), 2);
    QCOMPARE(getFieldNames(reread), QStringList({ "field1", "field3" }));
    QCOMPARE(getNamedDestinationPageWidths(reread), (std::map<QByteArray, int>{ { "destination3", 130 } }));

    // The source document is not changed
    QCOMPARE(getFieldNames(document).size(), qsizetype(4));
    QCOMPARE(getPageObjectCount(document), 4);
}

void PageOrganizerTest::pageLabelsFollowPages()
{
    RichDocument description;
    description.pageCount = 4;
    description.pageLabels = { { "C-", 0 }, { "E-", 2 } };

    const PDFDocument document = createRichDocument(description);
    QCOMPARE(getPageLabels(document), QStringList({ "C-1", "C-2", "E-1", "E-2" }));

    PDFDocument moved = document;
    QVERIFY(PDFPageOrganizer::movePages(&moved, { 0 }, 4));
    QCOMPARE(getPageLabels(moved), QStringList({ "C-2", "E-1", "E-2", "C-1" }));
    QCOMPARE(getPageLabels(read(write(moved))), QStringList({ "C-2", "E-1", "E-2", "C-1" }));

    PDFDocument deleted = document;
    QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 1, 2 }));
    QCOMPARE(getPageLabels(deleted), QStringList({ "C-1", "E-2" }));

    // Inserted pages keep their labels, pages without labels are numbered by their position
    RichDocument insertedDescription;
    insertedDescription.pageCount = 2;
    insertedDescription.firstWidth = 500;
    insertedDescription.pageLabels = { { "L-", 0 } };

    PDFDocument inserted = document;
    const PDFDocument insertedDocument = createRichDocument(insertedDescription);
    QVERIFY(PDFPageOrganizer::insertDocument(&inserted, &insertedDocument, 1));
    QCOMPARE(getPageLabels(inserted), QStringList({ "C-1", "L-1", "L-2", "C-2", "E-1", "E-2" }));

    PDFDocument insertedPlain = document;
    const PDFDocument plainDocument = createDocument(1, 700);
    QVERIFY(PDFPageOrganizer::insertDocument(&insertedPlain, &plainDocument, 2));
    QCOMPARE(getPageLabels(insertedPlain), QStringList({ "C-1", "C-2", "3", "E-1", "E-2" }));

    PDFDocument blank = document;
    QVERIFY(PDFPageOrganizer::insertBlankPages(&blank, 0, 1));
    QCOMPARE(getPageLabels(blank), QStringList({ "1", "C-1", "C-2", "E-1", "E-2" }));

    // Document without page labels gets none
    PDFDocument plain = createDocument(3);
    QVERIFY(PDFPageOrganizer::movePages(&plain, { 0 }, 3));
    const PDFDictionary* catalogDictionary = plain.getDictionaryFromObject(plain.getTrailerDictionary()->get("Root"));
    QVERIFY(!catalogDictionary->hasKey("PageLabels"));
}

void PageOrganizerTest::realDocument()
{
    // Organizes the pages of a real document, the test is run only, when PDF4QT_PAGE_ORGANIZER_TEST_FILE is set
    const QString fileName = qEnvironmentVariable("PDF4QT_PAGE_ORGANIZER_TEST_FILE");
    if (fileName.isEmpty())
    {
        QSKIP("PDF4QT_PAGE_ORGANIZER_TEST_FILE is not set.");
    }

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromFile(fileName);
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    const PDFInteger pageCount = PDFInteger(document.getCatalog()->getPageCount());
    const std::vector<int> pageWidths = getPageWidths(document);
    const QStringList fieldNames = getFieldNames(document);
    const size_t namedDestinationCount = document.getCatalog()->getNamedDestinations().size();
    QElapsedTimer timer;

    // Insertion of the document into itself, all pages twice
    PDFDocument inserted = document;
    timer.start();
    QVERIFY(PDFPageOrganizer::insertDocument(&inserted, &document, pageCount / 2));
    qInfo() << "insert:" << timer.elapsed() << "ms, pages:" << pageCount;
    QCOMPARE(PDFInteger(inserted.getCatalog()->getPageCount()), 2 * pageCount);
    QCOMPARE(inserted.getInfo()->title, document.getInfo()->title);
    QVERIFY(getFieldNames(inserted).size() >= fieldNames.size());
    QVERIFY(inserted.getCatalog()->getNamedDestinations().size() >= namedDestinationCount);

    PDFDocument reread = read(write(inserted));
    QCOMPARE(PDFInteger(reread.getCatalog()->getPageCount()), 2 * pageCount);
    QCOMPARE(getPageObjectCount(reread), int(2 * pageCount));

    // The first page is moved to the end
    PDFDocument moved = document;
    timer.start();
    QVERIFY(PDFPageOrganizer::movePages(&moved, { 0 }, pageCount));
    qInfo() << "move:" << timer.elapsed() << "ms";
    std::vector<int> movedPageWidths = pageWidths;
    std::rotate(movedPageWidths.begin(), movedPageWidths.begin() + 1, movedPageWidths.end());
    QCOMPARE(getPageWidths(moved), movedPageWidths);
    QCOMPARE(getFieldNames(moved), fieldNames);
    QCOMPARE(moved.getCatalog()->getNamedDestinations().size(), namedDestinationCount);
    QCOMPARE(getPageObjectCount(read(write(moved))), int(pageCount));

    // The first page is deleted
    if (pageCount > 1)
    {
        PDFDocument deleted = document;
        timer.start();
        QVERIFY(PDFPageOrganizer::deletePages(&deleted, { 0 }));
        qInfo() << "delete:" << timer.elapsed() << "ms";
        QCOMPARE(PDFInteger(deleted.getCatalog()->getPageCount()), pageCount - 1);

        const QByteArray data = write(deleted);
        reread = read(data);
        QCOMPARE(PDFInteger(reread.getCatalog()->getPageCount()), pageCount - 1);
        QCOMPARE(getPageObjectCount(reread), int(pageCount - 1));
        qInfo() << "size of the file without the first page:" << data.size() << "bytes, original" << QFileInfo(fileName).size() << "bytes";
    }
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
