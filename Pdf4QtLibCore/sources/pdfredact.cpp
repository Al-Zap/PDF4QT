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
#include "pdfoptimizer.h"
#include "pdfpathboolean.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfpainterutils.h"
#include "pdftextlayoutgenerator.h"
#include "pdfexecutionpolicy.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"
#include "pdfimage.h"
#include "pdfdbgheap.h"

#include <QPainter>
#include <QPainterPathStroker>

#include <set>

namespace pdf
{

/// Redaction mark (redact annotation) of a page
struct PDFRedactMark
{
    QPainterPath path;  ///< Marked area in the page space
    QColor color;       ///< Fill color (invalid, if the area is not filled)
};

class PDFRedactHelper
{
public:
    PDFRedactHelper() = delete;

    /// Returns area of the filled path
    static PDFReal getPathArea(const QPainterPath& path)
    {
        PDFReal area = 0.0;

        for (const QPolygonF& polygon : path.toFillPolygons())
        {
            PDFReal polygonArea = 0.0;
            const qsizetype count = polygon.size();
            for (qsizetype i = 0; i < count; ++i)
            {
                const QPointF& p1 = polygon[i];
                const QPointF& p2 = polygon[(i + 1) % count];
                polygonArea += p1.x() * p2.y() - p2.x() * p1.y();
            }
            area += qAbs(polygonArea) * 0.5;
        }

        return area;
    }

    /// Converts the color of an annotation (gray, rgb or cmyk) to QColor. Empty
    /// color array (or array of invalid size) gives an invalid color.
    static QColor getAnnotationColor(const std::vector<PDFReal>& color)
    {
        switch (color.size())
        {
            case 1:
                return QColor::fromRgbF(color[0], color[0], color[0]);

            case 3:
                return QColor::fromRgbF(color[0], color[1], color[2]);

            case 4:
                return QColor::fromCmykF(color[0], color[1], color[2], color[3]).toRgb();

            default:
                break;
        }

        return QColor();
    }

    /// Returns the redaction marks of the page
    static std::vector<PDFRedactMark> getMarks(const PDFDocument* document, const PDFPage* page, QColor fillColorOverride, std::set<PDFObjectReference>* markReferences)
    {
        std::vector<PDFRedactMark> marks;

        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), annotationReference);
            if (!annotation || annotation->getType() != AnnotationType::Redact)
            {
                continue;
            }

            const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation.get());
            Q_ASSERT(redactAnnotation);

            if (markReferences)
            {
                markReferences->insert(annotationReference);
            }

            PDFRedactMark mark;
            mark.path = redactAnnotation->getRedactionRegion().getPath();
            mark.path.setFillRule(Qt::WindingFill);
            mark.color = fillColorOverride.isValid() ? fillColorOverride : getAnnotationColor(redactAnnotation->getInteriorColor());

            if (!mark.path.isEmpty())
            {
                marks.push_back(std::move(mark));
            }
        }

        return marks;
    }

    /// Removes the pixels of the image under the marks. Returns false,
    /// if the image is covered by the marks completely.
    /// \param image Image
    /// \param transform Transformation of the image (maps unit square to the page space)
    /// \param marks Marks
    static bool redactImage(QImage& image, const QTransform& transform, const std::vector<PDFRedactMark>& marks)
    {
        if (image.isNull())
        {
            return false;
        }

        // Pixel (x, y) lies in the unit square at (x / width, 1 - y / height),
        // because the first row of the image is the top row.
        const QTransform pixelToUnit(1.0 / image.width(), 0.0, 0.0, -1.0 / image.height(), 0.0, 1.0);
        const QTransform pixelToPage = pixelToUnit * transform;

        if (!pixelToPage.isInvertible())
        {
            return false;
        }

        const QTransform pageToPixel = pixelToPage.inverted();
        const QRectF imageRect(0, 0, image.width(), image.height());

        QPainterPath coveredArea;
        coveredArea.setFillRule(Qt::WindingFill);
        for (const PDFRedactMark& mark : marks)
        {
            coveredArea.addPath(pageToPixel.map(mark.path));
        }

        if (coveredArea.contains(imageRect))
        {
            return false;
        }

        image = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);

        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setRenderHint(QPainter::Antialiasing, false);

        for (const PDFRedactMark& mark : marks)
        {
            // Pixels, which are only partially covered by the mark, are removed too
            QColor color = mark.color.isValid() ? mark.color : QColor(Qt::black);
            color.setAlpha(255);

            const QPainterPath markPath = pageToPixel.map(mark.path);
            painter.setPen(QPen(color, 1.0));
            painter.setBrush(color);
            painter.drawPath(markPath);
        }

        painter.end();
        return true;
    }

    /// Returns true, if the image is encoded by a lossy filter (JPEG, JPEG 2000)
    static bool isLossyImage(const PDFDocument* document, const PDFObject& imageObject)
    {
        const PDFDictionary* dictionary = document->getDictionaryFromObject(imageObject);
        if (!dictionary)
        {
            return false;
        }

        const PDFObject filter = document->getObject(dictionary->get("Filter"));
        if (filter.isName())
        {
            return isLossyFilter(filter.getString());
        }

        if (filter.isArray())
        {
            for (const PDFObject& item : *filter.getArray())
            {
                const PDFObject filterName = document->getObject(item);
                if (filterName.isName() && isLossyFilter(filterName.getString()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    static bool isLossyFilter(const QByteArray& filter)
    {
        return filter == "DCTDecode" || filter == "DCT" || filter == "JPXDecode";
    }

    /// Returns true, if all pixels of the image are opaque
    static bool isOpaque(const QImage& image)
    {
        if (!image.hasAlphaChannel())
        {
            return true;
        }

        const QImage alpha = image.convertToFormat(QImage::Format_Alpha8);
        for (int y = 0; y < alpha.height(); ++y)
        {
            const uchar* line = alpha.constScanLine(y);
            for (int x = 0; x < alpha.width(); ++x)
            {
                if (line[x] != 255)
                {
                    return false;
                }
            }
        }

        return true;
    }

    /// Returns the outline of the stroked path (in the coordinate space of the path)
    static QPainterPath createStrokeOutline(const QPainterPath& path, const PDFPageContentProcessorState& state, const QTransform& transform)
    {
        QPen pen = PDFPainterHelper::createPenFromState(&state, 1.0);

        // Hairline (zero width line) is painted as the thinnest visible line, which
        // is about a quarter of a point wide.
        const PDFReal scale = qSqrt(qAbs(transform.determinant()));
        const PDFReal minimalWidth = scale > 0.0 ? 0.25 / scale : 0.25;
        if (pen.widthF() < minimalWidth)
        {
            const QList<qreal> dashPattern = pen.style() == Qt::CustomDashLine ? state.getLineDashPattern().createForQPen(minimalWidth) : QList<qreal>();
            pen.setWidthF(minimalWidth);

            if (!dashPattern.isEmpty())
            {
                pen.setDashPattern(dashPattern);
            }
        }

        QPainterPathStroker stroker(pen);
        QPainterPath outline = stroker.createStroke(path);
        outline.setFillRule(Qt::WindingFill);
        return outline;
    }

    /// Removes the widgets, which were removed from the pages, from the form field
    /// (and from its descendants). Returns true, if no widget of the field remains,
    /// so the field itself must be removed from its parent.
    static bool pruneFormField(PDFDocumentBuilder& builder,
                               PDFObjectReference fieldReference,
                               const std::set<PDFObjectReference>& removedWidgets,
                               std::set<PDFObjectReference>& visitedFields,
                               std::set<PDFObjectReference>& removedFields)
    {
        if (!visitedFields.insert(fieldReference).second)
        {
            return false;
        }

        if (removedWidgets.count(fieldReference))
        {
            // Widget (or a field merged with its only widget) was removed
            removedFields.insert(fieldReference);
            return true;
        }

        const PDFObject fieldObject = builder.getObjectByReference(fieldReference);
        const PDFDictionary* fieldDictionary = builder.getDictionaryFromObject(fieldObject);
        if (!fieldDictionary)
        {
            return false;
        }

        bool isRemoved = false;
        const PDFObject kidsObject = builder.getObject(fieldDictionary->get("Kids"));
        if (kidsObject.isArray() && kidsObject.getArray()->getCount() > 0)
        {
            std::vector<PDFObject> kids;
            bool isChanged = false;

            for (const PDFObject& kid : *kidsObject.getArray())
            {
                if (kid.isReference() && pruneFormField(builder, kid.getReference(), removedWidgets, visitedFields, removedFields))
                {
                    isChanged = true;
                    continue;
                }

                kids.push_back(kid);
            }

            if (kids.empty())
            {
                isRemoved = true;
            }
            else if (isChanged)
            {
                PDFObjectFactory factory;
                factory.beginDictionary();
                factory.beginDictionaryItem("Kids");
                factory << kids;
                factory.endDictionaryItem();
                factory.endDictionary();
                builder.mergeTo(fieldReference, factory.takeObject());
            }
        }

        if (isRemoved)
        {
            removedFields.insert(fieldReference);
        }

        return isRemoved;
    }

    /// Removes form fields, whose all widgets were removed
    static void pruneFormFields(PDFDocumentBuilder& builder, const std::set<PDFObjectReference>& removedWidgets)
    {
        if (removedWidgets.empty())
        {
            return;
        }

        const PDFObject catalogObject = builder.getObjectByReference(builder.getCatalogReference());
        const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(catalogObject);
        if (!catalogDictionary)
        {
            return;
        }

        const PDFObject formObject = catalogDictionary->get("AcroForm");
        const PDFDictionary* formDictionary = builder.getDictionaryFromObject(builder.getObject(formObject));
        if (!formDictionary)
        {
            return;
        }

        const PDFObject fieldsObject = builder.getObject(formDictionary->get("Fields"));
        if (!fieldsObject.isArray())
        {
            return;
        }

        std::set<PDFObjectReference> visitedFields;
        std::set<PDFObjectReference> removedFields;
        std::vector<PDFObject> fields;

        for (const PDFObject& field : *fieldsObject.getArray())
        {
            if (field.isReference() && pruneFormField(builder, field.getReference(), removedWidgets, visitedFields, removedFields))
            {
                continue;
            }

            fields.push_back(field);
        }

        if (removedFields.empty())
        {
            return;
        }

        PDFDictionaryBuilder updatedFormDictionary(*formDictionary);
        PDFArrayBuilder fieldArray;
        for (const PDFObject& field : fields)
        {
            fieldArray.appendItem(field);
        }
        updatedFormDictionary.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(fieldArray)));

        const PDFObject calculationOrderObject = builder.getObject(formDictionary->get("CO"));
        if (calculationOrderObject.isArray())
        {
            PDFArrayBuilder calculationOrder;
            for (const PDFObject& field : *calculationOrderObject.getArray())
            {
                if (!field.isReference() || !removedFields.count(field.getReference()))
                {
                    calculationOrder.appendItem(field);
                }
            }

            updatedFormDictionary.setEntry(PDFInplaceOrMemoryString("CO"), PDFObject::createArray(std::move(calculationOrder)));
        }

        PDFObject updatedForm = PDFObject::createDictionary(std::move(updatedFormDictionary));

        if (formObject.isReference())
        {
            builder.setObject(formObject.getReference(), std::move(updatedForm));
        }
        else
        {
            PDFObjectFactory catalogFactory;
            catalogFactory.beginDictionary();
            catalogFactory.beginDictionaryItem("AcroForm");
            catalogFactory << updatedForm;
            catalogFactory.endDictionaryItem();
            catalogFactory.endDictionary();
            builder.mergeTo(builder.getCatalogReference(), catalogFactory.takeObject());
        }
    }
};

/// Page content processor, which decomposes the page into the edited content
/// elements (see \p PDFPageContentEditorProcessor), but characters of the text
/// lying under the redaction marks are replaced by the advances of the same size,
/// so the remaining characters stay at their positions.
class PDFRedactContentProcessor : public PDFPageContentEditorProcessor
{
    using BaseClass = PDFPageContentEditorProcessor;

public:
    PDFRedactContentProcessor(const PDFPage* page,
                              const PDFDocument* document,
                              const PDFFontCache* fontCache,
                              const PDFICCProfileCache* iccProfileCache,
                              const PDFCMS* CMS,
                              const PDFOptionalContentActivity* optionalContentActivity,
                              const PDFMeshQualitySettings& meshQualitySettings,
                              QPainterPath region) :
        BaseClass(page, document, fontCache, iccProfileCache, CMS, optionalContentActivity, QTransform(), meshQualitySettings),
        m_region(std::move(region)),
        m_regionBoundingRect(m_region.boundingRect())
    {

    }

    int getRemovedCharacterCount() const { return m_removedCharacterCount; }

protected:
    virtual void performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order) override
    {
        if (order == ProcessOrder::BeforeOperation)
        {
            BaseClass::performProcessTextSequence(redactTextSequence(textSequence), order);
        }
        else
        {
            BaseClass::performProcessTextSequence(textSequence, order);
        }
    }

private:
    /// Returns true, if the character cell (polygon in the page space) is redacted
    bool isRedacted(const QPolygonF& cell) const
    {
        const QRectF cellBoundingRect = cell.boundingRect();
        if (!cellBoundingRect.intersects(m_regionBoundingRect) && !m_regionBoundingRect.contains(cellBoundingRect.center()))
        {
            return false;
        }

        if (m_region.contains(cellBoundingRect.center()))
        {
            return true;
        }

        QPainterPath cellPath;
        cellPath.addPolygon(cell);
        cellPath.closeSubpath();

        const PDFReal cellArea = PDFRedactHelper::getPathArea(cellPath);
        if (qFuzzyIsNull(cellArea))
        {
            return false;
        }

        const PDFReal coveredArea = PDFRedactHelper::getPathArea(cellPath.intersected(m_region));
        return coveredArea >= cellArea * PDFRedact::CHARACTER_COVERAGE_LIMIT;
    }

    TextSequence redactTextSequence(const TextSequence& textSequence)
    {
        const PDFPageContentProcessorState* state = getGraphicState();
        const PDFFontPointer& font = state->getTextFont();

        if (!font || textSequence.items.empty())
        {
            return textSequence;
        }

        const bool isType3Font = font->getFontType() == FontType::Type3;
        const bool isHorizontalWritingSystem = !(font->getCMap() && font->getCMap()->isVertical());
        const PDFReal fontSize = state->getTextFontSize();
        const PDFReal absoluteFontSize = qAbs(fontSize);
        const PDFReal horizontalScaling = state->getTextHorizontalScaling() * 0.01;
        const PDFReal characterSpacing = state->getTextCharacterSpacing();
        const PDFReal wordSpacing = state->getTextWordSpacing();
        const PDFReal textRise = state->getTextRise();
        const PDFReal fontFactor = (fontSize < 0.0) ? -1.0 : 1.0;

        const QTransform adjustMatrix(horizontalScaling * fontFactor, 0.0, 0.0, fontFactor, 0.0, textRise);
        const QTransform ctm = state->getCurrentTransformationMatrix();
        QTransform textMatrix = state->getTextMatrix();

        QTransform fontAdjustedMatrix;
        if (isType3Font)
        {
            const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.get());
            QTransform fontMatrix = type3Font ? type3Font->getFontMatrix() : QTransform(0.001, 0.0, 0.0, 0.001, 0.0, 0.0);
            fontAdjustedMatrix = fontMatrix * QTransform(fontSize, 0.0, 0.0, fontSize, 0.0, 0.0) * adjustMatrix;
        }

        // Advance item, which moves the text position by the given displacement
        auto createAdvanceItem = [&](PDFReal displacementX, PDFReal displacementY)
        {
            PDFReal advance = 0.0;
            if (isHorizontalWritingSystem || isType3Font)
            {
                const PDFReal factor = 0.001 * fontSize * horizontalScaling;
                if (!qFuzzyIsNull(factor))
                {
                    advance = -displacementX / factor;
                }
            }
            else
            {
                const PDFReal factor = 0.001 * fontSize;
                if (!qFuzzyIsNull(factor))
                {
                    advance = -displacementY / factor;
                }
            }
            return TextSequenceItem(advance);
        };

        TextSequence result;
        result.items.reserve(textSequence.items.size());

        for (const TextSequenceItem& item : textSequence.items)
        {
            PDFReal displacementX = 0.0;
            PDFReal displacementY = 0.0;
            bool isCharacter = false;
            QPolygonF cell;

            if (!isType3Font)
            {
                if (item.isCharacter())
                {
                    isCharacter = true;

                    QPointF advance = isHorizontalWritingSystem ? QPointF(item.advance, 0) : QPointF(0, item.advance);
                    const PDFReal additionalAdvance = (item.character == QChar(QChar::Space)) ? wordSpacing + characterSpacing : characterSpacing;
                    if (isHorizontalWritingSystem)
                    {
                        advance.rx() += additionalAdvance;
                    }
                    else
                    {
                        advance.ry() += additionalAdvance;
                    }
                    advance.rx() *= horizontalScaling * fontFactor;

                    displacementX = advance.x();
                    displacementY = advance.y();

                    // Cell of the character in the glyph space - from the descent
                    // to the ascent (estimated from the font size) over the advance,
                    // united with the bounding rectangle of the glyph outline.
                    QRectF glyphRect = isHorizontalWritingSystem ? QRectF(0.0, -0.25 * absoluteFontSize, item.advance, 1.15 * absoluteFontSize)
                                                                 : QRectF(-0.5 * absoluteFontSize, -item.advance, absoluteFontSize, item.advance);
                    glyphRect = glyphRect.normalized();
                    if (item.glyph && !item.glyph->isEmpty())
                    {
                        glyphRect = glyphRect.united(item.glyph->getBoundingRect());
                    }

                    cell = (adjustMatrix * textMatrix * ctm).map(QPolygonF(glyphRect));
                }
                else if (item.isAdvance())
                {
                    if (isHorizontalWritingSystem)
                    {
                        displacementX = -item.advance * 0.001 * fontSize * horizontalScaling;
                    }
                    else
                    {
                        displacementY = -item.advance * 0.001 * fontSize;
                    }
                }
            }
            else
            {
                if (!item.isContentStream())
                {
                    displacementX = -item.advance * 0.001 * fontSize * horizontalScaling;
                }
                else
                {
                    isCharacter = true;

                    if (item.advance != 0.0)
                    {
                        qreal ry = 0.0;
                        fontAdjustedMatrix.map(item.advance, 0.0, &displacementX, &ry);
                    }

                    const QRectF textRect = QRectF(0.0, textRise - 0.25 * absoluteFontSize, displacementX, 1.15 * absoluteFontSize).normalized();
                    cell = (textMatrix * ctm).map(QPolygonF(textRect));
                }
            }

            if (isCharacter && isRedacted(cell))
            {
                TextSequenceItem advanceItem = createAdvanceItem(displacementX, displacementY);
                if (!advanceItem.isNull())
                {
                    result.items.push_back(advanceItem);
                }
                ++m_removedCharacterCount;
            }
            else
            {
                result.items.push_back(item);
            }

            textMatrix.translate(displacementX, displacementY);
        }

        return result;
    }

    QPainterPath m_region;
    QRectF m_regionBoundingRect;
    int m_removedCharacterCount = 0;
};

PDFRedact::PDFRedact(const PDFDocument* document,
                     const PDFFontCache* fontCache,
                     const PDFICCProfileCache* iccProfileCache,
                     const PDFCMS* cms,
                     const PDFOptionalContentActivity* optionalContentActivity,
                     const PDFMeshQualitySettings* meshQualitySettings,
                     QColor redactFillColor) :
    m_document(document),
    m_fontCache(fontCache),
    m_iccProfileCache(iccProfileCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_redactFillColor(redactFillColor)
{

}

int PDFRedact::getRedactionMarkCount(const PDFDocument* document)
{
    int count = 0;

    if (!document)
    {
        return count;
    }

    const PDFCatalog* catalog = document->getCatalog();
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));
            if (dictionary && document->getObject(dictionary->get("Subtype")).isName() && document->getObject(dictionary->get("Subtype")).getString() == "Redact")
            {
                ++count;
            }
        }
    }

    return count;
}

PDFDocument PDFRedact::perform(Options options)
{
    m_statistics = Statistics();
    m_errors.clear();

    PDFDocumentBuilder builder(m_document);
    std::set<PDFObjectReference> removedWidgets;
    const PDFCatalog* catalog = m_document->getCatalog();

    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        const PDFObjectReference pageReference = page->getPageReference();

        std::set<PDFObjectReference> markReferences;
        std::vector<PDFRedactMark> marks = PDFRedactHelper::getMarks(m_document, page, m_redactFillColor, &markReferences);

        if (markReferences.empty())
        {
            continue;
        }

        m_statistics.markCount += int(markReferences.size());

        // Marks are removed, even if their area is empty
        for (const PDFObjectReference& markReference : markReferences)
        {
            builder.removeAnnotation(pageReference, markReference);
        }

        if (marks.empty())
        {
            continue;
        }

        ++m_statistics.pageCount;

        QPainterPath region;
        region.setFillRule(Qt::WindingFill);
        for (const PDFRedactMark& mark : marks)
        {
            region.addPath(mark.path);
        }
        region = PDFPathBoolean::unite(QPainterPath(), region);
        region.setFillRule(Qt::WindingFill);
        const QRectF regionBoundingRect = region.boundingRect();

        // 1) Decompose the page into the content elements, characters
        //    under the marks are removed during this step.
        PDFRedactContentProcessor processor(page, m_document, m_fontCache, m_iccProfileCache, m_cms, m_optionalContentActivity, *m_meshQualitySettings, region);
        QList<PDFRenderError> errors = processor.processContents();
        for (const PDFRenderError& error : errors)
        {
            if (error.type == RenderErrorType::Error)
            {
                m_errors << PDFTranslationContext::tr("Page %1: %2").arg(pageIndex + 1).arg(error.message);
            }
        }
        m_statistics.removedCharacterCount += processor.getRemovedCharacterCount();

        PDFEditedPageContent content = processor.takeEditedPageContent();

        // 2) Write the elements, the ones under the marks are cut or removed
        PDFPageContentEditorContentStreamBuilder contentStreamBuilder(const_cast<PDFDocument*>(m_document));
        contentStreamBuilder.setFontDictionary(content.getFontDictionary());
        contentStreamBuilder.setGraphicStateDictionary(content.getGraphicStateDictionary());
        contentStreamBuilder.setShadingDictionary(content.getShadingDictionary());

        // XObject dictionary starts empty - forms are decomposed into the elements,
        // so the original forms (with the redacted content) are no longer used.
        contentStreamBuilder.setXObjectDictionary(PDFDictionaryBuilder());

        for (size_t elementIndex = 0; elementIndex < content.getElementCount(); ++elementIndex)
        {
            PDFEditedPageContentElement* element = content.getElement(elementIndex);
            const QTransform transform = element->getTransform();

            if (PDFEditedPageContentElementPath* pathElement = element->asPath())
            {
                const QPainterPath path = pathElement->getPath();
                const PDFPageContentProcessorState& state = pathElement->getState();
                const bool isStroking = pathElement->getStrokePath();
                const bool isFilling = pathElement->getFillPath();

                const PDFReal scale = qSqrt(qAbs(transform.determinant()));
                const PDFReal strokeMargin = isStroking ? qMax(state.getLineWidth() * scale, 1.0) * qMax(state.getMitterLimit(), 1.0) : 0.0;
                const QRectF pathBoundingRect = transform.map(path).boundingRect().adjusted(-strokeMargin, -strokeMargin, strokeMargin, strokeMargin);

                if (!pathBoundingRect.intersects(regionBoundingRect) || !transform.isInvertible())
                {
                    contentStreamBuilder.writeEditedElement(element);
                    continue;
                }

                const QPainterPath regionInElementSpace = transform.inverted().map(region);

                QPainterPath strokeOutline;
                if (isStroking)
                {
                    strokeOutline = PDFRedactHelper::createStrokeOutline(path, state, transform);
                }

                const bool isFillRedacted = isFilling && path.intersects(regionInElementSpace);
                const bool isStrokeRedacted = isStroking && strokeOutline.intersects(regionInElementSpace);

                if (!isFillRedacted && !isStrokeRedacted)
                {
                    contentStreamBuilder.writeEditedElement(element);
                    continue;
                }

                bool isSomethingWritten = false;

                if (isFilling)
                {
                    QPainterPath filledPath = isFillRedacted ? PDFPathBoolean::subtract(path, regionInElementSpace) : path;
                    if (!filledPath.isEmpty())
                    {
                        std::unique_ptr<PDFEditedPageContentElementPath> fillElement(pathElement->clone());
                        fillElement->setPath(std::move(filledPath));
                        fillElement->setStrokePath(false);
                        fillElement->setFillPath(true);
                        contentStreamBuilder.writeEditedElement(fillElement.get());
                        isSomethingWritten = true;
                    }
                }

                if (isStroking)
                {
                    if (!isStrokeRedacted)
                    {
                        std::unique_ptr<PDFEditedPageContentElementPath> strokeElement(pathElement->clone());
                        strokeElement->setStrokePath(true);
                        strokeElement->setFillPath(false);
                        contentStreamBuilder.writeEditedElement(strokeElement.get());
                        isSomethingWritten = true;
                    }
                    else
                    {
                        // The remaining part of the stroke is filled by the stroke color
                        QPainterPath remainingStroke = PDFPathBoolean::subtract(strokeOutline, regionInElementSpace);
                        if (!remainingStroke.isEmpty())
                        {
                            PDFPageContentProcessorState strokeState = state;
                            strokeState.setFillColorSpace(state.getStrokeColorSpacePointer());
                            strokeState.setFillColor(state.getStrokeColor(), state.getStrokeColorOriginal());
                            strokeState.setAlphaFilling(state.getAlphaStroking());

                            std::unique_ptr<PDFEditedPageContentElementPath> strokeElement(pathElement->clone());
                            strokeElement->setState(strokeState);
                            strokeElement->setPath(std::move(remainingStroke));
                            strokeElement->setStrokePath(false);
                            strokeElement->setFillPath(true);
                            contentStreamBuilder.writeEditedElement(strokeElement.get());
                            isSomethingWritten = true;
                        }
                    }
                }

                if (isSomethingWritten)
                {
                    ++m_statistics.redactedPathCount;
                }
                else
                {
                    ++m_statistics.removedPathCount;
                }

                continue;
            }

            if (PDFEditedPageContentElementImage* imageElement = element->asImage())
            {
                const QRectF imageBoundingRect = transform.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
                QPainterPath imagePath;
                imagePath.addPolygon(transform.map(QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0))));
                imagePath.closeSubpath();

                if (!imageBoundingRect.intersects(regionBoundingRect) || !region.intersects(imagePath))
                {
                    contentStreamBuilder.writeEditedElement(element);
                    continue;
                }

                QImage image = imageElement->getImage();
                if (!PDFRedactHelper::redactImage(image, transform, marks))
                {
                    // Image is covered completely (or it can't be decoded), it is removed
                    ++m_statistics.removedImageCount;
                    continue;
                }

                if (PDFRedactHelper::isLossyImage(m_document, imageElement->getImageObject()) && PDFRedactHelper::isOpaque(image))
                {
                    // Image encoded by a lossy filter is written as JPEG, otherwise it would grow a lot
                    PDFImage::ImageEncodeOptions encodeOptions;
                    encodeOptions.compression = PDFImage::ImageCompression::JPEG;
                    encodeOptions.jpegQuality = 90;
                    imageElement->setImageObject(PDFObject::createStream(PDFImage::createStreamFromImage(image.convertToFormat(QImage::Format_RGB888), encodeOptions)));
                }
                else
                {
                    imageElement->setImageObject(PDFObject());
                    imageElement->setImage(image);
                }
                contentStreamBuilder.writeEditedElement(imageElement);
                ++m_statistics.redactedImageCount;
                continue;
            }

            if (PDFEditedPageContentElementShading* shadingElement = element->asShading())
            {
                if (!element->getBoundingBox().intersects(regionBoundingRect) || !transform.isInvertible())
                {
                    contentStreamBuilder.writeEditedElement(element);
                    continue;
                }

                const QPainterPath regionInElementSpace = transform.inverted().map(region);
                const QPainterPath clipPath = shadingElement->getClipPath().isEmpty() ? shadingElement->getArea() : shadingElement->getClipPath();
                if (!clipPath.intersects(regionInElementSpace))
                {
                    contentStreamBuilder.writeEditedElement(element);
                    continue;
                }

                QPainterPath newClipPath = PDFPathBoolean::subtract(clipPath, regionInElementSpace);
                if (newClipPath.isEmpty())
                {
                    ++m_statistics.removedPathCount;
                    continue;
                }

                shadingElement->setClipPath(newClipPath);
                contentStreamBuilder.writeEditedElement(shadingElement);
                ++m_statistics.redactedShadingCount;
                continue;
            }

            // Text - characters were already removed by the processor
            contentStreamBuilder.writeEditedElement(element);
        }

        // 3) Fill the marked areas
        for (const PDFRedactMark& mark : marks)
        {
            if (mark.color.isValid() && mark.color.alpha() > 0)
            {
                contentStreamBuilder.writeStyledPath(mark.path, Qt::NoPen, QBrush(mark.color), false, true);
            }
        }

        for (const QString& error : contentStreamBuilder.getErrors())
        {
            m_errors << PDFTranslationContext::tr("Page %1: %2").arg(pageIndex + 1).arg(error);
        }

        PDFDictionaryBuilder fontDictionary = contentStreamBuilder.getFontDictionary();
        PDFDictionaryBuilder xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
        PDFDictionaryBuilder graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();
        PDFDictionaryBuilder shadingDictionary = contentStreamBuilder.getShadingDictionary();

        builder.replaceObjectsByReferences(fontDictionary);
        builder.replaceObjectsByReferences(xobjectDictionary);
        builder.replaceObjectsByReferences(graphicStateDictionary);
        builder.replaceObjectsByReferences(shadingDictionary);

        QByteArray compressedData = PDFFlateDecodeFilter::compress(contentStreamBuilder.getOutputContent());
        PDFDictionaryBuilder contentDictionary;
        contentDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
        contentDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        PDFObject contentObject = PDFObject::createStream(PDFStream(std::move(contentDictionary), std::move(compressedData)));

        // Resources are created from the current resources (which can be inherited
        // from the page tree), the regenerated categories are replaced.
        PDFDictionaryBuilder resourcesDictionary;
        if (const PDFDictionary* currentResourcesDictionary = m_document->getDictionaryFromObject(page->getResources()))
        {
            resourcesDictionary = PDFDictionaryBuilder(*currentResourcesDictionary);
        }

        auto setResources = [&resourcesDictionary](const char* key, const PDFDictionaryBuilder& dictionary)
        {
            if (!dictionary.isEmpty())
            {
                resourcesDictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createDictionary(dictionary));
            }
            else
            {
                resourcesDictionary.removeEntry(key);
            }
        };

        setResources("Font", fontDictionary);
        setResources("XObject", xobjectDictionary);
        setResources("ExtGState", graphicStateDictionary);
        setResources("Shading", shadingDictionary);

        // Page dictionary is replaced (not merged), so the categories of the resources,
        // which were regenerated, don't keep the original entries.
        PDFDictionaryBuilder pageDictionary;
        if (const PDFDictionary* currentPageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference)))
        {
            pageDictionary = PDFDictionaryBuilder(*currentPageDictionary);
        }

        pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resourcesDictionary)));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(builder.addObject(std::move(contentObject))));

        // Thumbnail shows the original content, the page is no longer
        // part of the structure tree (its marked content was not kept)
        pageDictionary.removeEntry("Thumb");
        pageDictionary.removeEntry("StructParents");

        builder.setObject(pageReference, PDFObject::createDictionary(std::move(pageDictionary)));

        // 4) Remove annotations lying in the marked areas
        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            if (markReferences.count(annotationReference))
            {
                continue;
            }

            PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_document->getStorage(), annotationReference);
            if (!annotation || annotation->getType() == AnnotationType::Popup || annotation->getType() == AnnotationType::Redact)
            {
                continue;
            }

            const QRectF annotationRectangle = annotation->getRectangle();
            QPainterPath annotationPath;
            annotationPath.addRect(annotationRectangle);

            if (!annotationRectangle.intersects(regionBoundingRect) || !region.intersects(annotationPath))
            {
                continue;
            }

            if (annotation->getType() == AnnotationType::Widget)
            {
                removedWidgets.insert(annotationReference);
            }

            builder.removeAnnotation(pageReference, annotationReference);
            ++m_statistics.removedAnnotationCount;
        }
    }

    PDFRedactHelper::pruneFormFields(builder, removedWidgets);

    if (m_statistics.pageCount > 0)
    {
        // Structure tree refers to the marked content of the redacted pages, which
        // was not kept, and its elements can contain the redacted text (alternate
        // descriptions, actual text).
        builder.removeStructureTree();
    }

    // Document data
    if (!options.testFlag(CopyOutline))
    {
        builder.removeOutline();
    }

    if (!options.testFlag(CopyTitle) || !options.testFlag(CopyMetadata))
    {
        PDFDictionaryBuilder infoDictionary;
        if (const PDFDictionary* currentInfoDictionary = m_document->getDictionaryFromObject(m_document->getTrailerDictionary()->get("Info")))
        {
            if (options.testFlag(CopyMetadata))
            {
                infoDictionary = PDFDictionaryBuilder(*currentInfoDictionary);
                infoDictionary.removeEntry("Title");
            }
            else if (options.testFlag(CopyTitle) && currentInfoDictionary->hasKey("Title"))
            {
                infoDictionary.setEntry(PDFInplaceOrMemoryString("Title"), m_document->getObject(currentInfoDictionary->get("Title")));
            }
        }

        builder.setDocumentInfo(builder.addObject(PDFObject::createDictionary(std::move(infoDictionary))));
    }

    if (!options.testFlag(CopyMetadata))
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Metadata");
        factory << PDFObject();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.getCatalogReference(), factory.takeObject());
    }

    PDFDocument redactedDocument = builder.build();

    // Objects, which are no longer used (original content streams, forms,
    // images, removed annotations), are removed from the document.
    PDFOptimizer optimizer(PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects | PDFOptimizer::MergeIdenticalObjects), nullptr);
    optimizer.setDocument(&redactedDocument);
    optimizer.optimize();
    return optimizer.takeOptimizedDocument();
}

std::vector<PDFRedactSearch::Pattern> PDFRedactSearch::getPatterns()
{
    return { Pattern::SocialSecurityNumber, Pattern::PhoneNumber, Pattern::EmailAddress, Pattern::CreditCardNumber, Pattern::Date };
}

QString PDFRedactSearch::getPatternName(Pattern pattern)
{
    switch (pattern)
    {
        case Pattern::None:
            return PDFTranslationContext::tr("None");

        case Pattern::SocialSecurityNumber:
            return PDFTranslationContext::tr("Social security numbers");

        case Pattern::PhoneNumber:
            return PDFTranslationContext::tr("Phone numbers");

        case Pattern::EmailAddress:
            return PDFTranslationContext::tr("E-mail addresses");

        case Pattern::CreditCardNumber:
            return PDFTranslationContext::tr("Credit card numbers");

        case Pattern::Date:
            return PDFTranslationContext::tr("Dates");
    }

    return QString();
}

QString PDFRedactSearch::getPatternKey(Pattern pattern)
{
    switch (pattern)
    {
        case Pattern::None:
            return QString();

        case Pattern::SocialSecurityNumber:
            return QStringLiteral("ssn");

        case Pattern::PhoneNumber:
            return QStringLiteral("phone");

        case Pattern::EmailAddress:
            return QStringLiteral("email");

        case Pattern::CreditCardNumber:
            return QStringLiteral("credit-card");

        case Pattern::Date:
            return QStringLiteral("date");
    }

    return QString();
}

PDFRedactSearch::Pattern PDFRedactSearch::getPatternByKey(const QString& key)
{
    for (Pattern pattern : getPatterns())
    {
        if (getPatternKey(pattern).compare(key.trimmed(), Qt::CaseInsensitive) == 0)
        {
            return pattern;
        }
    }

    return Pattern::None;
}

QString PDFRedactSearch::getPatternExpression(Pattern pattern)
{
    switch (pattern)
    {
        case Pattern::None:
            return QString();

        case Pattern::SocialSecurityNumber:
            return QStringLiteral(R"((?<!\d)\d{3}[- ]\d{2}[- ]\d{4}(?!\d))");

        case Pattern::PhoneNumber:
            return QStringLiteral(R"((?<![\d\w])(?:\+?1[-. ]?)?(?:\(\d{3}\) ?|\d{3}[-. ])\d{3}[-. ]\d{4}(?!\d))");

        case Pattern::EmailAddress:
            return QStringLiteral(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})");

        case Pattern::CreditCardNumber:
            return QStringLiteral(R"((?<!\d)(?:\d{4}[- ]?\d{4}[- ]?\d{4}[- ]?\d{4}|\d{4}[- ]?\d{6}[- ]?\d{5})(?!\d))");

        case Pattern::Date:
            return QStringLiteral(R"((?<!\d)(?:\d{1,2}[/.\-]\d{1,2}[/.\-](?:\d{4}|\d{2})|\d{4}-\d{2}-\d{2})(?!\d)|\b(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)[a-z]*\.? \d{1,2}(?:st|nd|rd|th)?,? \d{4}\b)");
    }

    return QString();
}

QRegularExpression PDFRedactSearch::createExpression(const Settings& settings, QString* errorMessage)
{
    QString pattern;

    if (!settings.text.isEmpty())
    {
        pattern = settings.regularExpression ? settings.text : QRegularExpression::escape(settings.text);

        if (settings.wholeWords)
        {
            pattern = QString(R"((?<![\w])(?:%1)(?![\w]))").arg(pattern);
        }
    }
    else
    {
        pattern = getPatternExpression(settings.pattern);
    }

    if (pattern.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Enter a text to search for, or choose a pattern.");
        }
        return QRegularExpression();
    }

    QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
    if (!settings.caseSensitive && (!settings.text.isEmpty() || settings.pattern != Pattern::Date))
    {
        patternOptions |= QRegularExpression::CaseInsensitiveOption;
    }

    if (settings.text.isEmpty() && settings.pattern == Pattern::Date)
    {
        // Month names are matched in any case
        patternOptions |= QRegularExpression::CaseInsensitiveOption;
    }

    QRegularExpression expression(pattern, patternOptions);
    if (!expression.isValid())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Invalid regular expression: %1").arg(expression.errorString());
        }
        return QRegularExpression();
    }

    return expression;
}

bool PDFRedactSearch::isValidExpression(const QRegularExpression& expression)
{
    return expression.isValid() && !expression.pattern().isEmpty();
}

PDFRedactSearch::Matches PDFRedactSearch::find(const PDFTextLayoutStorage& storage, const QRegularExpression& expression)
{
    Matches matches;

    if (!isValidExpression(expression))
    {
        return matches;
    }

    PDFFindResults results = storage.find(expression, PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks | PDFTextFlow::RemoveSoftHyphen));

    PDFTextLayoutCache cache([&storage](PDFInteger pageIndex) { return storage.getTextLayout(pageIndex); });

    for (const PDFFindResult& result : results)
    {
        if (result.matched.trimmed().isEmpty())
        {
            continue;
        }

        PDFTextSelection selection;
        selection.addItems(result.textSelectionItems, Qt::black);
        selection.build();

        for (auto it = selection.begin(); it != selection.end(); it = selection.nextPageRange(it))
        {
            const PDFInteger pageIndex = it->start.pageIndex;

            PDFTextLayoutGetter getter(&cache, pageIndex);
            PDFTextSelectionPainter painter(&selection);

            Match match;
            match.pageIndex = pageIndex;
            match.matched = result.matched;
            match.context = result.context;
            painter.prepareGeometry(pageIndex, getter, QTransform(), &match.quadrilaterals);

            if (!match.quadrilaterals.isEmpty())
            {
                matches.push_back(std::move(match));
            }
        }
    }

    std::stable_sort(matches.begin(), matches.end(), [](const Match& l, const Match& r) { return l.pageIndex < r.pageIndex; });
    return matches;
}

PDFTextLayoutStorage PDFRedactSearch::createTextLayouts(const PDFDocument* document,
                                                        const PDFFontCache* fontCache,
                                                        const PDFICCProfileCache* iccProfileCache,
                                                        const PDFCMS* cms,
                                                        const PDFOptionalContentActivity* optionalContentActivity,
                                                        const PDFMeshQualitySettings& meshQualitySettings)
{
    const PDFCatalog* catalog = document->getCatalog();
    PDFTextLayoutStorage storage(catalog->getPageCount());

    auto generateTextLayout = [&](PDFInteger pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        if (!page)
        {
            storage.setTextLayout(pageIndex, PDFTextLayout());
            return;
        }

        PDFTextLayoutGenerator generator(PDFRenderer::None, page, document, fontCache, iccProfileCache, cms, optionalContentActivity, QTransform(), meshQualitySettings);
        generator.processContents();
        storage.setTextLayout(pageIndex, generator.createTextLayout());
    };

    auto pageRange = PDFIntegerRange<PDFInteger>(0, catalog->getPageCount());
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), generateTextLayout);
    return storage;
}

int PDFRedactSearch::createMarks(PDFDocumentBuilder* builder, const PDFDocument* document, const Matches& matches, QColor color)
{
    int count = 0;
    const PDFCatalog* catalog = document->getCatalog();

    for (const Match& match : matches)
    {
        const PDFPage* page = (match.pageIndex >= 0 && match.pageIndex < PDFInteger(catalog->getPageCount())) ? catalog->getPage(match.pageIndex) : nullptr;
        if (!page || match.quadrilaterals.isEmpty())
        {
            continue;
        }

        PDFObjectReference annotation = builder->createAnnotationRedact(page->getPageReference(), match.quadrilaterals, color, color);
        builder->updateAnnotationAppearanceStreams(annotation);
        ++count;
    }

    return count;
}

int PDFRedactSearch::createPageMarks(PDFDocumentBuilder* builder, const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, QColor color)
{
    int count = 0;
    const PDFCatalog* catalog = document->getCatalog();

    for (PDFInteger pageIndex : pageIndices)
    {
        const PDFPage* page = (pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount())) ? catalog->getPage(pageIndex) : nullptr;
        if (!page)
        {
            continue;
        }

        PDFObjectReference annotation = builder->createAnnotationRedact(page->getPageReference(), page->getMediaBox(), color, color);
        builder->updateAnnotationAppearanceStreams(annotation);
        ++count;
    }

    return count;
}

}   // namespace pdf
