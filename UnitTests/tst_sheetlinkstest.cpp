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

#include "pdfsheetlinks.h"
#include "pdfredact.h"
#include "pdfdocumentbuilder.h"
#include "pdfannotation.h"
#include "pdfaction.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfoptionalcontent.h"
#include "pdfdocumentreader.h"
#include "pdfocrpagepreparer.h"

#include <QtTest>

using namespace pdf;

/// Links of the sheet references of a plan set (PDFSheetLinks)
class SheetLinksTest : public QObject
{
    Q_OBJECT

private slots:
    void sheetNumbers();
    void sheetsFromTitleBlocks();
    void sheetsFromPageLabels();
    void linksToOtherSheets();
    void createdLinksGoToSheets();
    void foreignLinksAreKept();
    void realDocument();

private:
    using Entries = std::vector<std::pair<QByteArray, PDFObject>>;

    static PDFObject createDictionary(Entries entries);
    static PDFObjectReference addPage(PDFDocumentBuilder* builder, QRectF mediaBox, QByteArray content);
    static PDFDocument createPlanSet();
    static PDFTextLayoutStorage getTextLayouts(const PDFDocument& document);
    static QStringList getNumbers(const PDFSheetLinks::Sheets& sheets);
};

PDFObject SheetLinksTest::createDictionary(Entries entries)
{
    PDFDictionaryBuilder dictionary;
    for (auto& entry : entries)
    {
        dictionary.addEntry(PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
    }
    return PDFObject::createDictionary(std::move(dictionary));
}

PDFObjectReference SheetLinksTest::addPage(PDFDocumentBuilder* builder, QRectF mediaBox, QByteArray content)
{
    PDFObjectReference font = builder->addObject(createDictionary({ { "Type", PDFObject::createName("Font") },
                                                                    { "Subtype", PDFObject::createName("Type1") },
                                                                    { "BaseFont", PDFObject::createName("Helvetica") },
                                                                    { "Encoding", PDFObject::createName("WinAnsiEncoding") } }));

    PDFDictionaryBuilder streamDictionary;
    streamDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    PDFObjectReference contentReference = builder->addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(content))));

    PDFObjectReference page = builder->appendPage(mediaBox);
    builder->mergeTo(page, createDictionary({ { "Resources", createDictionary({ { "Font", createDictionary({ { "F1", PDFObject::createReference(font) } }) } }) },
                                              { "Contents", PDFObject::createReference(contentReference) } }));
    return page;
}

PDFDocument SheetLinksTest::createPlanSet()
{
    // Landscape sheets 1224 x 792 (17 x 11 in) with the title block in the bottom right corner
    PDFDocumentBuilder builder;
    const QRectF sheet(0, 0, 1224, 792);

    // 0: cover sheet with the sheet index
    addPage(&builder, sheet, "BT /F1 12 Tf 100 600 Td (E-101 LIGHTING PLAN) Tj ET\n"
                             "BT /F1 12 Tf 100 580 Td (E-102 PHOTOMETRIC PLAN) Tj ET\n"
                             "BT /F1 12 Tf 100 560 Td (E-105 DETAILS) Tj ET\n"
                             "BT /F1 24 Tf 1100 40 Td (E-001) Tj ET\n");

    // 1: references in the notes, a fixture tag, a word which only contains a sheet number,
    //    a small revision number in the title block, an ambiguous sheet number
    addPage(&builder, sheet, "BT /F1 10 Tf 200 400 Td (SEE SHEET E-102 FOR CALCULATIONS) Tj ET\n"
                             "BT /F1 10 Tf 200 380 Td (TYPE SA1 POLE, OFFICE-E102, E-102.5.3, X-1) Tj ET\n"
                             "BT /F1 8 Tf 1000 60 Td (R-2) Tj ET\n"
                             "BT /F1 24 Tf 1100 40 Td (E-101) Tj ET\n");

    // 2: photometric plan referencing the lighting plan; a reference and a road number in large
    //    letters in the corner are not the sheet number, the sheet number is below its label
    addPage(&builder, sheet, "BT /F1 10 Tf 200 400 Td (MATCH LINE SEE E-101) Tj ET\n"
                             "BT /F1 30 Tf 850 100 Td (SEE SHEET E-105) Tj ET\n"
                             "BT /F1 30 Tf 850 160 Td (ROUTE T-388 WIDENING) Tj ET\n"
                             "BT /F1 8 Tf 1100 70 Td (SHEET NO.) Tj ET\n"
                             "BT /F1 24 Tf 1100 40 Td (E-102) Tj ET\n");

    // 3, 4: the same sheet number twice (copied sheet), it is not linked
    addPage(&builder, sheet, "BT /F1 24 Tf 1100 40 Td (X-1) Tj ET\n");
    addPage(&builder, sheet, "BT /F1 24 Tf 1100 40 Td (X-1) Tj ET\n");

    // 5: portrait page displayed as landscape (rotated by 90 degrees clockwise), the
    //    bottom right corner of the displayed sheet is the top right corner of the page;
    //    a sheet number in the bottom right corner of the page is not the title block
    PDFObjectReference rotated = addPage(&builder, QRectF(0, 0, 792, 1224), "BT /F1 24 Tf 700 1180 Td (E-105) Tj ET\n"
                                                                             "BT /F1 30 Tf 700 40 Td (Z-9) Tj ET\n");
    builder.setPageRotation(rotated, PageRotation::Rotate90);

    return builder.build();
}

PDFTextLayoutStorage SheetLinksTest::getTextLayouts(const PDFDocument& document)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &activity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);
    return PDFRedactSearch::createTextLayouts(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFMeshQualitySettings());
}

QStringList SheetLinksTest::getNumbers(const PDFSheetLinks::Sheets& sheets)
{
    QStringList numbers;
    for (const PDFSheetLinks::Sheet& sheet : sheets)
    {
        numbers << sheet.number;
    }
    return numbers;
}

void SheetLinksTest::sheetNumbers()
{
    for (const char* number : { "E-101", "E101", "LT-2", "LT2", "A1.01", "E-3B", "E.1", "FP-1" })
    {
        QVERIFY2(PDFSheetLinks::isSheetNumber(number), number);
    }

    for (const char* text : { "101", "E", "e-101", "LIGHT-E-101", "E-1011", "E-101-2", "ABCD-1", "SEE E-101" })
    {
        QVERIFY2(!PDFSheetLinks::isSheetNumber(text), text);
    }
}

void SheetLinksTest::sheetsFromTitleBlocks()
{
    const PDFDocument document = createPlanSet();
    const PDFSheetLinks::Sheets sheets = PDFSheetLinks::findSheets(&document, getTextLayouts(document));

    QCOMPARE(getNumbers(sheets), QStringList({ "E-001", "E-101", "E-102", "X-1", "X-1", "E-105" }));
    QVERIFY(!sheets[1].fromPageLabel);
    QVERIFY(sheets[1].rectangle.left() > 1000);
}

void SheetLinksTest::sheetsFromPageLabels()
{
    PDFDocumentBuilder builder;
    addPage(&builder, QRectF(0, 0, 1224, 792), "BT /F1 24 Tf 1100 40 Td (E-101) Tj ET\n");
    addPage(&builder, QRectF(0, 0, 1224, 792), "BT /F1 24 Tf 1100 40 Td (E-102) Tj ET\n");
    addPage(&builder, QRectF(0, 0, 1224, 792), "BT /F1 24 Tf 1100 40 Td (E-103) Tj ET\n");

    // AutoCAD writes the index and the name of the layout, the number is the last word;
    // a page label without a sheet number leaves the title block
    PDFArrayBuilder nums;
    nums.appendItem(PDFObject::createInteger(0));
    nums.appendItem(createDictionary({ { "P", PDFObject::createString("[1] Light LT-1") } }));
    nums.appendItem(PDFObject::createInteger(1));
    nums.appendItem(createDictionary({ { "P", PDFObject::createString("LT-2") } }));
    nums.appendItem(PDFObject::createInteger(2));
    nums.appendItem(createDictionary({ { "P", PDFObject::createString("Cover") } }));
    builder.mergeTo(builder.getCatalogReference(), createDictionary({ { "PageLabels", createDictionary({ { "Nums", PDFObject::createArray(std::move(nums)) } }) } }));

    const PDFDocument document = builder.build();
    const PDFSheetLinks::Sheets sheets = PDFSheetLinks::findSheets(&document, getTextLayouts(document));
    QCOMPARE(getNumbers(sheets), QStringList({ "LT-1", "LT-2", "E-103" }));
    QVERIFY(sheets[0].fromPageLabel);
    QVERIFY(sheets[1].fromPageLabel);
    QVERIFY(!sheets[2].fromPageLabel);
}

void SheetLinksTest::linksToOtherSheets()
{
    const PDFDocument document = createPlanSet();
    const PDFTextLayoutStorage storage = getTextLayouts(document);
    const PDFSheetLinks::Sheets sheets = PDFSheetLinks::findSheets(&document, storage);
    const PDFSheetLinks::Links links = PDFSheetLinks::findLinks(&document, storage, sheets);

    QStringList found;
    for (const PDFSheetLinks::Link& link : links)
    {
        found << QString("%1:%2->%3").arg(link.pageIndex).arg(link.text).arg(link.targetPageIndex);
        QVERIFY(link.rectangle.isValid());
    }
    found.sort();

    // Sheet index of the cover, the notes of the plans; no link to the own sheet, no link of the
    // ambiguous X-1, of a number inside of a word and of a number with more parts
    QCOMPARE(found, QStringList({ "0:E-101->1", "0:E-102->2", "0:E-105->5", "1:E-102->2", "2:E-101->1", "2:E-105->5" }));
}

void SheetLinksTest::createdLinksGoToSheets()
{
    const PDFDocument document = createPlanSet();
    const PDFTextLayoutStorage storage = getTextLayouts(document);
    const PDFSheetLinks::Links links = PDFSheetLinks::findLinks(&document, storage, PDFSheetLinks::findSheets(&document, storage));

    PDFDocumentBuilder builder(&document);
    QCOMPARE(PDFSheetLinks::createLinks(&builder, &document, links), int(links.size()));
    const PDFDocument linked = builder.build();
    QCOMPARE(PDFSheetLinks::getLinkCount(&linked), int(links.size()));

    const PDFCatalog* catalog = linked.getCatalog();
    const PDFObjectStorage* storagePtr = &linked.getStorage();
    int checked = 0;
    for (const PDFObjectReference& reference : catalog->getPage(1)->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(storagePtr, reference);
        QVERIFY(annotation);
        QCOMPARE(annotation->getType(), AnnotationType::Link);

        const PDFLinkAnnotation* link = static_cast<const PDFLinkAnnotation*>(annotation.data());
        const PDFActionGoTo* action = dynamic_cast<const PDFActionGoTo*>(link->getAction());
        QVERIFY(action);
        QCOMPARE(action->getDestination().getPageReference(), catalog->getPage(2)->getPageReference());
        QCOMPARE(action->getDestination().getDestinationType(), DestinationType::Fit);

        // No border, the reference stays readable
        const PDFDictionary* dictionary = storagePtr->getDictionaryFromObject(storagePtr->getObjectByReference(reference));
        const PDFArray* border = storagePtr->getObject(dictionary->get("Border")).getArray();
        QVERIFY(border);
        QCOMPARE(border->getCount(), size_t(3));
        QCOMPARE(border->getItem(2).getInteger(), PDFInteger(0));
        QVERIFY(link->getRectangle().left() > 150 && link->getRectangle().left() < 400);
        ++checked;
    }
    QCOMPARE(checked, 1);

    // Removal leaves the document without the sheet links
    PDFDocumentBuilder removeBuilder(&linked);
    QCOMPARE(PDFSheetLinks::removeLinks(&removeBuilder, &linked), int(links.size()));
    const PDFDocument unlinked = removeBuilder.build();
    QCOMPARE(PDFSheetLinks::getLinkCount(&unlinked), 0);
    QVERIFY(unlinked.getCatalog()->getPage(1)->getAnnotations().empty());
}

void SheetLinksTest::foreignLinksAreKept()
{
    const PDFDocument document = createPlanSet();
    const PDFTextLayoutStorage storage = getTextLayouts(document);
    const PDFSheetLinks::Sheets sheets = PDFSheetLinks::findSheets(&document, storage);

    // A link made by hand over the reference on the photometric plan
    PDFSheetLinks::Links links = PDFSheetLinks::findLinks(&document, storage, sheets);
    auto it = std::find_if(links.cbegin(), links.cend(), [](const PDFSheetLinks::Link& link) { return link.pageIndex == 2; });
    QVERIFY(it != links.cend());

    PDFDocumentBuilder builder(&document);
    builder.createAnnotationLink(document.getCatalog()->getPage(2)->getPageReference(), it->rectangle.adjusted(-2, -2, 2, 2), QString("https://example.com"), LinkHighlightMode::Invert);
    const PDFDocument withForeignLink = builder.build();

    // The covered reference is not linked again, and removal of the sheet links keeps the link
    const PDFSheetLinks::Links newLinks = PDFSheetLinks::findLinks(&withForeignLink, storage, sheets);
    QCOMPARE(newLinks.size(), links.size() - 1);

    PDFDocumentBuilder removeBuilder(&withForeignLink);
    QCOMPARE(PDFSheetLinks::removeLinks(&removeBuilder, &withForeignLink), 0);
    QCOMPARE(removeBuilder.build().getCatalog()->getPage(2)->getAnnotations().size(), size_t(1));
}

void SheetLinksTest::realDocument()
{
    // Sheet numbers and references of a real plan set, PDF4QT_SHEET_LINKS_TEST_FILE=<file>
    const QString fileName = qEnvironmentVariable("PDF4QT_SHEET_LINKS_TEST_FILE");
    if (fileName.isEmpty())
    {
        QSKIP("PDF4QT_SHEET_LINKS_TEST_FILE is not set");
    }

    PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    const PDFDocument document = reader.readFromFile(fileName);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    QElapsedTimer timer;
    timer.start();
    const PDFTextLayoutStorage storage = getTextLayouts(document);
    const qint64 layoutTime = timer.restart();
    const PDFSheetLinks::Sheets sheets = PDFSheetLinks::findSheets(&document, storage);
    const PDFSheetLinks::Links links = PDFSheetLinks::findLinks(&document, storage, sheets);
    const qint64 linkTime = timer.elapsed();

    for (size_t i = 0; i < sheets.size(); ++i)
    {
        int outgoing = 0;
        for (const PDFSheetLinks::Link& link : links)
        {
            outgoing += link.pageIndex == PDFInteger(i) ? 1 : 0;
        }

        qInfo().noquote() << QString("Page %1 label '%2': sheet '%3' (%4), %5 link(s)").arg(i + 1)
                             .arg(PDFOCRPagePreparer::getPageLabel(&document, PDFInteger(i)), sheets[i].number,
                                  sheets[i].fromPageLabel ? QStringLiteral("page label") : QStringLiteral("title block")).arg(outgoing);
    }
    for (const PDFSheetLinks::Link& link : links)
    {
        qInfo().noquote() << QString("  page %1: '%2' -> page %3").arg(link.pageIndex + 1).arg(link.text).arg(link.targetPageIndex + 1);
    }
    qInfo().noquote() << QString("Text layouts %1 ms, sheets and links %2 ms").arg(layoutTime).arg(linkTime);
}

QTEST_MAIN(SheetLinksTest)

#include "tst_sheetlinkstest.moc"
