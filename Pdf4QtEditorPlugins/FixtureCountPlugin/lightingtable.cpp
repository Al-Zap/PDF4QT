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

#include "lightingtable.h"

#include "pdfutils.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfpagestamper.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>

namespace pdfplugin
{

/// Helper functions of the table writer
class LightingTableWriterHelper
{
public:
    LightingTableWriterHelper() = delete;

    /// Padding of the cells relative to the font size
    static constexpr pdf::PDFReal PADDING = 0.45;

    /// Height of the rows relative to the font size
    static constexpr pdf::PDFReal ROW_HEIGHT = 1.6;

    /// Cap height of Helvetica relative to the font size
    static constexpr pdf::PDFReal CAP_HEIGHT = 0.718;

    /// Formats the number for the content stream
    static QByteArray number(pdf::PDFReal value);

    /// Returns true, if the cell holds a number (it is right aligned)
    static bool isNumber(const QString& text);

    /// Returns the widths of the text columns
    static std::vector<pdf::PDFReal> getColumnWidths(const LightingTable& table, pdf::PDFReal fontSize);

    /// Returns the width of the symbol column (or zero)
    static pdf::PDFReal getSymbolColumnWidth(const LightingTable& table, pdf::PDFReal fontSize);

    /// Appends the path of the marker symbol to the content stream
    static void appendSymbol(QByteArray& stream, const FixtureType& type, const QRectF& rect);

    /// Appends the text to the content stream
    static void appendText(QByteArray& stream, const QString& text, bool bold, pdf::PDFReal fontSize, const QPointF& position);

    /// Returns the text of the table, which can be written by the standard fonts
    static QString getCellText(const QString& text);

    /// Returns the subject of the table annotation
    static QString getSubject(LightingTableKind kind);
};

QByteArray LightingTableWriterHelper::number(pdf::PDFReal value)
{
    if (std::abs(value) < 0.0005)
    {
        return QByteArray("0");
    }

    QByteArray result = QByteArray::number(value, 'f', 3);
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

bool LightingTableWriterHelper::isNumber(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("^[-+]?[0-9][0-9,]*(\\.[0-9]+)?(\\s*(fc|lx|ft|m|%|'|:1))?$"));
    return expression.match(text.trimmed()).hasMatch();
}

QString LightingTableWriterHelper::getCellText(const QString& text)
{
    QString result = text;
    result.replace(QChar('\t'), QChar(' '));
    result.replace(QChar('\n'), QChar(' '));
    result.replace(QChar('\r'), QChar(' '));
    return pdf::PDFPageStamper::getRepresentableText(result);
}

std::vector<pdf::PDFReal> LightingTableWriterHelper::getColumnWidths(const LightingTable& table, pdf::PDFReal fontSize)
{
    const int columnCount = table.getColumnCount();
    std::vector<pdf::PDFReal> widths(columnCount, 2.0 * fontSize);

    for (const LightingTableRow& row : table.rows)
    {
        for (int i = 0; i < row.cells.size() && i < columnCount; ++i)
        {
            const pdf::PDFStampFont font = row.bold ? pdf::PDFStampFont::HelveticaBold : pdf::PDFStampFont::Helvetica;
            const pdf::PDFReal width = pdf::PDFPageStamper::getTextWidth(font, getCellText(row.cells[i]), fontSize) + 2.0 * PADDING * fontSize;
            widths[i] = qMax(widths[i], width);
        }
    }

    return widths;
}

pdf::PDFReal LightingTableWriterHelper::getSymbolColumnWidth(const LightingTable& table, pdf::PDFReal fontSize)
{
    return table.hasSymbolColumn ? ROW_HEIGHT * fontSize : 0.0;
}

void LightingTableWriterHelper::appendSymbol(QByteArray& stream, const FixtureType& type, const QRectF& rect)
{
    const QColor color = type.color.isValid() ? type.color : QColor(Qt::red);

    // Interior is the color of the marker over the white paper, the same
    // as the transparent interior of the markers on the sheet
    const pdf::PDFReal opacity = FixtureCountEditor::FILL_OPACITY;
    auto blend = [opacity](float component) { return number(component * opacity + 1.0 - opacity); };

    stream += blend(color.redF()) + ' ' + blend(color.greenF()) + ' ' + blend(color.blueF()) + " rg\n";
    stream += number(color.redF()) + ' ' + number(color.greenF()) + ' ' + number(color.blueF()) + " RG\n";
    stream += "1 w\n";

    if (type.shape == FixtureMarkerShape::Circle)
    {
        // Circle from four Bezier curves
        constexpr pdf::PDFReal kappa = 0.5522847498;
        const QPointF c = rect.center();
        const pdf::PDFReal rx = rect.width() * 0.5;
        const pdf::PDFReal ry = rect.height() * 0.5;
        const pdf::PDFReal kx = rx * kappa;
        const pdf::PDFReal ky = ry * kappa;

        auto point = [](pdf::PDFReal x, pdf::PDFReal y) { return number(x) + ' ' + number(y) + ' '; };
        stream += point(c.x() + rx, c.y()) + "m\n";
        stream += point(c.x() + rx, c.y() + ky) + point(c.x() + kx, c.y() + ry) + point(c.x(), c.y() + ry) + "c\n";
        stream += point(c.x() - kx, c.y() + ry) + point(c.x() - rx, c.y() + ky) + point(c.x() - rx, c.y()) + "c\n";
        stream += point(c.x() - rx, c.y() - ky) + point(c.x() - kx, c.y() - ry) + point(c.x(), c.y() - ry) + "c\n";
        stream += point(c.x() + kx, c.y() - ry) + point(c.x() + rx, c.y() - ky) + point(c.x() + rx, c.y()) + "c\n";
        stream += "b\n";
        return;
    }

    const QPolygonF polygon = FixtureType::getShapePolygon(type.shape, rect);
    for (int i = 0; i < polygon.size(); ++i)
    {
        if (i > 0 && polygon[i] == polygon.front())
        {
            break;
        }

        stream += number(polygon[i].x()) + ' ' + number(polygon[i].y()) + (i == 0 ? " m\n" : " l\n");
    }
    stream += "b\n";
}

void LightingTableWriterHelper::appendText(QByteArray& stream, const QString& text, bool bold, pdf::PDFReal fontSize, const QPointF& position)
{
    const QByteArray encoded = pdf::PDFEncoding::convertToEncoding(getCellText(text), pdf::PDFEncoding::Encoding::WinAnsi);
    if (encoded.isEmpty())
    {
        return;
    }

    stream += "BT\n";
    stream += (bold ? "/PDF4QT_HelvB " : "/PDF4QT_Helv ") + number(fontSize) + " Tf\n";
    stream += number(position.x()) + ' ' + number(position.y()) + " Td\n";
    stream += '<' + encoded.toHex() + "> Tj\n";
    stream += "ET\n";
}

QString LightingTableWriterHelper::getSubject(LightingTableKind kind)
{
    switch (kind)
    {
        case LightingTableKind::Legend:
            return QCoreApplication::translate("pdfplugin::LightingTableWriter", "Count Legend");
        case LightingTableKind::FootcandleStats:
            return QCoreApplication::translate("pdfplugin::LightingTableWriter", "Footcandle Statistics");
        case LightingTableKind::WireTakeoff:
            return QCoreApplication::translate("pdfplugin::LightingTableWriter", "Wire Takeoff");
        default:
            break;
    }

    return QCoreApplication::translate("pdfplugin::LightingTableWriter", "Table");
}

int LightingTable::getColumnCount() const
{
    int count = 0;
    for (const LightingTableRow& row : rows)
    {
        count = qMax(count, int(row.cells.size()));
    }
    return count;
}

pdf::PDFReal LightingTableWriter::getDefaultFontSize(const QSizeF& pageSize)
{
    // Letter / A4 sheets get 8 pt, ARCH D (24 x 36 in) gets about 11 pt
    const pdf::PDFReal shortSide = qMin(pageSize.width(), pageSize.height());
    return qBound(8.0, std::round(shortSide / 150.0), 24.0);
}

QSizeF LightingTableWriter::getTableSize(const LightingTable& table, pdf::PDFReal fontSize)
{
    using Helper = LightingTableWriterHelper;

    const std::vector<pdf::PDFReal> widths = Helper::getColumnWidths(table, fontSize);
    pdf::PDFReal width = Helper::getSymbolColumnWidth(table, fontSize);
    for (pdf::PDFReal columnWidth : widths)
    {
        width += columnWidth;
    }

    if (!table.title.isEmpty())
    {
        const pdf::PDFReal titleWidth = pdf::PDFPageStamper::getTextWidth(pdf::PDFStampFont::HelveticaBold, Helper::getCellText(table.title), fontSize) + 2.0 * Helper::PADDING * fontSize;
        width = qMax(width, titleWidth);
    }

    const int rowCount = int(table.rows.size()) + (table.title.isEmpty() ? 0 : 1);
    return QSizeF(qMax(width, 2.0 * fontSize), qMax(rowCount, 1) * Helper::ROW_HEIGHT * fontSize);
}

QByteArray LightingTableWriter::createContentStream(const LightingTable& table, pdf::PDFReal fontSize)
{
    using Helper = LightingTableWriterHelper;

    const QSizeF size = getTableSize(table, fontSize);
    std::vector<pdf::PDFReal> widths = Helper::getColumnWidths(table, fontSize);
    const pdf::PDFReal symbolWidth = Helper::getSymbolColumnWidth(table, fontSize);
    const pdf::PDFReal rowHeight = Helper::ROW_HEIGHT * fontSize;
    const pdf::PDFReal padding = Helper::PADDING * fontSize;
    const pdf::PDFReal baselineOffset = (rowHeight - Helper::CAP_HEIGHT * fontSize) * 0.5;

    // A long title widens the last column
    pdf::PDFReal columnsWidth = symbolWidth;
    for (pdf::PDFReal width : widths)
    {
        columnsWidth += width;
    }
    if (!widths.empty() && columnsWidth < size.width())
    {
        widths.back() += size.width() - columnsWidth;
    }

    QByteArray stream;
    stream += "q\n";

    // Opaque white background, so the table is readable over the drawing
    stream += "1 g 0 0 " + Helper::number(size.width()) + ' ' + Helper::number(size.height()) + " re f\n";

    pdf::PDFReal top = size.height();
    if (!table.title.isEmpty())
    {
        stream += "0.85 g 0 " + Helper::number(top - rowHeight) + ' ' + Helper::number(size.width()) + ' ' + Helper::number(rowHeight) + " re f\n";
        stream += "0 g\n";
        Helper::appendText(stream, table.title, true, fontSize, QPointF(padding, top - rowHeight + baselineOffset));
        top -= rowHeight;
    }

    for (const LightingTableRow& row : table.rows)
    {
        const pdf::PDFReal bottom = top - rowHeight;
        pdf::PDFReal x = 0.0;

        if (table.hasSymbolColumn)
        {
            if (row.symbol.isValid())
            {
                const pdf::PDFReal symbolSize = 0.9 * fontSize;
                const QRectF symbolRect(x + (symbolWidth - symbolSize) * 0.5, bottom + (rowHeight - symbolSize) * 0.5, symbolSize, symbolSize);
                Helper::appendSymbol(stream, row.symbol, symbolRect);
            }
            x += symbolWidth;
        }

        stream += "0 g\n";
        for (int i = 0; i < int(widths.size()); ++i)
        {
            if (i < row.cells.size())
            {
                const QString& text = row.cells[i];
                const pdf::PDFStampFont font = row.bold ? pdf::PDFStampFont::HelveticaBold : pdf::PDFStampFont::Helvetica;
                pdf::PDFReal textX = x + padding;
                if (Helper::isNumber(text))
                {
                    textX = x + widths[i] - padding - pdf::PDFPageStamper::getTextWidth(font, Helper::getCellText(text), fontSize);
                }
                Helper::appendText(stream, text, row.bold, fontSize, QPointF(textX, bottom + baselineOffset));
            }
            x += widths[i];
        }

        top = bottom;
    }

    // Grid: lines between the rows and the columns, frame
    stream += "0 G 0.4 w\n";
    const int rowLines = int(table.rows.size()) + (table.title.isEmpty() ? 0 : 1);
    for (int i = 1; i < rowLines; ++i)
    {
        const pdf::PDFReal y = size.height() - i * rowHeight;
        stream += "0 " + Helper::number(y) + " m " + Helper::number(size.width()) + ' ' + Helper::number(y) + " l S\n";
    }

    const pdf::PDFReal gridTop = size.height() - (table.title.isEmpty() ? 0.0 : rowHeight);
    pdf::PDFReal x = symbolWidth;
    for (int i = 0; i + 1 < int(widths.size()); ++i)
    {
        x += widths[i];
        stream += Helper::number(x) + " 0 m " + Helper::number(x) + ' ' + Helper::number(gridTop) + " l S\n";
    }
    if (table.hasSymbolColumn && !widths.empty())
    {
        stream += Helper::number(symbolWidth) + " 0 m " + Helper::number(symbolWidth) + ' ' + Helper::number(gridTop) + " l S\n";
    }

    stream += "0 G 1 w 0.5 0.5 " + Helper::number(size.width() - 1.0) + ' ' + Helper::number(size.height() - 1.0) + " re S\n";
    stream += "Q\n";
    return stream;
}

QString LightingTableWriter::getText(const LightingTable& table)
{
    QStringList lines;
    if (!table.title.isEmpty())
    {
        lines << table.title;
    }
    for (const LightingTableRow& row : table.rows)
    {
        lines << row.cells.join(QChar('\t'));
    }
    return lines.join(QChar('\n'));
}

QTransform LightingTableWriter::getRotationMatrix(pdf::PageRotation rotation)
{
    // The page is displayed rotated clockwise, so the table is rotated
    // counterclockwise in the page coordinate system (y axis points up)
    switch (rotation)
    {
        case pdf::PageRotation::Rotate90:
            return QTransform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0);
        case pdf::PageRotation::Rotate180:
            return QTransform(-1.0, 0.0, 0.0, -1.0, 0.0, 0.0);
        case pdf::PageRotation::Rotate270:
            return QTransform(0.0, -1.0, 1.0, 0.0, 0.0, 0.0);
        default:
            break;
    }

    return QTransform();
}

QRectF LightingTableWriter::getAnnotationRect(const QPointF& topLeft, const QSizeF& size, pdf::PageRotation rotation)
{
    const QTransform matrix = getRotationMatrix(rotation);
    const QRectF box = matrix.mapRect(QRectF(QPointF(0.0, 0.0), size));
    const QPointF mappedTopLeft = matrix.map(QPointF(0.0, size.height()));
    return box.translated(topLeft - mappedTopLeft);
}

QPointF LightingTableWriter::getTopLeft(const QRectF& rect, pdf::PageRotation rotation)
{
    // Relative position of the upright top left corner in the rectangle
    const QTransform matrix = getRotationMatrix(rotation);
    const QRectF box = matrix.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
    const QPointF mappedTopLeft = matrix.map(QPointF(0.0, 1.0)) - box.topLeft();
    const QRectF r = rect.normalized();
    return QPointF(r.left() + mappedTopLeft.x() * r.width(), r.top() + mappedTopLeft.y() * r.height());
}

pdf::PDFObjectReference LightingTableWriter::createTable(pdf::PDFDocumentBuilder* builder,
                                                         pdf::PDFObjectReference pageReference,
                                                         pdf::PageRotation rotation,
                                                         const QPointF& topLeft,
                                                         const LightingTable& table,
                                                         LightingTableKind kind,
                                                         LightingLegendScope scope,
                                                         pdf::PDFReal fontSize)
{
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Annot");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("Stamp");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("P");
    factory << pageReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Name");
    factory << pdf::WrapName(STAMP_NAME);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("CreationDate");
    factory << pdf::WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("T");
    factory << pdf::PDFAuthorSettings::getAuthorName();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("F");
    factory << pdf::PDFInteger(4); // Print
    factory.endDictionaryItem();
    factory.endDictionary();

    const pdf::PDFObjectReference annotation = builder->addObject(factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory.beginArray();
    factory << annotation;
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->appendTo(pageReference, factory.takeObject());

    writeTable(builder, annotation, rotation, topLeft, table, kind, scope, fontSize);
    return annotation;
}

bool LightingTableWriter::updateTable(pdf::PDFDocumentBuilder* builder,
                                      const pdf::PDFDocument* document,
                                      const LightingTableAnnotation& tableAnnotation,
                                      const LightingTable& table)
{
    const pdf::PDFCatalog* catalog = document->getCatalog();
    if (tableAnnotation.pageIndex < 0 || size_t(tableAnnotation.pageIndex) >= catalog->getPageCount() || !tableAnnotation.annotation.isValid())
    {
        return false;
    }

    const pdf::PDFPage* page = catalog->getPage(tableAnnotation.pageIndex);
    const pdf::PageRotation rotation = page->getPageRotation();
    const pdf::PDFReal fontSize = tableAnnotation.fontSize > 0.0 ? tableAnnotation.fontSize : getDefaultFontSize(page->getRotatedMediaBox().size());
    const QPointF topLeft = getTopLeft(tableAnnotation.rect, rotation);
    writeTable(builder, tableAnnotation.annotation, rotation, topLeft, table, tableAnnotation.kind, tableAnnotation.scope, fontSize);
    return true;
}

void LightingTableWriter::writeTable(pdf::PDFDocumentBuilder* builder,
                                     pdf::PDFObjectReference annotation,
                                     pdf::PageRotation rotation,
                                     const QPointF& topLeft,
                                     const LightingTable& table,
                                     LightingTableKind kind,
                                     LightingLegendScope scope,
                                     pdf::PDFReal fontSize)
{
    const QSizeF size = getTableSize(table, fontSize);
    const QTransform matrix = getRotationMatrix(rotation);
    const QRectF rect = getAnnotationRect(topLeft, size, rotation);
    const QByteArray content = createContentStream(table, fontSize);

    auto writeFont = [](pdf::PDFObjectFactory& factory, const char* baseFont)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Font");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << pdf::WrapName("Type1");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BaseFont");
        factory << pdf::WrapName(baseFont);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Encoding");
        factory << pdf::WrapName("WinAnsiEncoding");
        factory.endDictionaryItem();
        factory.endDictionary();
    };

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << QRectF(QPointF(0.0, 0.0), size);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Matrix");
    factory.beginArray();
    factory << matrix.m11() << matrix.m12() << matrix.m21() << matrix.m22() << matrix.dx() << matrix.dy();
    factory.endArray();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.beginDictionaryItem("Font");
    factory.beginDictionary();
    factory.beginDictionaryItem("PDF4QT_Helv");
    writeFont(factory, "Helvetica");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("PDF4QT_HelvB");
    writeFont(factory, "Helvetica-Bold");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Length");
    factory << pdf::PDFInteger(content.size());
    factory.endDictionaryItem();
    factory.endDictionary();

    pdf::PDFObject formDictionaryObject = factory.takeObject();
    const pdf::PDFDictionary* formDictionary = formDictionaryObject.getDictionary();
    Q_ASSERT(formDictionary);
    const pdf::PDFObjectReference form = builder->addObject(pdf::PDFObject::createStream(pdf::PDFStream(pdf::PDFDictionaryBuilder(*formDictionary), QByteArray(content))));

    factory.beginDictionary();
    factory.beginDictionaryItem("Rect");
    factory << rect;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("AP");
    factory.beginDictionary();
    factory.beginDictionaryItem("N");
    factory << form;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Contents");
    factory << getText(table);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subj");
    factory << LightingTableWriterHelper::getSubject(kind);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("M");
    factory << pdf::WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.beginDictionaryItem(TABLE_KIND_KEY);
    factory << pdf::WrapName(getKindKey(kind));
    factory.endDictionaryItem();
    factory.beginDictionaryItem(FONT_SIZE_KEY);
    factory << fontSize;
    factory.endDictionaryItem();
    if (kind == LightingTableKind::Legend)
    {
        factory.beginDictionaryItem(LEGEND_SCOPE_KEY);
        factory << pdf::WrapName(scope == LightingLegendScope::Document ? "Document" : "Sheet");
        factory.endDictionaryItem();
    }
    factory.endDictionary();

    builder->mergeTo(annotation, factory.takeObject());
}

LightingTableAnnotations LightingTableWriter::scan(const pdf::PDFDocument* document, LightingTableKind kind)
{
    LightingTableAnnotations result;

    if (!document)
    {
        return result;
    }

    pdf::PDFDocumentDataLoaderDecorator loader(document);
    const pdf::PDFCatalog* catalog = document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);
        for (const pdf::PDFObjectReference& annotationReference : page->getAnnotations())
        {
            const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));
            if (!dictionary || !dictionary->hasKey(TABLE_KIND_KEY))
            {
                continue;
            }

            LightingTableAnnotation tableAnnotation;
            tableAnnotation.kind = getKindFromKey(loader.readNameFromDictionary(dictionary, TABLE_KIND_KEY));
            if (tableAnnotation.kind == LightingTableKind::Invalid || (kind != LightingTableKind::Invalid && tableAnnotation.kind != kind))
            {
                continue;
            }

            tableAnnotation.pageIndex = pdf::PDFInteger(pageIndex);
            tableAnnotation.annotation = annotationReference;
            tableAnnotation.rect = loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized();
            tableAnnotation.fontSize = qBound(0.0, loader.readNumberFromDictionary(dictionary, FONT_SIZE_KEY, 0.0), 200.0);
            tableAnnotation.scope = loader.readNameFromDictionary(dictionary, LEGEND_SCOPE_KEY) == "Document" ? LightingLegendScope::Document : LightingLegendScope::Sheet;
            result.push_back(tableAnnotation);
        }
    }

    return result;
}

QByteArray LightingTableWriter::getKindKey(LightingTableKind kind)
{
    switch (kind)
    {
        case LightingTableKind::Legend:
            return QByteArrayLiteral("Legend");
        case LightingTableKind::FootcandleStats:
            return QByteArrayLiteral("FootcandleStats");
        case LightingTableKind::WireTakeoff:
            return QByteArrayLiteral("WireTakeoff");
        default:
            break;
    }

    return QByteArray();
}

LightingTableKind LightingTableWriter::getKindFromKey(const QByteArray& key)
{
    for (LightingTableKind kind : { LightingTableKind::Legend, LightingTableKind::FootcandleStats, LightingTableKind::WireTakeoff })
    {
        if (getKindKey(kind) == key)
        {
            return kind;
        }
    }

    return LightingTableKind::Invalid;
}

}   // namespace pdfplugin
