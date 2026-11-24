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

#include "pdfmarkupslist.h"
#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfmeasure.h"
#include "pdfocrpagepreparer.h"
#include "pdfoutline.h"

#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QPageLayout>
#include <QFontMetricsF>

#include <map>
#include <algorithm>
#include <numeric>
#include <set>

namespace pdf
{

/// Helper functions of the markups list
class PDFMarkupsListHelper
{
public:
    PDFMarkupsListHelper() = delete;

    /// Key of the annotation dictionary, which holds the fixture type of a count marker
    static constexpr const char* FIXTURE_TYPE_KEY = "PDF4QT_FixtureType";

    /// Determines the kind of the markup
    static PDFMarkupKind getKind(const PDFAnnotation* annotation, const PDFDictionary* dictionary, bool isMeasurement, bool isAngle);

    /// Returns the page index of the destination, or -1
    static PDFInteger getDestinationPageIndex(const PDFDocument* document, const PDFDestination& destination);

    /// Assigns the titles of the bookmarks to the pages they point to
    static void collectSheetNames(const PDFDocument* document, const PDFOutlineItem* item, QStringList& names, std::vector<bool>& assigned);

    /// Sort order of the kinds in the takeoff summary
    static int getKindOrder(PDFMarkupKind kind);

    /// Draws a table of the report. Returns the y coordinate below the table.
    /// New pages are started, when the table doesn't fit.
    static qreal drawTable(QPdfWriter* writer,
                           QPainter* painter,
                           const std::vector<QStringList>& table,
                           const std::vector<qreal>& columnWeights,
                           qreal y,
                           const QRectF& pageRect);
};

QString PDFMarkupsListItem::getLabel() const
{
    if (!subject.trimmed().isEmpty())
    {
        return subject.trimmed();
    }

    return PDFMarkupsList::getKindName(kind);
}

PDFMarkupKind PDFMarkupsListHelper::getKind(const PDFAnnotation* annotation, const PDFDictionary* dictionary, bool isMeasurement, bool isAngle)
{
    if (dictionary && dictionary->hasKey(FIXTURE_TYPE_KEY))
    {
        return PDFMarkupKind::Count;
    }

    if (isMeasurement)
    {
        if (isAngle)
        {
            return PDFMarkupKind::Angle;
        }

        return annotation->getType() == AnnotationType::Polygon ? PDFMarkupKind::Area : PDFMarkupKind::Length;
    }

    switch (annotation->getType())
    {
        case AnnotationType::Text:
            return PDFMarkupKind::Note;

        case AnnotationType::FreeText:
        {
            const PDFFreeTextAnnotation* freeText = static_cast<const PDFFreeTextAnnotation*>(annotation);
            switch (freeText->getIntent())
            {
                case PDFFreeTextAnnotation::Intent::Callout:
                    return PDFMarkupKind::Callout;
                case PDFFreeTextAnnotation::Intent::TypeWriter:
                    return PDFMarkupKind::Typewriter;
                default:
                    break;
            }

            // Callout line without the intent
            return freeText->getCalloutLine().isValid() ? PDFMarkupKind::Callout : PDFMarkupKind::TextBox;
        }

        case AnnotationType::Line:
        {
            const PDFLineAnnotation* line = static_cast<const PDFLineAnnotation*>(annotation);
            const bool hasArrow = line->getStartLineEnding() != AnnotationLineEnding::None || line->getEndLineEnding() != AnnotationLineEnding::None;
            return hasArrow ? PDFMarkupKind::Arrow : PDFMarkupKind::Line;
        }

        case AnnotationType::Square:
        case AnnotationType::Circle:
        {
            const PDFSimpleGeometryAnnotation* geometry = static_cast<const PDFSimpleGeometryAnnotation*>(annotation);
            if (geometry->getBorderEffect().isCloudy())
            {
                return PDFMarkupKind::Cloud;
            }
            return annotation->getType() == AnnotationType::Square ? PDFMarkupKind::Rectangle : PDFMarkupKind::Ellipse;
        }

        case AnnotationType::Polygon:
        case AnnotationType::Polyline:
        {
            const PDFPolygonalGeometryAnnotation* geometry = static_cast<const PDFPolygonalGeometryAnnotation*>(annotation);
            if (geometry->getBorderEffect().isCloudy() || geometry->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Cloud)
            {
                return PDFMarkupKind::Cloud;
            }
            return annotation->getType() == AnnotationType::Polygon ? PDFMarkupKind::Polygon : PDFMarkupKind::Polyline;
        }

        case AnnotationType::Ink:
            return PDFMarkupKind::Ink;

        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
            return PDFMarkupKind::TextMarkup;

        case AnnotationType::Stamp:
            return PDFMarkupKind::Stamp;

        default:
            break;
    }

    return PDFMarkupKind::Other;
}

PDFInteger PDFMarkupsListHelper::getDestinationPageIndex(const PDFDocument* document, const PDFDestination& destination)
{
    const PDFCatalog* catalog = document->getCatalog();
    const PDFDestination* resolvedDestination = &destination;

    if (destination.getDestinationType() == DestinationType::Named)
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
        return pageIndex != PDFCatalog::INVALID_PAGE_INDEX ? PDFInteger(pageIndex) : -1;
    }

    const PDFInteger pageIndex = resolvedDestination->getPageIndex();
    if (pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount())
    {
        return pageIndex;
    }

    return -1;
}

void PDFMarkupsListHelper::collectSheetNames(const PDFDocument* document, const PDFOutlineItem* item, QStringList& names, std::vector<bool>& assigned)
{
    if (!item)
    {
        return;
    }

    const PDFAction* action = item->getAction();
    if (action && action->getType() == ActionType::GoTo && !item->getTitle().trimmed().isEmpty())
    {
        const PDFActionGoTo* goToAction = static_cast<const PDFActionGoTo*>(action);
        const PDFInteger pageIndex = getDestinationPageIndex(document, goToAction->getDestination());
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

int PDFMarkupsListHelper::getKindOrder(PDFMarkupKind kind)
{
    switch (kind)
    {
        case PDFMarkupKind::Count:
            return 0;
        case PDFMarkupKind::Length:
            return 1;
        case PDFMarkupKind::Area:
            return 2;
        case PDFMarkupKind::Angle:
            return 3;
        default:
            break;
    }

    return 4;
}

QStringList PDFMarkupsList::getSheetNames(const PDFDocument* document)
{
    QStringList names;

    if (!document)
    {
        return names;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const size_t pageCount = catalog->getPageCount();
    std::vector<bool> assigned(pageCount, false);

    for (size_t i = 0; i < pageCount; ++i)
    {
        // Page label of a plan set is the sheet number (E-101). AutoCAD writes
        // the index of the layout before it ("[1] Light LT-1"). Plain page
        // numbers are not sheet names.
        QString label = PDFOCRPagePreparer::getPageLabel(document, PDFInteger(i)).simplified();
        if (label.startsWith(QChar('[')) && label.indexOf(QChar(']')) > 0)
        {
            label = label.mid(label.indexOf(QChar(']')) + 1).trimmed();
        }

        const bool hasLetter = std::any_of(label.cbegin(), label.cend(), [](QChar character) { return character.isLetter(); });
        names << (hasLetter ? label : PDFTranslationContext::tr("Page %1").arg(i + 1));
    }

    PDFMarkupsListHelper::collectSheetNames(document, catalog->getOutlineRootPtr().get(), names, assigned);
    return names;
}

PDFMarkupsListItems PDFMarkupsList::collect(const PDFDocument* document)
{
    PDFMarkupsListItems items;

    if (!document)
    {
        return items;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFCatalog* catalog = document->getCatalog();
    const QStringList sheetNames = getSheetNames(document);
    PDFDocumentDataLoaderDecorator loader(document);

    // Index of the item of each listed annotation (replies can be on another page,
    // than the annotation they reply to, so they are assigned after all pages are read)
    std::map<PDFObjectReference, size_t> itemIndices;

    struct Reply
    {
        PDFObjectReference inReplyTo;
        PDFAnnotationPtr annotation;
        size_t order = 0;
    };
    std::vector<Reply> replies;
    size_t order = 0;

    const size_t pageCount = catalog->getPageCount();
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
            if (!annotation)
            {
                continue;
            }

            const PDFMarkupAnnotation* markupAnnotation = annotation->asMarkupAnnotation();
            if (!markupAnnotation)
            {
                continue;
            }

            if (markupAnnotation->getInReplyTo().isValid())
            {
                // Grouped annotations (RT = Group) are listed as the annotation they belong to
                if (markupAnnotation->getReplyType() == PDFMarkupAnnotation::ReplyType::Reply)
                {
                    replies.push_back(Reply{ markupAnnotation->getInReplyTo(), annotation, order++ });
                }
                continue;
            }

            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotationReference));

            // Measured values
            const PDFMeasure measure = PDFMeasure::parse(storage, dictionary ? dictionary->get("Measure") : PDFObject());
            const bool isMeasureValid = measure.isRectilinear() && measure.getUnitsPerUserSpaceUnit() > 0.0;
            const std::vector<PDFAnnotationManipulator::MeasuredValue> values = PDFAnnotationManipulator::getMeasuredValues(annotation.data(), isMeasureValid ? measure : PDFMeasure());
            const bool isAngle = PDFAnnotationManipulator::isAngularMeasurement(annotation.data());

            PDFMarkupsListItem item;
            item.annotation = annotationReference;
            item.pageIndex = PDFInteger(pageIndex);
            item.sheet = pageIndex < size_t(sheetNames.size()) ? sheetNames[pageIndex] : PDFTranslationContext::tr("Page %1").arg(pageIndex + 1);
            item.kind = PDFMarkupsListHelper::getKind(annotation.data(), dictionary, !values.empty(), isAngle);
            item.subject = markupAnnotation->getSubject().trimmed();
            item.contents = markupAnnotation->getContents().trimmed();
            item.author = markupAnnotation->getWindowTitle().trimmed();
            item.modified = annotation->getLastModifiedDateTime().isValid() ? annotation->getLastModifiedDateTime() : markupAnnotation->getCreationDate();
            item.color = PDFAnnotation::getFillColorFromAnnotationColor(annotation->getColor(), 1.0);
            item.rectangle = annotation->getRectangle();

            if (item.kind == PDFMarkupKind::Count)
            {
                const QString fixtureType = loader.readTextStringFromDictionary(dictionary, PDFMarkupsListHelper::FIXTURE_TYPE_KEY, QString()).trimmed();
                if (!fixtureType.isEmpty())
                {
                    item.subject = fixtureType;
                }
            }

            if (isAngle && !values.empty())
            {
                item.angle = values.front().value;
            }
            else if (!values.empty())
            {
                // Values are in the units of the measure, the default user space
                // is measured in points, if the measure is not valid
                item.length = values.front().value;
                item.lengthUnit = isMeasureValid ? values.front().unit : PDFTranslationContext::tr("pt");

                if (values.size() > 1)
                {
                    item.area = values[1].value;
                    item.areaUnit = isMeasureValid ? values[1].unit : PDFTranslationContext::tr("sq pt");
                }
            }

            itemIndices[annotationReference] = items.size();
            items.push_back(std::move(item));
        }
    }

    // Replies: state replies define the status, the others are the discussion
    std::stable_sort(replies.begin(), replies.end(), [](const Reply& left, const Reply& right)
    {
        const QDateTime leftDate = left.annotation->getLastModifiedDateTime();
        const QDateTime rightDate = right.annotation->getLastModifiedDateTime();
        if (leftDate.isValid() && rightDate.isValid() && leftDate != rightDate)
        {
            return leftDate < rightDate;
        }
        return left.order < right.order;
    });

    for (const Reply& reply : replies)
    {
        // Replies to replies belong to the thread of the root annotation
        PDFObjectReference root = reply.inReplyTo;
        std::set<PDFObjectReference> visited;
        while (itemIndices.find(root) == itemIndices.end() && visited.insert(root).second)
        {
            PDFAnnotationPtr parent = PDFAnnotation::parse(storage, root);
            if (!parent || !parent->asMarkupAnnotation() || !parent->asMarkupAnnotation()->getInReplyTo().isValid())
            {
                break;
            }
            root = parent->asMarkupAnnotation()->getInReplyTo();
        }

        auto it = itemIndices.find(root);
        if (it == itemIndices.end())
        {
            continue;
        }

        PDFMarkupsListItem& item = items[it->second];
        const PDFMarkupAnnotation* markupAnnotation = reply.annotation->asMarkupAnnotation();
        const PDFTextAnnotation* textAnnotation = dynamic_cast<const PDFTextAnnotation*>(reply.annotation.data());

        // Jakub Melka: the parser of the text annotation fills in the default state
        // model (Marked), so a state reply is recognized by the entry State
        const PDFDictionary* replyDictionary = storage->getDictionaryFromObject(storage->getObject(reply.annotation->getSelfReference()));
        if (textAnnotation && replyDictionary && replyDictionary->hasKey("State"))
        {
            // Only the review model defines the status (Marked / Unmarked is a private check mark)
            if (textAnnotation->getStateModel() == QLatin1String("Review") && root == reply.inReplyTo)
            {
                item.status = getStatusFromState(textAnnotation->getState());
                item.statusAuthor = markupAnnotation->getWindowTitle();
                item.statusDate = reply.annotation->getLastModifiedDateTime();
            }
            continue;
        }

        ++item.replyCount;
        const QString author = markupAnnotation->getWindowTitle().trimmed();
        const QString text = markupAnnotation->getContents().trimmed();
        item.replies << (author.isEmpty() ? text : QString("%1: %2").arg(author, text));
    }

    return items;
}

PDFMarkupsSummaryItems PDFMarkupsList::summarize(const PDFMarkupsListItems& items)
{
    PDFMarkupsSummaryItems summary;

    for (const PDFMarkupsListItem& item : items)
    {
        const QString label = item.getLabel();
        auto it = std::find_if(summary.begin(), summary.end(), [&](const PDFMarkupsSummaryItem& summaryItem)
        {
            return summaryItem.label == label &&
                   summaryItem.kind == item.kind &&
                   (!item.hasLength() || !summaryItem.hasLength || summaryItem.lengthUnit == item.lengthUnit) &&
                   (!item.hasArea() || !summaryItem.hasArea || summaryItem.areaUnit == item.areaUnit);
        });

        if (it == summary.end())
        {
            PDFMarkupsSummaryItem summaryItem;
            summaryItem.label = label;
            summaryItem.kind = item.kind;
            summary.push_back(summaryItem);
            it = std::prev(summary.end());
        }

        ++it->count;
        ++it->statusCounts[size_t(item.status)];

        if (item.hasLength())
        {
            it->length += item.length;
            it->lengthUnit = item.lengthUnit;
            it->hasLength = true;
        }

        if (item.hasArea())
        {
            it->area += item.area;
            it->areaUnit = item.areaUnit;
            it->hasArea = true;
        }
    }

    std::stable_sort(summary.begin(), summary.end(), [](const PDFMarkupsSummaryItem& left, const PDFMarkupsSummaryItem& right)
    {
        const int leftOrder = PDFMarkupsListHelper::getKindOrder(left.kind);
        const int rightOrder = PDFMarkupsListHelper::getKindOrder(right.kind);
        if (leftOrder != rightOrder)
        {
            return leftOrder < rightOrder;
        }

        return QString::localeAwareCompare(left.label, right.label) < 0;
    });

    return summary;
}

PDFObjectReference PDFMarkupsList::setStatus(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFMarkupStatus status, const QString& author)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFObjectReference page = PDFAnnotationManipulator::findAnnotationPage(storage, annotation);
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotation));

    if (!page.isValid() || !dictionary)
    {
        return PDFObjectReference();
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const QRectF rectangle = loader.readRectangle(dictionary->get("Rect"), QRectF());
    const QString statusName = getStatusName(status);

    // Hidden reply, as other PDF applications create it - text annotation
    // with the state, which refers to the markup by the entry IRT
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Annot");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Text");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Rect");
    factory << QRectF(rectangle.topLeft(), QSizeF(0.0, 0.0)).normalized();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("P");
    factory << page;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("F");
    factory << PDFInteger(PDFAnnotation::Hidden | PDFAnnotation::NoZoom | PDFAnnotation::NoRotate | PDFAnnotation::Print);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("IRT");
    factory << annotation;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("StateModel");
    factory << WrapString("Review");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("State");
    factory << WrapString(getStateName(status).constData());
    factory.endDictionaryItem();
    factory.beginDictionaryItem("T");
    factory << author;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Contents");
    factory << (author.isEmpty() ? statusName : PDFTranslationContext::tr("%1 set by %2").arg(statusName, author));
    factory.endDictionaryItem();
    factory.beginDictionaryItem("M");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("CreationDate");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.endDictionary();

    const PDFObjectReference reply = builder->addObject(factory.takeObject());

    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory.beginArray();
    factory << reply;
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->appendTo(page, factory.takeObject());

    return reply;
}

PDFMarkupStatus PDFMarkupsList::getStatusFromState(const QString& state)
{
    if (state == QLatin1String("Accepted"))
    {
        return PDFMarkupStatus::Accepted;
    }
    if (state == QLatin1String("Rejected"))
    {
        return PDFMarkupStatus::Rejected;
    }
    if (state == QLatin1String("Cancelled"))
    {
        return PDFMarkupStatus::Cancelled;
    }
    if (state == QLatin1String("Completed"))
    {
        return PDFMarkupStatus::Done;
    }

    return PDFMarkupStatus::Open;
}

QByteArray PDFMarkupsList::getStateName(PDFMarkupStatus status)
{
    switch (status)
    {
        case PDFMarkupStatus::Accepted:
            return "Accepted";
        case PDFMarkupStatus::Rejected:
            return "Rejected";
        case PDFMarkupStatus::Cancelled:
            return "Cancelled";
        case PDFMarkupStatus::Done:
            return "Completed";
        default:
            break;
    }

    return "None";
}

QString PDFMarkupsList::getStatusName(PDFMarkupStatus status)
{
    switch (status)
    {
        case PDFMarkupStatus::Open:
            return PDFTranslationContext::tr("Open");
        case PDFMarkupStatus::Accepted:
            return PDFTranslationContext::tr("Accepted");
        case PDFMarkupStatus::Rejected:
            return PDFTranslationContext::tr("Rejected");
        case PDFMarkupStatus::Cancelled:
            return PDFTranslationContext::tr("Cancelled");
        case PDFMarkupStatus::Done:
            return PDFTranslationContext::tr("Done");
        default:
            break;
    }

    return QString();
}

QString PDFMarkupsList::getKindName(PDFMarkupKind kind)
{
    switch (kind)
    {
        case PDFMarkupKind::Note:
            return PDFTranslationContext::tr("Note");
        case PDFMarkupKind::TextBox:
            return PDFTranslationContext::tr("Text Box");
        case PDFMarkupKind::Callout:
            return PDFTranslationContext::tr("Callout");
        case PDFMarkupKind::Typewriter:
            return PDFTranslationContext::tr("Typewriter");
        case PDFMarkupKind::Line:
            return PDFTranslationContext::tr("Line");
        case PDFMarkupKind::Arrow:
            return PDFTranslationContext::tr("Arrow");
        case PDFMarkupKind::Rectangle:
            return PDFTranslationContext::tr("Rectangle");
        case PDFMarkupKind::Ellipse:
            return PDFTranslationContext::tr("Ellipse");
        case PDFMarkupKind::Polygon:
            return PDFTranslationContext::tr("Polygon");
        case PDFMarkupKind::Polyline:
            return PDFTranslationContext::tr("Polyline");
        case PDFMarkupKind::Cloud:
            return PDFTranslationContext::tr("Cloud");
        case PDFMarkupKind::Ink:
            return PDFTranslationContext::tr("Freehand");
        case PDFMarkupKind::TextMarkup:
            return PDFTranslationContext::tr("Text Markup");
        case PDFMarkupKind::Stamp:
            return PDFTranslationContext::tr("Stamp");
        case PDFMarkupKind::Count:
            return PDFTranslationContext::tr("Count");
        case PDFMarkupKind::Length:
            return PDFTranslationContext::tr("Length");
        case PDFMarkupKind::Area:
            return PDFTranslationContext::tr("Area");
        case PDFMarkupKind::Angle:
            return PDFTranslationContext::tr("Angle");
        case PDFMarkupKind::Other:
            return PDFTranslationContext::tr("Other");
    }

    return QString();
}

QString PDFMarkupsList::getColumnTitle(Column column)
{
    switch (column)
    {
        case ColumnSheet:
            return PDFTranslationContext::tr("Sheet");
        case ColumnKind:
            return PDFTranslationContext::tr("Type");
        case ColumnSubject:
            return PDFTranslationContext::tr("Subject");
        case ColumnContents:
            return PDFTranslationContext::tr("Comment");
        case ColumnMeasurement:
            return PDFTranslationContext::tr("Measurement");
        case ColumnStatus:
            return PDFTranslationContext::tr("Status");
        case ColumnAuthor:
            return PDFTranslationContext::tr("Author");
        case ColumnDate:
            return PDFTranslationContext::tr("Date");
        case ColumnReplies:
            return PDFTranslationContext::tr("Replies");
        default:
            break;
    }

    return QString();
}

QString PDFMarkupsList::formatValue(PDFReal value)
{
    // Two decimals at most, trailing zeros are removed
    QString text = QString::number(value, 'f', 2);
    if (text.contains(QChar('.')))
    {
        while (text.endsWith(QChar('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(QChar('.')))
        {
            text.chop(1);
        }
    }
    return text;
}

QString PDFMarkupsList::getMeasurementText(const PDFMarkupsListItem& item)
{
    auto valueWithUnit = [](PDFReal value, const QString& unit)
    {
        return unit.isEmpty() ? formatValue(value) : QString("%1 %2").arg(formatValue(value), unit);
    };

    if (item.hasAngle())
    {
        return QString("%1°").arg(formatValue(item.angle));
    }

    if (item.hasArea())
    {
        return PDFTranslationContext::tr("A = %1, P = %2").arg(valueWithUnit(item.area, item.areaUnit), valueWithUnit(item.length, item.lengthUnit));
    }

    if (item.hasLength())
    {
        return valueWithUnit(item.length, item.lengthUnit);
    }

    return QString();
}

QString PDFMarkupsList::getCellText(const PDFMarkupsListItem& item, Column column)
{
    switch (column)
    {
        case ColumnSheet:
            return item.sheet;
        case ColumnKind:
            return getKindName(item.kind);
        case ColumnSubject:
            return item.subject;
        case ColumnContents:
            return item.contents;
        case ColumnMeasurement:
            return getMeasurementText(item);
        case ColumnStatus:
            return getStatusName(item.status);
        case ColumnAuthor:
            return item.author;
        case ColumnDate:
            return item.modified.isValid() ? item.modified.toLocalTime().toString("yyyy-MM-dd HH:mm") : QString();
        case ColumnReplies:
            return item.replyCount > 0 ? QString::number(item.replyCount) : QString();
        default:
            break;
    }

    return QString();
}

std::vector<QStringList> PDFMarkupsList::createTable(const PDFMarkupsListItems& items)
{
    std::vector<QStringList> table;

    QStringList header;
    header << getColumnTitle(ColumnSheet)
           << PDFTranslationContext::tr("Page")
           << getColumnTitle(ColumnKind)
           << getColumnTitle(ColumnSubject)
           << getColumnTitle(ColumnContents)
           << PDFTranslationContext::tr("Length")
           << PDFTranslationContext::tr("Length Unit")
           << PDFTranslationContext::tr("Area")
           << PDFTranslationContext::tr("Area Unit")
           << PDFTranslationContext::tr("Angle")
           << getColumnTitle(ColumnStatus)
           << PDFTranslationContext::tr("Status By")
           << getColumnTitle(ColumnAuthor)
           << getColumnTitle(ColumnDate)
           << PDFTranslationContext::tr("Color")
           << getColumnTitle(ColumnReplies);
    table.push_back(header);

    for (const PDFMarkupsListItem& item : items)
    {
        QStringList row;
        row << item.sheet
            << QString::number(item.pageIndex + 1)
            << getKindName(item.kind)
            << item.subject
            << item.contents
            << (item.hasLength() ? formatValue(item.length) : QString())
            << (item.hasLength() ? item.lengthUnit : QString())
            << (item.hasArea() ? formatValue(item.area) : QString())
            << (item.hasArea() ? item.areaUnit : QString())
            << (item.hasAngle() ? formatValue(item.angle) : QString())
            << getStatusName(item.status)
            << item.statusAuthor
            << item.author
            << getCellText(item, ColumnDate)
            << (item.color.isValid() ? item.color.name() : QString())
            << item.replies.join(QStringLiteral(" | "));
        table.push_back(row);
    }

    return table;
}

std::vector<QStringList> PDFMarkupsList::createSummaryTable(const PDFMarkupsSummaryItems& items)
{
    std::vector<QStringList> table;

    QStringList header;
    header << PDFTranslationContext::tr("Item")
           << getColumnTitle(ColumnKind)
           << PDFTranslationContext::tr("Count")
           << PDFTranslationContext::tr("Total Length")
           << PDFTranslationContext::tr("Total Area");
    for (size_t i = 0; i < size_t(PDFMarkupStatus::LastStatus); ++i)
    {
        header << getStatusName(PDFMarkupStatus(i));
    }
    table.push_back(header);

    for (const PDFMarkupsSummaryItem& item : items)
    {
        QStringList row;
        row << item.label
            << getKindName(item.kind)
            << QString::number(item.count)
            << (item.hasLength ? QString("%1 %2").arg(formatValue(item.length), item.lengthUnit).trimmed() : QString())
            << (item.hasArea ? QString("%1 %2").arg(formatValue(item.area), item.areaUnit).trimmed() : QString());
        for (int statusCount : item.statusCounts)
        {
            row << QString::number(statusCount);
        }
        table.push_back(row);
    }

    return table;
}

QString PDFMarkupsList::escapeCsvField(const QString& fieldText)
{
    // Spreadsheets evaluate a field starting with these characters as a formula.
    // The text comes from the document (comments, subjects), so it is prefixed
    // by an apostrophe, which makes it a plain text.
    QString text = fieldText;
    if (!text.isEmpty() && QStringLiteral("=+-@\t\r").contains(text.front()))
    {
        text.prepend(QChar('\''));
    }

    if (text.contains(QChar(',')) || text.contains(QChar(';')) || text.contains(QChar('\t')) || text.contains(QChar('"')) || text.contains(QChar('\n')) || text.contains(QChar('\r')) ||
        text.startsWith(QChar(' ')) || text.endsWith(QChar(' ')))
    {
        QString escapedText = text;
        escapedText.replace(QChar('"'), QString("\"\""));
        return QString("\"%1\"").arg(escapedText);
    }

    return text;
}

QString PDFMarkupsList::createCsv(const std::vector<QStringList>& table, QChar separator)
{
    QString csv;
    for (const QStringList& row : table)
    {
        QStringList fields;
        for (const QString& field : row)
        {
            fields << escapeCsvField(field);
        }
        csv += fields.join(separator);
        csv += QStringLiteral("\r\n");
    }
    return csv;
}

qreal PDFMarkupsListHelper::drawTable(QPdfWriter* writer,
                                      QPainter* painter,
                                      const std::vector<QStringList>& table,
                                      const std::vector<qreal>& columnWeights,
                                      qreal y,
                                      const QRectF& pageRect)
{
    if (table.empty())
    {
        return y;
    }

    const qreal totalWeight = std::accumulate(columnWeights.cbegin(), columnWeights.cend(), 0.0);
    std::vector<qreal> columnWidths;
    for (qreal weight : columnWeights)
    {
        columnWidths.push_back(pageRect.width() * weight / totalWeight);
    }

    QFont font = painter->font();
    const QFont normalFont = font;
    QFont boldFont = font;
    boldFont.setBold(true);

    const qreal padding = writer->resolution() * 0.03;
    const qreal lineHeight = QFontMetricsF(normalFont, writer).height();
    const int maximalLines = 4;

    auto getRowHeight = [&](const QStringList& row, const QFont& rowFont)
    {
        QFontMetricsF metrics(rowFont, writer);
        qreal height = lineHeight;
        for (int i = 0; i < row.size() && i < int(columnWidths.size()); ++i)
        {
            const QRectF bounds = metrics.boundingRect(QRectF(0, 0, columnWidths[i] - 2 * padding, lineHeight * maximalLines), Qt::TextWordWrap, row[i]);
            height = qMax(height, qMin(bounds.height(), lineHeight * maximalLines));
        }
        return height + 2 * padding;
    };

    auto drawRow = [&](const QStringList& row, bool isHeader, int rowIndex)
    {
        const QFont& rowFont = isHeader ? boldFont : normalFont;
        painter->setFont(rowFont);
        const qreal height = getRowHeight(row, rowFont);

        if (isHeader)
        {
            painter->fillRect(QRectF(pageRect.left(), y, pageRect.width(), height), QColor(220, 226, 235));
        }
        else if (rowIndex % 2 == 0)
        {
            painter->fillRect(QRectF(pageRect.left(), y, pageRect.width(), height), QColor(246, 247, 249));
        }

        qreal x = pageRect.left();
        for (int i = 0; i < int(columnWidths.size()); ++i)
        {
            const QRectF cell(x + padding, y + padding, columnWidths[i] - 2 * padding, height - 2 * padding);
            if (i < row.size())
            {
                painter->save();
                painter->setClipRect(cell);
                painter->drawText(cell, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, row[i]);
                painter->restore();
            }
            x += columnWidths[i];
        }

        painter->setPen(QPen(QColor(200, 200, 200), 0));
        painter->drawLine(QPointF(pageRect.left(), y + height), QPointF(pageRect.right(), y + height));
        painter->setPen(Qt::black);
        y += height;
    };

    drawRow(table.front(), true, 0);

    for (size_t i = 1; i < table.size(); ++i)
    {
        const qreal height = getRowHeight(table[i], normalFont);
        if (y + height > pageRect.bottom())
        {
            writer->newPage();
            y = pageRect.top();
            drawRow(table.front(), true, 0);
        }

        drawRow(table[i], false, int(i));
    }

    painter->setFont(normalFont);
    return y;
}

bool PDFMarkupsList::writePdfReport(const QString& fileName, const QString& title, const PDFMarkupsListItems& items, QString* errorMessage)
{
    // QPdfWriter doesn't report the reason, why the file can't be written
    {
        QFile file(fileName);
        if (!file.open(QFile::WriteOnly | QFile::Truncate))
        {
            if (errorMessage)
            {
                *errorMessage = PDFTranslationContext::tr("Can't write the file '%1': %2").arg(fileName, file.errorString());
            }
            return false;
        }
    }

    QPdfWriter writer(fileName);
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Landscape, QMarginsF(12.7, 12.7, 12.7, 12.7), QPageLayout::Millimeter));
    writer.setResolution(300);
    writer.setTitle(PDFTranslationContext::tr("Markups Summary - %1").arg(title));
    writer.setCreator(QStringLiteral("PDF4QT"));

    QPainter painter;
    if (!painter.begin(&writer))
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Can't write the file '%1'.").arg(fileName);
        }
        return false;
    }

    const QRectF pageRect(QPointF(0, 0), QSizeF(writer.width(), writer.height()));

    QFont titleFont(QStringLiteral("Helvetica"));
    titleFont.setPointSizeF(16);
    titleFont.setBold(true);
    QFont headingFont(QStringLiteral("Helvetica"));
    headingFont.setPointSizeF(12);
    headingFont.setBold(true);
    QFont textFont(QStringLiteral("Helvetica"));
    textFont.setPointSizeF(8);

    qreal y = pageRect.top();
    auto drawLine = [&](const QString& text, const QFont& font, qreal spaceAfter)
    {
        painter.setFont(font);
        const qreal height = QFontMetricsF(font, &writer).height();
        painter.drawText(QRectF(pageRect.left(), y, pageRect.width(), height), Qt::AlignLeft | Qt::AlignVCenter, text);
        y += height + spaceAfter;
    };

    const qreal gap = writer.resolution() * 0.12;
    drawLine(PDFTranslationContext::tr("Markups Summary"), titleFont, gap * 0.25);
    drawLine(title, textFont, 0);
    drawLine(PDFTranslationContext::tr("Created %1, %2 markups").arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat)).arg(items.size()), textFont, gap);

    // Takeoff summary
    drawLine(PDFTranslationContext::tr("Takeoff Summary"), headingFont, gap * 0.25);
    painter.setFont(textFont);
    y = PDFMarkupsListHelper::drawTable(&writer, &painter, createSummaryTable(summarize(items)), { 3.0, 1.2, 0.8, 1.5, 1.5, 0.8, 0.8, 0.8, 0.8, 0.8 }, y, pageRect);
    y += gap;

    // List of the markups
    std::vector<QStringList> table;
    QStringList header;
    for (Column column : { ColumnSheet, ColumnKind, ColumnSubject, ColumnContents, ColumnMeasurement, ColumnStatus, ColumnAuthor, ColumnDate, ColumnReplies })
    {
        header << getColumnTitle(column);
    }
    table.push_back(header);

    for (const PDFMarkupsListItem& item : items)
    {
        QStringList row;
        for (Column column : { ColumnSheet, ColumnKind, ColumnSubject, ColumnContents, ColumnMeasurement, ColumnStatus, ColumnAuthor, ColumnDate })
        {
            row << getCellText(item, column);
        }
        row << item.replies.join(QChar('\n'));
        table.push_back(row);
    }

    const qreal headingHeight = QFontMetricsF(headingFont, &writer).height() + QFontMetricsF(textFont, &writer).height() * 3;
    if (y + headingHeight > pageRect.bottom())
    {
        writer.newPage();
        y = pageRect.top();
    }
    drawLine(PDFTranslationContext::tr("Markups"), headingFont, gap * 0.25);
    painter.setFont(textFont);
    PDFMarkupsListHelper::drawTable(&writer, &painter, table, { 1.2, 1.0, 1.4, 3.0, 1.6, 0.8, 1.0, 1.2, 2.0 }, y, pageRect);

    if (!painter.end())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Can't write the file '%1'.").arg(fileName);
        }
        return false;
    }

    return true;
}

}   // namespace pdf
