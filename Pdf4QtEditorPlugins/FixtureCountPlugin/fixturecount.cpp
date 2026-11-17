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

#include "fixturecount.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfocrpagepreparer.h"
#include "pdfoutline.h"
#include "pdfutils.h"

#include <QCoreApplication>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace pdfplugin
{

/// Helper functions of the fixture count, which are not a part of the interface
class FixtureCountHelper
{
public:
    FixtureCountHelper() = delete;

    /// Collects the sheet names from the outline item and its children. The first
    /// bookmark pointing to a page wins (bookmarks are visited in the document order).
    static void collectSheetNames(const pdf::PDFDocument* document, const pdf::PDFOutlineItem* item, QStringList& names, std::vector<bool>& assigned);

    /// Returns the page index of the destination, or -1
    static pdf::PDFInteger getDestinationPageIndex(const pdf::PDFDocument* document, const pdf::PDFDestination& destination);

    /// Returns the subject of the marker annotation
    static QString getSubject(const FixtureType& type);

    /// Returns the contents of the marker annotation
    static QString getContents(const FixtureType& type);
};

QString FixtureType::getShapeName(FixtureMarkerShape shape)
{
    switch (shape)
    {
        case FixtureMarkerShape::Circle:
            return QCoreApplication::translate("pdfplugin::FixtureType", "Circle");
        case FixtureMarkerShape::Square:
            return QCoreApplication::translate("pdfplugin::FixtureType", "Square");
        case FixtureMarkerShape::Diamond:
            return QCoreApplication::translate("pdfplugin::FixtureType", "Diamond");
        case FixtureMarkerShape::Triangle:
            return QCoreApplication::translate("pdfplugin::FixtureType", "Triangle");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

FixtureMarkerShape FixtureType::getShapeFromKey(const QString& key)
{
    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        const FixtureMarkerShape shape = FixtureMarkerShape(i);
        if (getShapeKey(shape) == key)
        {
            return shape;
        }
    }

    return FixtureMarkerShape::Circle;
}

QString FixtureType::getShapeKey(FixtureMarkerShape shape)
{
    switch (shape)
    {
        case FixtureMarkerShape::Circle:
            return QStringLiteral("Circle");
        case FixtureMarkerShape::Square:
            return QStringLiteral("Square");
        case FixtureMarkerShape::Diamond:
            return QStringLiteral("Diamond");
        case FixtureMarkerShape::Triangle:
            return QStringLiteral("Triangle");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QPolygonF FixtureType::getShapePolygon(FixtureMarkerShape shape, const QRectF& rect)
{
    const QRectF r = rect.normalized();
    QPolygonF polygon;

    switch (shape)
    {
        case FixtureMarkerShape::Diamond:
            polygon << QPointF(r.center().x(), r.top()) << QPointF(r.right(), r.center().y())
                    << QPointF(r.center().x(), r.bottom()) << QPointF(r.left(), r.center().y());
            break;

        case FixtureMarkerShape::Triangle:
            // The y axis of the page goes up, so the apex is at the bottom() of the rectangle,
            // which has the greater y coordinate
            polygon << QPointF(r.left(), r.top()) << QPointF(r.right(), r.top()) << QPointF(r.center().x(), r.bottom());
            break;

        case FixtureMarkerShape::Square:
            polygon << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
            break;

        case FixtureMarkerShape::Circle:
        default:
        {
            constexpr int segments = 24;
            for (int i = 0; i < segments; ++i)
            {
                const qreal angle = 2.0 * std::numbers::pi * i / segments;
                polygon << QPointF(r.center().x() + 0.5 * r.width() * std::cos(angle), r.center().y() + 0.5 * r.height() * std::sin(angle));
            }
            break;
        }
    }

    return polygon;
}

FixtureType FixtureType::createDefaultType(int index)
{
    // Colors are well distinguishable on a white drawing, the shapes cycle
    // with a different period, so the combinations do not repeat soon
    static constexpr QRgb colors[] = { 0xE6194B, 0x3CB44B, 0x4363D8, 0xF58231, 0x911EB4, 0x42D4F4, 0xF032E6, 0x9A6324, 0x469990, 0x808000 };
    constexpr int colorCount = int(std::size(colors));
    constexpr int shapeCount = int(FixtureMarkerShape::LastShape);

    FixtureType type;
    type.name = QString("T%1").arg(index + 1);
    type.color = QColor::fromRgb(colors[index % colorCount]);
    type.shape = FixtureMarkerShape((index + index / colorCount) % shapeCount);
    return type;
}

FixtureCountData FixtureCountData::scan(const pdf::PDFDocument* document)
{
    FixtureCountData data;

    if (!document)
    {
        return data;
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
            if (!dictionary || !dictionary->hasKey(FIXTURE_TYPE_KEY))
            {
                continue;
            }

            const QString typeName = loader.readTextStringFromDictionary(dictionary, FIXTURE_TYPE_KEY, QString()).trimmed();
            if (typeName.isEmpty())
            {
                continue;
            }

            FixtureMarker marker;
            marker.pageIndex = pdf::PDFInteger(pageIndex);
            marker.annotation = annotationReference;
            marker.typeName = typeName;
            marker.rect = loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized();
            marker.size = loader.readNumberFromDictionary(dictionary, FIXTURE_SIZE_KEY, qMin(marker.rect.width(), marker.rect.height()));
            data.m_markers.push_back(marker);
            data.m_counts[typeName][marker.pageIndex] += 1;

            auto it = std::find_if(data.m_types.cbegin(), data.m_types.cend(), [&typeName](const FixtureType& type) { return type.name == typeName; });
            if (it == data.m_types.cend())
            {
                FixtureType type;
                type.name = typeName;
                type.description = loader.readTextStringFromDictionary(dictionary, FIXTURE_DESCRIPTION_KEY, QString());
                type.shape = FixtureType::getShapeFromKey(QString::fromLatin1(loader.readNameFromDictionary(dictionary, FIXTURE_SHAPE_KEY)));

                std::vector<pdf::PDFReal> color = loader.readNumberArrayFromDictionary(dictionary, "C");
                if (color.size() == 3)
                {
                    type.color = QColor::fromRgbF(color[0], color[1], color[2]);
                }
                else
                {
                    type.color = Qt::red;
                }

                data.m_types.push_back(type);
            }
        }
    }

    return data;
}

int FixtureCountData::getCount(const QString& typeName, pdf::PDFInteger pageIndex) const
{
    auto it = m_counts.find(typeName);
    if (it == m_counts.cend())
    {
        return 0;
    }

    if (pageIndex == -1)
    {
        int count = 0;
        for (const auto& item : it->second)
        {
            count += item.second;
        }
        return count;
    }

    auto pageIt = it->second.find(pageIndex);
    return pageIt != it->second.cend() ? pageIt->second : 0;
}

int FixtureCountData::getTotalCount(pdf::PDFInteger pageIndex) const
{
    int count = 0;
    for (const auto& item : m_counts)
    {
        count += getCount(item.first, pageIndex);
    }
    return count;
}

std::vector<pdf::PDFInteger> FixtureCountData::getPagesWithMarkers() const
{
    std::vector<pdf::PDFInteger> pages;
    for (const FixtureMarker& marker : m_markers)
    {
        pages.push_back(marker.pageIndex);
    }

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

FixtureMarkers FixtureCountData::getMarkersOfType(const QString& typeName) const
{
    FixtureMarkers markers;
    std::copy_if(m_markers.cbegin(), m_markers.cend(), std::back_inserter(markers), [&typeName](const FixtureMarker& marker) { return marker.typeName == typeName; });
    return markers;
}

const FixtureMarker* FixtureCountData::findMarker(pdf::PDFInteger pageIndex, const QPointF& point, pdf::PDFReal tolerance) const
{
    const FixtureMarker* bestMarker = nullptr;
    pdf::PDFReal bestDistance = std::numeric_limits<pdf::PDFReal>::infinity();

    for (const FixtureMarker& marker : m_markers)
    {
        if (marker.pageIndex != pageIndex)
        {
            continue;
        }

        const QRectF rect = marker.rect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
        if (!rect.contains(point))
        {
            continue;
        }

        const QPointF difference = marker.getCenter() - point;
        const pdf::PDFReal distance = QPointF::dotProduct(difference, difference);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestMarker = &marker;
        }
    }

    return bestMarker;
}

QString FixtureCountHelper::getSubject(const FixtureType& type)
{
    return QCoreApplication::translate("pdfplugin::FixtureCountEditor", "Fixture %1").arg(type.name);
}

QString FixtureCountHelper::getContents(const FixtureType& type)
{
    if (type.description.isEmpty())
    {
        return type.name;
    }

    return QString("%1 - %2").arg(type.name, type.description);
}

pdf::PDFObjectReference FixtureCountEditor::createMarker(pdf::PDFDocumentBuilder* builder,
                                                         pdf::PDFObjectReference pageReference,
                                                         const QPointF& center,
                                                         pdf::PDFReal size,
                                                         const FixtureType& type)
{
    constexpr pdf::PDFReal borderWidth = 1.0;

    QRectF rect(center.x() - size * 0.5, center.y() - size * 0.5, size, size);
    const QString author = pdf::PDFAuthorSettings::getAuthorName();
    const QString subject = FixtureCountHelper::getSubject(type);
    const QString contents = FixtureCountHelper::getContents(type);

    pdf::PDFObjectReference annotation;
    switch (type.shape)
    {
        case FixtureMarkerShape::Circle:
            annotation = builder->createAnnotationCircle(pageReference, rect, borderWidth, type.color, type.color, author, subject, contents);
            break;

        case FixtureMarkerShape::Square:
            annotation = builder->createAnnotationSquare(pageReference, rect, borderWidth, type.color, type.color, author, subject, contents);
            break;

        case FixtureMarkerShape::Diamond:
        case FixtureMarkerShape::Triangle:
            annotation = builder->createAnnotationPolygon(pageReference, FixtureType::getShapePolygon(type.shape, rect), borderWidth, type.color, type.color, author, subject, contents);
            break;

        default:
            Q_ASSERT(false);
            return pdf::PDFObjectReference();
    }

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem(FixtureCountData::FIXTURE_TYPE_KEY);
    factory << type.name;
    factory.endDictionaryItem();
    if (!type.description.isEmpty())
    {
        factory.beginDictionaryItem(FixtureCountData::FIXTURE_DESCRIPTION_KEY);
        factory << type.description;
        factory.endDictionaryItem();
    }
    factory.beginDictionaryItem(FixtureCountData::FIXTURE_SHAPE_KEY);
    factory << pdf::WrapName(FixtureType::getShapeKey(type.shape).toLatin1());
    factory.endDictionaryItem();
    factory.beginDictionaryItem(FixtureCountData::FIXTURE_SIZE_KEY);
    factory << size;
    factory.endDictionaryItem();

    // The interior is transparent, so the fixture symbol under the marker stays visible,
    // the outline stays opaque. The appearance stream is generated after this entry
    // is written, so other viewers display the marker the same way.
    factory.beginDictionaryItem("ca");
    factory << FILL_OPACITY;
    factory.endDictionaryItem();
    factory.endDictionary();

    builder->mergeTo(annotation, factory.takeObject());
    builder->updateAnnotationAppearanceStreams(annotation);
    return annotation;
}

bool FixtureCountEditor::removeMarker(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const FixtureMarker& marker)
{
    const pdf::PDFCatalog* catalog = document->getCatalog();
    if (marker.pageIndex < 0 || size_t(marker.pageIndex) >= catalog->getPageCount())
    {
        return false;
    }

    builder->removeAnnotation(catalog->getPage(marker.pageIndex)->getPageReference(), marker.annotation);
    return true;
}

bool FixtureCountEditor::changeMarkerType(pdf::PDFDocumentBuilder* builder,
                                          const pdf::PDFDocument* document,
                                          const FixtureMarker& marker,
                                          const FixtureType& type)
{
    const pdf::PDFCatalog* catalog = document->getCatalog();
    if (marker.pageIndex < 0 || size_t(marker.pageIndex) >= catalog->getPageCount())
    {
        return false;
    }

    // Marker of the same shape is changed in place, so its position, its size
    // (it could be resized as an ordinary annotation) and its replies are kept
    const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(marker.annotation));
    if (dictionary)
    {
        pdf::PDFDocumentDataLoaderDecorator loader(document);
        const QByteArray shapeKey = loader.readNameFromDictionary(dictionary, FixtureCountData::FIXTURE_SHAPE_KEY);
        if (shapeKey == FixtureType::getShapeKey(type.shape).toLatin1())
        {
            pdf::PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("C");
            factory << pdf::WrapAnnotationColor(type.color);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("IC");
            factory << pdf::WrapAnnotationColor(type.color);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Subj");
            factory << FixtureCountHelper::getSubject(type);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Contents");
            factory << FixtureCountHelper::getContents(type);
            factory.endDictionaryItem();
            factory.beginDictionaryItem(FixtureCountData::FIXTURE_TYPE_KEY);
            factory << type.name;
            factory.endDictionaryItem();
            factory.beginDictionaryItem(FixtureCountData::FIXTURE_DESCRIPTION_KEY);
            factory << type.description;
            factory.endDictionaryItem();
            factory.endDictionary();

            builder->mergeTo(marker.annotation, factory.takeObject());
            builder->updateAnnotationAppearanceStreams(marker.annotation);
            return true;
        }
    }

    const pdf::PDFObjectReference pageReference = catalog->getPage(marker.pageIndex)->getPageReference();
    const pdf::PDFReal size = marker.size > 0.0 ? marker.size : qMin(marker.rect.width(), marker.rect.height());
    builder->removeAnnotation(pageReference, marker.annotation);
    return createMarker(builder, pageReference, marker.getCenter(), size, type).isValid();
}

pdf::PDFInteger FixtureCountHelper::getDestinationPageIndex(const pdf::PDFDocument* document, const pdf::PDFDestination& destination)
{
    const pdf::PDFCatalog* catalog = document->getCatalog();
    const pdf::PDFDestination* resolvedDestination = &destination;

    if (destination.getDestinationType() == pdf::DestinationType::Named)
    {
        resolvedDestination = catalog->getNamedDestination(destination.getName());
        if (!resolvedDestination)
        {
            return -1;
        }
    }

    if (resolvedDestination->getPageReference().isValid())
    {
        const size_t pageIndex = catalog->getPageIndexFromPageReference(resolvedDestination->getPageReference());
        if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
        {
            return pdf::PDFInteger(pageIndex);
        }
        return -1;
    }

    // Destinations of remote documents use page numbers, but destinations of
    // a few broken documents use them too
    const pdf::PDFInteger pageIndex = resolvedDestination->getPageIndex();
    if (pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount())
    {
        return pageIndex;
    }

    return -1;
}

void FixtureCountHelper::collectSheetNames(const pdf::PDFDocument* document, const pdf::PDFOutlineItem* item, QStringList& names, std::vector<bool>& assigned)
{
    if (!item)
    {
        return;
    }

    const pdf::PDFAction* action = item->getAction();
    if (action && action->getType() == pdf::ActionType::GoTo && !item->getTitle().trimmed().isEmpty())
    {
        const pdf::PDFActionGoTo* goToAction = static_cast<const pdf::PDFActionGoTo*>(action);
        const pdf::PDFInteger pageIndex = getDestinationPageIndex(document, goToAction->getDestination());
        if (pageIndex >= 0 && !assigned[pageIndex])
        {
            names[pageIndex] = item->getTitle().simplified();
            assigned[pageIndex] = true;
        }
    }

    for (size_t i = 0; i < item->getChildCount(); ++i)
    {
        collectSheetNames(document, item->getChild(i), names, assigned);
    }
}

QStringList FixtureCountReport::getSheetNames(const pdf::PDFDocument* document)
{
    QStringList names;

    if (!document)
    {
        return names;
    }

    const pdf::PDFCatalog* catalog = document->getCatalog();
    const size_t pageCount = catalog->getPageCount();
    std::vector<bool> assigned(pageCount, false);

    for (size_t i = 0; i < pageCount; ++i)
    {
        // Page label of a plan set is the sheet number (E-101). AutoCAD writes
        // the index of the layout before it ("[1] Light LT-1"). Plain page
        // numbers are not sheet names.
        QString label = pdf::PDFOCRPagePreparer::getPageLabel(document, pdf::PDFInteger(i)).simplified();
        if (label.startsWith(QChar('[')) && label.indexOf(QChar(']')) > 0)
        {
            label = label.mid(label.indexOf(QChar(']')) + 1).trimmed();
        }

        const bool hasLetter = std::any_of(label.cbegin(), label.cend(), [](QChar character) { return character.isLetter(); });
        names << (hasLetter ? label : QCoreApplication::translate("pdfplugin::FixtureCountReport", "Page %1").arg(i + 1));
    }

    FixtureCountHelper::collectSheetNames(document, catalog->getOutlineRootPtr().get(), names, assigned);
    return names;
}

std::vector<QStringList> FixtureCountReport::createTable(const FixtureTypes& types,
                                                         const FixtureCountData& data,
                                                         const QStringList& sheetNames)
{
    std::vector<QStringList> table;
    const std::vector<pdf::PDFInteger> pages = data.getPagesWithMarkers();

    QStringList header;
    header << QCoreApplication::translate("pdfplugin::FixtureCountReport", "Type");
    header << QCoreApplication::translate("pdfplugin::FixtureCountReport", "Description");
    for (pdf::PDFInteger pageIndex : pages)
    {
        header << (pageIndex < sheetNames.size() ? sheetNames[pageIndex] : QCoreApplication::translate("pdfplugin::FixtureCountReport", "Page %1").arg(pageIndex + 1));
    }
    header << QCoreApplication::translate("pdfplugin::FixtureCountReport", "Total");
    table.push_back(header);

    // Types, which are only in the document, but were not passed by the caller,
    // must not be lost in the report
    FixtureTypes allTypes = types;
    for (const FixtureType& type : data.getTypes())
    {
        auto it = std::find_if(allTypes.cbegin(), allTypes.cend(), [&type](const FixtureType& item) { return item.name == type.name; });
        if (it == allTypes.cend())
        {
            allTypes.push_back(type);
        }
    }

    for (const FixtureType& type : allTypes)
    {
        QStringList row;
        row << type.name << type.description;
        for (pdf::PDFInteger pageIndex : pages)
        {
            row << QString::number(data.getCount(type.name, pageIndex));
        }
        row << QString::number(data.getCount(type.name));
        table.push_back(row);
    }

    QStringList totals;
    totals << QCoreApplication::translate("pdfplugin::FixtureCountReport", "Total") << QString();
    for (pdf::PDFInteger pageIndex : pages)
    {
        totals << QString::number(data.getTotalCount(pageIndex));
    }
    totals << QString::number(data.getTotalCount());
    table.push_back(totals);

    return table;
}

QString FixtureCountReport::escapeCsvField(const QString& fieldText)
{
    // Spreadsheets evaluate a field starting with these characters as a formula.
    // The text comes from the document (bookmarks, type names), so it is
    // prefixed by an apostrophe, which makes it a plain text.
    QString text = fieldText;
    if (!text.isEmpty() && QStringLiteral("=+-@\t\r").contains(text.front()))
    {
        text.prepend(QChar('\''));
    }

    if (text.contains(QChar(',')) || text.contains(QChar('"')) || text.contains(QChar('\n')) || text.contains(QChar('\r')) ||
        text.startsWith(QChar(' ')) || text.endsWith(QChar(' ')))
    {
        QString escapedText = text;
        escapedText.replace(QChar('"'), QString("\"\""));
        return QString("\"%1\"").arg(escapedText);
    }

    return text;
}

QString FixtureCountReport::createCsv(const std::vector<QStringList>& table)
{
    QString csv;
    for (const QStringList& row : table)
    {
        QStringList fields;
        for (const QString& field : row)
        {
            fields << escapeCsvField(field);
        }
        csv += fields.join(QChar(','));
        csv += QStringLiteral("\r\n");
    }
    return csv;
}

}   // namespace pdfplugin
