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

#include "pdfplacemarkuptool.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentbuilder.h"
#include "pdfsecurityhandler.h"
#include "pdfcatalog.h"

#include <QAction>
#include <QPainter>
#include <QMouseEvent>
#include <QGuiApplication>

namespace pdf
{

PDFPlaceMarkupTool::PDFPlaceMarkupTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setSnapToAnnotations(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFPlaceMarkupTool::onPointPicked);

    updateActions();
}

void PDFPlaceMarkupTool::setMarkup(const QString& name, Creator creator, QImage preview, QSizeF size)
{
    m_name = name;
    m_creator = std::move(creator);
    m_preview = std::move(preview);
    m_size = size;
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFPlaceMarkupTool::drawPage(QPainter* painter,
                                  PDFInteger pageIndex,
                                  const PDFPrecompiledPage* compiledPage,
                                  PDFTextLayoutGetter& layoutGetter,
                                  const QTransform& pagePointToDevicePointMatrix,
                                  const PDFColorConvertor& convertor,
                                  QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pageIndex || !m_size.isValid())
    {
        return;
    }

    const QRectF pageRect(m_pagePoint.x() - m_size.width() * 0.5, m_pagePoint.y() - m_size.height() * 0.5, m_size.width(), m_size.height());
    const QRectF deviceRect = pagePointToDevicePointMatrix.mapRect(pageRect);

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setOpacity(0.7);
    if (!m_preview.isNull())
    {
        painter->drawImage(deviceRect, m_preview);
    }
    painter->setOpacity(1.0);
    QPen pen(QColor(0, 120, 215), 1.0, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(deviceRect);
    painter->restore();
}

void PDFPlaceMarkupTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(widget, event);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (pageIndex != m_pageIndex || pagePoint != m_pagePoint)
    {
        m_pageIndex = pageIndex;
        m_pagePoint = pagePoint;
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFPlaceMarkupTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_EMIT messageDisplayRequest(tr("Click on the page to place '%1'. Hold Shift to place more copies, press Esc to stop.").arg(m_name), 10000);
    }
    else
    {
        m_pageIndex = -1;
    }
}

void PDFPlaceMarkupTool::updateActions()
{
    BaseClass::updateActions();

    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setEnabled(isEnabled);
    }
}

std::vector<PDFObjectReference> PDFPlaceMarkupTool::placeMarkup(PDFInteger pageIndex, QPointF pagePoint)
{
    const PDFDocument* document = getDocument();
    if (!document || !m_creator || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return { };
    }

    PDFDocumentModifier modifier(document);
    const PDFObjectReference page = document->getCatalog()->getPage(pageIndex)->getPageReference();
    std::vector<PDFObjectReference> annotations = m_creator(modifier.getBuilder(), page, pageIndex, pagePoint);
    if (annotations.empty())
    {
        return annotations;
    }

    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    Q_EMIT markupPlaced();
    return annotations;
}

void PDFPlaceMarkupTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    placeMarkup(pageIndex, pagePoint);

    if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        setActive(false);
    }
}

}   // namespace pdf
