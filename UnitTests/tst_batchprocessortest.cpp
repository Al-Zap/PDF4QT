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



#include "pdfbatchprocessor.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextflow.h"
#include "pdfpagestamper.h"
#include "pdfocrengine.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfsecurityhandler.h"
#include "pdfform.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace pdf;

/// Batch processing (PDFBatchProcessor): steps of the sequence, JSON of the
/// sequences, output files and their policies, Bates numbers over the files,
/// failures and cancellation (no output is left), recognition of the text
class BatchProcessorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void actionJsonRoundTrip();
    void actionJsonRoundTrip_data();
    void sequenceJson();
    void validation();
    void pageSelection();
    void summaries();
    void findPdfFiles();
    void outputFileNames();
    void existingFilePolicies();
    void originalNeverOverwritten();
    void batesContinuesOverFiles();
    void headerFooterAndWatermark();
    void removeStamps();
    void flattenForms();
    void rotatePages();
    void optimizeAndSanitize();
    void setPassword();
    void protectedOriginal();
    void exports();
    void exportsSeeEarlierSteps();
    void failureLeavesNoOutput();
    void cancellation();
    void unreadableFile();
    void recognizeText();

private:
    static PDFDocument createDocument(int pageCount, const QString& text = QString(), const QString& title = QString());
    static PDFDocument createFormDocument();
    static PDFDocument createScanDocument();
    static QString writeDocument(const QString& fileName, const PDFDocument& document);
    static PDFDocument readDocument(const QString& fileName, const QString& password = QString(), PDFDocumentReader::Result* resultCode = nullptr);
    static QString pageText(const PDFDocument& document, PDFInteger pageIndex);
    static PDFBatchSettings folderSettings(const QString& directory, std::vector<PDFBatchAction> actions);
    QString inputPath(const QString& name) const { return m_input.filePath(name); }
    QString outputPath(const QString& name) const { return m_output.filePath(name); }

    QTemporaryDir m_directory;
    QDir m_input;
    QDir m_output;
    std::shared_ptr<PDFOCRTestEngineFactory> m_testEngine;
};

PDFDocument BatchProcessorTest::createDocument(int pageCount, const QString& text, const QString& title)
{
    PDFDocumentBuilder builder;

    PDFObjectReference fontReference;
    if (!text.isEmpty())
    {
        PDFDictionaryBuilder font;
        font.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
        font.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
        font.addEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
        font.addEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
        fontReference = builder.addObject(PDFObject::createDictionary(std::move(font)));
    }

    for (int i = 0; i < pageCount; ++i)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
        if (text.isEmpty())
        {
            continue;
        }

        const QByteArray content = QStringLiteral("BT /F1 24 Tf 72 700 Td (%1 %2) Tj ET").arg(text).arg(i + 1).toLatin1();
        PDFDictionaryBuilder contentDictionary;
        contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), QByteArray(content))));

        PDFDictionaryBuilder fonts;
        fonts.addEntry(PDFInplaceOrMemoryString("F1"), PDFObject::createReference(fontReference));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
        PDFDictionaryBuilder pageUpdate;
        pageUpdate.addEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentReference));
        pageUpdate.addEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
        builder.mergeTo(page, PDFObject::createDictionary(std::move(pageUpdate)));
    }

    if (!title.isEmpty())
    {
        builder.setDocumentTitle(title);
    }

    return builder.build();
}

PDFDocument BatchProcessorTest::createFormDocument()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));

    PDFDictionaryBuilder font;
    font.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    font.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    font.addEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
    PDFDictionaryBuilder fonts;
    fonts.addEntry(PDFInplaceOrMemoryString("Helv"), PDFObject::createReference(builder.addObject(PDFObject::createDictionary(std::move(font)))));
    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    const PDFObject resourcesObject = PDFObject::createDictionary(std::move(resources));

    // Appearance stream of the filled field (the core library flattens the appearance streams)
    const QByteArray content = "/Tx BMC q BT 0 g /Helv 20 Tf 2 12 Td (LED-42) Tj ET Q EMC";
    PDFDictionaryBuilder streamDictionary;
    streamDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    streamDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    PDFArrayBuilder bbox;
    bbox.appendItem(PDFObject::createReal(0.0));
    bbox.appendItem(PDFObject::createReal(0.0));
    bbox.appendItem(PDFObject::createReal(300.0));
    bbox.appendItem(PDFObject::createReal(40.0));
    streamDictionary.addEntry(PDFInplaceOrMemoryString("BBox"), PDFObject::createArray(std::move(bbox)));
    streamDictionary.addEntry(PDFInplaceOrMemoryString("Resources"), resourcesObject);
    streamDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
    const PDFObjectReference appearance = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), QByteArray(content))));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Annot");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Widget");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Rect");
    factory << QRectF(72, 600, 300, 40);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("P");
    factory << page;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("F");
    factory << PDFInteger(4);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("FT");
    factory << WrapName("Tx");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("T");
    factory << QString("Fixture");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("V");
    factory << QString("LED-42");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("DA");
    factory << WrapString("0 g /Helv 20 Tf");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("AP");
    factory.beginDictionary();
    factory.beginDictionaryItem("N");
    factory << appearance;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    const PDFObjectReference widget = builder.addObject(factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory << std::vector<PDFObjectReference>{ widget };
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.appendTo(page, factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("DR");
    factory << resourcesObject;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("DA");
    factory << WrapString("0 g /Helv 12 Tf");
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(builder.createAcroForm({ widget }), factory.takeObject());
    return builder.build();
}

PDFDocument BatchProcessorTest::createScanDocument()
{
    // Gray image covering the whole page, without any text
    PDFDocumentBuilder builder;
    const QRectF mediaBox(0, 0, 300, 200);
    const PDFObjectReference page = builder.appendPage(mediaBox);

    QByteArray imageData(32 * 32, '\xC0');
    PDFDictionaryBuilder imageDictionary;
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(32));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(32));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    imageDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(imageData.size()));
    const PDFObjectReference imageReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(imageDictionary), std::move(imageData))));

    const QByteArray content = "q 300 0 0 200 0 0 cm /Im1 Do Q";
    PDFDictionaryBuilder contentDictionary;
    contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), QByteArray(content))));

    PDFDictionaryBuilder xobjects;
    xobjects.addEntry(PDFInplaceOrMemoryString("Im1"), PDFObject::createReference(imageReference));
    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
    PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentReference));
    pageUpdate.addEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
    builder.mergeTo(page, PDFObject::createDictionary(std::move(pageUpdate)));
    return builder.build();
}

QString BatchProcessorTest::writeDocument(const QString& fileName, const PDFDocument& document)
{
    PDFDocumentWriter writer(nullptr);
    PDFOperationResult result = writer.write(fileName, &document, false);
    return result ? QString() : result.getErrorMessage();
}

PDFDocument BatchProcessorTest::readDocument(const QString& fileName, const QString& password, PDFDocumentReader::Result* resultCode)
{
    bool first = true;
    auto passwordCallback = [&](bool* ok) -> QString
    {
        *ok = first && !password.isEmpty();
        first = false;
        return password;
    };

    PDFDocumentReader reader(nullptr, passwordCallback, true, false);
    PDFDocument document = reader.readFromFile(fileName);
    if (resultCode)
    {
        *resultCode = reader.getReadingResult();
    }
    return document;
}

QString BatchProcessorTest::pageText(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFDocumentTextFlowFactory factory;
    const PDFDocumentTextFlow flow = factory.create(&document, { pageIndex }, PDFDocumentTextFlowFactory::Algorithm::Auto);
    QStringList texts;
    for (const PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.flags.testFlag(PDFDocumentTextFlow::Text))
        {
            texts << item.text;
        }
    }
    return texts.join(QChar(' '));
}

PDFBatchSettings BatchProcessorTest::folderSettings(const QString& directory, std::vector<PDFBatchAction> actions)
{
    PDFBatchSettings settings;
    settings.actions = std::move(actions);
    settings.outputLocation = PDFBatchSettings::OutputLocation::Folder;
    settings.outputDirectory = directory;
    settings.suffix = QString();
    return settings;
}

void BatchProcessorTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
    m_testEngine = std::make_shared<PDFOCRTestEngineFactory>();
    PDFOCREngineRegistry::getInstance()->registerFactory(m_testEngine);
}

void BatchProcessorTest::cleanupTestCase()
{
    PDFOCREngineRegistry::getInstance()->unregisterFactory(QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
}

void BatchProcessorTest::init()
{
    // Every test function gets empty input and output folders
    static int counter = 0;
    ++counter;
    QDir root(m_directory.path());
    const QString inputName = QStringLiteral("input%1").arg(counter);
    const QString outputName = QStringLiteral("output%1").arg(counter);
    QVERIFY(root.mkpath(inputName));
    m_input = QDir(root.filePath(inputName));
    m_output = QDir(root.filePath(outputName));
}

void BatchProcessorTest::actionJsonRoundTrip_data()
{
    QTest::addColumn<int>("type");
    for (int i = 0; i < int(PDFBatchAction::Type::TypeCount); ++i)
    {
        const PDFBatchAction::Type type = static_cast<PDFBatchAction::Type>(i);
        QTest::newRow(qPrintable(PDFBatchAction::getTypeId(type))) << i;
    }
}

void BatchProcessorTest::actionJsonRoundTrip()
{
    QFETCH(int, type);

    PDFBatchAction action = PDFBatchAction::create(static_cast<PDFBatchAction::Type>(type));
    QVERIFY(!action.getName().isEmpty());
    QCOMPARE(PDFBatchAction::getTypeFromId(PDFBatchAction::getTypeId(action.type)), std::optional<PDFBatchAction::Type>(action.type));

    // Non default values of all settings
    action.pages = PDFBatchAction::hasPages(action.type) ? QStringLiteral("2-3") : QString();
    action.headerFooter.texts[PDFHeaderFooterSettings::HeaderLeft] = QStringLiteral("Left\n{date}");
    action.headerFooter.style.font = PDFStampFont::CourierBold;
    action.headerFooter.style.fontSize = 13.5;
    action.headerFooter.style.color = QColor(0x12, 0x34, 0x56);
    action.headerFooter.style.opacity = 0.75;
    action.headerFooter.margins = QMarginsF(1, 2, 3, 4);
    action.headerFooter.variables.batesPrefix = QStringLiteral("ISL-");
    action.headerFooter.variables.batesSuffix = QStringLiteral("-X");
    action.headerFooter.variables.batesStart = 120;
    action.headerFooter.variables.batesDigits = 8;
    action.headerFooter.variables.startPageNumber = 5;
    action.headerFooter.variables.dateFormat = QStringLiteral("yyyy-MM-dd");
    action.replaceExisting = false;
    action.continueBatesNumbering = false;
    action.watermark.text = QStringLiteral("DRAFT\nCOPY");
    action.watermark.rotation = 30.0;
    action.watermark.relativeSize = 0.25;
    action.watermark.alignment = Qt::AlignTop | Qt::AlignRight;
    action.watermark.offset = QPointF(5, -7);
    action.watermark.behindContent = true;
    action.removedStamps = PDFPageStamper::Watermark;
    action.rotation = 270;
    action.ocrConfiguration.languages = QStringList({ QStringLiteral("deu"), QStringLiteral("eng") });
    action.ocrConfiguration.profile = PDFOCRModelProfile::Best;
    action.ocrDecisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;
    action.ocrAllowPageErrors = true;
    action.optimizationFlags = PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects | PDFOptimizer::MergeIdenticalObjects);
    action.optimizeImages = true;
    action.imageGoal = PDFImageOptimizer::OptimizationGoal::MinimumSize;
    action.imageTargetDpi = 200;
    action.sanitizationFlags = PDFDocumentSanitizer::SanitizationFlags(PDFDocumentSanitizer::Outline | PDFDocumentSanitizer::PageLabels);
    action.userPassword = QStringLiteral("secret");
    action.ownerPassword = QStringLiteral("owner");
    action.wordIncludeGraphics = false;
    action.wordGraphicsResolution = 300;
    action.imageFormat = QStringLiteral("jpg");
    action.imageResolution = 96;

    const QJsonObject json = action.toJson();
    QString errorMessage;
    const PDFBatchAction restored = PDFBatchAction::fromJson(json, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(restored.type, action.type);
    QCOMPARE(restored.toJson(), json);

    // Passwords are never written into a sequence
    const QByteArray text = QJsonDocument(json).toJson();
    QVERIFY(!text.contains("secret"));
    QVERIFY(!text.contains("owner"));
    QVERIFY(restored.userPassword.isEmpty());
    QVERIFY(restored.ownerPassword.isEmpty());

    switch (action.type)
    {
        case PDFBatchAction::Type::HeaderFooter:
        case PDFBatchAction::Type::BatesNumbering:
            QCOMPARE(restored.headerFooter.texts, action.headerFooter.texts);
            QCOMPARE(restored.headerFooter.style, action.headerFooter.style);
            QCOMPARE(restored.headerFooter.margins, action.headerFooter.margins);
            QCOMPARE(restored.headerFooter.variables.batesPrefix, action.headerFooter.variables.batesPrefix);
            QCOMPARE(restored.headerFooter.variables.batesStart, action.headerFooter.variables.batesStart);
            QCOMPARE(restored.headerFooter.variables.batesDigits, action.headerFooter.variables.batesDigits);
            QCOMPARE(restored.headerFooter.variables.startPageNumber, action.headerFooter.variables.startPageNumber);
            QCOMPARE(restored.continueBatesNumbering, false);
            QCOMPARE(restored.replaceExisting, false);
            QCOMPARE(restored.pages, QStringLiteral("2-3"));
            break;

        case PDFBatchAction::Type::Watermark:
            QCOMPARE(restored.watermark.text, action.watermark.text);
            QCOMPARE(restored.watermark.rotation, action.watermark.rotation);
            QCOMPARE(restored.watermark.relativeSize, action.watermark.relativeSize);
            QCOMPARE(restored.watermark.alignment, action.watermark.alignment);
            QCOMPARE(restored.watermark.offset, action.watermark.offset);
            QCOMPARE(restored.watermark.behindContent, true);
            break;

        case PDFBatchAction::Type::RemoveStamps:
            QCOMPARE(restored.removedStamps, PDFPageStamper::StampKinds(PDFPageStamper::Watermark));
            break;

        case PDFBatchAction::Type::RotatePages:
            QCOMPARE(restored.rotation, 270);
            break;

        case PDFBatchAction::Type::RecognizeText:
            QCOMPARE(restored.ocrConfiguration.languages, action.ocrConfiguration.languages);
            QCOMPARE(restored.ocrConfiguration.profile, PDFOCRModelProfile::Best);
            QCOMPARE(restored.ocrDecisionPolicy, PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText);
            QCOMPARE(restored.ocrAllowPageErrors, true);
            break;

        case PDFBatchAction::Type::Optimize:
            QCOMPARE(restored.optimizationFlags, action.optimizationFlags);
            QCOMPARE(restored.optimizeImages, true);
            QCOMPARE(restored.imageGoal, PDFImageOptimizer::OptimizationGoal::MinimumSize);
            QCOMPARE(restored.imageTargetDpi, 200);
            break;

        case PDFBatchAction::Type::Sanitize:
            QCOMPARE(restored.sanitizationFlags, action.sanitizationFlags);
            break;

        case PDFBatchAction::Type::ExportWord:
            QCOMPARE(restored.wordIncludeGraphics, false);
            QCOMPARE(restored.wordGraphicsResolution, 300);
            break;

        case PDFBatchAction::Type::ExportImages:
            QCOMPARE(restored.imageFormat, QStringLiteral("jpg"));
            QCOMPARE(restored.imageResolution, 96);
            break;

        default:
            break;
    }
}

void BatchProcessorTest::sequenceJson()
{
    PDFBatchSettings settings;
    settings.actions.push_back(PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering));
    settings.actions.push_back(PDFBatchAction::create(PDFBatchAction::Type::FlattenForms));
    settings.actions.push_back(PDFBatchAction::create(PDFBatchAction::Type::ExportText));
    settings.outputLocation = PDFBatchSettings::OutputLocation::NextToOriginal;
    settings.outputDirectory = QStringLiteral("C:/Plans/Out");
    settings.suffix = QStringLiteral("_stamped");
    settings.existingFilePolicy = PDFBatchSettings::ExistingFilePolicy::Skip;
    settings.password = QStringLiteral("filepassword");

    const QJsonObject json = settings.toJson();
    QCOMPARE(json.value("version").toInt(), 1);
    QVERIFY(!QJsonDocument(json).toJson().contains("filepassword"));

    QString errorMessage;
    PDFBatchSettings restored = PDFBatchSettings::fromJson(json, &errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(restored.actions.size(), size_t(3));
    QCOMPARE(restored.actions[0].type, PDFBatchAction::Type::BatesNumbering);
    QCOMPARE(restored.actions[1].type, PDFBatchAction::Type::FlattenForms);
    QCOMPARE(restored.actions[2].type, PDFBatchAction::Type::ExportText);
    QCOMPARE(restored.outputLocation, PDFBatchSettings::OutputLocation::NextToOriginal);
    QCOMPARE(restored.outputDirectory, settings.outputDirectory);
    QCOMPARE(restored.suffix, settings.suffix);
    QCOMPARE(restored.existingFilePolicy, PDFBatchSettings::ExistingFilePolicy::Skip);
    QVERIFY(restored.password.isEmpty());

    for (PDFBatchSettings::ExistingFilePolicy policy : { PDFBatchSettings::ExistingFilePolicy::Rename, PDFBatchSettings::ExistingFilePolicy::Overwrite })
    {
        settings.existingFilePolicy = policy;
        QCOMPARE(PDFBatchSettings::fromJson(settings.toJson(), nullptr).existingFilePolicy, policy);
    }

    // Unknown steps are reported, the known ones are kept
    QJsonObject broken = json;
    QJsonArray actions = broken.value("actions").toArray();
    actions.insert(1, QJsonObject({ { "type", "teleport" } }));
    broken["actions"] = actions;
    restored = PDFBatchSettings::fromJson(broken, &errorMessage);
    QVERIFY(errorMessage.contains("teleport"));
    QCOMPARE(restored.actions.size(), size_t(3));

    PDFBatchAction unknown = PDFBatchAction::fromJson(QJsonObject({ { "type", "nothing" } }), &errorMessage);
    QCOMPARE(unknown.type, PDFBatchAction::Type::TypeCount);
    QVERIFY(!unknown.validate().isEmpty());
    QVERIFY(!PDFBatchAction::getTypeFromId("nothing").has_value());

    // Watermark image is kept in the sequence
    PDFBatchAction watermark = PDFBatchAction::create(PDFBatchAction::Type::Watermark);
    watermark.watermark.text.clear();
    watermark.watermark.image = QImage(20, 10, QImage::Format_ARGB32);
    watermark.watermark.image.fill(QColor(10, 200, 30));
    const PDFBatchAction restoredWatermark = PDFBatchAction::fromJson(watermark.toJson(), nullptr);
    QCOMPARE(restoredWatermark.watermark.image.size(), QSize(20, 10));
    QCOMPARE(restoredWatermark.watermark.image.pixelColor(5, 5), QColor(10, 200, 30));
    QVERIFY(restoredWatermark.validate().isEmpty());

    // Alignment identifiers
    for (Qt::Alignment alignment : { Qt::Alignment(Qt::AlignCenter), Qt::AlignTop | Qt::AlignHCenter, Qt::AlignBottom | Qt::AlignHCenter,
                                     Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignRight | Qt::AlignVCenter, Qt::AlignTop | Qt::AlignLeft,
                                     Qt::AlignTop | Qt::AlignRight, Qt::AlignBottom | Qt::AlignLeft, Qt::AlignBottom | Qt::AlignRight })
    {
        watermark.watermark.alignment = alignment;
        QCOMPARE(PDFBatchAction::fromJson(watermark.toJson(), nullptr).watermark.alignment, alignment);
    }
}

void BatchProcessorTest::validation()
{
    PDFBatchSettings settings = folderSettings(m_output.path(), { });
    QVERIFY(settings.validate().join('\n').contains("Add at least one action"));
    QVERIFY(!settings.writesDocument());

    // Valid default steps of all types (with a password)
    for (int i = 0; i < int(PDFBatchAction::Type::TypeCount); ++i)
    {
        PDFBatchAction action = PDFBatchAction::create(static_cast<PDFBatchAction::Type>(i));
        if (action.type == PDFBatchAction::Type::SetPassword)
        {
            QVERIFY(!action.validate().isEmpty());
            action.userPassword = QStringLiteral("pwd");
        }
        QVERIFY2(action.validate().isEmpty(), qPrintable(action.validate().join('\n')));
    }

    // Password must be the last modifying step, exports may follow
    PDFBatchAction password = PDFBatchAction::create(PDFBatchAction::Type::SetPassword);
    password.userPassword = QStringLiteral("pwd");
    settings.actions = { password, PDFBatchAction::create(PDFBatchAction::Type::ExportText) };
    QVERIFY(settings.validate().isEmpty());
    QVERIFY(settings.writesDocument());
    settings.actions = { password, PDFBatchAction::create(PDFBatchAction::Type::Optimize) };
    QVERIFY(settings.validate().join('\n').contains("last action"));

    // The same export twice would write the same file
    settings.actions = { PDFBatchAction::create(PDFBatchAction::Type::ExportWord), PDFBatchAction::create(PDFBatchAction::Type::ExportWord) };
    QVERIFY(settings.validate().join('\n').contains("more than once"));
    QVERIFY(!settings.writesDocument());

    // Output
    settings.actions = { PDFBatchAction::create(PDFBatchAction::Type::Optimize) };
    settings.outputDirectory.clear();
    QVERIFY(settings.validate().join('\n').contains("output folder"));
    settings.outputLocation = PDFBatchSettings::OutputLocation::NextToOriginal;
    settings.suffix.clear();
    QVERIFY(settings.validate().join('\n').contains("suffix"));
    settings.suffix = QStringLiteral("_a/b");
    QVERIFY(settings.validate().join('\n').contains("must not contain"));
    settings.suffix = QStringLiteral("_ok");
    QVERIFY(settings.validate().isEmpty());

    // Invalid settings of the steps
    auto expectError = [](PDFBatchAction action, const char* text)
    {
        const QString errors = action.validate().join('\n');
        QVERIFY2(errors.contains(QLatin1String(text)), qPrintable(errors));
        QVERIFY(errors.startsWith(action.getName()));
    };

    PDFBatchAction action = PDFBatchAction::create(PDFBatchAction::Type::HeaderFooter);
    action.headerFooter.texts = { };
    expectError(action, "enter a text");
    action = PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering);
    action.headerFooter.texts[PDFHeaderFooterSettings::FooterRight] = QStringLiteral("{page}");
    expectError(action, "{bates}");
    action = PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering);
    action.headerFooter.variables.batesDigits = 16;
    expectError(action, "digits");
    action = PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering);
    action.headerFooter.variables.batesStart = -1;
    expectError(action, "negative");
    action.headerFooter.variables.batesStart = 1;
    action.pages = QStringLiteral("3-x");
    expectError(action, "invalid pages");
    action = PDFBatchAction::create(PDFBatchAction::Type::Watermark);
    action.watermark.text = QStringLiteral("  ");
    expectError(action, "enter a text or select an image");
    action = PDFBatchAction::create(PDFBatchAction::Type::Watermark);
    action.watermark.relativeSize = 0.0;
    expectError(action, "size");
    action = PDFBatchAction::create(PDFBatchAction::Type::RemoveStamps);
    action.removedStamps = PDFPageStamper::None;
    expectError(action, "select the stamps");
    action = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    action.rotation = 45;
    expectError(action, "90, 180 or 270");
    action = PDFBatchAction::create(PDFBatchAction::Type::RecognizeText);
    action.ocrConfiguration.languages.clear();
    expectError(action, "languages");
    action = PDFBatchAction::create(PDFBatchAction::Type::RecognizeText);
    action.ocrConfiguration.existingTextPolicy = PDFOCRExistingTextPolicy::ReviewOnly;
    expectError(action, "writes nothing");
    action = PDFBatchAction::create(PDFBatchAction::Type::Optimize);
    action.optimizationFlags = PDFOptimizer::None;
    expectError(action, "select what to optimize");
    action = PDFBatchAction::create(PDFBatchAction::Type::Optimize);
    action.imageTargetDpi = 5000;
    expectError(action, "2400");
    action = PDFBatchAction::create(PDFBatchAction::Type::Sanitize);
    action.sanitizationFlags = PDFDocumentSanitizer::None;
    expectError(action, "select the information");
    action = PDFBatchAction::create(PDFBatchAction::Type::ExportWord);
    action.wordGraphicsResolution = 10;
    expectError(action, "36 to 600");
    action = PDFBatchAction::create(PDFBatchAction::Type::ExportImages);
    action.imageFormat = QStringLiteral("xyz");
    expectError(action, "not supported");
    action = PDFBatchAction::create(PDFBatchAction::Type::ExportImages);
    action.imageResolution = 5000;
    expectError(action, "10 to 1200");

    // Processor reports a missing OCR engine
    PDFBatchAction ocr = PDFBatchAction::create(PDFBatchAction::Type::RecognizeText);
    ocr.ocrConfiguration.engineId = QStringLiteral("no-such-engine");
    PDFBatchProcessor processor(folderSettings(m_output.path(), { ocr }));
    QVERIFY(processor.validate().join('\n').contains("no-such-engine"));
}

void BatchProcessorTest::pageSelection()
{
    PDFBatchAction action = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    QString errorMessage;
    QCOMPARE(action.getPageIndices(4, &errorMessage), std::vector<PDFInteger>({ 0, 1, 2, 3 }));

    action.pages = QStringLiteral("1, 3-");
    QCOMPARE(action.getPageIndices(4, &errorMessage), std::vector<PDFInteger>({ 0, 2, 3 }));
    QVERIFY(errorMessage.isEmpty());

    action.pages = QStringLiteral("7");
    QVERIFY(action.getPageIndices(4, &errorMessage).empty());
    QVERIFY(!errorMessage.isEmpty());

    // Steps without pages work with the whole document
    PDFBatchAction optimize = PDFBatchAction::create(PDFBatchAction::Type::Optimize);
    optimize.pages = QStringLiteral("2");
    QCOMPARE(optimize.getPageIndices(3, nullptr).size(), size_t(3));
    QVERIFY(!PDFBatchAction::hasPages(PDFBatchAction::Type::FlattenForms));
    QVERIFY(PDFBatchAction::hasPages(PDFBatchAction::Type::ExportImages));
}

void BatchProcessorTest::summaries()
{
    for (int i = 0; i < int(PDFBatchAction::Type::TypeCount); ++i)
    {
        PDFBatchAction action = PDFBatchAction::create(static_cast<PDFBatchAction::Type>(i));
        QVERIFY2(!action.getSummary().isEmpty(), qPrintable(action.getName()));
        if (PDFBatchAction::hasPages(action.type))
        {
            action.pages = QStringLiteral("2-4");
            QVERIFY(action.getSummary().contains("2-4"));
        }
    }

    PDFBatchAction bates = PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering);
    bates.headerFooter.variables.batesPrefix = QStringLiteral("ISL");
    bates.headerFooter.variables.batesStart = 7;
    bates.headerFooter.variables.batesDigits = 4;
    QVERIFY(bates.getSummary().startsWith("ISL0007"));
    QVERIFY(bates.getSummary().contains("footer-right"));
    QVERIFY(bates.getSummary().contains("continued"));

    PDFBatchAction password = PDFBatchAction::create(PDFBatchAction::Type::SetPassword);
    QVERIFY(password.getSummary().contains("Enter"));
    password.ownerPassword = QStringLiteral("x");
    QVERIFY(password.getSummary().contains("change"));
    password.userPassword = QStringLiteral("y");
    QVERIFY(password.getSummary().contains("open"));

    QCOMPARE(PDFBatchProcessor::getStatusName(PDFBatchProcessor::FileResult::Status::Succeeded), QStringLiteral("Done"));
    QCOMPARE(PDFBatchProcessor::getStatusName(PDFBatchProcessor::FileResult::Status::Cancelled), QStringLiteral("Stopped"));
}

void BatchProcessorTest::findPdfFiles()
{
    const PDFDocument document = createDocument(1);
    QVERIFY(m_input.mkpath("sub/deeper"));
    for (const QString& name : { QStringLiteral("plan10.pdf"), QStringLiteral("plan2.pdf"), QStringLiteral("Plan1.PDF"), QStringLiteral("sub/inner.pdf"), QStringLiteral("sub/deeper/deep.pdf") })
    {
        QVERIFY(writeDocument(inputPath(name), document).isEmpty());
    }
    QFile textFile(inputPath("notes.txt"));
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.close();

    QStringList files = PDFBatchProcessor::findPdfFiles(m_input.path(), false);
    QStringList names;
    for (const QString& file : files)
    {
        names << m_input.relativeFilePath(file);
    }
    QCOMPARE(names, QStringList({ "Plan1.PDF", "plan2.pdf", "plan10.pdf" }));

    files = PDFBatchProcessor::findPdfFiles(m_input.path(), true);
    QCOMPARE(files.size(), 5);
    QVERIFY(files.contains(QDir::cleanPath(inputPath("sub/deeper/deep.pdf"))));
}

void BatchProcessorTest::outputFileNames()
{
    PDFBatchAction images = PDFBatchAction::create(PDFBatchAction::Type::ExportImages);
    PDFBatchSettings settings = folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::ExportWord),
                                                                  PDFBatchAction::create(PDFBatchAction::Type::Optimize),
                                                                  PDFBatchAction::create(PDFBatchAction::Type::ExportText), images });
    settings.suffix = QStringLiteral("_done");

    PDFBatchProcessor processor(settings);
    const QStringList files = processor.getOutputFiles(inputPath("site.plan.pdf"));
    QCOMPARE(files, QStringList({ outputPath("site.plan_done.pdf"), outputPath("site.plan_done.docx"), outputPath("site.plan_done.txt"), outputPath("site.plan_done-001.png") }));

    settings.outputLocation = PDFBatchSettings::OutputLocation::NextToOriginal;
    PDFBatchProcessor nextToOriginal(settings);
    QCOMPARE(nextToOriginal.getOutputFiles(inputPath("a.pdf")).front(), inputPath("a_done.pdf"));

    // Unique file names
    QFile file(outputPath("x.pdf"));
    QVERIFY(m_output.mkpath("."));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QCOMPARE(PDFBatchProcessor::getUniqueFileName(outputPath("y.pdf")), outputPath("y.pdf"));
    QCOMPARE(PDFBatchProcessor::getUniqueFileName(outputPath("x.pdf")), outputPath("x (2).pdf"));
    QCOMPARE(PDFBatchProcessor::getUniqueFileName(outputPath("x.pdf"), { outputPath("x (2).pdf") }), outputPath("x (3).pdf"));
    QCOMPARE(PDFBatchProcessor::getUniqueFileName(outputPath("y.pdf"), { outputPath("y.pdf") }), outputPath("y (2).pdf"));
}

void BatchProcessorTest::existingFilePolicies()
{
    QVERIFY(writeDocument(inputPath("plan.pdf"), createDocument(2, "Plan")).isEmpty());

    PDFBatchSettings settings = folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::RotatePages), PDFBatchAction::create(PDFBatchAction::Type::ExportText) });

    // First run creates the files, the output folder is created
    PDFBatchProcessor processor(settings);
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputFiles, QStringList({ outputPath("plan.pdf"), outputPath("plan.txt") }));
    QCOMPARE(result.pageCount, 2);
    QVERIFY(result.message.contains("2 file"));

    // Rename: all outputs of the file get the same number
    result = processor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.outputFiles, QStringList({ outputPath("plan (2).pdf"), outputPath("plan (2).txt") }));

    // Only the text file exists with the next number: both get the following number
    QFile blocker(outputPath("plan (3).txt"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    result = processor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.outputFiles, QStringList({ outputPath("plan (4).pdf"), outputPath("plan (4).txt") }));

    // Skip
    settings.existingFilePolicy = PDFBatchSettings::ExistingFilePolicy::Skip;
    PDFBatchProcessor skipProcessor(settings);
    result = skipProcessor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Skipped);
    QVERIFY(result.outputFiles.isEmpty());

    // Overwrite
    QFile old(outputPath("plan.txt"));
    QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
    old.write("old");
    old.close();
    settings.existingFilePolicy = PDFBatchSettings::ExistingFilePolicy::Overwrite;
    PDFBatchProcessor overwriteProcessor(settings);
    result = overwriteProcessor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.outputFiles, QStringList({ outputPath("plan.pdf"), outputPath("plan.txt") }));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QVERIFY(old.readAll().contains("Plan 1"));
}

void BatchProcessorTest::originalNeverOverwritten()
{
    QVERIFY(writeDocument(inputPath("plan.pdf"), createDocument(1, "Original")).isEmpty());
    QFile originalFile(inputPath("plan.pdf"));
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    const QByteArray original = originalFile.readAll();
    originalFile.close();

    // Output folder equal to the folder of the original, without a suffix
    PDFBatchSettings settings = folderSettings(m_input.path(), { PDFBatchAction::create(PDFBatchAction::Type::RotatePages) });
    settings.existingFilePolicy = PDFBatchSettings::ExistingFilePolicy::Overwrite;
    PDFBatchProcessor processor(settings);
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("overwrite the original"));

    QFile file(inputPath("plan.pdf"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);

    // Next to the original with a suffix
    settings.outputLocation = PDFBatchSettings::OutputLocation::NextToOriginal;
    settings.suffix = QStringLiteral("_rotated");
    PDFBatchProcessor nextProcessor(settings);
    result = nextProcessor.processFile(inputPath("plan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.outputFiles, QStringList({ inputPath("plan_rotated.pdf") }));
}

void BatchProcessorTest::batesContinuesOverFiles()
{
    QVERIFY(writeDocument(inputPath("a.pdf"), createDocument(3)).isEmpty());
    QVERIFY(writeDocument(inputPath("b.pdf"), createDocument(2)).isEmpty());

    PDFBatchAction bates = PDFBatchAction::create(PDFBatchAction::Type::BatesNumbering);
    bates.headerFooter.variables.batesPrefix = QStringLiteral("ISL");
    bates.headerFooter.variables.batesDigits = 5;
    bates.headerFooter.variables.batesStart = 10;

    PDFBatchProcessor processor(folderSettings(m_output.path(), { bates }));
    PDFBatchProcessor::FileResult first = processor.processFile(inputPath("a.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    PDFBatchProcessor::FileResult second = processor.processFile(inputPath("b.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(first.isSuccess(), qPrintable(first.message));
    QVERIFY2(second.isSuccess(), qPrintable(second.message));
    QVERIFY(first.details.join(' ').contains("ISL00010 to ISL00012"));
    QVERIFY(second.details.join(' ').contains("ISL00013 to ISL00014"));

    const PDFDocument a = readDocument(outputPath("a.pdf"));
    const PDFDocument b = readDocument(outputPath("b.pdf"));
    QVERIFY(pageText(a, 0).contains("ISL00010"));
    QVERIFY(pageText(a, 2).contains("ISL00012"));
    QVERIFY(pageText(b, 0).contains("ISL00013"));
    QVERIFY(pageText(b, 1).contains("ISL00014"));

    // A new batch restarts the numbers
    processor.restartNumbering();
    QFile::remove(outputPath("b.pdf"));
    second = processor.processFile(inputPath("b.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(second.details.join(' ').contains("ISL00010 to ISL00011"));

    // Numbers are not continued, when the step says so
    bates.continueBatesNumbering = false;
    PDFBatchSettings settings = folderSettings(m_output.path(), { bates });
    settings.existingFilePolicy = PDFBatchSettings::ExistingFilePolicy::Overwrite;
    PDFBatchProcessor restarting(settings);
    restarting.processFile(inputPath("a.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    second = restarting.processFile(inputPath("b.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(second.details.join(' ').contains("ISL00010 to ISL00011"));
}

void BatchProcessorTest::headerFooterAndWatermark()
{
    QVERIFY(writeDocument(inputPath("Lighting Plan.pdf"), createDocument(3, "Sheet")).isEmpty());

    PDFBatchAction header = PDFBatchAction::create(PDFBatchAction::Type::HeaderFooter);
    header.headerFooter.texts[PDFHeaderFooterSettings::HeaderLeft] = QStringLiteral("{file}");
    header.pages = QStringLiteral("2-3");
    PDFBatchAction watermark = PDFBatchAction::create(PDFBatchAction::Type::Watermark);
    watermark.watermark.text = QStringLiteral("PRELIMINARY");
    watermark.pages = QStringLiteral("1");

    int progressCalls = 0;
    QStringList progressNames;
    PDFBatchProcessor processor(folderSettings(m_output.path(), { header, watermark }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("Lighting Plan.pdf"), [&](int step, int stepCount, const QString& name)
    {
        QCOMPARE(stepCount, 2);
        QCOMPARE(step, progressCalls++);
        progressNames << name;
    }, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(progressCalls, 3);
    QCOMPARE(progressNames.front(), header.getName());

    const PDFDocument document = readDocument(outputPath("Lighting Plan.pdf"));
    QCOMPARE(PDFPageStamper::getStampKinds(&document, 0), PDFPageStamper::StampKinds(PDFPageStamper::Watermark));
    QCOMPARE(PDFPageStamper::getStampKinds(&document, 1), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
    QCOMPARE(PDFPageStamper::getStampKinds(&document, 2), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
    QVERIFY(pageText(document, 1).contains("Lighting Plan.pdf"));
    QVERIFY(pageText(document, 1).contains("Page 1 of"));
    QVERIFY(pageText(document, 0).contains("PRELIMINARY"));
    QVERIFY(pageText(document, 0).contains("Sheet 1"));
}

void BatchProcessorTest::removeStamps()
{
    const PDFDocument document = createDocument(2, "Text");
    PDFHeaderFooterSettings headerFooter;
    headerFooter.texts[PDFHeaderFooterSettings::FooterCenter] = QStringLiteral("Footer");
    PDFPageStamper::Result stamped = PDFPageStamper::addHeaderFooter(&document, headerFooter, { }, false);
    QVERIFY(stamped.isSuccess());
    PDFWatermarkSettings watermarkSettings;
    watermarkSettings.text = QStringLiteral("COPY");
    stamped = PDFPageStamper::addWatermark(stamped.document.data(), watermarkSettings, { }, false);
    QVERIFY(stamped.isSuccess());
    QVERIFY(writeDocument(inputPath("stamped.pdf"), *stamped.document).isEmpty());

    PDFBatchAction remove = PDFBatchAction::create(PDFBatchAction::Type::RemoveStamps);
    remove.removedStamps = PDFPageStamper::Watermark;
    PDFBatchProcessor processor(folderSettings(m_output.path(), { remove }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("stamped.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.details.join(' ').contains("2 stamp"));

    const PDFDocument output = readDocument(outputPath("stamped.pdf"));
    QCOMPARE(PDFPageStamper::getStampedPageCount(&output, PDFPageStamper::Watermark), PDFInteger(0));
    QCOMPARE(PDFPageStamper::getStampedPageCount(&output, PDFPageStamper::HeaderFooter), PDFInteger(2));
    QVERIFY(pageText(output, 0).contains("Text 1"));
}

void BatchProcessorTest::flattenForms()
{
    QVERIFY(writeDocument(inputPath("form.pdf"), createFormDocument()).isEmpty());
    QVERIFY(writeDocument(inputPath("plain.pdf"), createDocument(1, "Plain")).isEmpty());

    PDFBatchProcessor processor(folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::FlattenForms) }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("form.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.details.join(' ').contains("1 form field"));

    const PDFDocument flattened = readDocument(outputPath("form.pdf"));
    QVERIFY(flattened.getCatalog()->getFormObject().isNull() || !flattened.getStorage().getObject(flattened.getCatalog()->getFormObject()).isDictionary());
    QVERIFY(pageText(flattened, 0).contains("LED-42"));

    PDFFormManager formManager(nullptr);
    formManager.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&flattened), nullptr));
    QVERIFY(!formManager.hasAcroForm());
    formManager.setDocument(PDFModifiedDocument());

    // A document without a form is written unchanged
    result = processor.processFile(inputPath("plain.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY(result.isSuccess());
    QVERIFY(result.details.join(' ').contains("no form fields"));
}

void BatchProcessorTest::rotatePages()
{
    QVERIFY(writeDocument(inputPath("pages.pdf"), createDocument(3)).isEmpty());

    PDFBatchAction rotate = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    rotate.pages = QStringLiteral("2");
    rotate.rotation = 90;
    PDFBatchAction rotateAll = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    rotateAll.rotation = 180;
    PDFBatchAction rotateBack = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    rotateBack.pages = QStringLiteral("3");
    rotateBack.rotation = 270;

    PDFBatchProcessor processor(folderSettings(m_output.path(), { rotate, rotateAll, rotateBack }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("pages.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const PDFDocument document = readDocument(outputPath("pages.pdf"));
    QCOMPARE(document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate180);
    QCOMPARE(document.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate270);
    QCOMPARE(document.getCatalog()->getPage(2)->getPageRotation(), PageRotation::Rotate90);
}

void BatchProcessorTest::optimizeAndSanitize()
{
    QVERIFY(writeDocument(inputPath("info.pdf"), createDocument(4, "Optimize", "Secret Title")).isEmpty());
    QCOMPARE(readDocument(inputPath("info.pdf")).getInfo()->title, QStringLiteral("Secret Title"));

    PDFBatchAction optimize = PDFBatchAction::create(PDFBatchAction::Type::Optimize);
    PDFBatchAction sanitize = PDFBatchAction::create(PDFBatchAction::Type::Sanitize);
    PDFBatchProcessor processor(folderSettings(m_output.path(), { optimize, sanitize }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("info.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const PDFDocument document = readDocument(outputPath("info.pdf"));
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(document.getInfo()->title.isEmpty());
    QVERIFY(pageText(document, 3).contains("Optimize 4"));

    // Images are recompressed only when asked; the step works also without the images
    optimize.optimizeImages = true;
    optimize.imageGoal = PDFImageOptimizer::OptimizationGoal::MinimumSize;
    optimize.imageTargetDpi = 72;
    QVERIFY(writeDocument(inputPath("scan.pdf"), createScanDocument()).isEmpty());
    PDFBatchProcessor imageProcessor(folderSettings(m_output.path(), { optimize }));
    result = imageProcessor.processFile(inputPath("scan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(readDocument(outputPath("scan.pdf")).getCatalog()->getPageCount(), size_t(1));
}

void BatchProcessorTest::setPassword()
{
    QVERIFY(writeDocument(inputPath("open.pdf"), createDocument(1, "Protected")).isEmpty());

    PDFBatchAction password = PDFBatchAction::create(PDFBatchAction::Type::SetPassword);
    password.userPassword = QStringLiteral("open-me");
    password.ownerPassword = QStringLiteral("change-me");
    PDFBatchProcessor processor(folderSettings(m_output.path(), { password, PDFBatchAction::create(PDFBatchAction::Type::ExportText) }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("open.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    PDFDocumentReader::Result readResult = PDFDocumentReader::Result::OK;
    readDocument(outputPath("open.pdf"), QString(), &readResult);
    QVERIFY(readResult != PDFDocumentReader::Result::OK);

    const PDFDocument document = readDocument(outputPath("open.pdf"), QStringLiteral("open-me"), &readResult);
    QCOMPARE(readResult, PDFDocumentReader::Result::OK);
    QVERIFY(pageText(document, 0).contains("Protected 1"));
    QCOMPARE(document.getStorage().getSecurityHandler()->getMode(), EncryptionMode::Standard);

    // The text export after the password still sees the text
    QFile text(outputPath("open.txt"));
    QVERIFY(text.open(QIODevice::ReadOnly));
    QVERIFY(text.readAll().contains("Protected 1"));
}

void BatchProcessorTest::protectedOriginal()
{
    // Original with a password to open it
    PDFDocument document = createDocument(1, "Locked");
    PDFSecurityHandlerFactory::SecuritySettings security;
    security.algorithm = PDFSecurityHandlerFactory::AES_256;
    security.userPassword = QStringLiteral("user");
    security.ownerPassword = QStringLiteral("owner");
    security.permissions = uint32_t(PDFSecurityHandler::Permission::CopyContent) | uint32_t(PDFSecurityHandler::Permission::PrintHighResolution);
    PDFDocumentBuilder builder(&document);
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(security));
    document = builder.build();
    QVERIFY(writeDocument(inputPath("locked.pdf"), document).isEmpty());

    PDFBatchSettings settings = folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::ExportText) });
    PDFBatchProcessor noPassword(settings);
    PDFBatchProcessor::FileResult result = noPassword.processFile(inputPath("locked.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("password"));

    settings.password = QStringLiteral("user");
    PDFBatchProcessor withPassword(settings);
    result = withPassword.processFile(inputPath("locked.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    // Changes are not allowed by the user password
    settings.actions = { PDFBatchAction::create(PDFBatchAction::Type::RotatePages) };
    PDFBatchProcessor modify(settings);
    result = modify.processFile(inputPath("locked.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Skipped);
    QVERIFY(result.message.contains("owner password"));

    // The owner password allows the changes, the copy keeps the encryption
    settings.password = QStringLiteral("owner");
    PDFBatchProcessor owner(settings);
    result = owner.processFile(inputPath("locked.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    PDFDocumentReader::Result readResult = PDFDocumentReader::Result::OK;
    readDocument(outputPath("locked.pdf"), QString(), &readResult);
    QVERIFY(readResult != PDFDocumentReader::Result::OK);
    const PDFDocument rotated = readDocument(outputPath("locked.pdf"), QStringLiteral("user"), &readResult);
    QCOMPARE(readResult, PDFDocumentReader::Result::OK);
    QCOMPARE(rotated.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate90);
}

void BatchProcessorTest::exports()
{
    QVERIFY(writeDocument(inputPath("doc.pdf"), createDocument(3, "Export")).isEmpty());

    PDFBatchAction images = PDFBatchAction::create(PDFBatchAction::Type::ExportImages);
    images.imageResolution = 36;
    images.pages = QStringLiteral("1, 3");
    PDFBatchAction word = PDFBatchAction::create(PDFBatchAction::Type::ExportWord);
    word.wordIncludeGraphics = false;

    PDFBatchProcessor processor(folderSettings(m_output.path(), { word, PDFBatchAction::create(PDFBatchAction::Type::ExportText), images }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("doc.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    // No PDF is written without a modifying step
    QCOMPARE(result.outputFiles, QStringList({ outputPath("doc.docx"), outputPath("doc.txt"), outputPath("doc-001.png"), outputPath("doc-003.png") }));
    QVERIFY(!QFileInfo::exists(outputPath("doc.pdf")));

    QFile docx(outputPath("doc.docx"));
    QVERIFY(docx.open(QIODevice::ReadOnly));
    QVERIFY(docx.read(2) == "PK");

    QFile text(outputPath("doc.txt"));
    QVERIFY(text.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(text.readAll());
    QVERIFY(content.contains("Export 1"));
    QVERIFY(content.contains("Export 3"));
    QCOMPARE(content.count(QChar('\f')), 2);

    // 612 x 792 points at 36 DPI
    QImage image(outputPath("doc-003.png"));
    QCOMPARE(image.size(), QSize(306, 396));
    QVERIFY(!QFileInfo::exists(outputPath("doc-002.png")));
}

void BatchProcessorTest::exportsSeeEarlierSteps()
{
    QVERIFY(writeDocument(inputPath("doc.pdf"), createDocument(1, "Body")).isEmpty());

    PDFBatchAction footer = PDFBatchAction::create(PDFBatchAction::Type::HeaderFooter);
    footer.headerFooter.texts = { };
    footer.headerFooter.texts[PDFHeaderFooterSettings::FooterLeft] = QStringLiteral("STAMPED FOOTER");
    PDFBatchAction rotate = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    PDFBatchAction images = PDFBatchAction::create(PDFBatchAction::Type::ExportImages);
    images.imageResolution = 36;

    // Text export between the steps: it has the footer; image after the rotation is landscape
    PDFBatchProcessor processor(folderSettings(m_output.path(), { footer, PDFBatchAction::create(PDFBatchAction::Type::ExportText), rotate, images }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("doc.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputFiles.front(), outputPath("doc.pdf"));

    QFile text(outputPath("doc.txt"));
    QVERIFY(text.open(QIODevice::ReadOnly));
    QVERIFY(text.readAll().contains("STAMPED FOOTER"));
    QCOMPARE(QImage(outputPath("doc-001.png")).size(), QSize(396, 306));
}

void BatchProcessorTest::failureLeavesNoOutput()
{
    QVERIFY(writeDocument(inputPath("short.pdf"), createDocument(2, "Short")).isEmpty());

    // The export is written, then the rotation of a missing page fails
    PDFBatchAction rotate = PDFBatchAction::create(PDFBatchAction::Type::RotatePages);
    rotate.pages = QStringLiteral("5");
    PDFBatchProcessor processor(folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::ExportText), rotate }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("short.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.startsWith(rotate.getName()));
    QVERIFY(result.outputFiles.isEmpty());
    QVERIFY(!QFileInfo::exists(outputPath("short.txt")));
    QVERIFY(!QFileInfo::exists(outputPath("short.pdf")));

    // Invalid settings fail every file
    PDFBatchProcessor invalid(folderSettings(QString(), { PDFBatchAction::create(PDFBatchAction::Type::Optimize) }));
    result = invalid.processFile(inputPath("short.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("output folder"));
}

void BatchProcessorTest::cancellation()
{
    QVERIFY(writeDocument(inputPath("doc.pdf"), createDocument(2, "Cancel")).isEmpty());

    PDFBatchCancelToken token;
    token.cancel();
    QVERIFY(token.isOperationCancelled());

    PDFBatchProcessor processor(folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::ExportText), PDFBatchAction::create(PDFBatchAction::Type::Optimize) }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("doc.pdf"), PDFBatchProcessor::ProgressCallback(), &token);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Cancelled);
    QVERIFY(!QFileInfo::exists(outputPath("doc.txt")));

    // Cancelled during the file (from the progress of the second step): the export is removed
    PDFBatchCancelToken laterToken;
    result = processor.processFile(inputPath("doc.pdf"), [&laterToken](int step, int, const QString&)
    {
        if (step == 1)
        {
            laterToken.cancel();
        }
    }, &laterToken);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Cancelled);
    QVERIFY(result.outputFiles.isEmpty());
    QVERIFY(!QFileInfo::exists(outputPath("doc.txt")));
    QVERIFY(!QFileInfo::exists(outputPath("doc.pdf")));
}

void BatchProcessorTest::unreadableFile()
{
    QFile file(inputPath("broken.pdf"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("This is not a PDF file.");
    file.close();

    PDFBatchProcessor processor(folderSettings(m_output.path(), { PDFBatchAction::create(PDFBatchAction::Type::Optimize) }));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("broken.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("cannot be read"));
    QCOMPARE(result.inputFile, inputPath("broken.pdf"));

    result = processor.processFile(inputPath("missing.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("does not exist"));
}

void BatchProcessorTest::recognizeText()
{
    QVERIFY(writeDocument(inputPath("scan.pdf"), createScanDocument()).isEmpty());

    // The test engine finds one word in the middle of the page
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        const QRectF rect(input.image.width() * 0.2, input.image.height() * 0.4, input.image.width() * 0.4, input.image.height() * 0.1);
        PDFOCRRawBlock block;
        block.rect = rect;
        PDFOCRRawLine line;
        line.rect = rect;
        PDFOCRRawWord word;
        word.text = QStringLiteral("Luminaire");
        word.rect = rect;
        word.rawConfidence = 95.0;
        line.words.push_back(word);
        block.lines.push_back(line);
        output.blocks.push_back(block);
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;
        return output;
    });

    PDFBatchAction ocr = PDFBatchAction::create(PDFBatchAction::Type::RecognizeText);
    ocr.ocrConfiguration.engineId = QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER);
    ocr.ocrConfiguration.workerCount = 1;
    ocr.ocrConfiguration.detectBlankPages = false;

    PDFBatchProcessor processor(folderSettings(m_output.path(), { ocr, PDFBatchAction::create(PDFBatchAction::Type::ExportText) }));
    QVERIFY2(processor.validate().isEmpty(), qPrintable(processor.validate().join('\n')));
    PDFBatchProcessor::FileResult result = processor.processFile(inputPath("scan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.details.join(' ').contains("Text recognized on 1 page"));

    const PDFDocument document = readDocument(outputPath("scan.pdf"));
    std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(&document, 0);
    QVERIFY(layer.has_value());

    QFile text(outputPath("scan.txt"));
    QVERIFY(text.open(QIODevice::ReadOnly));
    QVERIFY(text.readAll().contains("Luminaire"));

    // A page with a text needs no recognition, the file is written anyway
    QVERIFY(writeDocument(inputPath("text.pdf"), createDocument(1, "Digital text of a page, which needs no recognition at all")).isEmpty());
    PDFBatchProcessor textProcessor(folderSettings(m_output.path(), { ocr }));
    result = textProcessor.processFile(inputPath("text.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.details.join(' ').contains("No page needed"));

    // A failed page fails the file, unless the page errors are allowed
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.error = PDFOCRError::create(PDFOCRErrorCode::WorkerCrashed, QStringLiteral("Simulated failure"), QStringLiteral("Recognition"));
        return output;
    });
    QFile::remove(outputPath("scan.pdf"));
    PDFBatchProcessor failing(folderSettings(m_output.path(), { ocr }));
    result = failing.processFile(inputPath("scan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QCOMPARE(result.status, PDFBatchProcessor::FileResult::Status::Failed);
    QVERIFY(result.message.contains("Simulated failure"));
    QVERIFY(!QFileInfo::exists(outputPath("scan.pdf")));

    ocr.ocrAllowPageErrors = true;
    PDFBatchProcessor tolerant(folderSettings(m_output.path(), { ocr }));
    result = tolerant.processFile(inputPath("scan.pdf"), PDFBatchProcessor::ProgressCallback(), nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.details.join(' ').contains("failed"));
}

QTEST_MAIN(BatchProcessorTest)

#include "tst_batchprocessortest.moc"
