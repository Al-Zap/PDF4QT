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


#include "pdfpagestamper.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontentprocessor.h"
#include "pdftextlayout.h"
#include "pdfobjectutils.h"
#include "pdfrenderer.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfoptionalcontent.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QBuffer>
#include <QPainter>

using namespace pdf;

/// Tests of PDFPageStamper: headers and footers, Bates numbering and watermarks
class PageStamperTest : public QObject
{
    Q_OBJECT

private slots:
    void formatBatesNumber();
    void expandVariables();
    void textMetrics();
    void fontAndPositionNames();
    void stampSpaceMatrix_data();
    void stampSpaceMatrix();
    void headerFooterText();
    void batesNumbering();
    void batesNumberingOfSelectedPages();
    void rotatedPageHeaderAtDisplayedTop_data();
    void rotatedPageHeaderAtDisplayedTop();
    void unbalancedContentIsIsolated();
    void replaceAndRemoveStamps();
    void removeRestoresOriginalContent();
    void watermarkText();
    void watermarkImageOverAndBehindContent();
    void watermarkAlignment();
    void sharedObjects();
    void sharedResourcesAreNotChanged();
    void inheritedResourcesAreKept();
    void stampsSurviveWriteAndRead();
    void invalidInput();

private:
    struct PageDescription
    {
        QRectF mediaBox = QRectF(0, 0, 612, 792);
        QRectF cropBox;
        int rotate = 0;
        PDFReal userUnit = 0.0;
        QByteArray content = "0 0 1 rg 100 100 50 50 re f\n";
    };

    static PDFDocument createDocument(const std::vector<PageDescription>& pages);
    static PDFDocument createDocument(int pageCount);
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);

    /// Returns the text items of the page (text and its bounding rectangle in the page space)
    static std::vector<std::pair<QString, QRectF>> getTextItems(const PDFDocument& document, PDFInteger pageIndex);

    /// Returns the bounding rectangle of the text item containing the text (or an invalid rectangle)
    static QRectF findText(const PDFDocument& document, PDFInteger pageIndex, const QString& text);

    /// Returns the whole text of the page
    static QString getPageText(const PDFDocument& document, PDFInteger pageIndex);

    /// Renders the page into an image of the size of the displayed media box
    static QImage renderPage(const PDFDocument& document, PDFInteger pageIndex, QList<PDFRenderError>* errors);

    /// Returns the references of the content streams of the page
    static std::vector<PDFObjectReference> getContentReferences(const PDFDocument& document, PDFInteger pageIndex);

    /// Returns the decoded content of the stream
    static QByteArray getStreamContent(const PDFDocument& document, PDFObjectReference reference);

    /// Returns the form XObjects of the stamps on the page (from the page resources)
    static std::vector<PDFObjectReference> getStampForms(const PDFDocument& document, PDFInteger pageIndex);
};

PDFDocument PageStamperTest::createDocument(const std::vector<PageDescription>& pages)
{
    PDFDocumentBuilder builder;
    for (const PageDescription& description : pages)
    {
        const PDFObjectReference page = builder.appendPage(description.mediaBox);

        PDFDictionaryBuilder contentDictionary;
        contentDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(description.content.size()));
        const PDFObjectReference content = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), QByteArray(description.content))));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << content;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory.beginDictionary();
        factory.endDictionary();
        factory.endDictionaryItem();
        if (description.cropBox.isValid())
        {
            factory.beginDictionaryItem("CropBox");
            factory << description.cropBox;
            factory.endDictionaryItem();
        }
        if (description.rotate != 0)
        {
            factory.beginDictionaryItem("Rotate");
            factory << PDFInteger(description.rotate);
            factory.endDictionaryItem();
        }
        if (description.userUnit > 0.0)
        {
            factory.beginDictionaryItem("UserUnit");
            factory << description.userUnit;
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        builder.mergeTo(page, factory.takeObject());
    }

    return read(write(builder.build()));
}

PDFDocument PageStamperTest::createDocument(int pageCount)
{
    return createDocument(std::vector<PageDescription>(pageCount));
}

QByteArray PageStamperTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument PageStamperTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
    }
    return document;
}

/// Collects the drawn characters into runs of text (characters following each other)
class PageStamperTextCollector : public PDFPageContentProcessor
{
public:
    using PDFPageContentProcessor::PDFPageContentProcessor;

    std::vector<std::pair<QString, QRectF>> items;

protected:
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        const QPointF origin = info.matrix.map(QPointF(0.0, 0.0));
        const QPolygonF polygon = info.matrix.map(QPolygonF(QRectF(0.0, 0.0, info.advance, info.fontSize * 0.7)));
        const QRectF boundingRect = polygon.boundingRect();

        const bool isContinuation = !items.empty() && QLineF(origin, m_lastEnd).length() < 0.5;
        if (isContinuation)
        {
            items.back().first.append(info.character);
            items.back().second = items.back().second.united(boundingRect);
        }
        else
        {
            items.emplace_back(QString(info.character), boundingRect);
        }

        m_lastEnd = info.matrix.map(QPointF(info.advance, 0.0));
    }

private:
    QPointF m_lastEnd;
};

std::vector<std::pair<QString, QRectF>> PageStamperTest::getTextItems(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity));
    PDFCMSGeneric cms;

    PageStamperTextCollector collector(document.getCatalog()->getPage(pageIndex), &document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, QTransform(), PDFMeshQualitySettings());
    const QList<PDFRenderError> errors = collector.processContents();
    for (const PDFRenderError& error : errors)
    {
        qWarning() << error.message;
    }
    return collector.items;
}

QRectF PageStamperTest::findText(const PDFDocument& document, PDFInteger pageIndex, const QString& text)
{
    for (const auto& item : getTextItems(document, pageIndex))
    {
        if (item.first.contains(text))
        {
            return item.second;
        }
    }
    return QRectF();
}

QString PageStamperTest::getPageText(const PDFDocument& document, PDFInteger pageIndex)
{
    QStringList texts;
    for (const auto& item : getTextItems(document, pageIndex))
    {
        texts << item.first;
    }
    return texts.join(QChar('\n'));
}

QImage PageStamperTest::renderPage(const PDFDocument& document, PDFInteger pageIndex, QList<PDFRenderError>* errors)
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
    *errors = renderer.render(&painter, QRectF(QPointF(0, 0), size), pageIndex);
    painter.end();
    return image;
}

std::vector<PDFObjectReference> PageStamperTest::getContentReferences(const PDFDocument& document, PDFInteger pageIndex)
{
    std::vector<PDFObjectReference> references;
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFDictionary* pageDictionary = document.getDictionaryFromObject(document.getObjectByReference(page->getPageReference()));
    PDFObject contents = pageDictionary->get("Contents");
    if (contents.isReference() && document.getObject(contents).isStream())
    {
        references.push_back(contents.getReference());
        return references;
    }

    contents = document.getObject(contents);
    if (contents.isArray())
    {
        for (const PDFObject& item : *contents.getArray())
        {
            references.push_back(item.getReference());
        }
    }
    return references;
}

QByteArray PageStamperTest::getStreamContent(const PDFDocument& document, PDFObjectReference reference)
{
    const PDFObject& object = document.getObjectByReference(reference);
    return object.isStream() ? document.getDecodedStream(object.getStream()) : QByteArray();
}

std::vector<PDFObjectReference> PageStamperTest::getStampForms(const PDFDocument& document, PDFInteger pageIndex)
{
    std::vector<PDFObjectReference> forms;
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    const PDFDictionary* xobjects = resources ? document.getDictionaryFromObject(resources->get("XObject")) : nullptr;
    if (xobjects)
    {
        for (size_t i = 0; i < xobjects->getCount(); ++i)
        {
            if (xobjects->getKey(i).getString().startsWith("PDF4QTStamp"))
            {
                forms.push_back(xobjects->getValue(i).getReference());
            }
        }
    }
    return forms;
}

void PageStamperTest::formatBatesNumber()
{
    QCOMPARE(PDFPageStamper::formatBatesNumber("ABC", 1, 6, QString()), QString("ABC000001"));
    QCOMPARE(PDFPageStamper::formatBatesNumber("SMITH-", 1234, 6, "-CONF"), QString("SMITH-001234-CONF"));
    QCOMPARE(PDFPageStamper::formatBatesNumber(QString(), 1234567, 6, QString()), QString("1234567"));
    QCOMPARE(PDFPageStamper::formatBatesNumber(QString(), 7, 0, QString()), QString("7"));
    QCOMPARE(PDFPageStamper::formatBatesNumber(QString(), 7, 40, QString()), QString("000000000000007"));
}

void PageStamperTest::expandVariables()
{
    PDFStampVariables variables;
    variables.startPageNumber = 3;
    variables.batesPrefix = "DEF";
    variables.batesStart = 98;
    variables.batesDigits = 4;
    variables.batesSuffix = "X";
    variables.dateTime = QDateTime(QDate(2026, 11, 24), QTime(14, 5));
    variables.dateFormat = "yyyy-MM-dd";
    variables.timeFormat = "HH:mm";
    variables.fileName = "plan.pdf";

    QCOMPARE(PDFPageStamper::expandVariables("Page {page} of {pages}", variables, 0, 10, "Title"), QString("Page 3 of 12"));
    QCOMPARE(PDFPageStamper::expandVariables("Page {page} of {pages}", variables, 9, 10, "Title"), QString("Page 12 of 12"));
    QCOMPARE(PDFPageStamper::expandVariables("{bates}", variables, 0, 10, QString()), QString("DEF0098X"));
    QCOMPARE(PDFPageStamper::expandVariables("{bates}", variables, 3, 10, QString()), QString("DEF0101X"));
    QCOMPARE(PDFPageStamper::expandVariables("{date} {time}", variables, 0, 1, QString()), QString("2026-11-24 14:05"));
    QCOMPARE(PDFPageStamper::expandVariables("{FILE} - { title }", variables, 0, 1, "Lighting"), QString("plan.pdf - Lighting"));
    QCOMPARE(PDFPageStamper::expandVariables("{{page} {unknown} {page", variables, 0, 1, QString()), QString("{page} {unknown} {page"));
    QCOMPARE(PDFPageStamper::expandVariables(QString(), variables, 0, 1, QString()), QString());
}

void PageStamperTest::textMetrics()
{
    // Widths of the Adobe font metrics: H 722, e 556, l 222, o 556
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::Helvetica, "Hello", 10.0), 22.78);
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::HelveticaBold, "Hello", 10.0), 24.45);
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::TimesRoman, "Hello", 10.0), 22.22);
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::TimesBold, "Hello", 10.0), 22.78);
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::Courier, "abc", 12.0), 21.6);
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::CourierBold, QString(), 12.0), 0.0);

    // Characters outside of WinAnsiEncoding are written as '?'
    QCOMPARE(PDFPageStamper::getRepresentableText(QString::fromUtf8("Café €5 日本\tA\u0001")), QString::fromUtf8("Café €5 ?? A?"));
    QCOMPARE(PDFPageStamper::getTextWidth(PDFStampFont::Helvetica, QString::fromUtf8("日"), 1000.0), PDFPageStamper::getTextWidth(PDFStampFont::Helvetica, "?", 1000.0));

    QVERIFY(PDFPageStamper::getFontAscent(PDFStampFont::TimesRoman) > 0.0);
    QVERIFY(PDFPageStamper::getFontDescent(PDFStampFont::Courier) < 0.0);
}

void PageStamperTest::fontAndPositionNames()
{
    QCOMPARE(PDFPageStamper::getFontName(PDFStampFont::HelveticaBold), QByteArray("Helvetica-Bold"));
    QCOMPARE(PDFPageStamper::getFontFromName("helvetica-bold"), std::optional<PDFStampFont>(PDFStampFont::HelveticaBold));
    QCOMPARE(PDFPageStamper::getFontFromName("Times-Roman"), std::optional<PDFStampFont>(PDFStampFont::TimesRoman));
    QCOMPARE(PDFPageStamper::getFontFromName("times"), std::optional<PDFStampFont>(PDFStampFont::TimesRoman));
    QCOMPARE(PDFPageStamper::getFontFromName("COURIER"), std::optional<PDFStampFont>(PDFStampFont::Courier));
    QVERIFY(!PDFPageStamper::getFontFromName("Comic Sans").has_value());

    for (int i = 0; i < PDFHeaderFooterSettings::PositionCount; ++i)
    {
        const auto position = static_cast<PDFHeaderFooterSettings::Position>(i);
        QCOMPARE(PDFHeaderFooterSettings::getPositionFromName(PDFHeaderFooterSettings::getPositionName(position)), position);
    }
    QCOMPARE(PDFHeaderFooterSettings::getPositionFromName(" Footer-Right "), PDFHeaderFooterSettings::FooterRight);
    QCOMPARE(PDFHeaderFooterSettings::getPositionFromName("middle"), PDFHeaderFooterSettings::PositionCount);
}

void PageStamperTest::stampSpaceMatrix_data()
{
    QTest::addColumn<int>("rotate");
    QTest::addColumn<QRectF>("cropBox");
    QTest::addColumn<double>("userUnit");

    QTest::newRow("0") << 0 << QRectF() << 0.0;
    QTest::newRow("90") << 90 << QRectF() << 0.0;
    QTest::newRow("180") << 180 << QRectF() << 0.0;
    QTest::newRow("270") << 270 << QRectF() << 0.0;
    QTest::newRow("0 crop") << 0 << QRectF(50, 60, 400, 500) << 0.0;
    QTest::newRow("90 crop") << 90 << QRectF(50, 60, 400, 500) << 0.0;
    QTest::newRow("180 crop") << 180 << QRectF(50, 60, 400, 500) << 0.0;
    QTest::newRow("270 crop") << 270 << QRectF(50, 60, 400, 500) << 0.0;
    QTest::newRow("90 user unit") << 90 << QRectF() << 2.0;
}

void PageStamperTest::stampSpaceMatrix()
{
    QFETCH(int, rotate);
    QFETCH(QRectF, cropBox);
    QFETCH(double, userUnit);

    PageDescription description;
    description.rotate = rotate;
    description.cropBox = cropBox;
    description.userUnit = userUnit;
    const PDFDocument document = createDocument({ description });
    const PDFPage* page = document.getCatalog()->getPage(0);

    const QRectF visibleBox = cropBox.isValid() ? cropBox : description.mediaBox;
    const PDFReal unit = userUnit > 0.0 ? userUnit : 1.0;
    const QSizeF size = PDFPageStamper::getStampSpaceSize(page);
    const bool isSideways = rotate == 90 || rotate == 270;
    QCOMPARE(size, QSizeF((isSideways ? visibleBox.height() : visibleBox.width()) * unit, (isSideways ? visibleBox.width() : visibleBox.height()) * unit));

    // The stamp space is mapped onto the visible box of the page, as the renderer displays it:
    // bottom left corner of the stamp space is the bottom left corner of the displayed page.
    const QTransform stampToPage = PDFPageStamper::getStampSpaceToPageMatrix(page);
    const QRectF deviceRect(QPointF(0, 0), page->getRotatedMediaBox().size());
    const QTransform pageToDevice = PDFRenderer::createPagePointToDevicePointMatrix(page, deviceRect);
    const QTransform stampToDevice = stampToPage * pageToDevice;

    const QRectF deviceVisibleBox = pageToDevice.mapRect(visibleBox);
    auto compare = [](QPointF a, QPointF b) { return qAbs(a.x() - b.x()) < 0.001 && qAbs(a.y() - b.y()) < 0.001; };
    QVERIFY(compare(stampToDevice.map(QPointF(0, 0)), deviceVisibleBox.bottomLeft()));
    QVERIFY(compare(stampToDevice.map(QPointF(size.width(), 0)), deviceVisibleBox.bottomRight()));
    QVERIFY(compare(stampToDevice.map(QPointF(0, size.height())), deviceVisibleBox.topLeft()));
    QVERIFY(compare(stampToDevice.map(QPointF(size.width(), size.height())), deviceVisibleBox.topRight()));
}

void PageStamperTest::headerFooterText()
{
    const PDFDocument document = createDocument(3);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::HeaderCenter] = "CONFIDENTIAL";
    settings.texts[PDFHeaderFooterSettings::HeaderLeft] = "Project\nPhase 2";
    settings.texts[PDFHeaderFooterSettings::FooterRight] = "Page {page} of {pages}";
    settings.style.fontSize = 12.0;

    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QVERIFY(result.document);
    QCOMPARE(result.pageCount, 3);
    QVERIFY(result.firstBatesNumber.isEmpty());
    QVERIFY(result.flags.testFlag(PDFModifiedDocument::PageContents));

    const PDFDocument stamped = read(write(*result.document));
    for (PDFInteger i = 0; i < 3; ++i)
    {
        QCOMPARE(PDFPageStamper::getStampKinds(&stamped, i), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));

        const QRectF header = findText(stamped, i, "CONFIDENTIAL");
        QVERIFY(header.isValid());
        QVERIFY(qAbs(header.center().x() - 306.0) < 2.0);
        QVERIFY(header.top() > 700.0);
        QVERIFY(qMax(header.top(), header.bottom()) <= 792.0 - 36.0 + 1.0);

        const QString pageText = QString("Page %1 of 3").arg(i + 1);
        const QRectF footer = findText(stamped, i, pageText);
        QVERIFY2(footer.isValid(), qPrintable(getPageText(stamped, i)));
        QVERIFY(qMin(footer.top(), footer.bottom()) >= 36.0 - 4.0);
        QVERIFY(qMax(footer.top(), footer.bottom()) < 60.0);
        QVERIFY(qAbs(footer.right() - (612.0 - 36.0)) < 2.0);

        const QRectF firstLine = findText(stamped, i, "Project");
        const QRectF secondLine = findText(stamped, i, "Phase 2");
        QVERIFY(firstLine.isValid() && secondLine.isValid());
        QVERIFY(firstLine.center().y() > secondLine.center().y());
        QVERIFY(qAbs(firstLine.left() - 36.0) < 2.0);
        QVERIFY(qAbs(secondLine.left() - 36.0) < 2.0);
    }

    QList<PDFRenderError> errors;
    renderPage(stamped, 0, &errors);
    QVERIFY(errors.isEmpty());

    // Original document is not changed
    QCOMPARE(PDFPageStamper::getStampKinds(&document, 0), PDFPageStamper::StampKinds(PDFPageStamper::None));
    QCOMPARE(getContentReferences(document, 0).size(), size_t(1));
}

void PageStamperTest::batesNumbering()
{
    const PDFDocument document = createDocument(4);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterRight] = "{bates}";
    settings.variables.batesPrefix = "ABC";
    settings.variables.batesStart = 41;
    settings.variables.batesDigits = 6;

    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.firstBatesNumber, QString("ABC000041"));
    QCOMPARE(result.lastBatesNumber, QString("ABC000044"));
    QCOMPARE(result.nextBatesNumber, PDFInteger(45));

    for (PDFInteger i = 0; i < 4; ++i)
    {
        const QRectF rect = findText(*result.document, i, QString("ABC0000%1").arg(41 + i));
        QVERIFY(rect.isValid());
        QVERIFY(rect.center().y() < 100.0);
    }

    // Continuation in the next document
    settings.variables.batesStart = result.nextBatesNumber;
    const PDFPageStamper::Result next = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QCOMPARE(next.firstBatesNumber, QString("ABC000045"));
    QVERIFY(findText(*next.document, 0, "ABC000045").isValid());
}

void PageStamperTest::batesNumberingOfSelectedPages()
{
    const PDFDocument document = createDocument(5);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterLeft] = "{bates} / {page}";
    settings.variables.batesPrefix = "P";
    settings.variables.batesDigits = 3;

    // Duplicates and invalid indices are ignored, the order is the order of the pages
    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, { 3, 1, 3, -1, 99 }, false);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.pageCount, 2);
    QCOMPARE(result.lastBatesNumber, QString("P002"));

    QCOMPARE(PDFPageStamper::getStampKinds(result.document.data(), 0), PDFPageStamper::StampKinds(PDFPageStamper::None));
    QCOMPARE(PDFPageStamper::getStampKinds(result.document.data(), 1), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
    QCOMPARE(PDFPageStamper::getStampKinds(result.document.data(), 2), PDFPageStamper::StampKinds(PDFPageStamper::None));
    QCOMPARE(PDFPageStamper::getStampKinds(result.document.data(), 3), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
    QCOMPARE(PDFPageStamper::getStampedPageCount(result.document.data(), PDFPageStamper::AllKinds), PDFInteger(2));

    QVERIFY(findText(*result.document, 1, "P001 / 1").isValid());
    QVERIFY(findText(*result.document, 3, "P002 / 2").isValid());
    QVERIFY(getPageText(*result.document, 0).isEmpty());
}

void PageStamperTest::rotatedPageHeaderAtDisplayedTop_data()
{
    QTest::addColumn<int>("rotate");
    QTest::newRow("0") << 0;
    QTest::newRow("90") << 90;
    QTest::newRow("180") << 180;
    QTest::newRow("270") << 270;
}

void PageStamperTest::rotatedPageHeaderAtDisplayedTop()
{
    QFETCH(int, rotate);

    PageDescription description;
    description.rotate = rotate;
    description.cropBox = QRectF(20, 30, 500, 700);
    const PDFDocument document = createDocument({ description });

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::HeaderLeft] = "TOPLEFT";
    settings.texts[PDFHeaderFooterSettings::FooterRight] = "BOTTOMRIGHT";
    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());

    const PDFPage* page = result.document->getCatalog()->getPage(0);
    const QRectF deviceRect(QPointF(0, 0), page->getRotatedMediaBox().size());
    const QTransform pageToDevice = PDFRenderer::createPagePointToDevicePointMatrix(page, deviceRect);
    const QRectF visible = pageToDevice.mapRect(description.cropBox);

    const QRectF topLeft = pageToDevice.mapRect(findText(*result.document, 0, "TOPLEFT"));
    const QRectF bottomRight = pageToDevice.mapRect(findText(*result.document, 0, "BOTTOMRIGHT"));
    QVERIFY(topLeft.isValid() && bottomRight.isValid());

    // Text is upright on the displayed page (wider than tall) and at the corners of the visible box
    QVERIFY(topLeft.width() > topLeft.height());
    QVERIFY(bottomRight.width() > bottomRight.height());
    QVERIFY(qAbs(topLeft.left() - (visible.left() + 36.0)) < 2.0);
    QVERIFY(topLeft.top() >= visible.top() + 36.0 - 2.0 && topLeft.top() < visible.top() + 50.0);
    QVERIFY(qAbs(bottomRight.right() - (visible.right() - 36.0)) < 2.0);
    QVERIFY(bottomRight.bottom() <= visible.bottom() - 36.0 + 4.0 && bottomRight.bottom() > visible.bottom() - 50.0);
}

void PageStamperTest::unbalancedContentIsIsolated()
{
    // Original content leaves a scaled transformation, an open text object and marked content
    PageDescription unbalanced;
    unbalanced.content = "q 0.25 0 0 0.25 0 0 cm 1 0 0 rg /P BMC BT\n";
    PageDescription balanced;
    const PDFDocument document = createDocument({ balanced, unbalanced });

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterCenter] = "FOOTER";
    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());

    const std::vector<PDFObjectReference> balancedContents = getContentReferences(*result.document, 0);
    const std::vector<PDFObjectReference> unbalancedContents = getContentReferences(*result.document, 1);
    QCOMPARE(balancedContents.size(), size_t(4));
    QCOMPARE(unbalancedContents.size(), size_t(4));
    QCOMPARE(getStreamContent(*result.document, balancedContents[0]), QByteArray("q\n"));
    QCOMPARE(getStreamContent(*result.document, balancedContents[2]), QByteArray("Q\n"));
    QCOMPARE(getStreamContent(*result.document, unbalancedContents[0]), QByteArray("q\n"));
    QCOMPARE(getStreamContent(*result.document, unbalancedContents[2]), QByteArray("ET\nEMC\nQ\nQ\n"));

    // Isolation streams of balanced pages are shared, the footer is at the same place on both pages
    QCOMPARE(balancedContents[0], unbalancedContents[0]);
    const QRectF balancedFooter = findText(*result.document, 0, "FOOTER");
    const QRectF unbalancedFooter = findText(*result.document, 1, "FOOTER");
    QVERIFY(balancedFooter.isValid());
    QVERIFY(unbalancedFooter.isValid());
    QVERIFY(qAbs(balancedFooter.center().x() - unbalancedFooter.center().x()) < 0.5);
    QVERIFY(qAbs(balancedFooter.center().y() - unbalancedFooter.center().y()) < 0.5);

    // More Q than q
    PageDescription overclosed;
    overclosed.content = "Q Q 0 0 1 rg\n";
    const PDFDocument overclosedDocument = createDocument({ overclosed });
    const PDFPageStamper::Result overclosedResult = PDFPageStamper::addHeaderFooter(&overclosedDocument, settings, {}, false);
    QVERIFY(overclosedResult.isSuccess());
    const std::vector<PDFObjectReference> overclosedContents = getContentReferences(*overclosedResult.document, 0);
    QCOMPARE(getStreamContent(*overclosedResult.document, overclosedContents.front()), QByteArray("q\nq\nq\n"));
    QCOMPARE(getStreamContent(*overclosedResult.document, overclosedContents[2]), QByteArray("Q\n"));
}

void PageStamperTest::replaceAndRemoveStamps()
{
    const PDFDocument document = createDocument(2);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::HeaderRight] = "FIRST";
    PDFPageStamper::Result first = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(first.isSuccess());

    // Added without replacing: both headers are present
    settings.texts[PDFHeaderFooterSettings::HeaderRight] = "SECOND";
    PDFPageStamper::Result added = PDFPageStamper::addHeaderFooter(first.document.data(), settings, {}, false);
    QVERIFY(added.isSuccess());
    QCOMPARE(added.removedStampCount, 0);
    QVERIFY(findText(*added.document, 0, "FIRST").isValid());
    QVERIFY(findText(*added.document, 0, "SECOND").isValid());
    QCOMPARE(getStampForms(*added.document, 0).size(), size_t(2));
    QCOMPARE(getContentReferences(*added.document, 0).size(), size_t(5));

    // Replaced: only the new header is present
    settings.texts[PDFHeaderFooterSettings::HeaderRight] = "THIRD";
    PDFPageStamper::Result replaced = PDFPageStamper::addHeaderFooter(added.document.data(), settings, {}, true);
    QVERIFY(replaced.isSuccess());
    QCOMPARE(replaced.removedStampCount, 4);
    QVERIFY(!findText(*replaced.document, 0, "FIRST").isValid());
    QVERIFY(!findText(*replaced.document, 0, "SECOND").isValid());
    QVERIFY(findText(*replaced.document, 0, "THIRD").isValid());
    QCOMPARE(getStampForms(*replaced.document, 0).size(), size_t(1));
    QCOMPARE(getContentReferences(*replaced.document, 0).size(), size_t(4));

    // Watermark is not replaced by a header
    PDFWatermarkSettings watermark;
    watermark.text = "DRAFT";
    PDFPageStamper::Result watermarked = PDFPageStamper::addWatermark(replaced.document.data(), watermark, {}, false);
    QVERIFY(watermarked.isSuccess());
    PDFPageStamper::Result headerReplaced = PDFPageStamper::addHeaderFooter(watermarked.document.data(), settings, {}, true);
    QCOMPARE(PDFPageStamper::getStampKinds(headerReplaced.document.data(), 0), PDFPageStamper::StampKinds(PDFPageStamper::AllKinds));
    QVERIFY(findText(*headerReplaced.document, 0, "DRAFT").isValid());

    // Removal of the watermarks only
    PDFPageStamper::Result withoutWatermark = PDFPageStamper::removeStamps(headerReplaced.document.data(), PDFPageStamper::Watermark, {});
    QVERIFY(withoutWatermark.isSuccess());
    QCOMPARE(withoutWatermark.removedStampCount, 2);
    QCOMPARE(withoutWatermark.pageCount, 2);
    QCOMPARE(PDFPageStamper::getStampKinds(withoutWatermark.document.data(), 1), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
    QVERIFY(!findText(*withoutWatermark.document, 0, "DRAFT").isValid());
    QVERIFY(findText(*withoutWatermark.document, 0, "THIRD").isValid());

    // Nothing to remove: document is not changed
    PDFPageStamper::Result nothing = PDFPageStamper::removeStamps(withoutWatermark.document.data(), PDFPageStamper::Watermark, {});
    QVERIFY(nothing.isSuccess());
    QVERIFY(!nothing.document);
    QCOMPARE(nothing.removedStampCount, 0);
}

void PageStamperTest::removeRestoresOriginalContent()
{
    const PDFDocument document = createDocument(2);
    const std::vector<PDFObjectReference> originalContents = getContentReferences(document, 0);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterCenter] = "{page}";
    PDFPageStamper::Result stamped = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    PDFWatermarkSettings watermark;
    watermark.text = "COPY";
    watermark.behindContent = true;
    PDFPageStamper::Result watermarked = PDFPageStamper::addWatermark(stamped.document.data(), watermark, {}, false);
    QVERIFY(watermarked.isSuccess());
    QCOMPARE(getContentReferences(*watermarked.document, 0).size(), size_t(5));

    const PDFDocument reread = read(write(*watermarked.document));
    PDFPageStamper::Result removed = PDFPageStamper::removeStamps(&reread, PDFPageStamper::AllKinds, {});
    QVERIFY(removed.isSuccess());
    QCOMPARE(removed.removedStampCount, 4);

    for (PDFInteger i = 0; i < 2; ++i)
    {
        QCOMPARE(getContentReferences(*removed.document, i), getContentReferences(document, i));
        QCOMPARE(PDFPageStamper::getStampKinds(removed.document.data(), i), PDFPageStamper::StampKinds(PDFPageStamper::None));
        QVERIFY(getStampForms(*removed.document, i).empty());

        const PDFPage* page = removed.document->getCatalog()->getPage(i);
        const PDFDictionary* pageDictionary = removed.document->getDictionaryFromObject(removed.document->getObjectByReference(page->getPageReference()));
        QVERIFY(!pageDictionary->hasKey("PieceInfo"));
        QVERIFY(getPageText(*removed.document, i).isEmpty());
    }
    QCOMPARE(getContentReferences(*removed.document, 0), originalContents);
}

void PageStamperTest::watermarkText()
{
    const PDFDocument document = createDocument(2);

    PDFWatermarkSettings settings;
    settings.text = "DRAFT";
    settings.style.opacity = 0.3;
    settings.relativeSize = 0.6;
    settings.rotation = 0.0;

    const PDFPageStamper::Result result = PDFPageStamper::addWatermark(&document, settings, {}, false);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.pageCount, 2);
    QCOMPARE(PDFPageStamper::getStampKinds(result.document.data(), 0), PDFPageStamper::StampKinds(PDFPageStamper::Watermark));

    // Fitted to 60 % of the page width, centered
    const QRectF rect = findText(*result.document, 0, "DRAFT");
    QVERIFY(rect.isValid());
    QVERIFY(qAbs(rect.center().x() - 306.0) < 5.0);
    QVERIFY(qAbs(rect.width() - 612.0 * 0.6) < 0.06 * 612.0);

    // Opacity is set by the graphic state of the form
    const std::vector<PDFObjectReference> forms = getStampForms(*result.document, 0);
    QCOMPARE(forms.size(), size_t(1));
    const PDFObject& formObject = result.document->getObjectByReference(forms.front());
    QVERIFY(formObject.isStream());
    const PDFDictionary* formResources = result.document->getDictionaryFromObject(formObject.getStream()->getDictionary()->get("Resources"));
    QVERIFY(formResources);
    const PDFDictionary* graphicStates = result.document->getDictionaryFromObject(formResources->get("ExtGState"));
    QVERIFY(graphicStates);
    const PDFDictionary* graphicState = result.document->getDictionaryFromObject(graphicStates->get("GS0"));
    QVERIFY(graphicState);
    PDFDocumentDataLoaderDecorator loader(result.document.data());
    QCOMPARE(loader.readNumberFromDictionary(graphicState, "ca", 1.0), 0.3);
    QCOMPARE(loader.readNumberFromDictionary(graphicState, "CA", 1.0), 0.3);
    QVERIFY(getStreamContent(*result.document, forms.front()).contains("/GS0 gs"));

    // Watermark is marked as a pagination artifact
    const std::vector<PDFObjectReference> contents = getContentReferences(*result.document, 0);
    QVERIFY(getStreamContent(*result.document, contents.back()).startsWith("/Artifact <</Type /Pagination /Subtype /Watermark>> BDC"));

    // Rotated watermark with a fixed font size and more lines
    settings.text = "DO NOT\nCOPY";
    settings.rotation = 90.0;
    settings.style.fontSize = 40.0;
    const PDFPageStamper::Result rotated = PDFPageStamper::addWatermark(&document, settings, {}, false);
    QVERIFY(rotated.isSuccess());
    const QRectF line1 = findText(*rotated.document, 0, "DO NOT");
    const QRectF line2 = findText(*rotated.document, 0, "COPY");
    QVERIFY(line1.isValid() && line2.isValid());
    QVERIFY(line1.height() > line1.width());
    QVERIFY(line1.center().x() < line2.center().x());
    QVERIFY(qAbs(line1.height() - PDFPageStamper::getTextWidth(PDFStampFont::HelveticaBold, "DO NOT", 40.0)) < 4.0);

    QList<PDFRenderError> errors;
    renderPage(*rotated.document, 0, &errors);
    QVERIFY(errors.isEmpty());
}

void PageStamperTest::watermarkImageOverAndBehindContent()
{
    // Page is covered by an opaque white rectangle
    PageDescription description;
    description.content = "1 1 1 rg 0 0 612 792 re f\n";
    const PDFDocument document = createDocument({ description });

    QImage image(40, 20, QImage::Format_ARGB32);
    image.fill(QColor(255, 0, 0));
    image.setPixelColor(0, 0, QColor(0, 0, 0, 0));

    PDFWatermarkSettings settings;
    settings.image = image;
    settings.rotation = 0.0;
    settings.relativeSize = 0.5;
    settings.style.opacity = 1.0;

    const PDFPageStamper::Result over = PDFPageStamper::addWatermark(&document, settings, {}, false);
    QVERIFY(over.isSuccess());

    // Image with an alpha channel has a soft mask
    const std::vector<PDFObjectReference> forms = getStampForms(*over.document, 0);
    QCOMPARE(forms.size(), size_t(1));
    const PDFStream* formStream = over.document->getObjectByReference(forms.front()).getStream();
    const PDFDictionary* formResources = over.document->getDictionaryFromObject(formStream->getDictionary()->get("Resources"));
    const PDFDictionary* xobjects = over.document->getDictionaryFromObject(formResources->get("XObject"));
    const PDFObject& imageObject = over.document->getObject(xobjects->get("Im0"));
    QVERIFY(imageObject.isStream());
    PDFDocumentDataLoaderDecorator loader(over.document.data());
    QCOMPARE(loader.readIntegerFromDictionary(imageObject.getStream()->getDictionary(), "Width", 0), PDFInteger(40));
    QVERIFY(imageObject.getStream()->getDictionary()->hasKey("SMask"));

    QList<PDFRenderError> errors;
    QImage rendered = renderPage(*over.document, 0, &errors);
    QVERIFY(errors.isEmpty());
    QColor center = rendered.pixelColor(306, 396);
    QVERIFY2(center.red() > 200 && center.green() < 60 && center.blue() < 60, qPrintable(center.name()));

    // Image is 306 x 153 points (half of the page width), outside of it the page is white
    QCOMPARE(rendered.pixelColor(306, 396 - 90), QColor(Qt::white));
    QCOMPARE(rendered.pixelColor(306 - 170, 396), QColor(Qt::white));
    QColor inside = rendered.pixelColor(306 + 140, 396 + 60);
    QVERIFY(inside.red() > 200 && inside.green() < 60);

    // Behind the opaque content, the watermark is not visible
    settings.behindContent = true;
    const PDFPageStamper::Result behind = PDFPageStamper::addWatermark(&document, settings, {}, false);
    QVERIFY(behind.isSuccess());
    const std::vector<PDFObjectReference> contents = getContentReferences(*behind.document, 0);
    QVERIFY(getStreamContent(*behind.document, contents.front()).contains("Do"));
    rendered = renderPage(*behind.document, 0, &errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(rendered.pixelColor(306, 396), QColor(Qt::white));
}

void PageStamperTest::watermarkAlignment()
{
    const PDFDocument document = createDocument(1);

    PDFWatermarkSettings settings;
    settings.text = "CORNER";
    settings.rotation = 0.0;
    settings.style.fontSize = 20.0;
    settings.alignment = Qt::AlignRight | Qt::AlignBottom;
    settings.margins = QMarginsF(10, 10, 10, 10);
    settings.offset = QPointF(-5, 15);

    const PDFPageStamper::Result result = PDFPageStamper::addWatermark(&document, settings, {}, false);
    QVERIFY(result.isSuccess());
    const QRectF rect = findText(*result.document, 0, "CORNER");
    QVERIFY(rect.isValid());
    QVERIFY(qAbs(rect.right() - (612.0 - 10.0 - 5.0)) < 2.0);
    QVERIFY(qMin(rect.top(), rect.bottom()) > 10.0 + 15.0 - 2.0);
    QVERIFY(qMin(rect.top(), rect.bottom()) < 10.0 + 15.0 + 8.0);

    settings.alignment = Qt::AlignLeft | Qt::AlignTop;
    settings.offset = QPointF();
    const PDFPageStamper::Result topLeft = PDFPageStamper::addWatermark(&document, settings, {}, false);
    const QRectF topLeftRect = findText(*topLeft.document, 0, "CORNER");
    QVERIFY(qAbs(topLeftRect.left() - 10.0) < 2.0);
    QVERIFY(qMax(topLeftRect.top(), topLeftRect.bottom()) > 792.0 - 10.0 - 8.0);
    QVERIFY(qMax(topLeftRect.top(), topLeftRect.bottom()) <= 792.0 - 10.0 + 1.0);
}

void PageStamperTest::sharedObjects()
{
    const PDFDocument document = createDocument(10);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::HeaderCenter] = "SAME ON ALL PAGES";
    PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());

    std::set<PDFObjectReference> forms;
    std::set<PDFObjectReference> invocations;
    for (PDFInteger i = 0; i < 10; ++i)
    {
        for (const PDFObjectReference& form : getStampForms(*result.document, i))
        {
            forms.insert(form);
        }
        invocations.insert(getContentReferences(*result.document, i).back());
    }
    QCOMPARE(forms.size(), size_t(1));
    QCOMPARE(invocations.size(), size_t(1));

    // Page numbers: a form for each page, but a single font
    settings.texts[PDFHeaderFooterSettings::HeaderCenter] = "{page}";
    result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    forms.clear();
    std::set<PDFObjectReference> fonts;
    for (PDFInteger i = 0; i < 10; ++i)
    {
        for (const PDFObjectReference& form : getStampForms(*result.document, i))
        {
            forms.insert(form);
            const PDFStream* stream = result.document->getObjectByReference(form).getStream();
            const PDFDictionary* resources = result.document->getDictionaryFromObject(stream->getDictionary()->get("Resources"));
            const PDFDictionary* fontDictionary = result.document->getDictionaryFromObject(resources->get("Font"));
            fonts.insert(fontDictionary->getValue(0).getReference());
        }
    }
    QCOMPARE(forms.size(), size_t(10));
    QCOMPARE(fonts.size(), size_t(1));

    const PDFDictionary* font = result.document->getDictionaryFromObject(result.document->getObjectByReference(*fonts.begin()));
    PDFDocumentDataLoaderDecorator loader(result.document.data());
    QCOMPARE(loader.readNameFromDictionary(font, "BaseFont"), QByteArray("Helvetica"));
    QCOMPARE(loader.readNameFromDictionary(font, "Encoding"), QByteArray("WinAnsiEncoding"));
}

void PageStamperTest::sharedResourcesAreNotChanged()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 612, 792));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 612, 792));

    PDFObjectFactory resourcesFactory;
    resourcesFactory.beginDictionary();
    resourcesFactory.beginDictionaryItem("Font");
    resourcesFactory.beginDictionary();
    resourcesFactory.beginDictionaryItem("F1");
    resourcesFactory.beginDictionary();
    resourcesFactory.beginDictionaryItem("Type");
    resourcesFactory << WrapName("Font");
    resourcesFactory.endDictionaryItem();
    resourcesFactory.beginDictionaryItem("Subtype");
    resourcesFactory << WrapName("Type1");
    resourcesFactory.endDictionaryItem();
    resourcesFactory.beginDictionaryItem("BaseFont");
    resourcesFactory << WrapName("Times-Roman");
    resourcesFactory.endDictionaryItem();
    resourcesFactory.endDictionary();
    resourcesFactory.endDictionaryItem();
    resourcesFactory.endDictionary();
    resourcesFactory.endDictionaryItem();
    resourcesFactory.endDictionary();
    const PDFObjectReference resources = builder.addObject(resourcesFactory.takeObject());

    const QByteArray content = "BT /F1 12 Tf 100 700 Td (ORIGINAL) Tj ET\n";
    PDFDictionaryBuilder contentDictionary;
    contentDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), QByteArray(content))));

    for (const PDFObjectReference page : { page1, page2 })
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Resources");
        factory << resources;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Contents");
        factory << contentReference;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(page, factory.takeObject());
    }

    const PDFDocument document = read(write(builder.build()));
    PDFWatermarkSettings settings;
    settings.text = "WATERMARK";
    const PDFPageStamper::Result result = PDFPageStamper::addWatermark(&document, settings, { 0 }, false);
    QVERIFY(result.isSuccess());

    // Page 2 still uses the shared resources (unchanged), page 1 has its own copy with the stamp
    const PDFPage* stampedPage = result.document->getCatalog()->getPage(0);
    const PDFPage* otherPage = result.document->getCatalog()->getPage(1);
    const PDFDictionary* otherPageDictionary = result.document->getDictionaryFromObject(result.document->getObjectByReference(otherPage->getPageReference()));
    QCOMPARE(otherPageDictionary->get("Resources"), PDFObject::createReference(resources));
    const PDFDictionary* sharedResources = result.document->getDictionaryFromObject(otherPageDictionary->get("Resources"));
    QVERIFY(!sharedResources->hasKey("XObject"));
    QCOMPARE(getStampForms(*result.document, 0).size(), size_t(1));
    const PDFDictionary* stampedResources = result.document->getDictionaryFromObject(stampedPage->getResources());
    QVERIFY(stampedResources->hasKey("Font"));

    // Original text is still drawn by its font
    QVERIFY(findText(*result.document, 0, "ORIGINAL").isValid());
    QVERIFY(findText(*result.document, 0, "WATERMARK").isValid());
    QVERIFY(findText(*result.document, 1, "ORIGINAL").isValid());
    QVERIFY(!findText(*result.document, 1, "WATERMARK").isValid());
}

void PageStamperTest::inheritedResourcesAreKept()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));

    // Resources are inherited from the page tree root
    PDFObjectFactory rootFactory;
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("Resources");
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("Font");
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("F1");
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("Type");
    rootFactory << WrapName("Font");
    rootFactory.endDictionaryItem();
    rootFactory.beginDictionaryItem("Subtype");
    rootFactory << WrapName("Type1");
    rootFactory.endDictionaryItem();
    rootFactory.beginDictionaryItem("BaseFont");
    rootFactory << WrapName("Courier");
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
    builder.mergeTo(catalog->get("Pages").getReference(), rootFactory.takeObject());

    const QByteArray content = "BT /F1 12 Tf 100 700 Td (INHERITED) Tj ET\n";
    PDFDictionaryBuilder contentDictionary;
    contentDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), QByteArray(content))));
    PDFObjectFactory pageFactory;
    pageFactory.beginDictionary();
    pageFactory.beginDictionaryItem("Contents");
    pageFactory << contentReference;
    pageFactory.endDictionaryItem();
    pageFactory.endDictionary();
    builder.mergeTo(page, pageFactory.takeObject());

    const PDFDocument document = read(write(builder.build()));
    QVERIFY(findText(document, 0, "INHERITED").isValid());

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterLeft] = "STAMP";
    const PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());
    QVERIFY(findText(*result.document, 0, "INHERITED").isValid());
    QVERIFY(findText(*result.document, 0, "STAMP").isValid());
}

void PageStamperTest::stampsSurviveWriteAndRead()
{
    const PDFDocument document = createDocument(3);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::FooterRight] = "{bates}";
    settings.variables.batesPrefix = "LEGAL";
    PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, settings, {}, false);
    QVERIFY(result.isSuccess());

    const PDFDocument reread = read(write(*result.document));
    QCOMPARE(PDFPageStamper::getStampedPageCount(&reread, PDFPageStamper::HeaderFooter), PDFInteger(3));
    QVERIFY(findText(reread, 2, "LEGAL000003").isValid());

    // Stamp content streams changed by another tool are foreign content, not stamps
    PDFDocumentBuilder builder(&reread);
    const PDFPage* page = reread.getCatalog()->getPage(0);
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << getContentReferences(reread, 0).front();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page->getPageReference(), factory.takeObject());
    const PDFDocument changed = builder.build();
    QCOMPARE(PDFPageStamper::getStampKinds(&changed, 0), PDFPageStamper::StampKinds(PDFPageStamper::None));
    QCOMPARE(PDFPageStamper::getStampKinds(&changed, 1), PDFPageStamper::StampKinds(PDFPageStamper::HeaderFooter));
}

void PageStamperTest::invalidInput()
{
    const PDFDocument document = createDocument(2);

    PDFHeaderFooterSettings emptySettings;
    PDFPageStamper::Result result = PDFPageStamper::addHeaderFooter(&document, emptySettings, {}, false);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.document);

    PDFHeaderFooterSettings settings;
    settings.texts[PDFHeaderFooterSettings::HeaderLeft] = "X";
    result = PDFPageStamper::addHeaderFooter(&document, settings, { 5, 7 }, false);
    QVERIFY(!result.isSuccess());

    result = PDFPageStamper::addHeaderFooter(nullptr, settings, {}, false);
    QVERIFY(!result.isSuccess());

    PDFWatermarkSettings watermark;
    result = PDFPageStamper::addWatermark(&document, watermark, {}, false);
    QVERIFY(!result.isSuccess());

    result = PDFPageStamper::removeStamps(nullptr, PDFPageStamper::AllKinds, {});
    QVERIFY(!result.isSuccess());

    QCOMPARE(PDFPageStamper::getStampKinds(&document, 10), PDFPageStamper::StampKinds(PDFPageStamper::None));
    QCOMPARE(PDFPageStamper::getStampedPageCount(nullptr, PDFPageStamper::AllKinds), PDFInteger(0));
}

QTEST_MAIN(PageStamperTest)

#include "tst_pagestampertest.moc"
