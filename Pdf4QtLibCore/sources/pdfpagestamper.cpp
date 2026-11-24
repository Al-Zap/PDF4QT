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
#include "pdfobjectutils.h"
#include "pdfencoding.h"
#include "pdfstreamfilters.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfconstants.h"
#include "pdfexception.h"

#include <QtMath>

#include <map>
#include <numeric>
#include <algorithm>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Font metrics and encoding of the texts of the stamps, and other helper functions
class PDFPageStamperHelper
{
public:
    PDFPageStamperHelper() = delete;

    /// Key of the page piece dictionary, under which the stamps are recorded
    static constexpr const char* PIECE_INFO_KEY = "PDF4QT_Stamp";

    /// Prefix of the names of the form XObjects in the page resources
    static constexpr const char* XOBJECT_PREFIX = "PDF4QTStamp";

    static constexpr int RECORD_VERSION = 1;

    /// Widths of the characters 32 to 255 of WinAnsiEncoding (Adobe font metrics
    /// of the standard fonts, Courier has all characters 600 units wide)
    static constexpr std::array<uint16_t, 224> HELVETICA_WIDTHS = {
        278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
        1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
        333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
        556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 761,
        556, 0, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
        0, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 0, 500, 667,
        278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
        400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
        667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
        722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
        556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
        556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500
    };

    static constexpr std::array<uint16_t, 224> HELVETICA_BOLD_WIDTHS = {
        278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
        975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
        333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
        611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 761,
        556, 0, 278, 556, 500, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
        0, 278, 278, 500, 500, 350, 556, 1000, 333, 1000, 556, 333, 944, 0, 500, 667,
        278, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 333, 737, 333,
        400, 584, 333, 333, 333, 611, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611,
        722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
        722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
        556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278,
        611, 611, 611, 611, 611, 611, 611, 584, 611, 611, 611, 611, 611, 556, 611, 556
    };

    static constexpr std::array<uint16_t, 224> TIMES_ROMAN_WIDTHS = {
        250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
        500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
        921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
        556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
        333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
        500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541, 761,
        500, 0, 333, 500, 444, 1000, 500, 500, 333, 1000, 556, 333, 889, 0, 611, 0,
        0, 333, 333, 444, 444, 350, 500, 1000, 333, 980, 389, 333, 722, 0, 444, 722,
        250, 333, 500, 500, 500, 500, 200, 500, 333, 760, 276, 500, 564, 333, 760, 333,
        400, 564, 300, 300, 333, 500, 453, 250, 333, 300, 310, 500, 750, 750, 750, 444,
        722, 722, 722, 722, 722, 722, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
        722, 722, 722, 722, 722, 722, 722, 564, 722, 722, 722, 722, 722, 722, 556, 500,
        444, 444, 444, 444, 444, 444, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
        500, 500, 500, 500, 500, 500, 500, 564, 500, 500, 500, 500, 500, 500, 500, 500
    };

    static constexpr std::array<uint16_t, 224> TIMES_BOLD_WIDTHS = {
        250, 333, 555, 500, 500, 1000, 833, 278, 333, 333, 500, 570, 250, 333, 250, 278,
        500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500,
        930, 722, 667, 722, 722, 667, 611, 778, 778, 389, 500, 778, 667, 944, 722, 778,
        611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667, 333, 278, 333, 581, 500,
        333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833, 556, 500,
        556, 556, 444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394, 520, 761,
        500, 0, 333, 500, 500, 1000, 500, 500, 333, 1000, 556, 333, 1000, 0, 667, 0,
        0, 333, 333, 500, 500, 350, 500, 1000, 333, 1000, 389, 333, 722, 0, 444, 722,
        250, 333, 500, 500, 500, 500, 220, 500, 333, 747, 300, 500, 570, 333, 747, 333,
        400, 570, 300, 300, 333, 556, 540, 250, 333, 300, 330, 500, 750, 750, 750, 500,
        722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 389, 389, 389, 389,
        722, 722, 778, 778, 778, 778, 778, 570, 778, 722, 722, 722, 722, 722, 611, 556,
        500, 500, 500, 500, 500, 500, 722, 444, 444, 444, 444, 444, 278, 278, 278, 278,
        500, 556, 500, 500, 500, 500, 500, 570, 500, 556, 556, 556, 556, 500, 556, 500
    };

    static PDFReal getCharacterWidth(PDFStampFont font, unsigned char code);
    static QByteArray encodeText(const QString& text);
    static QByteArray toLiteralString(const QByteArray& text);
    static QByteArray toNumber(PDFReal value);
    static QByteArray toMatrix(const QTransform& matrix);
    static QByteArray toColor(const QColor& color);
    static QByteArray kindToName(PDFPageStamper::StampKind kind);
    static PDFPageStamper::StampKind nameToKind(const QByteArray& name);
    static std::vector<PDFInteger> normalizePageIndices(const PDFDocument* document, std::vector<PDFInteger> pageIndices);
    static std::vector<PDFObjectReference> getContentReferences(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary);
    static QStringList splitLines(const QString& text);
};

/// One stamp recorded on the page
struct PDFPageStampRecordItem
{
    PDFPageStamper::StampKind kind = PDFPageStamper::None;
    PDFObjectReference contents;
    QByteArray xobjectName;
};

/// Record of the stamps of a page (/PieceInfo /PDF4QT_Stamp /Private)
struct PDFPageStampRecord
{
    std::vector<PDFPageStampRecordItem> stamps;
    PDFObjectReference isolationBegin;
    PDFObjectReference isolationEnd;

    /// Reads the record of the page. Stamps, whose content streams are no longer
    /// a part of the page content (the page was changed by another tool), are omitted.
    static PDFPageStampRecord read(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary);

    /// Creates the private data of the record
    PDFObject createPrivateData() const;

    PDFPageStamper::StampKinds getKinds() const;
};

/// Resources of the form XObject of a stamp
struct PDFPageStampFormResources
{
    std::set<PDFStampFont> fonts;
    PDFReal opacity = 1.0;
    bool hasImage = false;
};

/// Writes the stamps into the pages of the modified document. Objects shared by the
/// stamps (fonts, graphic states, image, identical forms and content streams) are created once.
class PDFPageStampWriter
{
public:
    explicit PDFPageStampWriter(const PDFDocument* document);

    /// Updates the page: removes the stamps of the given kinds, and adds a new stamp (if
    /// \p formContent is not empty). Returns the number of the removed stamps.
    PDFInteger updatePage(PDFInteger pageIndex,
                          PDFPageStamper::StampKinds removedKinds,
                          PDFPageStamper::StampKind addedKind,
                          const QByteArray& formContent,
                          const PDFPageStampFormResources& resources,
                          bool behindContent);

    /// Sets the image of the image watermarks
    void setImage(const QImage& image) { m_image = image; }

    PDFDocumentModifier& getModifier() { return m_modifier; }

private:
    PDFObjectReference createStream(PDFDictionaryBuilder dictionary, const QByteArray& data, bool compress);
    PDFObjectReference getFont(PDFStampFont font);
    PDFObjectReference getGraphicState(PDFReal opacity);
    PDFObjectReference getImage();
    PDFObjectReference getForm(const QSizeF& size, const QByteArray& content, const PDFPageStampFormResources& resources);
    PDFObjectReference getSharedStream(const QByteArray& content, bool compress);

    const PDFDocument* m_document;
    PDFDocumentModifier m_modifier;
    QImage m_image;
    std::map<PDFStampFont, PDFObjectReference> m_fonts;
    std::map<PDFReal, PDFObjectReference> m_graphicStates;
    PDFObjectReference m_imageReference;
    std::map<QByteArray, PDFObjectReference> m_forms;
    std::map<QByteArray, PDFObjectReference> m_sharedStreams;
};

/// Creates the contents of the form XObjects of the stamps
class PDFPageStampContentBuilder
{
public:
    PDFPageStampContentBuilder() = delete;

    static QByteArray createHeaderFooterContent(const QSizeF& size,
                                                const PDFHeaderFooterSettings& settings,
                                                const std::array<QString, PDFHeaderFooterSettings::PositionCount>& texts,
                                                PDFPageStampFormResources* resources);

    static QByteArray createWatermarkContent(const QSizeF& size,
                                             const PDFWatermarkSettings& settings,
                                             const QString& text,
                                             PDFPageStampFormResources* resources);

    static QByteArray getFontResourceName(PDFStampFont font);

private:
    static void beginContent(QByteArray& content, const PDFStampTextStyle& style, PDFPageStampFormResources* resources);
    static QPointF getAlignedCenter(const QSizeF& size, const QSizeF& boundingBoxSize, const PDFWatermarkSettings& settings);
};

PDFReal PDFPageStamperHelper::getCharacterWidth(PDFStampFont font, unsigned char code)
{
    if (code < 32)
    {
        return 0.0;
    }

    const size_t index = code - 32;
    switch (font)
    {
        case PDFStampFont::Helvetica:
            return HELVETICA_WIDTHS[index];
        case PDFStampFont::HelveticaBold:
            return HELVETICA_BOLD_WIDTHS[index];
        case PDFStampFont::TimesRoman:
            return TIMES_ROMAN_WIDTHS[index];
        case PDFStampFont::TimesBold:
            return TIMES_BOLD_WIDTHS[index];
        case PDFStampFont::Courier:
        case PDFStampFont::CourierBold:
            return 600.0;
    }

    return 0.0;
}

QByteArray PDFPageStamperHelper::encodeText(const QString& text)
{
    return PDFEncoding::convertToEncoding(PDFPageStamper::getRepresentableText(text), PDFEncoding::Encoding::WinAnsi);
}

QByteArray PDFPageStamperHelper::toLiteralString(const QByteArray& text)
{
    QByteArray result;
    result.reserve(text.size() + 2);
    result.append('(');
    for (const char character : text)
    {
        const unsigned char code = static_cast<unsigned char>(character);
        if (character == '(' || character == ')' || character == '\\')
        {
            result.append('\\');
            result.append(character);
        }
        else if (code < 32 || code >= 127)
        {
            result.append('\\');
            result.append(QByteArray::number(code, 8).rightJustified(3, '0'));
        }
        else
        {
            result.append(character);
        }
    }
    result.append(')');
    return result;
}

QByteArray PDFPageStamperHelper::toNumber(PDFReal value)
{
    if (qAbs(value) < 0.00005)
    {
        return "0";
    }

    QByteArray result = QByteArray::number(value, 'f', 4);
    while (result.endsWith('0'))
    {
        result.chop(1);
    }
    if (result.endsWith('.'))
    {
        result.chop(1);
    }
    return result;
}

QByteArray PDFPageStamperHelper::toMatrix(const QTransform& matrix)
{
    return toNumber(matrix.m11()) + ' ' + toNumber(matrix.m12()) + ' ' + toNumber(matrix.m21()) + ' ' +
           toNumber(matrix.m22()) + ' ' + toNumber(matrix.dx()) + ' ' + toNumber(matrix.dy());
}

QByteArray PDFPageStamperHelper::toColor(const QColor& color)
{
    const QColor rgb = color.isValid() ? color.toRgb() : QColor(Qt::black);
    return toNumber(rgb.redF()) + ' ' + toNumber(rgb.greenF()) + ' ' + toNumber(rgb.blueF());
}

QByteArray PDFPageStamperHelper::kindToName(PDFPageStamper::StampKind kind)
{
    switch (kind)
    {
        case PDFPageStamper::HeaderFooter:
            return "HeaderFooter";
        case PDFPageStamper::Watermark:
            return "Watermark";
        default:
            break;
    }

    return QByteArray();
}

PDFPageStamper::StampKind PDFPageStamperHelper::nameToKind(const QByteArray& name)
{
    if (name == "HeaderFooter")
    {
        return PDFPageStamper::HeaderFooter;
    }
    if (name == "Watermark")
    {
        return PDFPageStamper::Watermark;
    }
    return PDFPageStamper::None;
}

std::vector<PDFInteger> PDFPageStamperHelper::normalizePageIndices(const PDFDocument* document, std::vector<PDFInteger> pageIndices)
{
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (pageIndices.empty())
    {
        pageIndices.resize(pageCount);
        std::iota(pageIndices.begin(), pageIndices.end(), PDFInteger(0));
        return pageIndices;
    }

    std::sort(pageIndices.begin(), pageIndices.end());
    pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());
    pageIndices.erase(std::remove_if(pageIndices.begin(), pageIndices.end(), [pageCount](PDFInteger index) { return index < 0 || index >= pageCount; }), pageIndices.end());
    return pageIndices;
}

std::vector<PDFObjectReference> PDFPageStamperHelper::getContentReferences(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary)
{
    std::vector<PDFObjectReference> references;

    auto addArray = [&references](const PDFArray* array)
    {
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFObject& item = array->getItem(i);
            if (item.isReference())
            {
                references.push_back(item.getReference());
            }
        }
    };

    const PDFObject& contents = pageDictionary->get("Contents");
    if (contents.isReference())
    {
        const PDFObject& dereferenced = storage->getObjectByReference(contents.getReference());
        if (dereferenced.isStream())
        {
            references.push_back(contents.getReference());
        }
        else if (dereferenced.isArray())
        {
            addArray(dereferenced.getArray());
        }
    }
    else if (contents.isArray())
    {
        addArray(contents.getArray());
    }

    return references;
}

QStringList PDFPageStamperHelper::splitLines(const QString& text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));
    return normalized.split(QChar('\n'));
}

PDFPageStampRecord PDFPageStampRecord::read(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary)
{
    PDFPageStampRecord record;
    if (!pageDictionary)
    {
        return record;
    }

    const PDFDictionary* pieceInfo = storage->getDictionaryFromObject(pageDictionary->get("PieceInfo"));
    const PDFDictionary* entry = pieceInfo ? storage->getDictionaryFromObject(pieceInfo->get(PDFPageStamperHelper::PIECE_INFO_KEY)) : nullptr;
    const PDFDictionary* privateData = entry ? storage->getDictionaryFromObject(entry->get("Private")) : nullptr;
    if (!privateData)
    {
        return record;
    }

    const std::vector<PDFObjectReference> contentReferences = PDFPageStamperHelper::getContentReferences(storage, pageDictionary);
    auto isContent = [&contentReferences](PDFObjectReference reference)
    {
        return reference.isValid() && std::find(contentReferences.cbegin(), contentReferences.cend(), reference) != contentReferences.cend();
    };

    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFObject& stampsObject = storage->getObject(privateData->get("Stamps"));
    if (stampsObject.isArray())
    {
        const PDFArray* stamps = stampsObject.getArray();
        for (size_t i = 0; i < stamps->getCount(); ++i)
        {
            const PDFDictionary* stampDictionary = storage->getDictionaryFromObject(stamps->getItem(i));
            if (!stampDictionary)
            {
                continue;
            }

            PDFPageStampRecordItem item;
            item.kind = PDFPageStamperHelper::nameToKind(loader.readNameFromDictionary(stampDictionary, "Kind"));
            item.contents = loader.readReferenceFromDictionary(stampDictionary, "Contents");
            item.xobjectName = loader.readNameFromDictionary(stampDictionary, "XObject");

            if (item.kind != PDFPageStamper::None && isContent(item.contents))
            {
                record.stamps.push_back(item);
            }
        }
    }

    record.isolationBegin = loader.readReferenceFromDictionary(privateData, "IsolationBegin");
    record.isolationEnd = loader.readReferenceFromDictionary(privateData, "IsolationEnd");
    if (!isContent(record.isolationBegin) || !isContent(record.isolationEnd))
    {
        record.isolationBegin = PDFObjectReference();
        record.isolationEnd = PDFObjectReference();
    }

    return record;
}

PDFObject PDFPageStampRecord::createPrivateData() const
{
    PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Version");
    factory << PDFInteger(PDFPageStamperHelper::RECORD_VERSION);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Stamps");
    factory.beginArray();
    for (const PDFPageStampRecordItem& item : stamps)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("Kind");
        factory << WrapName(PDFPageStamperHelper::kindToName(item.kind));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Contents");
        factory << item.contents;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("XObject");
        factory << WrapName(item.xobjectName);
        factory.endDictionaryItem();
        factory.endDictionary();
    }
    factory.endArray();
    factory.endDictionaryItem();

    if (isolationBegin.isValid() && isolationEnd.isValid())
    {
        factory.beginDictionaryItem("IsolationBegin");
        factory << isolationBegin;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("IsolationEnd");
        factory << isolationEnd;
        factory.endDictionaryItem();
    }

    factory.beginDictionaryItem("Producer");
    factory << QString::fromLatin1(PDF_LIBRARY_NAME);
    factory.endDictionaryItem();

    factory.endDictionary();
    return factory.takeObject();
}

PDFPageStamper::StampKinds PDFPageStampRecord::getKinds() const
{
    PDFPageStamper::StampKinds kinds = PDFPageStamper::None;
    for (const PDFPageStampRecordItem& item : stamps)
    {
        kinds |= item.kind;
    }
    return kinds;
}

PDFPageStampWriter::PDFPageStampWriter(const PDFDocument* document) :
    m_document(document),
    m_modifier(document)
{

}

PDFObjectReference PDFPageStampWriter::createStream(PDFDictionaryBuilder dictionary, const QByteArray& data, bool compress)
{
    QByteArray content = data;
    if (compress)
    {
        content = PDFFlateDecodeFilter::compress(data);
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    return m_modifier.getBuilder()->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
}

PDFObjectReference PDFPageStampWriter::getFont(PDFStampFont font)
{
    auto it = m_fonts.find(font);
    if (it != m_fonts.cend())
    {
        return it->second;
    }

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Font");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Type1");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BaseFont");
    factory << WrapName(PDFPageStamper::getFontName(font));
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Encoding");
    factory << WrapName("WinAnsiEncoding");
    factory.endDictionaryItem();
    factory.endDictionary();

    const PDFObjectReference reference = m_modifier.getBuilder()->addObject(factory.takeObject());
    m_fonts[font] = reference;
    return reference;
}

PDFObjectReference PDFPageStampWriter::getGraphicState(PDFReal opacity)
{
    auto it = m_graphicStates.find(opacity);
    if (it != m_graphicStates.cend())
    {
        return it->second;
    }

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("ExtGState");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("ca");
    factory << opacity;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("CA");
    factory << opacity;
    factory.endDictionaryItem();
    factory.endDictionary();

    const PDFObjectReference reference = m_modifier.getBuilder()->addObject(factory.takeObject());
    m_graphicStates[opacity] = reference;
    return reference;
}

PDFObjectReference PDFPageStampWriter::getImage()
{
    if (m_imageReference.isValid() || m_image.isNull())
    {
        return m_imageReference;
    }

    const bool hasAlpha = m_image.hasAlphaChannel();
    const QImage rgbImage = m_image.convertToFormat(QImage::Format_RGB888);

    auto getSamples = [](const QImage& image, int bytesPerPixel)
    {
        QByteArray samples;
        const int lineSize = image.width() * bytesPerPixel;
        samples.reserve(lineSize * image.height());
        for (int y = 0; y < image.height(); ++y)
        {
            samples.append(reinterpret_cast<const char*>(image.constScanLine(y)), lineSize);
        }
        return samples;
    };

    auto createImageDictionary = [&](const QImage& image, const char* colorSpace)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
        dictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
        dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName(colorSpace));
        dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        return dictionary;
    };

    PDFDictionaryBuilder imageDictionary = createImageDictionary(rgbImage, "DeviceRGB");
    if (hasAlpha)
    {
        const QImage alphaImage = m_image.convertToFormat(QImage::Format_ARGB32).convertToFormat(QImage::Format_Alpha8);
        const PDFObjectReference softMaskReference = createStream(createImageDictionary(alphaImage, "DeviceGray"), getSamples(alphaImage, 1), true);
        imageDictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(softMaskReference));
    }

    m_imageReference = createStream(std::move(imageDictionary), getSamples(rgbImage, 3), true);
    return m_imageReference;
}

PDFObjectReference PDFPageStampWriter::getForm(const QSizeF& size, const QByteArray& content, const PDFPageStampFormResources& resources)
{
    PDFDictionaryBuilder fonts;
    QByteArray key = PDFPageStamperHelper::toNumber(size.width()) + ' ' + PDFPageStamperHelper::toNumber(size.height()) + '\n';
    for (const PDFStampFont font : resources.fonts)
    {
        fonts.setEntry(PDFInplaceOrMemoryString(PDFPageStampContentBuilder::getFontResourceName(font)), PDFObject::createReference(getFont(font)));
        key += PDFPageStampContentBuilder::getFontResourceName(font) + ' ';
    }
    key += '\n' + PDFPageStamperHelper::toNumber(resources.opacity) + (resources.hasImage ? " image\n" : "\n") + content;

    auto it = m_forms.find(key);
    if (it != m_forms.cend())
    {
        return it->second;
    }

    PDFDictionaryBuilder resourcesDictionary;
    if (!fonts.isEmpty())
    {
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    }
    if (resources.opacity < 1.0)
    {
        PDFDictionaryBuilder graphicStates;
        graphicStates.setEntry(PDFInplaceOrMemoryString("GS0"), PDFObject::createReference(getGraphicState(resources.opacity)));
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("ExtGState"), PDFObject::createDictionary(std::move(graphicStates)));
    }
    if (resources.hasImage)
    {
        PDFDictionaryBuilder xobjects;
        xobjects.setEntry(PDFInplaceOrMemoryString("Im0"), PDFObject::createReference(getImage()));
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
    }

    PDFObjectFactory boundingBoxFactory;
    boundingBoxFactory << QRectF(QPointF(0.0, 0.0), size);

    PDFDictionaryBuilder formDictionary;
    formDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    formDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    formDictionary.setEntry(PDFInplaceOrMemoryString("BBox"), boundingBoxFactory.takeObject());
    formDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resourcesDictionary)));

    const PDFObjectReference reference = createStream(std::move(formDictionary), content, true);
    m_forms[key] = reference;
    return reference;
}

PDFObjectReference PDFPageStampWriter::getSharedStream(const QByteArray& content, bool compress)
{
    auto it = m_sharedStreams.find(content);
    if (it != m_sharedStreams.cend())
    {
        return it->second;
    }

    const PDFObjectReference reference = createStream(PDFDictionaryBuilder(), content, compress);
    m_sharedStreams[content] = reference;
    return reference;
}

PDFInteger PDFPageStampWriter::updatePage(PDFInteger pageIndex,
                                          PDFPageStamper::StampKinds removedKinds,
                                          PDFPageStamper::StampKind addedKind,
                                          const QByteArray& formContent,
                                          const PDFPageStampFormResources& resources,
                                          bool behindContent)
{
    PDFDocumentBuilder* builder = m_modifier.getBuilder();
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return 0;
    }

    const PDFObjectReference pageReference = page->getPageReference();
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(pageReference));
    if (!pageDictionary)
    {
        return 0;
    }

    std::vector<PDFObjectReference> contentReferences = PDFPageStamperHelper::getContentReferences(storage, pageDictionary);
    PDFPageStampRecord record = PDFPageStampRecord::read(storage, pageDictionary);

    // Resources are copied into the page dictionary (inherited, or shared
    // resources of other pages stay untouched)
    PDFDictionaryBuilder resourcesDictionary = PDFObjectUtils::copyDictionary(storage, page->getResources());
    PDFDictionaryBuilder xobjects = PDFObjectUtils::copyDictionary(storage, resourcesDictionary.get("XObject"));

    // Removal of the stamps
    PDFInteger removedCount = 0;
    for (auto it = record.stamps.begin(); it != record.stamps.end();)
    {
        if (removedKinds.testFlag(it->kind))
        {
            contentReferences.erase(std::remove(contentReferences.begin(), contentReferences.end(), it->contents), contentReferences.end());
            if (!it->xobjectName.isEmpty())
            {
                xobjects.removeEntry(it->xobjectName);
            }
            it = record.stamps.erase(it);
            ++removedCount;
        }
        else
        {
            ++it;
        }
    }

    const bool addStamp = addedKind != PDFPageStamper::None && !formContent.isEmpty();
    if (removedCount == 0 && !addStamp)
    {
        return 0;
    }

    if (record.stamps.empty() && !addStamp && record.isolationBegin.isValid())
    {
        // No stamp is left, the isolation of the original content is removed
        auto beginIt = std::find(contentReferences.begin(), contentReferences.end(), record.isolationBegin);
        if (beginIt != contentReferences.end())
        {
            contentReferences.erase(beginIt);
        }
        auto endIt = std::find(contentReferences.rbegin(), contentReferences.rend(), record.isolationEnd);
        if (endIt != contentReferences.rend())
        {
            contentReferences.erase(std::next(endIt).base());
        }
        record.isolationBegin = PDFObjectReference();
        record.isolationEnd = PDFObjectReference();
    }

    if (addStamp)
    {
        // Isolation of the original content: the original content can leave a changed
        // transformation matrix, clipping path or colors behind, which would move or
        // hide the stamps drawn over it. The content is enclosed between "q" and "Q"
        // streams, which balance also an unbalanced content.
        if (!record.isolationBegin.isValid() && !contentReferences.empty())
        {
            QByteArray originalContent;
            for (const PDFObjectReference& reference : contentReferences)
            {
                const PDFObject& object = storage->getObjectByReference(reference);
                if (object.isStream())
                {
                    try
                    {
                        originalContent.append(builder->getDecodedStream(object.getStream()));
                    }
                    catch (const PDFException&)
                    {
                        // Damaged stream is rendered as an empty one
                    }
                    originalContent.append('\n');
                }
            }

            const PDFOCRTextLayerWriter::ContentBalance balance = PDFOCRTextLayerWriter::computeContentBalance(originalContent);

            QByteArray beginContent;
            for (int i = 0; i < 1 + qMax(0, -balance.graphicStateDepth); ++i)
            {
                beginContent.append("q\n");
            }

            QByteArray endContent;
            for (int i = 0; i < qMax(0, balance.textObjectDepth); ++i)
            {
                endContent.append("ET\n");
            }
            for (int i = 0; i < qMax(0, balance.markedContentDepth); ++i)
            {
                endContent.append("EMC\n");
            }
            for (int i = 0; i < 1 + qMax(0, balance.graphicStateDepth); ++i)
            {
                endContent.append("Q\n");
            }

            record.isolationBegin = getSharedStream(beginContent, false);
            record.isolationEnd = getSharedStream(endContent, false);
            contentReferences.insert(contentReferences.begin(), record.isolationBegin);
            contentReferences.push_back(record.isolationEnd);
        }

        // Form XObject of the stamp and its name in the page resources
        const QSizeF size = PDFPageStamper::getStampSpaceSize(page);
        const PDFObjectReference formReference = getForm(size, formContent, resources);

        QByteArray xobjectName;
        for (int i = 1; xobjectName.isEmpty() || xobjects.hasKey(xobjectName); ++i)
        {
            xobjectName = QByteArray(PDFPageStamperHelper::XOBJECT_PREFIX) + QByteArray::number(i);
        }
        xobjects.setEntry(PDFInplaceOrMemoryString(xobjectName), PDFObject::createReference(formReference));

        // Content stream drawing the form, marked as a pagination artifact
        QByteArray invocation = "/Artifact <</Type /Pagination";
        if (addedKind == PDFPageStamper::Watermark)
        {
            invocation += " /Subtype /Watermark";
        }
        invocation += ">> BDC\nq\n";
        invocation += PDFPageStamperHelper::toMatrix(PDFPageStamper::getStampSpaceToPageMatrix(page)) + " cm\n";
        invocation += '/' + xobjectName + " Do\nQ\nEMC\n";

        const PDFObjectReference invocationReference = getSharedStream(invocation, false);
        if (behindContent)
        {
            contentReferences.insert(contentReferences.begin(), invocationReference);
        }
        else
        {
            contentReferences.push_back(invocationReference);
        }

        PDFPageStampRecordItem item;
        item.kind = addedKind;
        item.contents = invocationReference;
        item.xobjectName = xobjectName;
        record.stamps.push_back(item);
    }

    // Page dictionary
    PDFDictionaryBuilder newPageDictionary(*pageDictionary);

    PDFObjectFactory contentsFactory;
    contentsFactory << contentReferences;
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), contentsFactory.takeObject());

    if (xobjects.isEmpty())
    {
        resourcesDictionary.removeEntry("XObject");
    }
    else
    {
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
    }
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resourcesDictionary)));

    PDFDictionaryBuilder pieceInfo = PDFObjectUtils::copyDictionary(storage, pageDictionary->get("PieceInfo"));
    if (record.stamps.empty())
    {
        pieceInfo.removeEntry(PDFPageStamperHelper::PIECE_INFO_KEY);
    }
    else
    {
        PDFObjectFactory entryFactory;
        entryFactory.beginDictionary();
        entryFactory.beginDictionaryItem("LastModified");
        entryFactory << WrapCurrentDateTime();
        entryFactory.endDictionaryItem();
        entryFactory.beginDictionaryItem("Private");
        entryFactory << record.createPrivateData();
        entryFactory.endDictionaryItem();
        entryFactory.endDictionary();
        pieceInfo.setEntry(PDFInplaceOrMemoryString(PDFPageStamperHelper::PIECE_INFO_KEY), entryFactory.takeObject());
    }

    if (pieceInfo.isEmpty())
    {
        newPageDictionary.removeEntry("PieceInfo");
    }
    else
    {
        newPageDictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"), PDFObject::createDictionary(std::move(pieceInfo)));

        // Page with a piece dictionary must have the modification date
        PDFObjectFactory dateFactory;
        dateFactory << WrapCurrentDateTime();
        newPageDictionary.setEntry(PDFInplaceOrMemoryString("LastModified"), dateFactory.takeObject());
    }

    builder->setObject(pageReference, PDFObject::createDictionary(std::move(newPageDictionary)));
    m_modifier.markPageContentsChanged();
    return removedCount;
}

QByteArray PDFPageStampContentBuilder::getFontResourceName(PDFStampFont font)
{
    return "F" + QByteArray::number(static_cast<int>(font));
}

void PDFPageStampContentBuilder::beginContent(QByteArray& content, const PDFStampTextStyle& style, PDFPageStampFormResources* resources)
{
    content.append("q\n");
    resources->opacity = qBound(0.0, style.opacity, 1.0);
    if (resources->opacity < 1.0)
    {
        content.append("/GS0 gs\n");
    }
}

QPointF PDFPageStampContentBuilder::getAlignedCenter(const QSizeF& size, const QSizeF& boundingBoxSize, const PDFWatermarkSettings& settings)
{
    const QMarginsF& margins = settings.margins;

    PDFReal x = size.width() * 0.5;
    if (settings.alignment.testFlag(Qt::AlignLeft))
    {
        x = margins.left() + boundingBoxSize.width() * 0.5;
    }
    else if (settings.alignment.testFlag(Qt::AlignRight))
    {
        x = size.width() - margins.right() - boundingBoxSize.width() * 0.5;
    }

    PDFReal y = size.height() * 0.5;
    if (settings.alignment.testFlag(Qt::AlignTop))
    {
        y = size.height() - margins.top() - boundingBoxSize.height() * 0.5;
    }
    else if (settings.alignment.testFlag(Qt::AlignBottom))
    {
        y = margins.bottom() + boundingBoxSize.height() * 0.5;
    }

    return QPointF(x, y) + settings.offset;
}

QByteArray PDFPageStampContentBuilder::createHeaderFooterContent(const QSizeF& size,
                                                                 const PDFHeaderFooterSettings& settings,
                                                                 const std::array<QString, PDFHeaderFooterSettings::PositionCount>& texts,
                                                                 PDFPageStampFormResources* resources)
{
    const PDFStampFont font = settings.style.font;
    const PDFReal fontSize = settings.style.fontSize > 0.0 ? settings.style.fontSize : 10.0;
    const PDFReal leading = fontSize * 1.2;
    const PDFReal ascent = PDFPageStamper::getFontAscent(font) * fontSize;
    const PDFReal descent = PDFPageStamper::getFontDescent(font) * fontSize;
    const QMarginsF& margins = settings.margins;

    QByteArray text;
    for (int position = 0; position < PDFHeaderFooterSettings::PositionCount; ++position)
    {
        if (texts[position].isEmpty())
        {
            continue;
        }

        const QStringList lines = PDFPageStamperHelper::splitLines(texts[position]);
        const bool isHeader = position <= PDFHeaderFooterSettings::HeaderRight;
        const int column = position % 3;

        for (int i = 0; i < lines.size(); ++i)
        {
            const QByteArray encoded = PDFPageStamperHelper::encodeText(lines[i]);
            if (encoded.isEmpty())
            {
                continue;
            }

            const PDFReal width = PDFPageStamper::getTextWidth(font, lines[i], fontSize);
            PDFReal x = margins.left();
            if (column == 1)
            {
                x = (size.width() - width) * 0.5;
            }
            else if (column == 2)
            {
                x = size.width() - margins.right() - width;
            }

            const PDFReal y = isHeader ? size.height() - margins.top() - ascent - i * leading
                                       : margins.bottom() - descent + (lines.size() - 1 - i) * leading;

            text.append("1 0 0 1 " + PDFPageStamperHelper::toNumber(x) + ' ' + PDFPageStamperHelper::toNumber(y) + " Tm\n");
            text.append(PDFPageStamperHelper::toLiteralString(encoded) + " Tj\n");
        }
    }

    if (text.isEmpty())
    {
        return QByteArray();
    }

    QByteArray content;
    beginContent(content, settings.style, resources);
    resources->fonts.insert(font);
    content.append(PDFPageStamperHelper::toColor(settings.style.color) + " rg\nBT\n/" + getFontResourceName(font) + ' ' + PDFPageStamperHelper::toNumber(fontSize) + " Tf\n");
    content.append(text);
    content.append("ET\nQ\n");
    return content;
}

QByteArray PDFPageStampContentBuilder::createWatermarkContent(const QSizeF& size,
                                                              const PDFWatermarkSettings& settings,
                                                              const QString& text,
                                                              PDFPageStampFormResources* resources)
{
    const PDFReal angle = qDegreesToRadians(settings.rotation);
    const PDFReal cosAngle = qAbs(qCos(angle)) < 1e-9 ? 0.0 : qCos(angle);
    const PDFReal sinAngle = qAbs(qSin(angle)) < 1e-9 ? 0.0 : qSin(angle);
    const PDFReal relativeSize = qBound(0.01, settings.relativeSize, 1.0);

    // Size of the rotated rectangle w x h
    auto getRotatedSize = [cosAngle, sinAngle](PDFReal w, PDFReal h)
    {
        return QSizeF(w * qAbs(cosAngle) + h * qAbs(sinAngle), w * qAbs(sinAngle) + h * qAbs(cosAngle));
    };

    auto getScaleToFit = [&](const QSizeF& rotatedSize)
    {
        if (rotatedSize.width() <= 0.0 || rotatedSize.height() <= 0.0)
        {
            return 0.0;
        }
        return qMin(size.width() * relativeSize / rotatedSize.width(), size.height() * relativeSize / rotatedSize.height());
    };

    QByteArray content;

    if (!text.trimmed().isEmpty())
    {
        const PDFStampFont font = settings.style.font;
        const QStringList lines = PDFPageStamperHelper::splitLines(text);
        const PDFReal ascent = PDFPageStamper::getFontAscent(font);
        const PDFReal descent = PDFPageStamper::getFontDescent(font);
        const PDFReal leading = 1.2;

        // Block of the lines for the font size 1
        PDFReal blockWidth = 0.0;
        for (const QString& line : lines)
        {
            blockWidth = qMax(blockWidth, PDFPageStamper::getTextWidth(font, line, 1.0));
        }
        const PDFReal blockHeight = (lines.size() - 1) * leading + ascent - descent;

        PDFReal fontSize = settings.style.fontSize;
        if (fontSize <= 0.0)
        {
            fontSize = getScaleToFit(getRotatedSize(blockWidth, blockHeight));
        }

        if (fontSize <= 0.0 || blockWidth <= 0.0)
        {
            return QByteArray();
        }

        const QSizeF boundingBoxSize = getRotatedSize(blockWidth * fontSize, blockHeight * fontSize);
        const QPointF center = getAlignedCenter(size, boundingBoxSize, settings);

        beginContent(content, settings.style, resources);
        resources->fonts.insert(font);

        QTransform matrix(cosAngle, sinAngle, -sinAngle, cosAngle, center.x(), center.y());
        content.append(PDFPageStamperHelper::toMatrix(matrix) + " cm\n");
        content.append(PDFPageStamperHelper::toColor(settings.style.color) + " rg\nBT\n/" + getFontResourceName(font) + ' ' + PDFPageStamperHelper::toNumber(fontSize) + " Tf\n");

        for (int i = 0; i < lines.size(); ++i)
        {
            const QByteArray encoded = PDFPageStamperHelper::encodeText(lines[i]);
            if (encoded.isEmpty())
            {
                continue;
            }

            const PDFReal x = -PDFPageStamper::getTextWidth(font, lines[i], fontSize) * 0.5;
            const PDFReal y = (blockHeight * 0.5 - ascent - i * leading) * fontSize;
            content.append("1 0 0 1 " + PDFPageStamperHelper::toNumber(x) + ' ' + PDFPageStamperHelper::toNumber(y) + " Tm\n");
            content.append(PDFPageStamperHelper::toLiteralString(encoded) + " Tj\n");
        }

        content.append("ET\nQ\n");
    }
    else if (!settings.image.isNull() && settings.image.width() > 0 && settings.image.height() > 0)
    {
        const PDFReal aspectRatio = PDFReal(settings.image.height()) / PDFReal(settings.image.width());
        const PDFReal scale = getScaleToFit(getRotatedSize(1.0, aspectRatio));
        const PDFReal width = scale;
        const PDFReal height = scale * aspectRatio;
        if (width <= 0.0 || height <= 0.0)
        {
            return QByteArray();
        }

        const QPointF center = getAlignedCenter(size, getRotatedSize(width, height), settings);

        beginContent(content, settings.style, resources);
        resources->hasImage = true;

        QTransform matrix(cosAngle, sinAngle, -sinAngle, cosAngle, center.x(), center.y());
        content.append(PDFPageStamperHelper::toMatrix(matrix) + " cm\n");
        content.append(PDFPageStamperHelper::toNumber(width) + " 0 0 " + PDFPageStamperHelper::toNumber(height) + ' ' +
                       PDFPageStamperHelper::toNumber(-width * 0.5) + ' ' + PDFPageStamperHelper::toNumber(-height * 0.5) + " cm\n");
        content.append("/Im0 Do\nQ\n");
    }

    return content;
}

bool PDFHeaderFooterSettings::isEmpty() const
{
    return std::all_of(texts.cbegin(), texts.cend(), [](const QString& text) { return text.trimmed().isEmpty(); });
}

QString PDFHeaderFooterSettings::getPositionName(Position position)
{
    switch (position)
    {
        case HeaderLeft:
            return QStringLiteral("header-left");
        case HeaderCenter:
            return QStringLiteral("header-center");
        case HeaderRight:
            return QStringLiteral("header-right");
        case FooterLeft:
            return QStringLiteral("footer-left");
        case FooterCenter:
            return QStringLiteral("footer-center");
        case FooterRight:
            return QStringLiteral("footer-right");
        default:
            break;
    }

    return QString();
}

PDFHeaderFooterSettings::Position PDFHeaderFooterSettings::getPositionFromName(const QString& name)
{
    for (int i = 0; i < PositionCount; ++i)
    {
        const Position position = static_cast<Position>(i);
        if (getPositionName(position).compare(name.trimmed(), Qt::CaseInsensitive) == 0)
        {
            return position;
        }
    }

    return PositionCount;
}

PDFWatermarkSettings::PDFWatermarkSettings()
{
    style.font = PDFStampFont::HelveticaBold;
    style.fontSize = 0.0;
    style.color = QColor(255, 0, 0);
    style.opacity = 0.3;
}

PDFPageStamper::Result PDFPageStamper::addHeaderFooter(const PDFDocument* document,
                                                       const PDFHeaderFooterSettings& settings,
                                                       std::vector<PDFInteger> pageIndices,
                                                       bool replaceExisting)
{
    Result result;
    if (!document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    if (settings.isEmpty())
    {
        result.errorMessage = PDFTranslationContext::tr("Header and footer texts are empty.");
        return result;
    }

    pageIndices = PDFPageStamperHelper::normalizePageIndices(document, std::move(pageIndices));
    if (pageIndices.empty())
    {
        result.errorMessage = PDFTranslationContext::tr("No pages are selected.");
        return result;
    }

    const bool hasBates = std::any_of(settings.texts.cbegin(), settings.texts.cend(), [](const QString& text) { return text.contains(QLatin1String("{bates}")); });
    const QString title = document->getInfo()->title;
    const PDFInteger count = PDFInteger(pageIndices.size());

    try
    {
        PDFPageStampWriter writer(document);

        for (PDFInteger i = 0; i < count; ++i)
        {
            const PDFInteger pageIndex = pageIndices[i];
            const PDFPage* page = document->getCatalog()->getPage(pageIndex);

            std::array<QString, PDFHeaderFooterSettings::PositionCount> texts;
            for (size_t position = 0; position < texts.size(); ++position)
            {
                texts[position] = expandVariables(settings.texts[position], settings.variables, i, count, title);
            }

            PDFPageStampFormResources resources;
            const QByteArray content = PDFPageStampContentBuilder::createHeaderFooterContent(getStampSpaceSize(page), settings, texts, &resources);
            result.removedStampCount += writer.updatePage(pageIndex, replaceExisting ? HeaderFooter : None, HeaderFooter, content, resources, false);
            ++result.pageCount;
        }

        if (writer.getModifier().finalize())
        {
            result.document = writer.getModifier().getDocument();
            result.flags = writer.getModifier().getFlags();
        }
    }
    catch (const PDFException& exception)
    {
        result = Result();
        result.errorMessage = exception.getMessage();
        return result;
    }

    if (hasBates)
    {
        result.firstBatesNumber = formatBatesNumber(settings.variables.batesPrefix, settings.variables.batesStart, settings.variables.batesDigits, settings.variables.batesSuffix);
        result.lastBatesNumber = formatBatesNumber(settings.variables.batesPrefix, settings.variables.batesStart + count - 1, settings.variables.batesDigits, settings.variables.batesSuffix);
    }
    result.nextBatesNumber = settings.variables.batesStart + count;
    return result;
}

PDFPageStamper::Result PDFPageStamper::addWatermark(const PDFDocument* document,
                                                    const PDFWatermarkSettings& settings,
                                                    std::vector<PDFInteger> pageIndices,
                                                    bool replaceExisting)
{
    Result result;
    if (!document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    if (settings.text.trimmed().isEmpty() && settings.image.isNull())
    {
        result.errorMessage = PDFTranslationContext::tr("Watermark has neither a text, nor an image.");
        return result;
    }

    pageIndices = PDFPageStamperHelper::normalizePageIndices(document, std::move(pageIndices));
    if (pageIndices.empty())
    {
        result.errorMessage = PDFTranslationContext::tr("No pages are selected.");
        return result;
    }

    const QString title = document->getInfo()->title;
    const PDFInteger count = PDFInteger(pageIndices.size());

    try
    {
        PDFPageStampWriter writer(document);
        writer.setImage(settings.image);

        for (PDFInteger i = 0; i < count; ++i)
        {
            const PDFInteger pageIndex = pageIndices[i];
            const PDFPage* page = document->getCatalog()->getPage(pageIndex);
            const QString text = expandVariables(settings.text, settings.variables, i, count, title);

            PDFPageStampFormResources resources;
            const QByteArray content = PDFPageStampContentBuilder::createWatermarkContent(getStampSpaceSize(page), settings, text, &resources);
            result.removedStampCount += writer.updatePage(pageIndex, replaceExisting ? Watermark : None, Watermark, content, resources, settings.behindContent);
            ++result.pageCount;
        }

        if (writer.getModifier().finalize())
        {
            result.document = writer.getModifier().getDocument();
            result.flags = writer.getModifier().getFlags();
        }
    }
    catch (const PDFException& exception)
    {
        result = Result();
        result.errorMessage = exception.getMessage();
        return result;
    }

    result.nextBatesNumber = settings.variables.batesStart + count;
    return result;
}

PDFPageStamper::Result PDFPageStamper::removeStamps(const PDFDocument* document, StampKinds kinds, std::vector<PDFInteger> pageIndices)
{
    Result result;
    if (!document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    pageIndices = PDFPageStamperHelper::normalizePageIndices(document, std::move(pageIndices));

    try
    {
        PDFPageStampWriter writer(document);
        for (const PDFInteger pageIndex : pageIndices)
        {
            const PDFInteger removed = writer.updatePage(pageIndex, kinds, None, QByteArray(), PDFPageStampFormResources(), false);
            if (removed > 0)
            {
                result.removedStampCount += removed;
                ++result.pageCount;
            }
        }

        if (result.removedStampCount > 0 && writer.getModifier().finalize())
        {
            result.document = writer.getModifier().getDocument();
            result.flags = writer.getModifier().getFlags();
        }
    }
    catch (const PDFException& exception)
    {
        result = Result();
        result.errorMessage = exception.getMessage();
    }

    return result;
}

PDFPageStamper::StampKinds PDFPageStamper::getStampKinds(const PDFDocument* document, PDFInteger pageIndex)
{
    if (!document || pageIndex < 0 || pageIndex >= PDFInteger(document->getCatalog()->getPageCount()))
    {
        return None;
    }

    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return None;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page->getPageReference()));
    return PDFPageStampRecord::read(storage, pageDictionary).getKinds();
}

PDFInteger PDFPageStamper::getStampedPageCount(const PDFDocument* document, StampKinds kinds)
{
    if (!document)
    {
        return 0;
    }

    PDFInteger count = 0;
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (getStampKinds(document, i) & kinds)
        {
            ++count;
        }
    }
    return count;
}

QString PDFPageStamper::formatBatesNumber(const QString& prefix, PDFInteger number, int digits, const QString& suffix)
{
    const QString numberText = QString::number(qAbs(number)).rightJustified(qBound(1, digits, 15), QChar('0'));
    return prefix + (number < 0 ? QStringLiteral("-") : QString()) + numberText + suffix;
}

QString PDFPageStamper::expandVariables(const QString& text, const PDFStampVariables& variables, PDFInteger index, PDFInteger count, const QString& title)
{
    QString result;
    result.reserve(text.size());

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];
        if (character != QChar('{'))
        {
            result.append(character);
            continue;
        }

        if (i + 1 < text.size() && text[i + 1] == QChar('{'))
        {
            result.append(QChar('{'));
            ++i;
            continue;
        }

        const qsizetype end = text.indexOf(QChar('}'), i + 1);
        if (end < 0)
        {
            result.append(character);
            continue;
        }

        const QString name = text.mid(i + 1, end - i - 1).trimmed().toLower();
        QString value;
        bool isKnown = true;

        if (name == QLatin1String("page"))
        {
            value = QString::number(variables.startPageNumber + index);
        }
        else if (name == QLatin1String("pages"))
        {
            value = QString::number(variables.startPageNumber + count - 1);
        }
        else if (name == QLatin1String("bates"))
        {
            value = formatBatesNumber(variables.batesPrefix, variables.batesStart + index, variables.batesDigits, variables.batesSuffix);
        }
        else if (name == QLatin1String("date"))
        {
            value = variables.dateTime.toString(variables.dateFormat);
        }
        else if (name == QLatin1String("time"))
        {
            value = variables.dateTime.toString(variables.timeFormat);
        }
        else if (name == QLatin1String("file"))
        {
            value = variables.fileName;
        }
        else if (name == QLatin1String("title"))
        {
            value = title;
        }
        else
        {
            isKnown = false;
        }

        if (isKnown)
        {
            result.append(value);
            i = end;
        }
        else
        {
            result.append(character);
        }
    }

    return result;
}

QString PDFPageStamper::getRepresentableText(const QString& text)
{
    QString result;
    result.reserve(text.size());

    for (const QChar character : text)
    {
        QChar converted = QChar('?');
        if (character == QChar('\t'))
        {
            converted = QChar(' ');
        }
        else if (character.unicode() >= 32 && character.unicode() != 127)
        {
            const QByteArray encoded = PDFEncoding::convertToEncoding(QString(character), PDFEncoding::Encoding::WinAnsi);
            const unsigned char code = encoded.isEmpty() ? 0 : static_cast<unsigned char>(encoded.front());
            if (code >= 32 && code != 127 && PDFPageStamperHelper::getCharacterWidth(PDFStampFont::Helvetica, code) > 0.0)
            {
                converted = character;
            }
        }
        result.append(converted);
    }

    return result;
}

PDFReal PDFPageStamper::getTextWidth(PDFStampFont font, const QString& text, PDFReal fontSize)
{
    PDFReal width = 0.0;
    for (const char character : PDFPageStamperHelper::encodeText(text))
    {
        width += PDFPageStamperHelper::getCharacterWidth(font, static_cast<unsigned char>(character));
    }
    return width * fontSize / 1000.0;
}

PDFReal PDFPageStamper::getFontAscent(PDFStampFont font)
{
    switch (font)
    {
        case PDFStampFont::Helvetica:
        case PDFStampFont::HelveticaBold:
            return 0.718;
        case PDFStampFont::TimesRoman:
        case PDFStampFont::TimesBold:
            return 0.683;
        case PDFStampFont::Courier:
        case PDFStampFont::CourierBold:
            return 0.629;
    }

    return 0.718;
}

PDFReal PDFPageStamper::getFontDescent(PDFStampFont font)
{
    switch (font)
    {
        case PDFStampFont::Helvetica:
        case PDFStampFont::HelveticaBold:
            return -0.207;
        case PDFStampFont::TimesRoman:
        case PDFStampFont::TimesBold:
            return -0.217;
        case PDFStampFont::Courier:
        case PDFStampFont::CourierBold:
            return -0.157;
    }

    return -0.207;
}

QByteArray PDFPageStamper::getFontName(PDFStampFont font)
{
    switch (font)
    {
        case PDFStampFont::Helvetica:
            return "Helvetica";
        case PDFStampFont::HelveticaBold:
            return "Helvetica-Bold";
        case PDFStampFont::TimesRoman:
            return "Times-Roman";
        case PDFStampFont::TimesBold:
            return "Times-Bold";
        case PDFStampFont::Courier:
            return "Courier";
        case PDFStampFont::CourierBold:
            return "Courier-Bold";
    }

    return "Helvetica";
}

std::optional<PDFStampFont> PDFPageStamper::getFontFromName(const QString& name)
{
    const QString normalized = name.trimmed().toLower().remove(QChar(' ')).remove(QChar('_'));
    for (const PDFStampFont font : { PDFStampFont::Helvetica, PDFStampFont::HelveticaBold, PDFStampFont::TimesRoman,
                                     PDFStampFont::TimesBold, PDFStampFont::Courier, PDFStampFont::CourierBold })
    {
        if (QString::fromLatin1(getFontName(font)).toLower() == normalized)
        {
            return font;
        }
    }

    if (normalized == QLatin1String("times"))
    {
        return PDFStampFont::TimesRoman;
    }
    if (normalized == QLatin1String("times-roman-bold"))
    {
        return PDFStampFont::TimesBold;
    }

    return std::nullopt;
}

QSizeF PDFPageStamper::getStampSpaceSize(const PDFPage* page)
{
    if (!page)
    {
        return QSizeF();
    }

    QRectF box = page->getCropBox().intersected(page->getMediaBox());
    if (!box.isValid() || box.isEmpty())
    {
        box = page->getMediaBox();
    }

    const PDFReal userUnit = page->getUserUnit() > 0.0 ? page->getUserUnit() : 1.0;
    const QSizeF size(box.width() * userUnit, box.height() * userUnit);
    return PDFPage::getRotatedSize(size, page->getPageRotation());
}

QTransform PDFPageStamper::getStampSpaceToPageMatrix(const PDFPage* page)
{
    if (!page)
    {
        return QTransform();
    }

    QRectF box = page->getCropBox().intersected(page->getMediaBox());
    if (!box.isValid() || box.isEmpty())
    {
        box = page->getMediaBox();
    }

    const PDFReal userUnit = page->getUserUnit() > 0.0 ? page->getUserUnit() : 1.0;
    const PDFReal x0 = box.left();
    const PDFReal y0 = box.top();
    const PDFReal w = box.width();
    const PDFReal h = box.height();

    // Point (u, v) of the displayed page (in user units) is mapped to the
    // user space point x = m11 * u + m21 * v + dx, y = m12 * u + m22 * v + dy.
    // The page is displayed rotated clockwise by the page rotation.
    QTransform rotation;
    switch (page->getPageRotation())
    {
        case PageRotation::None:
            rotation = QTransform(1.0, 0.0, 0.0, 1.0, x0, y0);
            break;

        case PageRotation::Rotate90:
            rotation = QTransform(0.0, 1.0, -1.0, 0.0, x0 + w, y0);
            break;

        case PageRotation::Rotate180:
            rotation = QTransform(-1.0, 0.0, 0.0, -1.0, x0 + w, y0 + h);
            break;

        case PageRotation::Rotate270:
            rotation = QTransform(0.0, -1.0, 1.0, 0.0, x0, y0 + h);
            break;
    }

    return QTransform::fromScale(1.0 / userUnit, 1.0 / userUnit) * rotation;
}

}   // namespace pdf
