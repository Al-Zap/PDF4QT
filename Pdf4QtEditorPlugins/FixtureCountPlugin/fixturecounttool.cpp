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

#include "fixturecounttool.h"

#include "pdfdrawspacecontroller.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdfplugin
{

FixtureCountTool::FixtureCountTool(pdf::PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_markerSize(12.0),
    m_hoverPageIndex(-1),
    m_leftButtonPressed(false),
    m_dragging(false),
    m_selectingSample(false),
    m_rightButtonHandled(false),
    m_samplePageIndex(-1)
{
    setCursor(QCursor(Qt::CrossCursor));
}

void FixtureCountTool::drawPage(QPainter* painter,
                                pdf::PDFInteger pageIndex,
                                const pdf::PDFPrecompiledPage* compiledPage,
                                pdf::PDFTextLayoutGetter& layoutGetter,
                                const QTransform& pagePointToDevicePointMatrix,
                                const pdf::PDFColorConvertor& convertor,
                                QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (m_selectingSample)
    {
        if (pageIndex == m_samplePageIndex)
        {
            const QRectF sampleRect = QRectF(m_sampleStartPoint, m_sampleEndPoint).normalized();
            const QPolygonF polygon = pagePointToDevicePointMatrix.map(QPolygonF(sampleRect));

            painter->save();
            QPen pen(convertor.convert(QColor(Qt::blue), false, false), 1.0, Qt::DashLine);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(polygon);
            painter->restore();
        }
        return;
    }

    if (pageIndex != m_hoverPageIndex || !m_currentType.isValid() || m_dragging)
    {
        return;
    }

    const QRectF rect(m_hoverPagePoint.x() - m_markerSize * 0.5, m_hoverPagePoint.y() - m_markerSize * 0.5, m_markerSize, m_markerSize);
    const QPolygonF polygon = pagePointToDevicePointMatrix.map(FixtureType::getShapePolygon(m_currentType.shape, rect));

    QColor color = convertor.convert(m_currentType.color, false, false);
    QColor fillColor = color;
    fillColor.setAlphaF(static_cast<float>(FixtureCountEditor::FILL_OPACITY));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.0, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(fillColor);
    painter->drawPolygon(polygon);
    painter->restore();
}

void FixtureCountTool::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    // Keys 1 - 9 select the fixture type, so they must not be used as shortcuts
    if (event->modifiers() == Qt::NoModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9)
    {
        event->accept();
        return;
    }

    event->ignore();
}

void FixtureCountTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (event->modifiers() == Qt::NoModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9)
    {
        event->accept();
        Q_EMIT typeRequested(event->key() - Qt::Key_1);
        return;
    }

    event->ignore();
}

void FixtureCountTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    switch (event->button())
    {
        case Qt::LeftButton:
        {
            event->accept();
            m_leftButtonPressed = true;
            m_dragging = false;
            m_pressPosition = event->pos();
            m_lastMousePosition = event->pos();

            QPointF pagePoint;
            const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
            m_selectingSample = event->modifiers().testFlag(Qt::ControlModifier) && pageIndex != -1;
            if (m_selectingSample)
            {
                m_samplePageIndex = pageIndex;
                m_sampleStartPoint = pagePoint;
                m_sampleEndPoint = pagePoint;
            }
            break;
        }

        case Qt::RightButton:
        {
            QPointF pagePoint;
            const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
            m_rightButtonHandled = pageIndex != -1 && (!m_markerHitTest || m_markerHitTest(pageIndex, pagePoint));
            if (m_rightButtonHandled)
            {
                event->accept();
                Q_EMIT markerRemoveRequested(pageIndex, pagePoint);
            }
            else
            {
                event->ignore();
            }
            break;
        }

        default:
            // Middle button pans the view as usual
            event->ignore();
            break;
    }
}

void FixtureCountTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::RightButton)
    {
        event->setAccepted(m_rightButtonHandled);
        m_rightButtonHandled = false;
        return;
    }

    if (event->button() != Qt::LeftButton || !m_leftButtonPressed)
    {
        event->ignore();
        return;
    }

    event->accept();
    m_leftButtonPressed = false;

    if (m_selectingSample)
    {
        m_selectingSample = false;
        const QRectF sampleRect = QRectF(m_sampleStartPoint, m_sampleEndPoint).normalized();
        Q_EMIT getProxy()->repaintNeeded();

        if (m_dragging && !sampleRect.isEmpty())
        {
            m_dragging = false;
            Q_EMIT sampleSelected(m_samplePageIndex, sampleRect);
        }
        else
        {
            m_dragging = false;
            Q_EMIT messageDisplayRequest(tr("Hold Ctrl and drag a rectangle around one fixture symbol to find all similar symbols on the sheet."), 5000);
        }
        return;
    }

    if (m_dragging)
    {
        m_dragging = false;
        updateHover(event->pos());
        return;
    }

    QPointF pagePoint;
    const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (pageIndex == -1)
    {
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier))
    {
        Q_EMIT markerRemoveRequested(pageIndex, pagePoint);
    }
    else
    {
        Q_EMIT markerRequested(pageIndex, pagePoint);
    }
}

void FixtureCountTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (m_leftButtonPressed)
    {
        if (!m_dragging && (event->pos() - m_pressPosition).manhattanLength() > DRAG_THRESHOLD)
        {
            m_dragging = true;
        }

        if (m_dragging && m_selectingSample)
        {
            QPointF pagePoint;
            const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
            if (pageIndex == m_samplePageIndex)
            {
                m_sampleEndPoint = pagePoint;
                Q_EMIT getProxy()->repaintNeeded();
            }
            return;
        }

        if (m_dragging)
        {
            const QPoint difference = event->pos() - m_lastMousePosition;
            getProxy()->scrollByPixels(difference);
            m_lastMousePosition = event->pos();
            Q_EMIT getProxy()->repaintNeeded();
            return;
        }
    }

    updateHover(event->pos());
}

void FixtureCountTool::setCurrentType(const FixtureType& type)
{
    m_currentType = type;
    Q_EMIT getProxy()->repaintNeeded();
}

void FixtureCountTool::setMarkerHitTest(std::function<bool(pdf::PDFInteger, const QPointF&)> hitTest)
{
    m_markerHitTest = std::move(hitTest);
}

void FixtureCountTool::setMarkerSize(pdf::PDFReal markerSize)
{
    m_markerSize = markerSize;
    Q_EMIT getProxy()->repaintNeeded();
}

void FixtureCountTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    m_hoverPageIndex = -1;
    m_leftButtonPressed = false;
    m_dragging = false;
    m_selectingSample = false;

    if (active)
    {
        Q_EMIT messageDisplayRequest(tr("Click a fixture to count it. Shift + click or right click removes a marker, keys 1 - 9 select the type. Ctrl + drag around a symbol finds all similar symbols."), 10000);
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void FixtureCountTool::updateHover(const QPoint& widgetPoint)
{
    QPointF pagePoint;
    const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(widgetPoint, &pagePoint);

    if (pageIndex != m_hoverPageIndex || pagePoint != m_hoverPagePoint)
    {
        m_hoverPageIndex = pageIndex;
        m_hoverPagePoint = pagePoint;
        Q_EMIT getProxy()->repaintNeeded();
    }
}

}   // namespace pdfplugin
