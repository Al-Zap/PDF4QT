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
#include "pdfdocumentbuilder.h"
#include "pdfannotation.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QBuffer>
#include <QLocale>
#include <QPainter>
#include <QJsonArray>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QStandardPaths>
#include <QCoreApplication>

namespace pdf
{

/// Layout of a custom stamp (in points, y axis pointing down)
class PDFCustomStampHelper
{
public:
    PDFCustomStampHelper() = delete;

    static constexpr PDFReal PADDING = 6.0;
    static constexpr PDFReal BORDER_WIDTH = 2.0;
    static constexpr PDFReal TITLE_SIZE = 18.0;
    static constexpr PDFReal TEXT_SIZE = 8.0;
    static constexpr PDFReal LINE_SPACING = 1.5;
    static constexpr PDFReal MAXIMAL_LOGO_FRACTION = 0.42;

    struct Layout
    {
        QSizeF size;
        QRectF logoRect;
        QRectF titleRect;
        std::vector<QRectF> lineRects;
        QFont titleFont;
        QFont textFont;
    };

    static Layout createLayout(const PDFCustomStamp& stamp, const QStringList& lines);

    /// Fonts are measured in points (72 dpi) independently of the screen
    static QFontMetricsF getMetrics(const QFont& font);

    /// Returns the font for the painter: sizes of the fonts of the layout are in
    /// points of the stamp, the size of a font in points of the painter depends
    /// on the resolution of the paint device
    static QFont getPainterFont(QFont font, QPainter* painter);
};

PDFCustomStampHelper::Layout PDFCustomStampHelper::createLayout(const PDFCustomStamp& stamp, const QStringList& lines)
{
    Layout layout;

    layout.titleFont = QFont(QStringLiteral("Helvetica"));
    layout.titleFont.setPointSizeF(TITLE_SIZE);
    layout.titleFont.setBold(true);
    layout.textFont = QFont(QStringLiteral("Helvetica"));
    layout.textFont.setPointSizeF(TEXT_SIZE);
    layout.textFont.setBold(true);

    const QFontMetricsF titleMetrics = getMetrics(layout.titleFont);
    const QFontMetricsF textMetrics = getMetrics(layout.textFont);

    const PDFReal width = qBound(72.0, stamp.width, 720.0);
    const PDFReal titleHeight = stamp.title.isEmpty() ? 0.0 : titleMetrics.height();
    const PDFReal lineHeight = textMetrics.height() + LINE_SPACING;
    const PDFReal textHeight = titleHeight + lineHeight * lines.size();
    const PDFReal contentHeight = qMax(textHeight, 24.0);

    // Logo is on the left, it has the height of the text block
    PDFReal logoWidth = 0.0;
    const QImage logo = stamp.getLogoImage();
    if (!logo.isNull() && logo.height() > 0)
    {
        logoWidth = qMin(contentHeight * logo.width() / logo.height(), (width - 2 * PADDING) * MAXIMAL_LOGO_FRACTION);
        const PDFReal logoHeight = logoWidth * logo.height() / logo.width();
        layout.logoRect = QRectF(PADDING + BORDER_WIDTH, PADDING + BORDER_WIDTH + (contentHeight - logoHeight) * 0.5, logoWidth, logoHeight);
    }

    const PDFReal textLeft = PADDING + BORDER_WIDTH + (logoWidth > 0.0 ? logoWidth + PADDING : 0.0);
    const PDFReal textWidth = width - textLeft - PADDING - BORDER_WIDTH;
    PDFReal y = PADDING + BORDER_WIDTH + (contentHeight - textHeight) * 0.5;

    if (titleHeight > 0.0)
    {
        layout.titleRect = QRectF(textLeft, y, textWidth, titleHeight);
        y += titleHeight;
    }

    for (int i = 0; i < lines.size(); ++i)
    {
        layout.lineRects.push_back(QRectF(textLeft, y, textWidth, lineHeight));
        y += lineHeight;
    }

    layout.size = QSizeF(width, contentHeight + 2 * (PADDING + BORDER_WIDTH));
    return layout;
}

QFont PDFCustomStampHelper::getPainterFont(QFont font, QPainter* painter)
{
    const int dpi = painter->device() ? painter->device()->logicalDpiY() : 72;
    if (dpi > 0)
    {
        font.setPointSizeF(font.pointSizeF() * 72.0 / dpi);
    }
    return font;
}

QFontMetricsF PDFCustomStampHelper::getMetrics(const QFont& font)
{
    QImage image(1, 1, QImage::Format_ARGB32);
    image.setDotsPerMeterX(qRound(72.0 / 0.0254));
    image.setDotsPerMeterY(qRound(72.0 / 0.0254));
    return QFontMetricsF(font, &image);
}

QImage PDFCustomStamp::getLogoImage() const
{
    QImage image;
    if (!logo.isEmpty())
    {
        image.loadFromData(logo);
    }
    return image;
}

QJsonObject PDFCustomStamp::toJson() const
{
    QJsonObject object;
    object["id"] = id;
    object["name"] = name;
    object["title"] = title;
    object["text"] = text;
    object["company"] = company;
    object["color"] = color.name(QColor::HexRgb);
    object["width"] = width;
    if (!logo.isEmpty())
    {
        object["logo"] = QString::fromLatin1(logo.toBase64());
    }
    return object;
}

PDFCustomStamp PDFCustomStamp::fromJson(const QJsonObject& object)
{
    PDFCustomStamp stamp;
    stamp.id = object["id"].toString();
    stamp.name = object["name"].toString();
    stamp.title = object["title"].toString();
    stamp.text = object["text"].toString();
    stamp.company = object["company"].toString();
    const QColor color = QColor::fromString(object["color"].toString());
    if (color.isValid())
    {
        stamp.color = color;
    }
    stamp.width = qBound(72.0, object["width"].toDouble(216.0), 720.0);
    stamp.logo = QByteArray::fromBase64(object["logo"].toString().toLatin1());

    // Logo must be an image (the file can come from anywhere)
    if (!stamp.logo.isEmpty() && stamp.getLogoImage().isNull())
    {
        stamp.logo.clear();
    }
    return stamp;
}

PDFCustomStamps PDFCustomStampLibrary::getDefaultStamps()
{
    const QString byLine = PDFTranslationContext::tr("By {author} on {date}");

    PDFCustomStamps stamps;

    PDFCustomStamp reviewed;
    reviewed.id = QStringLiteral("Reviewed");
    reviewed.name = PDFTranslationContext::tr("Reviewed");
    reviewed.title = PDFTranslationContext::tr("REVIEWED");
    reviewed.text = QStringLiteral("{company}\n") + byLine;
    reviewed.color = QColor(21, 101, 192);
    stamps.push_back(reviewed);

    PDFCustomStamp approved;
    approved.id = QStringLiteral("Approved");
    approved.name = PDFTranslationContext::tr("Approved");
    approved.title = PDFTranslationContext::tr("APPROVED");
    approved.text = QStringLiteral("{company}\n") + byLine;
    approved.color = QColor(46, 125, 50);
    stamps.push_back(approved);

    PDFCustomStamp approvedAsNoted;
    approvedAsNoted.id = QStringLiteral("ApprovedAsNoted");
    approvedAsNoted.name = PDFTranslationContext::tr("Approved as Noted");
    approvedAsNoted.title = PDFTranslationContext::tr("APPROVED AS NOTED");
    approvedAsNoted.text = QStringLiteral("{company}\n") + byLine;
    approvedAsNoted.color = QColor(46, 125, 50);
    approvedAsNoted.width = 252.0;
    stamps.push_back(approvedAsNoted);

    PDFCustomStamp revise;
    revise.id = QStringLiteral("ReviseAndResubmit");
    revise.name = PDFTranslationContext::tr("Revise and Resubmit");
    revise.title = PDFTranslationContext::tr("REVISE AND RESUBMIT");
    revise.text = QStringLiteral("{company}\n") + byLine;
    revise.color = QColor(198, 40, 40);
    revise.width = 270.0;
    stamps.push_back(revise);

    PDFCustomStamp asBuilt;
    asBuilt.id = QStringLiteral("AsBuilt");
    asBuilt.name = PDFTranslationContext::tr("As Built");
    asBuilt.title = PDFTranslationContext::tr("AS BUILT");
    asBuilt.text = byLine;
    asBuilt.color = QColor(66, 66, 66);
    stamps.push_back(asBuilt);

    return stamps;
}

QString PDFCustomStampLibrary::createId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QString PDFCustomStampLibrary::expandText(const QString& text, const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime)
{
    QString result = text;
    result.replace(QStringLiteral("{date}"), QLocale().toString(dateTime.date(), QLocale::ShortFormat));
    result.replace(QStringLiteral("{time}"), QLocale().toString(dateTime.time(), QLocale::ShortFormat));
    result.replace(QStringLiteral("{author}"), author);
    result.replace(QStringLiteral("{company}"), stamp.company);
    return result;
}

QStringList PDFCustomStampLibrary::getLines(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime)
{
    QStringList lines;
    for (const QString& line : stamp.text.split(QChar('\n')))
    {
        const QString expandedLine = expandText(line, stamp, author, dateTime).trimmed();
        if (!expandedLine.isEmpty())
        {
            lines << expandedLine;
        }
    }
    return lines;
}

QSizeF PDFCustomStampLibrary::getSize(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime)
{
    return PDFCustomStampHelper::createLayout(stamp, getLines(stamp, author, dateTime)).size;
}

void PDFCustomStampLibrary::draw(QPainter* painter, const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime)
{
    const QStringList lines = getLines(stamp, author, dateTime);
    const PDFCustomStampHelper::Layout layout = PDFCustomStampHelper::createLayout(stamp, lines);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Frame with rounded corners, inner part is slightly tinted
    const qreal halfBorder = PDFCustomStampHelper::BORDER_WIDTH * 0.5;
    const QRectF frame = QRectF(QPointF(0, 0), layout.size).adjusted(halfBorder, halfBorder, -halfBorder, -halfBorder);
    QColor fillColor = stamp.color;
    fillColor.setAlphaF(0.06);
    painter->setPen(QPen(stamp.color, PDFCustomStampHelper::BORDER_WIDTH));
    painter->setBrush(fillColor);
    painter->drawRoundedRect(frame, 5.0, 5.0);

    const QImage logo = stamp.getLogoImage();
    if (!logo.isNull() && layout.logoRect.isValid())
    {
        painter->drawImage(layout.logoRect, logo);
    }

    painter->setPen(stamp.color);
    if (layout.titleRect.isValid())
    {
        // Long titles are made smaller, so they fit the width
        QFont font = layout.titleFont;
        const PDFReal titleWidth = PDFCustomStampHelper::getMetrics(font).horizontalAdvance(stamp.title);
        if (titleWidth > layout.titleRect.width())
        {
            font.setPointSizeF(font.pointSizeF() * layout.titleRect.width() / titleWidth);
        }
        painter->setFont(PDFCustomStampHelper::getPainterFont(font, painter));
        painter->drawText(layout.titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, stamp.title);
    }

    painter->setFont(PDFCustomStampHelper::getPainterFont(layout.textFont, painter));
    const QFontMetricsF textMetrics = PDFCustomStampHelper::getMetrics(layout.textFont);
    for (int i = 0; i < lines.size(); ++i)
    {
        const QRectF& rect = layout.lineRects[i];
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, textMetrics.elidedText(lines[i], Qt::ElideRight, rect.width()));
    }

    painter->restore();
}

QImage PDFCustomStampLibrary::renderPreview(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime, PDFReal scale)
{
    const QSizeF size = getSize(stamp, author, dateTime);
    QImage image((size * scale).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDotsPerMeterX(qRound(72.0 * scale / 0.0254));
    image.setDotsPerMeterY(qRound(72.0 * scale / 0.0254));

    QPainter painter(&image);
    painter.scale(scale, scale);
    draw(&painter, stamp, author, dateTime);
    painter.end();

    return image;
}

PDFObjectReference PDFCustomStampLibrary::createAnnotation(PDFDocumentBuilder* builder,
                                                           PDFObjectReference page,
                                                           QPointF center,
                                                           const PDFCustomStamp& stamp,
                                                           const QString& author,
                                                           const QDateTime& dateTime,
                                                           int rotation)
{
    const QSizeF size = getSize(stamp, author, dateTime);

    // Appearance stream is painted by the painter into a content stream
    PDFContentStreamBuilder contentBuilder(size, PDFContentStreamBuilder::CoordinateSystem::Qt);
    QPainter* painter = contentBuilder.begin();
    draw(painter, stamp, author, dateTime);
    PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);

    std::vector<PDFObject> copiedObjects = builder->copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
    Q_ASSERT(copiedObjects.size() == 2);
    const PDFObjectReference resourcesReference = copiedObjects[0].getReference();
    const PDFObjectReference formReference = copiedObjects[1].getReference();

    // Stamp is upright on the screen also on a rotated page - the form is rotated
    // against the rotation of the page. Rotation of the page is clockwise.
    rotation = ((rotation % 360) + 360) % 360;
    QTransform matrix;
    matrix.rotate(rotation);
    const QRectF bbox(QPointF(0, 0), size);
    const QRectF transformedBox = matrix.mapRect(bbox);
    matrix = matrix * QTransform::fromTranslate(-transformedBox.left(), -transformedBox.top());
    const QRectF rectangle(center.x() - transformedBox.width() * 0.5, center.y() - transformedBox.height() * 0.5, transformedBox.width(), transformedBox.height());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << bbox;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Matrix");
    factory.beginArray();
    factory << matrix.m11() << matrix.m12() << matrix.m21() << matrix.m22() << matrix.dx() << matrix.dy();
    factory.endArray();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory << resourcesReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(formReference, factory.takeObject());

    const QString title = stamp.title.isEmpty() ? stamp.name : stamp.title;
    const QStringList lines = getLines(stamp, author, dateTime);

    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Annot");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Stamp");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Rect");
    factory << rectangle;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("P");
    factory << page;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Name");
    factory << WrapName((QByteArray(NAME_PREFIX) + stamp.id.toUtf8().toPercentEncoding().replace('%', '_')).constData());
    factory.endDictionaryItem();
    factory.beginDictionaryItem("F");
    factory << PDFInteger(PDFAnnotation::Print);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("C");
    factory << WrapAnnotationColor(stamp.color);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("T");
    factory << author;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subj");
    factory << stamp.name;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Contents");
    factory << (lines.isEmpty() ? title : title + QChar('\n') + lines.join(QChar('\n')));
    factory.endDictionaryItem();
    factory.beginDictionaryItem("M");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("CreationDate");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("AP");
    factory.beginDictionary();
    factory.beginDictionaryItem("N");
    factory << formReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();

    const PDFObjectReference annotation = builder->addObject(factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory.beginArray();
    factory << annotation;
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->appendTo(page, factory.takeObject());

    return annotation;
}

bool PDFCustomStampLibrary::isCustomStampName(const QByteArray& name)
{
    static const std::array<const char*, 14> standardNames = { "Approved", "AsIs", "Confidential", "Departmental", "Draft", "Experimental", "Expired",
                                                               "Final", "ForComment", "ForPublicRelease", "NotApproved", "NotForPublicRelease", "Sold", "TopSecret" };
    if (name.isEmpty())
    {
        return false;
    }

    return std::none_of(standardNames.cbegin(), standardNames.cend(), [&name](const char* standardName) { return name == standardName; });
}

QByteArray PDFCustomStampLibrary::toJson(const PDFCustomStamps& stamps)
{
    QJsonArray array;
    for (const PDFCustomStamp& stamp : stamps)
    {
        array.append(stamp.toJson());
    }

    QJsonObject object;
    object["type"] = QStringLiteral("PDF4QT-Stamps");
    object["version"] = 1;
    object["stamps"] = array;
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

PDFCustomStamps PDFCustomStampLibrary::fromJson(const QByteArray& data, bool* ok)
{
    PDFCustomStamps stamps;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    const bool isValid = error.error == QJsonParseError::NoError && document.isObject() && document.object()["type"].toString() == QLatin1String("PDF4QT-Stamps");
    if (ok)
    {
        *ok = isValid;
    }

    if (!isValid)
    {
        return stamps;
    }

    for (const QJsonValue& value : document.object()["stamps"].toArray())
    {
        PDFCustomStamp stamp = PDFCustomStamp::fromJson(value.toObject());
        if (stamp.isValid())
        {
            stamps.push_back(std::move(stamp));
        }
    }

    return stamps;
}

QString PDFCustomStampLibrary::getDefaultFileName()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("stamps.json"));
}

PDFCustomStamps PDFCustomStampLibrary::load(const QString& fileName)
{
    QFile file(fileName.isEmpty() ? getDefaultFileName() : fileName);
    if (!file.open(QFile::ReadOnly))
    {
        return getDefaultStamps();
    }

    bool ok = false;
    PDFCustomStamps stamps = fromJson(file.readAll(), &ok);
    return ok ? stamps : getDefaultStamps();
}

bool PDFCustomStampLibrary::save(const PDFCustomStamps& stamps, const QString& fileName)
{
    const QString effectiveFileName = fileName.isEmpty() ? getDefaultFileName() : fileName;
    QDir().mkpath(QFileInfo(effectiveFileName).absolutePath());

    QSaveFile file(effectiveFileName);
    if (!file.open(QFile::WriteOnly))
    {
        return false;
    }

    file.write(toJson(stamps));
    return file.commit();
}

}   // namespace pdf
