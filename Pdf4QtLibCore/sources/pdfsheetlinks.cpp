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
#include "pdfocrpagepreparer.h"
#include "pdfrenderer.h"

#include <QHash>

#include <algorithm>

#include "pdfdbgheap.h"

namespace pdf
{

QRegularExpression PDFSheetLinks::getSheetNumberExpression()
{
    // Not after a letter, a digit or a dash/dot joined to a word (OFFICE-E101), and not
    // followed by a letter, a digit or a dash/dot with a digit (E-101-2, E-101.5.3)
    return QRegularExpression(QStringLiteral("(?<![A-Za-z0-9])(?<![A-Za-z0-9][-.])[A-Z]{1,3}[-.]?\\d{1,3}(?:\\.\\d{1,2})?[A-Z]?(?![A-Za-z0-9]|[-.]\\d)"));
}

bool PDFSheetLinks::isSheetNumber(const QString& text)
{
    const QRegularExpressionMatch match = getSheetNumberExpression().match(text.trimmed());
    return match.hasMatch() && match.capturedStart() == 0 && match.capturedLength() == text.trimmed().length();
}

QRectF PDFSheetLinks::getRectangle(const QPolygonF& quadrilaterals)
{
    return quadrilaterals.boundingRect();
}

PDFSheetLinks::Sheets PDFSheetLinks::findSheets(const PDFDocument* document, const PDFTextLayoutStorage& storage)
{
    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    Sheets sheets(pageCount);

    const QRegularExpression expression = getSheetNumberExpression();
    const PDFRedactSearch::Matches matches = PDFRedactSearch::find(storage, expression);

    for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        // Page label of a plan set is the sheet number. AutoCAD writes the index and
        // the name of the layout ("[1] Light LT-1"), the number is the last one.
        const QString label = PDFOCRPagePreparer::getPageLabel(document, pageIndex).simplified();
        QString labelNumber;
        QRegularExpressionMatchIterator iterator = expression.globalMatch(label);
        while (iterator.hasNext())
        {
            labelNumber = iterator.next().captured();
        }

        if (!labelNumber.isEmpty())
        {
            sheets[pageIndex].number = labelNumber;
            sheets[pageIndex].fromPageLabel = true;
        }
    }

    // Title block: the largest sheet number in the bottom right corner of the page, as
    // it is displayed (with the rotation of the page), alone on its line or after a label
    // ("SHEET NO."). References ("SEE SHEET C09.2"), fixture types ("TYPE S3") and other
    // texts with a sheet number are not the number of the sheet.
    const QRegularExpression labelExpression(QStringLiteral("^(?:SHEET|SHT\\.?|DWG\\.?|DRAWING)?\\s*(?:NO\\.?|NUMBER|#)?\\s*:?$"), QRegularExpression::CaseInsensitiveOption);
    std::vector<qreal> bestSizes(pageCount, 0.0);
    std::vector<qreal> bestCorners(pageCount, 0.0);
    for (const PDFRedactSearch::Match& match : matches)
    {
        const PDFInteger pageIndex = match.pageIndex;
        if (pageIndex < 0 || pageIndex >= pageCount || sheets[pageIndex].fromPageLabel)
        {
            continue;
        }

        const PDFPage* page = catalog->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        const QRectF rectangle = getRectangle(match.quadrilaterals);
        // Position on the displayed page, from (0, 0) top left to (1, 1) bottom right
        const QRectF displayedRectangle = page->getRotatedMediaBox();
        const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), displayedRectangle.size()));
        const QPointF devicePoint = matrix.map(rectangle.center());
        const QPointF center(devicePoint.x() / displayedRectangle.width(), devicePoint.y() / displayedRectangle.height());
        const bool isInCorner = center.x() <= 1.0 && center.y() <= 1.0 && ((center.x() > 0.6 && center.y() > 0.75) || (center.x() > 0.85 && center.y() > 0.6));
        if (!isInCorner)
        {
            continue;
        }

        QString restOfLine = match.context;
        restOfLine.remove(restOfLine.indexOf(match.matched.trimmed()), match.matched.trimmed().length());
        if (!labelExpression.match(restOfLine.simplified()).hasMatch())
        {
            continue;
        }

        const qreal size = qMin(rectangle.width(), rectangle.height());
        const qreal corner = center.x() + center.y();
        if (size > bestSizes[pageIndex] * 1.05 || (size > bestSizes[pageIndex] * 0.95 && corner > bestCorners[pageIndex]))
        {
            bestSizes[pageIndex] = qMax(size, bestSizes[pageIndex]);
            bestCorners[pageIndex] = corner;
            sheets[pageIndex].number = match.matched.trimmed();
            sheets[pageIndex].rectangle = rectangle;
        }
    }

    return sheets;
}

std::vector<QRectF> PDFSheetLinks::getForeignLinkRectangles(const PDFDocument* document, PDFInteger pageIndex)
{
    std::vector<QRectF> rectangles;

    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return rectangles;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotationReference));
        if (!dictionary || dictionary->hasKey(SHEET_LINK_KEY))
        {
            continue;
        }

        PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
        if (annotation && annotation->getType() == AnnotationType::Link)
        {
            rectangles.push_back(annotation->getRectangle().normalized());
        }
    }

    return rectangles;
}

PDFSheetLinks::Links PDFSheetLinks::findLinks(const PDFDocument* document, const PDFTextLayoutStorage& storage, const Sheets& sheets)
{
    Links links;

    // Sheet numbers, which are not unique, are not linked
    QHash<QString, PDFInteger> pageOfNumber;
    QHash<QString, int> countOfNumber;
    for (size_t pageIndex = 0; pageIndex < sheets.size(); ++pageIndex)
    {
        const QString& number = sheets[pageIndex].number;
        if (!number.isEmpty())
        {
            pageOfNumber[number] = PDFInteger(pageIndex);
            ++countOfNumber[number];
        }
    }

    QStringList numbers;
    for (auto it = countOfNumber.cbegin(); it != countOfNumber.cend(); ++it)
    {
        if (it.value() == 1)
        {
            numbers << QRegularExpression::escape(it.key());
        }
    }

    if (numbers.isEmpty())
    {
        return links;
    }

    // Longer numbers first, so E-101A is not matched as E-101
    std::sort(numbers.begin(), numbers.end(), [](const QString& l, const QString& r) { return l.length() > r.length(); });
    const QRegularExpression expression(QStringLiteral("(?<![A-Za-z0-9])(?<![A-Za-z0-9][-.])(?:%1)(?![A-Za-z0-9]|[-.]\\d)").arg(numbers.join(QChar('|'))));

    std::map<PDFInteger, std::vector<QRectF>> foreignLinks;
    for (const PDFRedactSearch::Match& match : PDFRedactSearch::find(storage, expression))
    {
        const QString number = match.matched.trimmed();
        const PDFInteger targetPageIndex = pageOfNumber.value(number, -1);
        if (targetPageIndex < 0 || targetPageIndex == match.pageIndex)
        {
            continue;
        }

        const QRectF rectangle = getRectangle(match.quadrilaterals);
        if (!rectangle.isValid())
        {
            continue;
        }

        auto it = foreignLinks.find(match.pageIndex);
        if (it == foreignLinks.end())
        {
            it = foreignLinks.emplace(match.pageIndex, getForeignLinkRectangles(document, match.pageIndex)).first;
        }

        // A place, which already has a link of another origin, keeps it
        const qreal area = rectangle.width() * rectangle.height();
        const bool isCovered = std::any_of(it->second.cbegin(), it->second.cend(), [&rectangle, area](const QRectF& linkRectangle)
        {
            const QRectF intersection = linkRectangle.intersected(rectangle);
            return intersection.isValid() && intersection.width() * intersection.height() > 0.5 * area;
        });
        if (isCovered)
        {
            continue;
        }

        Link link;
        link.pageIndex = match.pageIndex;
        link.targetPageIndex = targetPageIndex;
        link.text = number;
        link.rectangle = rectangle;
        links.push_back(std::move(link));
    }

    return links;
}

int PDFSheetLinks::createLinks(PDFDocumentBuilder* builder, const PDFDocument* document, const Links& links)
{
    const PDFCatalog* catalog = document->getCatalog();
    int count = 0;

    for (const Link& link : links)
    {
        const PDFPage* page = catalog->getPage(link.pageIndex);
        const PDFPage* targetPage = catalog->getPage(link.targetPageIndex);
        if (!page || !targetPage)
        {
            continue;
        }

        PDFObjectReference action = builder->createActionGoTo(PDFDestination::createFit(targetPage->getPageReference()));
        PDFObjectReference annotation = builder->createAnnotationLink(page->getPageReference(), link.rectangle, action, LinkHighlightMode::Invert);

        // No border (the default border is a black line), and the mark of the sheet links
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Border");
        factory << std::initializer_list<PDFInteger>{ 0, 0, 0 };
        factory.endDictionaryItem();
        factory.beginDictionaryItem(SHEET_LINK_KEY);
        factory << true;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
        ++count;
    }

    return count;
}

std::vector<std::pair<PDFObjectReference, PDFObjectReference>> PDFSheetLinks::getSheetLinkAnnotations(const PDFDocument* document)
{
    std::vector<std::pair<PDFObjectReference, PDFObjectReference>> annotations;

    const PDFCatalog* catalog = document->getCatalog();
    const PDFObjectStorage* storage = &document->getStorage();
    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotationReference));
            if (dictionary && dictionary->hasKey(SHEET_LINK_KEY))
            {
                annotations.emplace_back(page->getPageReference(), annotationReference);
            }
        }
    }

    return annotations;
}

int PDFSheetLinks::removeLinks(PDFDocumentBuilder* builder, const PDFDocument* document)
{
    const auto annotations = getSheetLinkAnnotations(document);
    for (const auto& item : annotations)
    {
        builder->removeAnnotation(item.first, item.second);
    }

    return int(annotations.size());
}

int PDFSheetLinks::getLinkCount(const PDFDocument* document)
{
    return int(getSheetLinkAnnotations(document).size());
}

}   // namespace pdf
