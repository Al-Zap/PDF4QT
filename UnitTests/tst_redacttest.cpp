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


#include "pdfredact.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdftextlayoutgenerator.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfconstants.h"
#include "pdfannotation.h"
#include "pdfform.h"
#include "pdfexecutionpolicy.h"
#include "pdfimage.h"

#include <QtTest>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryDir>
#include <QCoreApplication>

class RedactTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Search
    void test_patterns_data();
    void test_patterns();
    void test_createExpression();
    void test_find_quadrilaterals();
    void test_createMarks();

    // Apply
    void test_text_removed();
    void test_text_positions_kept();
    void test_text_scaling_spacing_rotation();
    void test_text_word_spacing_space_removed();
    void test_invisible_text_removed();
    void test_form_xobject_content_removed();
    void test_image_pixels_removed();
    void test_image_covered_removed();
    void test_image_jpeg_kept_as_jpeg();
    void test_paths_cut();
    void test_shading_clipped();
    void test_annotations_and_fields_removed();
    void test_untouched_pages_kept();
    void test_fill_colors();
    void test_marks_removed_and_statistics();
    void test_empty_mark_removed();
    void test_document_data_options();
    void test_structure_tree_removed();
    void test_page_marks();
    void test_no_marks();
    void test_write_read_round_trip();
    void test_secret_not_in_written_file();

    // Command line
    void test_pdftool();

private:
    struct TestDocument
    {
        pdf::PDFDocument document;
        pdf::PDFObjectReference page1;
        pdf::PDFObjectReference page2;
        pdf::PDFObjectReference page1Content;
        pdf::PDFObjectReference page2Content;
        pdf::PDFObjectReference image;
        pdf::PDFObjectReference form;
        pdf::PDFObjectReference note;
        pdf::PDFObjectReference link;
        pdf::PDFObjectReference widget;
    };

    using Entries = std::vector<std::pair<QByteArray, pdf::PDFObject>>;

    static pdf::PDFObject createDictionary(Entries entries);
    static pdf::PDFObject createRealArray(std::vector<pdf::PDFReal> numbers);
    static pdf::PDFObject createStream(Entries entries, QByteArray content);
    static pdf::PDFObject createFontResources(pdf::PDFDocumentBuilder* builder);

    /// Creates a document with a single page (612 x 792) with the given content
    static pdf::PDFDocument createSimpleDocument(QByteArray content, Entries extraResources = Entries());

    /// Creates the test document - page one contains the secrets, page two is not redacted
    static TestDocument createTestDocument(bool tagged = false);

    /// Adds redaction marks to the page
    static pdf::PDFDocument addMarks(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex, const std::vector<QRectF>& rectangles, QColor color = Qt::black);

    /// Applies the marks
    static pdf::PDFDocument redact(const pdf::PDFDocument& document, QColor color, pdf::PDFRedact::Options options = pdf::PDFRedact::KeepDocumentData, pdf::PDFRedact::Statistics* statistics = nullptr, QStringList* errors = nullptr);

    static pdf::PDFTextLayoutStorage getTextLayouts(const pdf::PDFDocument& document);
    static QString getPageText(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex);

    /// Returns bounding rectangles of the characters of the page (with the characters)
    static std::vector<std::pair<QChar, QRectF>> getCharacters(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex);

    /// Returns bounding box of the first occurrence of the word on the page
    static QRectF getWordBoundingBox(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex, const QString& word);

    /// Decomposes the page into the edited content elements
    static pdf::PDFEditedPageContent getContent(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex);

    /// Returns true, if any object of the document (decoded stream, string, name) contains the text
    static bool documentContains(const pdf::PDFDocument& document, const QByteArray& text);

    static pdf::PDFDocument readBack(const pdf::PDFDocument& document, QByteArray* data = nullptr);
    static QString getPdfToolPath();
};

void RedactTest::initTestCase()
{
    pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::SingleThreaded);
}

pdf::PDFObject RedactTest::createDictionary(Entries entries)
{
    pdf::PDFDictionaryBuilder dictionary;
    for (auto& entry : entries)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
    }
    return pdf::PDFObject::createDictionary(std::move(dictionary));
}

pdf::PDFObject RedactTest::createRealArray(std::vector<pdf::PDFReal> numbers)
{
    pdf::PDFArrayBuilder array;
    for (pdf::PDFReal number : numbers)
    {
        array.appendItem(pdf::PDFObject::createReal(number));
    }
    return pdf::PDFObject::createArray(std::move(array));
}

pdf::PDFObject RedactTest::createStream(Entries entries, QByteArray content)
{
    pdf::PDFDictionaryBuilder dictionary;
    for (auto& entry : entries)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
    }
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
    return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
}

pdf::PDFObject RedactTest::createFontResources(pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFObjectReference font = builder->addObject(createDictionary({ { "Type", pdf::PDFObject::createName("Font") },
                                                                         { "Subtype", pdf::PDFObject::createName("Type1") },
                                                                         { "BaseFont", pdf::PDFObject::createName("Helvetica") },
                                                                         { "Encoding", pdf::PDFObject::createName("WinAnsiEncoding") } }));
    return createDictionary({ { "F1", pdf::PDFObject::createReference(font) } });
}

pdf::PDFDocument RedactTest::createSimpleDocument(QByteArray content, Entries extraResources)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    pdf::PDFObjectReference contentReference = builder.addObject(createStream({ }, std::move(content)));

    Entries resources = { { "Font", createFontResources(&builder) } };
    for (auto& entry : extraResources)
    {
        resources.push_back(std::move(entry));
    }

    builder.mergeTo(page, createDictionary({ { "Resources", createDictionary(std::move(resources)) },
                                             { "Contents", pdf::PDFObject::createReference(contentReference) } }));
    return builder.build();
}

RedactTest::TestDocument RedactTest::createTestDocument(bool tagged)
{
    TestDocument result;

    pdf::PDFDocumentBuilder builder;
    result.page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    result.page2 = builder.appendPage(QRectF(0, 0, 612, 792));

    pdf::PDFObject fonts = createFontResources(&builder);

    // Image - 20 x 20 green pixels, not compressed
    QByteArray imageData;
    for (int i = 0; i < 20 * 20; ++i)
    {
        imageData.append(char(0x00));
        imageData.append('\xFF');
        imageData.append(char(0x00));
    }
    result.image = builder.addObject(createStream({ { "Type", pdf::PDFObject::createName("XObject") },
                                                    { "Subtype", pdf::PDFObject::createName("Image") },
                                                    { "Width", pdf::PDFObject::createInteger(20) },
                                                    { "Height", pdf::PDFObject::createInteger(20) },
                                                    { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                    { "BitsPerComponent", pdf::PDFObject::createInteger(8) } }, imageData));

    // Form with a secret text
    result.form = builder.addObject(createStream({ { "Type", pdf::PDFObject::createName("XObject") },
                                                   { "Subtype", pdf::PDFObject::createName("Form") },
                                                   { "BBox", createRealArray({ 0, 0, 612, 792 }) },
                                                   { "Resources", createDictionary({ { "Font", fonts } }) } },
                                                 "BT /F1 12 Tf 72 300 Td (FORMSECRET) Tj ET"));

    QByteArray content1 = "BT /F1 12 Tf 72 700 Td (Name: John Smith SSN 123-45-6789 end) Tj ET\n"
                          "BT /F1 12 Tf 72 650 Td (Public line stays) Tj ET\n"
                          "1 0 0 rg 72 500 100 50 re f\n"
                          "0 0 1 RG 2 w 50 420 m 550 420 l S\n"
                          "q 100 0 0 100 300 500 cm /Im1 Do Q\n"
                          "/Fm1 Do\n"
                          "q BT 3 Tr /F1 12 Tf 300 300 Td (OCRSECRET) Tj ET Q\n"
                          "BT /F1 8 Tf 40 150 Td (Mail john@example.com phone 555-123-4567 date 03/14/2025 card 4111 1111 1111 1111) Tj ET\n";
    QByteArray content2 = "BT /F1 12 Tf 72 700 Td (Page two untouched) Tj ET\n";

    result.page1Content = builder.addObject(createStream({ }, content1));
    result.page2Content = builder.addObject(createStream({ }, content2));

    result.note = builder.addObject(createDictionary({ { "Type", pdf::PDFObject::createName("Annot") },
                                                       { "Subtype", pdf::PDFObject::createName("Text") },
                                                       { "Rect", createRealArray({ 80, 290, 100, 310 }) },
                                                       { "Contents", pdf::PDFObject::createString("NOTESECRET") },
                                                       { "P", pdf::PDFObject::createReference(result.page1) } }));
    result.link = builder.addObject(createDictionary({ { "Type", pdf::PDFObject::createName("Annot") },
                                                       { "Subtype", pdf::PDFObject::createName("Link") },
                                                       { "Rect", createRealArray({ 72, 640, 200, 662 }) },
                                                       { "P", pdf::PDFObject::createReference(result.page1) } }));
    result.widget = builder.addObject(createDictionary({ { "Type", pdf::PDFObject::createName("Annot") },
                                                         { "Subtype", pdf::PDFObject::createName("Widget") },
                                                         { "FT", pdf::PDFObject::createName("Tx") },
                                                         { "T", pdf::PDFObject::createString("SecretField") },
                                                         { "V", pdf::PDFObject::createString("FIELDSECRET") },
                                                         { "Rect", createRealArray({ 300, 100, 450, 120 }) },
                                                         { "P", pdf::PDFObject::createReference(result.page1) } }));

    pdf::PDFArrayBuilder annotations;
    annotations.appendItem(pdf::PDFObject::createReference(result.note));
    annotations.appendItem(pdf::PDFObject::createReference(result.link));
    annotations.appendItem(pdf::PDFObject::createReference(result.widget));

    Entries page1Entries = { { "Resources", createDictionary({ { "Font", fonts },
                                                               { "XObject", createDictionary({ { "Im1", pdf::PDFObject::createReference(result.image) },
                                                                                               { "Fm1", pdf::PDFObject::createReference(result.form) } }) } }) },
                             { "Contents", pdf::PDFObject::createReference(result.page1Content) },
                             { "Annots", pdf::PDFObject::createArray(std::move(annotations)) } };
    if (tagged)
    {
        page1Entries.emplace_back("StructParents", pdf::PDFObject::createInteger(0));
    }
    builder.mergeTo(result.page1, createDictionary(std::move(page1Entries)));
    builder.mergeTo(result.page2, createDictionary({ { "Resources", createDictionary({ { "Font", fonts } }) },
                                                     { "Contents", pdf::PDFObject::createReference(result.page2Content) } }));

    // Form, document information, outline, metadata
    pdf::PDFArrayBuilder fields;
    fields.appendItem(pdf::PDFObject::createReference(result.widget));
    pdf::PDFObjectReference acroForm = builder.addObject(createDictionary({ { "Fields", pdf::PDFObject::createArray(std::move(fields)) } }));

    pdf::PDFObjectReference outlineRoot = builder.addObject(createDictionary({ { "Type", pdf::PDFObject::createName("Outlines") } }));
    pdf::PDFObjectReference outlineItem = builder.addObject(createDictionary({ { "Title", pdf::PDFObject::createString("OUTLINESECRET") },
                                                                               { "Parent", pdf::PDFObject::createReference(outlineRoot) } }));
    builder.mergeTo(outlineRoot, createDictionary({ { "First", pdf::PDFObject::createReference(outlineItem) },
                                                    { "Last", pdf::PDFObject::createReference(outlineItem) },
                                                    { "Count", pdf::PDFObject::createInteger(1) } }));

    Entries catalogEntries = { { "AcroForm", pdf::PDFObject::createReference(acroForm) },
                               { "Outlines", pdf::PDFObject::createReference(outlineRoot) } };

    if (tagged)
    {
        pdf::PDFArrayBuilder kids;
        kids.appendItem(createDictionary({ { "S", pdf::PDFObject::createName("P") },
                                           { "ActualText", pdf::PDFObject::createString("TAGSECRET") },
                                           { "Pg", pdf::PDFObject::createReference(result.page1) } }));
        pdf::PDFObjectReference structTreeRoot = builder.addObject(createDictionary({ { "Type", pdf::PDFObject::createName("StructTreeRoot") },
                                                                                      { "K", pdf::PDFObject::createArray(std::move(kids)) } }));
        catalogEntries.emplace_back("StructTreeRoot", pdf::PDFObject::createReference(structTreeRoot));
        catalogEntries.emplace_back("MarkInfo", createDictionary({ { "Marked", pdf::PDFObject::createBool(true) } }));
    }

    builder.mergeTo(builder.getCatalogReference(), createDictionary(std::move(catalogEntries)));
    builder.setCatalogMetadata("<x:xmpmeta>METASECRET</x:xmpmeta>");
    builder.setDocumentInfo(builder.addObject(createDictionary({ { "Title", pdf::PDFObject::createString("Secret Title") },
                                                                 { "Author", pdf::PDFObject::createString("Secret Author") } })));

    result.document = builder.build();
    return result;
}

pdf::PDFDocument RedactTest::addMarks(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex, const std::vector<QRectF>& rectangles, QColor color)
{
    pdf::PDFDocumentBuilder builder(&document);
    pdf::PDFObjectReference page = document.getCatalog()->getPage(pageIndex)->getPageReference();
    for (const QRectF& rectangle : rectangles)
    {
        pdf::PDFObjectReference annotation = builder.createAnnotationRedact(page, rectangle, color, color);
        builder.updateAnnotationAppearanceStreams(annotation);
    }
    return builder.build();
}

pdf::PDFDocument RedactTest::redact(const pdf::PDFDocument& document, QColor color, pdf::PDFRedact::Options options, pdf::PDFRedact::Statistics* statistics, QStringList* errors)
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(&document), &activity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    pdf::PDFMeshQualitySettings meshQualitySettings;

    pdf::PDFRedact redactor(&document, &fontCache, &iccProfileCache, &cms, &activity, &meshQualitySettings, color);
    pdf::PDFDocument result = redactor.perform(options);

    if (statistics)
    {
        *statistics = redactor.getStatistics();
    }
    if (errors)
    {
        *errors = redactor.getErrors();
    }

    return result;
}

pdf::PDFTextLayoutStorage RedactTest::getTextLayouts(const pdf::PDFDocument& document)
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(&document), &activity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);
    return pdf::PDFRedactSearch::createTextLayouts(&document, &fontCache, &iccProfileCache, &cms, &activity, pdf::PDFMeshQualitySettings());
}

QString RedactTest::getPageText(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex)
{
    pdf::PDFTextLayoutStorage storage = getTextLayouts(document);
    pdf::PDFTextLayout layout = storage.getTextLayout(pageIndex);
    QStringList texts;
    for (const pdf::PDFTextFlow& flow : pdf::PDFTextFlow::createTextFlows(layout, pdf::PDFTextFlow::SeparateBlocks, pageIndex))
    {
        texts << flow.getText();
    }
    return texts.join("\n");
}

std::vector<std::pair<QChar, QRectF>> RedactTest::getCharacters(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex)
{
    std::vector<std::pair<QChar, QRectF>> result;
    pdf::PDFTextLayoutStorage storage = getTextLayouts(document);
    pdf::PDFTextLayout layout = storage.getTextLayout(pageIndex);
    for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                result.emplace_back(character.character, character.boundingBox.boundingRect());
            }
        }
    }
    return result;
}

QRectF RedactTest::getWordBoundingBox(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex, const QString& word)
{
    pdf::PDFTextLayoutStorage storage = getTextLayouts(document);
    pdf::PDFRedactSearch::Settings settings;
    settings.text = word;
    settings.caseSensitive = true;
    pdf::PDFRedactSearch::Matches matches = pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr));
    for (const pdf::PDFRedactSearch::Match& match : matches)
    {
        if (match.pageIndex == pageIndex)
        {
            return match.getBoundingRect();
        }
    }
    return QRectF();
}

pdf::PDFEditedPageContent RedactTest::getContent(const pdf::PDFDocument& document, pdf::PDFInteger pageIndex)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(&document), &activity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    pdf::PDFPageContentEditorProcessor processor(page, &document, &fontCache, &iccProfileCache, &cms, &activity, QTransform(), pdf::PDFMeshQualitySettings());
    processor.processContents();
    return processor.takeEditedPageContent();
}

bool RedactTest::documentContains(const pdf::PDFDocument& document, const QByteArray& text)
{
    std::function<bool(const pdf::PDFObject&)> contains = [&](const pdf::PDFObject& object) -> bool
    {
        switch (object.getType())
        {
            case pdf::PDFObject::Type::String:
            case pdf::PDFObject::Type::Name:
                return object.getString().contains(text);

            case pdf::PDFObject::Type::Array:
                for (const pdf::PDFObject& item : *object.getArray())
                {
                    if (contains(item))
                    {
                        return true;
                    }
                }
                return false;

            case pdf::PDFObject::Type::Dictionary:
            {
                const pdf::PDFDictionary* dictionary = object.getDictionary();
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    if (contains(dictionary->getValue(i)))
                    {
                        return true;
                    }
                }
                return false;
            }

            case pdf::PDFObject::Type::Stream:
            {
                const pdf::PDFStream* stream = object.getStream();
                if (document.getDecodedStream(stream).contains(text))
                {
                    return true;
                }
                for (size_t i = 0; i < stream->getDictionary()->getCount(); ++i)
                {
                    if (contains(stream->getDictionary()->getValue(i)))
                    {
                        return true;
                    }
                }
                return false;
            }

            default:
                return false;
        }
    };

    for (const pdf::PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
    {
        if (contains(entry.object))
        {
            return true;
        }
    }

    return contains(pdf::PDFObject::createDictionary(pdf::PDFDictionaryBuilder(*document.getTrailerDictionary())));
}

pdf::PDFDocument RedactTest::readBack(const pdf::PDFDocument& document, QByteArray* data)
{
    QByteArray buffer;
    {
        QBuffer device(&buffer);
        device.open(QBuffer::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        pdf::PDFOperationResult result = writer.write(&device, &document);
        if (!result)
        {
            qWarning() << result.getErrorMessage();
        }
    }

    if (data)
    {
        *data = buffer;
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    return reader.readFromBuffer(buffer);
}

QString RedactTest::getPdfToolPath()
{
    QDir directory(QCoreApplication::applicationDirPath());
    for (const QString& name : { QStringLiteral("PdfTool"), QStringLiteral("PdfTool.exe") })
    {
        if (directory.exists(name))
        {
            return directory.absoluteFilePath(name);
        }
    }
    return QString();
}

void RedactTest::test_patterns_data()
{
    QTest::addColumn<int>("pattern");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");

    using Pattern = pdf::PDFRedactSearch::Pattern;
    QTest::newRow("ssn") << int(Pattern::SocialSecurityNumber) << "SSN 123-45-6789, not 1123-45-67890 or 12-345-6789" << QStringList({ "123-45-6789" });
    QTest::newRow("phone") << int(Pattern::PhoneNumber) << "Call 555-123-4567 or (610) 555-0100 or +1 800.555.1212, not 12345" << QStringList({ "555-123-4567", "(610) 555-0100", "+1 800.555.1212" });
    QTest::newRow("email") << int(Pattern::EmailAddress) << "Write to john.smith@example.com or a_b@mail.co.uk, not john@ or @example.com" << QStringList({ "john.smith@example.com", "a_b@mail.co.uk" });
    QTest::newRow("card") << int(Pattern::CreditCardNumber) << "Card 4111 1111 1111 1111 or 4111-1111-1111-1111 or 3782 822463 10005, not 4111 1111 1111" << QStringList({ "4111 1111 1111 1111", "4111-1111-1111-1111", "3782 822463 10005" });
    QTest::newRow("date") << int(Pattern::Date) << "Dates 03/14/2025, 3.4.25, 2025-03-14, March 14, 2025 and jan 1 2024, not 123/45/6789" << QStringList({ "03/14/2025", "3.4.25", "2025-03-14", "March 14, 2025", "jan 1 2024" });
}

void RedactTest::test_patterns()
{
    QFETCH(int, pattern);
    QFETCH(QString, text);
    QFETCH(QStringList, expected);

    pdf::PDFRedactSearch::Settings settings;
    settings.pattern = static_cast<pdf::PDFRedactSearch::Pattern>(pattern);

    QString errorMessage;
    QRegularExpression expression = pdf::PDFRedactSearch::createExpression(settings, &errorMessage);
    QVERIFY2(pdf::PDFRedactSearch::isValidExpression(expression), qPrintable(errorMessage));

    QStringList found;
    QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
    while (iterator.hasNext())
    {
        found << iterator.next().captured(0);
    }
    QCOMPARE(found, expected);

    // Names and keys
    QVERIFY(!pdf::PDFRedactSearch::getPatternName(settings.pattern).isEmpty());
    QCOMPARE(pdf::PDFRedactSearch::getPatternByKey(pdf::PDFRedactSearch::getPatternKey(settings.pattern)), settings.pattern);
    QCOMPARE(pdf::PDFRedactSearch::getPatternByKey(pdf::PDFRedactSearch::getPatternKey(settings.pattern).toUpper()), settings.pattern);
}

void RedactTest::test_createExpression()
{
    using Settings = pdf::PDFRedactSearch::Settings;
    QString errorMessage;

    // Empty settings
    QVERIFY(!pdf::PDFRedactSearch::isValidExpression(pdf::PDFRedactSearch::createExpression(Settings(), &errorMessage)));
    QVERIFY(!errorMessage.isEmpty());
    QCOMPARE(pdf::PDFRedactSearch::getPatternByKey("unknown"), pdf::PDFRedactSearch::Pattern::None);
    QVERIFY(pdf::PDFRedactSearch::getPatternExpression(pdf::PDFRedactSearch::Pattern::None).isEmpty());
    QCOMPARE(pdf::PDFRedactSearch::getPatterns().size(), size_t(5));

    // Invalid regular expression
    Settings invalid;
    invalid.text = "(abc";
    invalid.regularExpression = true;
    errorMessage.clear();
    QVERIFY(!pdf::PDFRedactSearch::isValidExpression(pdf::PDFRedactSearch::createExpression(invalid, &errorMessage)));
    QVERIFY(errorMessage.contains("regular expression"));

    // Plain text is escaped
    Settings plain;
    plain.text = "a.b (c)";
    QRegularExpression expression = pdf::PDFRedactSearch::createExpression(plain, nullptr);
    QVERIFY(expression.match("x a.b (c) y").hasMatch());
    QVERIFY(!expression.match("axb (c)").hasMatch());
    QVERIFY(expression.match("A.B (C)").hasMatch());

    // Case sensitivity
    plain.caseSensitive = true;
    expression = pdf::PDFRedactSearch::createExpression(plain, nullptr);
    QVERIFY(!expression.match("A.B (C)").hasMatch());

    // Whole words
    Settings words;
    words.text = "cat";
    QVERIFY(pdf::PDFRedactSearch::createExpression(words, nullptr).match("concatenate").hasMatch());
    words.wholeWords = true;
    QVERIFY(!pdf::PDFRedactSearch::createExpression(words, nullptr).match("concatenate").hasMatch());
    QVERIFY(pdf::PDFRedactSearch::createExpression(words, nullptr).match("the cat sat").hasMatch());

    // Regular expression with whole words
    Settings regex;
    regex.text = "c[a-z]t|dog";
    regex.regularExpression = true;
    regex.wholeWords = true;
    expression = pdf::PDFRedactSearch::createExpression(regex, nullptr);
    QVERIFY(expression.match("a cot").hasMatch());
    QVERIFY(expression.match("a dog").hasMatch());
    QVERIFY(!expression.match("dogs").hasMatch());

    // Text has a priority over the pattern
    Settings both;
    both.text = "John";
    both.pattern = pdf::PDFRedactSearch::Pattern::EmailAddress;
    QVERIFY(pdf::PDFRedactSearch::createExpression(both, nullptr).match("John").hasMatch());
}

void RedactTest::test_find_quadrilaterals()
{
    TestDocument test = createTestDocument();
    pdf::PDFTextLayoutStorage storage = getTextLayouts(test.document);

    pdf::PDFRedactSearch::Settings settings;
    settings.pattern = pdf::PDFRedactSearch::Pattern::SocialSecurityNumber;
    pdf::PDFRedactSearch::Matches matches = pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr));
    QCOMPARE(matches.size(), size_t(1));
    QCOMPARE(matches.front().pageIndex, pdf::PDFInteger(0));
    QCOMPARE(matches.front().matched, QString("123-45-6789"));
    QVERIFY(matches.front().context.contains("John Smith"));
    QCOMPARE(matches.front().quadrilaterals.size(), qsizetype(4));

    // The match lies on the line of the text (baseline 700, font size 12)
    const QRectF boundingRect = matches.front().getBoundingRect();
    QVERIFY(boundingRect.top() < 702.0);
    QVERIFY(boundingRect.bottom() > 706.0);
    QVERIFY(boundingRect.left() > 150.0);
    QVERIFY(boundingRect.width() > 50.0 && boundingRect.width() < 80.0);

    // Multiple occurrences on multiple pages, sorted by the page
    settings = pdf::PDFRedactSearch::Settings();
    settings.text = "e";
    matches = pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr));
    QVERIFY(matches.size() > 5);
    QVERIFY(std::is_sorted(matches.cbegin(), matches.cend(), [](const auto& l, const auto& r) { return l.pageIndex < r.pageIndex; }));
    QCOMPARE(matches.back().pageIndex, pdf::PDFInteger(1));

    // Invisible text is found too (OCR layer)
    settings.text = "OCRSECRET";
    QCOMPARE(pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr)).size(), size_t(1));

    // Invalid expression finds nothing
    QVERIFY(pdf::PDFRedactSearch::find(storage, QRegularExpression()).empty());
}

void RedactTest::test_createMarks()
{
    TestDocument test = createTestDocument();
    pdf::PDFTextLayoutStorage storage = getTextLayouts(test.document);

    pdf::PDFRedactSearch::Settings settings;
    settings.pattern = pdf::PDFRedactSearch::Pattern::EmailAddress;
    pdf::PDFRedactSearch::Matches matches = pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr));
    QCOMPARE(matches.size(), size_t(1));

    // Invalid matches are skipped
    pdf::PDFRedactSearch::Match invalidMatch;
    invalidMatch.pageIndex = 10;
    invalidMatch.quadrilaterals = matches.front().quadrilaterals;
    matches.push_back(invalidMatch);

    pdf::PDFDocumentBuilder builder(&test.document);
    QCOMPARE(pdf::PDFRedactSearch::createMarks(&builder, &test.document, matches, Qt::red), 1);
    pdf::PDFDocument marked = builder.build();
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&marked), 1);
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&test.document), 0);
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(nullptr), 0);

    // The mark has the region of the match and the color
    const pdf::PDFPage* page = marked.getCatalog()->getPage(0);
    bool found = false;
    for (const pdf::PDFObjectReference& reference : page->getAnnotations())
    {
        pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&marked.getStorage(), reference);
        if (annotation && annotation->getType() == pdf::AnnotationType::Redact)
        {
            const pdf::PDFRedactAnnotation* redactAnnotation = dynamic_cast<const pdf::PDFRedactAnnotation*>(annotation.get());
            QVERIFY(redactAnnotation->getRedactionRegion().getPath().boundingRect().contains(matches.front().getBoundingRect().center()));
            QCOMPARE(redactAnnotation->getInteriorColor().size(), size_t(3));
            QCOMPARE(redactAnnotation->getInteriorColor()[0], 1.0);
            found = true;
        }
    }
    QVERIFY(found);
}

void RedactTest::test_text_removed()
{
    TestDocument test = createTestDocument();

    pdf::PDFTextLayoutStorage storage = getTextLayouts(test.document);
    pdf::PDFRedactSearch::Settings settings;
    settings.pattern = pdf::PDFRedactSearch::Pattern::SocialSecurityNumber;
    pdf::PDFRedactSearch::Matches matches = pdf::PDFRedactSearch::find(storage, pdf::PDFRedactSearch::createExpression(settings, nullptr));

    pdf::PDFDocumentBuilder builder(&test.document);
    pdf::PDFRedactSearch::createMarks(&builder, &test.document, matches, Qt::black);
    pdf::PDFDocument marked = builder.build();

    pdf::PDFRedact::Statistics statistics;
    QStringList errors;
    pdf::PDFDocument redacted = redact(marked, Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics, &errors);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));

    const QString text = getPageText(redacted, 0);
    QVERIFY2(!text.contains("123-45-6789"), qPrintable(text));
    QVERIFY(!text.contains("6789"));
    QVERIFY(text.contains("Name: John Smith SSN"));
    QVERIFY(text.contains("end"));
    QVERIFY(text.contains("Public line stays"));
    QVERIFY(text.contains("john@example.com"));
    QCOMPARE(statistics.removedCharacterCount, 11);

    // The secret is not in any object of the document
    QVERIFY(documentContains(marked, "123-45-6789"));
    QVERIFY(!documentContains(redacted, "123-45-6789"));
    QVERIFY(!documentContains(redacted, "6789"));
}

void RedactTest::test_text_positions_kept()
{
    TestDocument test = createTestDocument();
    const QRectF ssn = getWordBoundingBox(test.document, 0, "123-45-6789");
    const QRectF end = getWordBoundingBox(test.document, 0, "end");
    const QRectF name = getWordBoundingBox(test.document, 0, "Name");
    QVERIFY(ssn.isValid() && end.isValid() && name.isValid());

    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { ssn }), Qt::black);

    const QRectF endAfter = getWordBoundingBox(redacted, 0, "end");
    const QRectF nameAfter = getWordBoundingBox(redacted, 0, "Name");
    QVERIFY(endAfter.isValid());
    QVERIFY(qAbs(endAfter.left() - end.left()) < 0.01);
    QVERIFY(qAbs(endAfter.top() - end.top()) < 0.01);
    QVERIFY(qAbs(nameAfter.left() - name.left()) < 0.01);
    QVERIFY(!getPageText(redacted, 0).contains("-45-"));
}

void RedactTest::test_text_scaling_spacing_rotation()
{
    // Horizontal scaling, character spacing and a rotated text
    QByteArray content = "q BT /F1 10 Tf 150 Tz 0.5 Tc 50 600 Td (Alpha CLASSIFIED Omega) Tj ET Q\n"
                         "q 0 1 -1 0 400 100 cm BT /F1 10 Tf 0 0 Td (Left HIDDEN Right) Tj ET Q\n"
                         "BT /F1 10 Tf 50 500 Td [(Kern) -500 (TOPSECRET) -1000 (After)] TJ ET\n";
    pdf::PDFDocument document = createSimpleDocument(content);

    const QRectF secret = getWordBoundingBox(document, 0, "CLASSIFIED");
    const QRectF omega = getWordBoundingBox(document, 0, "Omega");
    const QRectF hidden = getWordBoundingBox(document, 0, "HIDDEN");
    const QRectF right = getWordBoundingBox(document, 0, "Right");
    const QRectF topSecret = getWordBoundingBox(document, 0, "TOPSECRET");
    const QRectF after = getWordBoundingBox(document, 0, "After");
    QVERIFY2(secret.isValid() && hidden.isValid() && topSecret.isValid(), qPrintable(getPageText(document, 0)));
    QVERIFY(hidden.height() > hidden.width()); // rotated

    pdf::PDFDocument redacted = redact(addMarks(document, 0, { secret, hidden, topSecret }), Qt::black);
    const QString text = getPageText(redacted, 0);
    QVERIFY2(!text.contains("CLASSIFIED") && !text.contains("SECRET") && !text.contains("HIDDEN"), qPrintable(text));
    QVERIFY(QString(text).remove(' ').contains("Alpha") && text.contains("Omega") && text.contains("Left") && text.contains("Right") && text.contains("Kern") && text.contains("After"));

    auto compare = [](const QRectF& a, const QRectF& b) { return qAbs(a.left() - b.left()) < 0.01 && qAbs(a.top() - b.top()) < 0.01 && qAbs(a.width() - b.width()) < 0.01; };
    QVERIFY(compare(getWordBoundingBox(redacted, 0, "Omega"), omega));
    QVERIFY(compare(getWordBoundingBox(redacted, 0, "Right"), right));
    QVERIFY(compare(getWordBoundingBox(redacted, 0, "After"), after));
    QVERIFY(!documentContains(redacted, "CLASSIFIED"));
    QVERIFY(!documentContains(redacted, "SECRET"));
    QVERIFY(!documentContains(redacted, "HIDDEN"));
}

void RedactTest::test_text_word_spacing_space_removed()
{
    // Word spacing applies to the removed spaces, the following word must stay in place
    QByteArray content = "BT /F1 10 Tf 8 Tw 50 600 Td (One two three four) Tj ET\n";
    pdf::PDFDocument document = createSimpleDocument(content);

    const QRectF two = getWordBoundingBox(document, 0, "two three");
    const QRectF four = getWordBoundingBox(document, 0, "four");
    QVERIFY(two.isValid());

    pdf::PDFDocument redacted = redact(addMarks(document, 0, { two }), Qt::black);
    const QString text = getPageText(redacted, 0);
    QVERIFY2(!text.contains("two") && !text.contains("three"), qPrintable(text));
    const QRectF fourAfter = getWordBoundingBox(redacted, 0, "four");
    QVERIFY(qAbs(fourAfter.left() - four.left()) < 0.01);
}

void RedactTest::test_invisible_text_removed()
{
    TestDocument test = createTestDocument();
    const QRectF ocr = getWordBoundingBox(test.document, 0, "OCRSECRET");
    QVERIFY(ocr.isValid());

    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { ocr }), Qt::black);
    QVERIFY(!getPageText(redacted, 0).contains("OCRSECRET"));
    QVERIFY(!documentContains(redacted, "OCRSECRET"));
    QVERIFY(getPageText(redacted, 0).contains("FORMSECRET"));
}

void RedactTest::test_form_xobject_content_removed()
{
    TestDocument test = createTestDocument();
    const QRectF form = getWordBoundingBox(test.document, 0, "FORMSECRET");
    QVERIFY(form.isValid());

    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { form }), Qt::black);
    QVERIFY(!getPageText(redacted, 0).contains("FORMSECRET"));
    QVERIFY(!documentContains(redacted, "FORMSECRET"));

    // The original form is no longer in the document
    QVERIFY(redacted.getStorage().getObject(test.form).isNull());
    QVERIFY(getPageText(redacted, 0).contains("OCRSECRET"));
    QVERIFY(getPageText(redacted, 0).contains("Public line stays"));
}

void RedactTest::test_image_pixels_removed()
{
    TestDocument test = createTestDocument();

    // Image is placed at 300, 500 with size 100 x 100 - mark its left half
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { QRectF(250, 450, 100, 200) }), Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.redactedImageCount, 1);
    QCOMPARE(statistics.removedImageCount, 0);

    // Original image object is removed
    QVERIFY(redacted.getStorage().getObject(test.image).isNull());

    pdf::PDFEditedPageContent content = getContent(redacted, 0);
    const pdf::PDFEditedPageContentElementImage* imageElement = nullptr;
    for (size_t i = 0; i < content.getElementCount(); ++i)
    {
        if (content.getElement(i)->asImage())
        {
            imageElement = content.getElement(i)->asImage();
        }
    }
    QVERIFY(imageElement);
    QVERIFY(qAbs(imageElement->getBoundingBox().left() - 300.0) < 0.01);
    QVERIFY(qAbs(imageElement->getBoundingBox().width() - 100.0) < 0.01);

    QImage image = imageElement->getImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(image.size(), QSize(20, 20));
    for (int y = 0; y < 20; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            const QRgb pixel = image.pixel(x, y);
            if (x < 9)
            {
                QCOMPARE(pixel, qRgb(0, 0, 0));
            }
            else if (x > 10)
            {
                QCOMPARE(pixel, qRgb(0, 255, 0));
            }
        }
    }
}

void RedactTest::test_image_covered_removed()
{
    TestDocument test = createTestDocument();
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { QRectF(290, 490, 120, 120) }), Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.removedImageCount, 1);
    QCOMPARE(statistics.redactedImageCount, 0);

    pdf::PDFEditedPageContent content = getContent(redacted, 0);
    for (size_t i = 0; i < content.getElementCount(); ++i)
    {
        QVERIFY(!content.getElement(i)->asImage());
    }
}

void RedactTest::test_image_jpeg_kept_as_jpeg()
{
    QImage image(64, 64, QImage::Format_RGB888);
    image.fill(QColor(40, 160, 220));
    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = pdf::PDFImage::ImageCompression::JPEG;
    pdf::PDFStream imageStream = pdf::PDFImage::createStreamFromImage(image, options);

    pdf::PDFDocument document = createSimpleDocument("q 200 0 0 200 100 100 cm /Im1 Do Q\n", { { "XObject", createDictionary({ { "Im1", pdf::PDFObject::createStream(std::move(imageStream)) } }) } });
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(document, 0, { QRectF(50, 50, 100, 300) }), Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.redactedImageCount, 1);

    pdf::PDFEditedPageContent content = getContent(redacted, 0);
    bool found = false;
    for (size_t i = 0; i < content.getElementCount(); ++i)
    {
        if (const pdf::PDFEditedPageContentElementImage* imageElement = content.getElement(i)->asImage())
        {
            const pdf::PDFDictionary* dictionary = redacted.getDictionaryFromObject(imageElement->getImageObject());
            QVERIFY(dictionary);
            QCOMPARE(redacted.getObject(dictionary->get("Filter")).getString(), QByteArray("DCTDecode"));

            QImage redactedImage = imageElement->getImage().convertToFormat(QImage::Format_RGB32);
            QCOMPARE(redactedImage.size(), QSize(64, 64));
            QVERIFY(qGray(redactedImage.pixel(5, 32)) < 20);
            QVERIFY(qAbs(qBlue(redactedImage.pixel(60, 32)) - 220) < 20);
            found = true;
        }
    }
    QVERIFY(found);
}

void RedactTest::test_paths_cut()
{
    TestDocument test = createTestDocument();

    // Red rectangle 72, 500, 100 x 50 - mark its right half; blue line at y = 420
    // from x = 50 to 550 - mark its middle part
    const QRectF rectangleMark(122, 480, 100, 100);
    const QRectF lineMark(250, 400, 100, 40);
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { rectangleMark, lineMark }), QColor(), pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.redactedPathCount, 2);

    pdf::PDFEditedPageContent content = getContent(redacted, 0);
    QRectF redBoundingBox;
    std::vector<QRectF> blueParts;
    for (size_t i = 0; i < content.getElementCount(); ++i)
    {
        const pdf::PDFEditedPageContentElementPath* pathElement = content.getElement(i)->asPath();
        if (!pathElement)
        {
            continue;
        }

        const QColor fillColor = pathElement->getState().getFillColor();
        const QRectF boundingBox = pathElement->getBoundingBox();
        if (pathElement->getFillPath() && fillColor == QColor(Qt::red))
        {
            redBoundingBox = redBoundingBox.united(boundingBox);
        }
        else if (pathElement->getFillPath() && fillColor == QColor(Qt::blue))
        {
            blueParts.push_back(boundingBox);
        }

        if (fillColor == QColor(Qt::black))
        {
            // Fill of the mark
            continue;
        }

        // Nothing else is painted inside of the marks
        QVERIFY(!boundingBox.intersects(rectangleMark.adjusted(1, 1, -1, -1)) || !pathElement->getPath().intersects(pathElement->getTransform().inverted().mapRect(rectangleMark.adjusted(1, 1, -1, -1))));
        QVERIFY(!pathElement->getTransform().map(pathElement->getPath()).intersects(lineMark.adjusted(1, 1, -1, -1)));
    }

    QVERIFY(qAbs(redBoundingBox.left() - 72.0) < 0.01);
    QVERIFY(qAbs(redBoundingBox.right() - 122.0) < 0.01);
    QVERIFY(qAbs(redBoundingBox.height() - 50.0) < 0.01);

    // Stroked line is converted to two filled parts
    QCOMPARE(blueParts.size(), size_t(1));
    QVERIFY(qAbs(blueParts.front().left() - 50.0) < 1.5);
    QVERIFY(qAbs(blueParts.front().right() - 550.0) < 1.5);
    QVERIFY(qAbs(blueParts.front().height() - 2.0) < 0.1);
}

void RedactTest::test_shading_clipped()
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
    pdf::PDFObject function = createDictionary({ { "FunctionType", pdf::PDFObject::createInteger(2) },
                                                 { "Domain", createRealArray({ 0, 1 }) },
                                                 { "C0", createRealArray({ 1, 0, 0 }) },
                                                 { "C1", createRealArray({ 0, 0, 1 }) },
                                                 { "N", pdf::PDFObject::createReal(1.0) } });
    pdf::PDFObject shading = createDictionary({ { "ShadingType", pdf::PDFObject::createInteger(2) },
                                                { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                { "Coords", createRealArray({ 100, 0, 300, 0 }) },
                                                { "Function", function } });
    pdf::PDFObjectReference content = builder.addObject(createStream({ }, "q 100 100 200 200 re W n /Sh1 sh Q\n"));
    builder.mergeTo(page, createDictionary({ { "Resources", createDictionary({ { "Shading", createDictionary({ { "Sh1", shading } }) } }) },
                                             { "Contents", pdf::PDFObject::createReference(content) } }));
    pdf::PDFDocument document = builder.build();

    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(document, 0, { QRectF(150, 50, 50, 300) }), QColor(), pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.redactedShadingCount, 1);

    pdf::PDFEditedPageContent redactedContent = getContent(redacted, 0);
    bool found = false;
    for (size_t i = 0; i < redactedContent.getElementCount(); ++i)
    {
        if (const pdf::PDFEditedPageContentElementShading* shadingElement = redactedContent.getElement(i)->asShading())
        {
            const QPainterPath clip = shadingElement->getTransform().map(shadingElement->getClipPath());
            QVERIFY(!clip.isEmpty());
            QVERIFY(!clip.contains(QPointF(175, 200)));
            QVERIFY(clip.contains(QPointF(120, 200)));
            QVERIFY(clip.contains(QPointF(250, 200)));
            found = true;
        }
    }
    QVERIFY(found);

    // Fully covered shading is removed
    redacted = redact(addMarks(document, 0, { QRectF(0, 0, 612, 792) }), QColor(), pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.redactedShadingCount, 0);
    redactedContent = getContent(redacted, 0);
    for (size_t i = 0; i < redactedContent.getElementCount(); ++i)
    {
        QVERIFY(!redactedContent.getElement(i)->asShading());
    }
}

void RedactTest::test_annotations_and_fields_removed()
{
    TestDocument test = createTestDocument();

    // Note annotation (80, 290, 100, 310) and the widget (300, 100, 450, 120) are marked, the link is not
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { QRectF(70, 280, 40, 40), QRectF(350, 90, 20, 20) }), Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.removedAnnotationCount, 2);

    const pdf::PDFPage* page = redacted.getCatalog()->getPage(0);
    QCOMPARE(page->getAnnotations().size(), size_t(1));
    QCOMPARE(page->getAnnotations().front(), test.link);

    QVERIFY(!documentContains(redacted, "NOTESECRET"));
    QVERIFY(!documentContains(redacted, "FIELDSECRET"));

    // Field is removed from the form
    const pdf::PDFDictionary* catalog = redacted.getDictionaryFromObject(redacted.getTrailerDictionary()->get("Root"));
    const pdf::PDFDictionary* acroForm = redacted.getDictionaryFromObject(catalog->get("AcroForm"));
    QVERIFY(acroForm);
    const pdf::PDFObject fields = redacted.getObject(acroForm->get("Fields"));
    QVERIFY(fields.isArray());
    QCOMPARE(fields.getArray()->getCount(), size_t(0));
}

void RedactTest::test_untouched_pages_kept()
{
    TestDocument test = createTestDocument();
    pdf::PDFDocument marked = addMarks(test.document, 0, { QRectF(60, 690, 50, 20) });
    pdf::PDFDocument redacted = redact(marked, Qt::black);

    const pdf::PDFObject page2Before = marked.getStorage().getObject(test.page2);
    const pdf::PDFObject page2After = redacted.getStorage().getObject(test.page2);
    QCOMPARE(page2After, page2Before);
    QCOMPARE(redacted.getStorage().getObject(test.page2Content), marked.getStorage().getObject(test.page2Content));
    QCOMPARE(getPageText(redacted, 1), getPageText(test.document, 1));

    // Redacted page got a new content stream
    const pdf::PDFPage* page1 = redacted.getCatalog()->getPage(0);
    QVERIFY(page1->getContents().isStream() || page1->getContents().isArray());
    QVERIFY(redacted.getStorage().getObject(test.page1Content).isNull());
}

void RedactTest::test_fill_colors()
{
    pdf::PDFDocument document = createSimpleDocument("BT /F1 12 Tf 72 700 Td (Colored) Tj ET\n");
    const QRectF mark(60, 690, 100, 30);

    auto getLastFillColor = [](const pdf::PDFDocument& redacted, QRectF* boundingBox) -> QColor
    {
        pdf::PDFEditedPageContent content = getContent(redacted, 0);
        for (size_t i = content.getElementCount(); i > 0; --i)
        {
            if (const pdf::PDFEditedPageContentElementPath* path = content.getElement(i - 1)->asPath())
            {
                *boundingBox = path->getBoundingBox();
                return path->getState().getFillColor();
            }
        }
        return QColor();
    };

    QRectF boundingBox;

    // Color of the mark
    pdf::PDFDocument redacted = redact(addMarks(document, 0, { mark }, Qt::red), QColor());
    QCOMPARE(getLastFillColor(redacted, &boundingBox), QColor(Qt::red));
    QVERIFY(qAbs(boundingBox.left() - mark.left()) < 0.01 && qAbs(boundingBox.width() - mark.width()) < 0.01);

    // Override color
    redacted = redact(addMarks(document, 0, { mark }, Qt::red), Qt::white);
    QCOMPARE(getLastFillColor(redacted, &boundingBox), QColor(Qt::white));

    // Mark without the interior color is not filled
    pdf::PDFDocumentBuilder builder(&document);
    pdf::PDFObjectReference annotation = builder.createAnnotationRedact(document.getCatalog()->getPage(0)->getPageReference(), mark, Qt::red, Qt::red);
    builder.mergeTo(annotation, createDictionary({ { "IC", pdf::PDFObject() } }));
    pdf::PDFDocument withoutColor = builder.build();
    redacted = redact(withoutColor, QColor());
    QVERIFY(!getLastFillColor(redacted, &boundingBox).isValid());
    QVERIFY(!getPageText(redacted, 0).contains("Colored"));

    // Gray and CMYK interior colors
    for (const auto& [color, expected] : { std::make_pair(std::vector<pdf::PDFReal>{ 0.5 }, QColor::fromRgbF(0.5, 0.5, 0.5)),
                                           std::make_pair(std::vector<pdf::PDFReal>{ 0, 0, 0, 1 }, QColor(Qt::black)) })
    {
        pdf::PDFDocumentBuilder colorBuilder(&document);
        pdf::PDFObjectReference colorAnnotation = colorBuilder.createAnnotationRedact(document.getCatalog()->getPage(0)->getPageReference(), mark, Qt::red, Qt::red);
        colorBuilder.mergeTo(colorAnnotation, createDictionary({ { "IC", createRealArray(color) } }));
        pdf::PDFDocument colored = colorBuilder.build();
        redacted = redact(colored, QColor());
        const QColor fillColor = getLastFillColor(redacted, &boundingBox);
        QVERIFY(qAbs(fillColor.redF() - expected.redF()) < 0.01);
        QVERIFY(qAbs(fillColor.greenF() - expected.greenF()) < 0.01);
    }
}

void RedactTest::test_marks_removed_and_statistics()
{
    TestDocument test = createTestDocument();
    pdf::PDFDocument marked = addMarks(test.document, 0, { QRectF(60, 690, 50, 20), QRectF(250, 450, 100, 200) });
    marked = addMarks(marked, 1, { QRectF(60, 690, 50, 20) });
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&marked), 3);

    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(marked, Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&redacted), 0);
    QCOMPARE(statistics.markCount, 3);
    QCOMPARE(statistics.pageCount, 2);
    QVERIFY(statistics.removedCharacterCount > 0);
    QCOMPARE(statistics.redactedImageCount, 1);
    QVERIFY(!getPageText(redacted, 1).contains("Page"));
    QVERIFY(getPageText(redacted, 1).contains("untouched"));
}

void RedactTest::test_empty_mark_removed()
{
    TestDocument test = createTestDocument();
    pdf::PDFDocumentBuilder builder(&test.document);
    pdf::PDFObjectReference annotation = builder.createAnnotationRedact(test.page1, QRectF(100, 100, 0, 0), Qt::black, Qt::black);
    builder.mergeTo(annotation, createDictionary({ { "QuadPoints", pdf::PDFObject() } }));
    pdf::PDFDocument marked = builder.build();
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&marked), 1);

    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(marked, Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&redacted), 0);
    QCOMPARE(statistics.markCount, 1);
    QCOMPARE(statistics.pageCount, 0);

    // Content of the page is not changed
    QCOMPARE(redacted.getStorage().getObject(test.page1Content), test.document.getStorage().getObject(test.page1Content));
}

void RedactTest::test_document_data_options()
{
    TestDocument test = createTestDocument();
    pdf::PDFDocument marked = addMarks(test.document, 0, { QRectF(60, 690, 50, 20) });

    auto getInfo = [](const pdf::PDFDocument& document, const char* key)
    {
        const pdf::PDFDictionary* info = document.getDictionaryFromObject(document.getTrailerDictionary()->get("Info"));
        const pdf::PDFObject value = info ? document.getObject(info->get(key)) : pdf::PDFObject();
        return value.isString() ? value.getString() : QByteArray();
    };

    // Everything is kept
    pdf::PDFDocument redacted = redact(marked, Qt::black, pdf::PDFRedact::KeepDocumentData);
    QCOMPARE(getInfo(redacted, "Title"), QByteArray("Secret Title"));
    QCOMPARE(getInfo(redacted, "Author"), QByteArray("Secret Author"));
    QVERIFY(documentContains(redacted, "OUTLINESECRET"));
    QVERIFY(documentContains(redacted, "METASECRET"));

    // Nothing is kept
    redacted = redact(marked, Qt::black, pdf::PDFRedact::None);
    QVERIFY(getInfo(redacted, "Title").isEmpty());
    QVERIFY(!documentContains(redacted, "Secret Author"));
    QVERIFY(!documentContains(redacted, "Secret Title"));
    QVERIFY(!documentContains(redacted, "OUTLINESECRET"));
    QVERIFY(!documentContains(redacted, "METASECRET"));

    // Title only
    redacted = redact(marked, Qt::black, pdf::PDFRedact::CopyTitle);
    QCOMPARE(getInfo(redacted, "Title"), QByteArray("Secret Title"));
    QVERIFY(!documentContains(redacted, "Secret Author"));

    // Metadata without title
    redacted = redact(marked, Qt::black, pdf::PDFRedact::Options(pdf::PDFRedact::CopyMetadata | pdf::PDFRedact::CopyOutline));
    QVERIFY(getInfo(redacted, "Title").isEmpty());
    QCOMPARE(getInfo(redacted, "Author"), QByteArray("Secret Author"));
    QVERIFY(documentContains(redacted, "METASECRET"));
    QVERIFY(documentContains(redacted, "OUTLINESECRET"));
}

void RedactTest::test_structure_tree_removed()
{
    TestDocument test = createTestDocument(true);
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { QRectF(60, 690, 50, 20) }), Qt::black);
    QVERIFY(documentContains(test.document, "TAGSECRET"));
    QVERIFY(!documentContains(redacted, "TAGSECRET"));

    const pdf::PDFDictionary* catalog = redacted.getDictionaryFromObject(redacted.getTrailerDictionary()->get("Root"));
    QVERIFY(!catalog->hasKey("StructTreeRoot"));
    const pdf::PDFDictionary* page = redacted.getDictionaryFromObject(redacted.getStorage().getObject(test.page1));
    QVERIFY(!page->hasKey("StructParents"));

    // Without redacted pages the tree stays
    redacted = redact(test.document, Qt::black);
    QVERIFY(documentContains(redacted, "TAGSECRET"));
}

void RedactTest::test_page_marks()
{
    TestDocument test = createTestDocument();
    pdf::PDFDocumentBuilder builder(&test.document);
    QCOMPARE(pdf::PDFRedactSearch::createPageMarks(&builder, &test.document, { 1, 5, -1 }, Qt::black), 1);
    pdf::PDFDocument marked = builder.build();

    pdf::PDFDocument redacted = redact(marked, Qt::black);
    QVERIFY(getPageText(redacted, 1).trimmed().isEmpty());
    QVERIFY(getPageText(redacted, 0).contains("Public line stays"));
    QVERIFY(!documentContains(redacted, "Page two untouched"));
}

void RedactTest::test_no_marks()
{
    TestDocument test = createTestDocument();
    pdf::PDFRedact::Statistics statistics;
    pdf::PDFDocument redacted = redact(test.document, Qt::black, pdf::PDFRedact::KeepDocumentData, &statistics);
    QCOMPARE(statistics.markCount, 0);
    QCOMPARE(statistics.pageCount, 0);
    QCOMPARE(getPageText(redacted, 0), getPageText(test.document, 0));
    QCOMPARE(redacted.getStorage().getObject(test.page1), test.document.getStorage().getObject(test.page1));
}

void RedactTest::test_write_read_round_trip()
{
    TestDocument test = createTestDocument();
    const QRectF ssn = getWordBoundingBox(test.document, 0, "123-45-6789");
    pdf::PDFDocument redacted = redact(addMarks(test.document, 0, { ssn, QRectF(250, 450, 100, 200) }), Qt::black);

    pdf::PDFDocument readDocument = readBack(redacted);
    QCOMPARE(readDocument.getCatalog()->getPageCount(), size_t(2));
    const QString text = getPageText(readDocument, 0);
    QVERIFY(!text.contains("6789"));
    QVERIFY(text.contains("Public line stays"));
    QVERIFY(text.contains("FORMSECRET"));
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&readDocument), 0);
}

void RedactTest::test_secret_not_in_written_file()
{
    // Uncompressed original - the secrets are in the file as plain text
    TestDocument test = createTestDocument();
    QByteArray original;
    readBack(test.document, &original);
    QVERIFY(original.contains("FORMSECRET"));
    QVERIFY(original.contains("OCRSECRET"));

    const QRectF form = getWordBoundingBox(test.document, 0, "FORMSECRET");
    const QRectF ocr = getWordBoundingBox(test.document, 0, "OCRSECRET");
    QByteArray written;
    pdf::PDFDocument readDocument = readBack(redact(addMarks(test.document, 0, { form, ocr }), Qt::black), &written);
    QVERIFY(!written.contains("FORMSECRET"));
    QVERIFY(!written.contains("OCRSECRET"));
    QVERIFY(!documentContains(readDocument, "FORMSECRET"));
    QVERIFY(!documentContains(readDocument, "OCRSECRET"));
}

void RedactTest::test_pdftool()
{
    const QString pdfTool = getPdfToolPath();
    if (pdfTool.isEmpty())
    {
        QSKIP("PdfTool is not built next to the test");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    TestDocument test = createTestDocument();
    const QString input = directory.filePath("input.pdf");
    {
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(input, &test.document, false));
    }

    auto run = [&](const QStringList& arguments, QString* output) -> int
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(pdfTool, arguments);
        if (!process.waitForFinished(120000))
        {
            return -1;
        }
        if (output)
        {
            *output = QString::fromUtf8(process.readAll());
        }
        return process.exitCode();
    };

    auto read = [](const QString& fileName)
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        return reader.readFromFile(fileName);
    };

    // Search, pattern, area and pages
    QString output;
    const QString redactedFile = directory.filePath("redacted.pdf");
    QCOMPARE(run({ "redact", input, redactedFile, "--redact-search", "John Smith", "--redact-pattern", "ssn", "--redact-pattern", "email",
                   "--redact-area", "1:290,290,100,30", "--redact-pages", "2", "--redact-copy-title" }, &output), 0);
    QVERIFY2(output.contains("1 occurrence"), qPrintable(output));

    pdf::PDFDocument redacted = read(redactedFile);
    QCOMPARE(redacted.getCatalog()->getPageCount(), size_t(2));
    QString text = getPageText(redacted, 0);
    QVERIFY2(!text.contains("John") && !text.contains("6789") && !text.contains("example") && !text.contains("OCRSECRET"), qPrintable(text));
    QVERIFY(text.contains("Public line stays"));
    QVERIFY(getPageText(redacted, 1).trimmed().isEmpty());
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&redacted), 0);
    QVERIFY(!documentContains(redacted, "Secret Author"));
    QVERIFY(documentContains(redacted, "Secret Title"));

    // Mark only - the marks are added for review, the content is kept
    const QString markedFile = directory.filePath("marked.pdf");
    QCOMPARE(run({ "redact", input, markedFile, "--redact-regex", "Pub[a-z]+", "--redact-mark-only" }, &output), 0);
    pdf::PDFDocument marked = read(markedFile);
    QCOMPARE(pdf::PDFRedact::getRedactionMarkCount(&marked), 1);
    QVERIFY(getPageText(marked, 0).contains("Public"));

    // Applying the marks of the document
    const QString appliedFile = directory.filePath("applied.pdf");
    QCOMPARE(run({ "redact", markedFile, appliedFile, "--redact-color", "marks" }, &output), 0);
    pdf::PDFDocument applied = read(appliedFile);
    QVERIFY(!getPageText(applied, 0).contains("Public"));
    QVERIFY(getPageText(applied, 0).contains("line stays"));

    // Invalid arguments
    QVERIFY(run({ "redact", input, redactedFile, "--redact-pattern", "unknown" }, &output) != 0);
    QVERIFY(output.contains("redact-pattern"));
    QVERIFY(run({ "redact", input, redactedFile, "--redact-area", "1:10,10" }, &output) != 0);
    QVERIFY(run({ "redact", input, redactedFile, "--redact-area", "0:10,10,5,5" }, &output) != 0);
    QVERIFY(run({ "redact", input, redactedFile, "--redact-area", "9:10,10,5,5" }, &output) != 0);
    QVERIFY(run({ "redact", input, redactedFile, "--redact-color", "nocolor" }, &output) != 0);
    QVERIFY(run({ "redact", input, redactedFile, "--redact-regex", "(abc" }, &output) != 0);
    QVERIFY(run({ "redact", input }, &output) != 0);
}

QTEST_MAIN(RedactTest)

#include "tst_redacttest.moc"
