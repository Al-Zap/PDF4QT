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


#include "lightingshapetool.h"

#include "pdfdrawspacecontroller.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdfplugin
{

LightingShapeTool::LightingShapeTool(pdf::PDFDrawWidgetProxy* proxy, Mode mode, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_mode(mode),
    m_color(QColor(0, 102, 204)),
    m_pageIndex(-1),
    m_hoverPageIndex(-1),
    m_leftButtonPressed(false),
    m_dragging(false),
    m_pressPageIndex(-1)
{
    setCursor(QCursor(Qt::CrossCursor));
}

void LightingShapeTool::drawPage(QPainter* painter,
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

    const pdf::PDFInteger shapePageIndex = m_points.isEmpty() ? m_hoverPageIndex : m_pageIndex;
    if (pageIndex != shapePageIndex)
    {
        return;
    }

    const QPolygonF shape = getLiveShape();
    const QColor color = convertor.convert(m_color, false, false);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 1.5, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);

    if (shape.size() >= 2)
    {
        const QPolygonF devicePolygon = pagePointToDevicePointMatrix.map(shape);
        if (m_mode == Mode::Area)
        {
            QColor fillColor = color;
            fillColor.setAlphaF(0.12f);
            painter->setBrush(fillColor);
            painter->drawPolygon(devicePolygon);
        }
        else
        {
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(devicePolygon);
        }
    }

    // Picked points
    painter->setPen(QPen(color, 1.0));
    painter->setBrush(Qt::white);
    for (const QPointF& point : m_points)
    {
        const QPointF devicePoint = pagePointToDevicePointMatrix.map(point);
        painter->drawRect(QRectF(devicePoint - QPointF(3, 3), QSizeF(6, 6)));
    }

    // Crosshair of a placed point
    if (m_mode == Mode::Point && m_hoverPageIndex == pageIndex)
    {
        const QPointF devicePoint = pagePointToDevicePointMatrix.map(m_hoverPoint);
        painter->drawLine(devicePoint - QPointF(8, 0), devicePoint + QPointF(8, 0));
        painter->drawLine(devicePoint - QPointF(0, 8), devicePoint + QPointF(0, 8));
    }

    // Live readout next to the cursor
    if (m_labelProvider && m_hoverPageIndex == pageIndex && shape.size() >= 2)
    {
        const QString label = m_labelProvider(pageIndex, shape);
        if (!label.isEmpty())
        {
            const QPointF devicePoint = pagePointToDevicePointMatrix.map(m_hoverPoint) + QPointF(14, 18);
            const QFontMetricsF metrics(painter->font());
            QRectF textRect = metrics.boundingRect(label);
            textRect.moveTopLeft(devicePoint);
            textRect.adjust(-4, -2, 4, 2);
            painter->setPen(QPen(color, 1.0));
            painter->setBrush(QColor(255, 255, 255, 230));
            painter->drawRect(textRect);
            painter->setPen(Qt::black);
            painter->drawText(textRect, Qt::AlignCenter, label);
        }
    }

    painter->restore();
}

void LightingShapeTool::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    // Keys finishing or correcting the shape must not be used as shortcuts while picking
    const bool isPicking = !m_points.isEmpty();
    switch (event->key())
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Backspace:
        case Qt::Key_Escape:
            if (isPicking)
            {
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    event->ignore();
}

void LightingShapeTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (m_points.isEmpty())
    {
        event->ignore();
        return;
    }

    switch (event->key())
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            event->accept();
            finishShape();
            return;

        case Qt::Key_Backspace:
            event->accept();
            removeLastPoint();
            return;

        case Qt::Key_Escape:
            event->accept();
            cancelShape();
            return;

        default:
            break;
    }

    event->ignore();
}

void LightingShapeTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::RightButton && !m_points.isEmpty())
    {
        event->accept();
        finishShape();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        event->ignore();
        return;
    }

    event->accept();
    m_leftButtonPressed = true;
    m_dragging = false;
    m_pressPosition = event->pos();
    m_pressPageIndex = getProxy()->getPageUnderPoint(event->pos(), &m_pressPagePoint);
}

void LightingShapeTool::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton && m_mode != Mode::Point && !m_points.isEmpty())
    {
        // The first click of the double click has already added the point
        event->accept();
        m_leftButtonPressed = false;
        finishShape();
        return;
    }

    event->ignore();
}

void LightingShapeTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::RightButton)
    {
        event->setAccepted(!m_points.isEmpty());
        return;
    }

    if (event->button() != Qt::LeftButton || !m_leftButtonPressed)
    {
        event->ignore();
        return;
    }

    event->accept();
    m_leftButtonPressed = false;

    QPointF pagePoint;
    const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

    if (m_dragging)
    {
        m_dragging = false;

        // Dragged box of an area (only when no polygon is being clicked)
        if (m_mode == Mode::Area && m_points.isEmpty() && pageIndex == m_pressPageIndex && pageIndex != -1)
        {
            const QRectF rect = QRectF(m_pressPagePoint, pagePoint).normalized();
            if (!rect.isEmpty())
            {
                m_hoverPageIndex = -1;
                Q_EMIT getProxy()->repaintNeeded();
                Q_EMIT shapePicked(pageIndex, QPolygonF(rect).mid(0, 4));
            }
        }
        return;
    }

    if (pageIndex == -1)
    {
        return;
    }

    if (m_mode == Mode::Point)
    {
        Q_EMIT shapePicked(pageIndex, QPolygonF({ pagePoint }));
        return;
    }

    addPoint(pageIndex, pagePoint);
}

void LightingShapeTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (m_leftButtonPressed && !m_dragging && (event->pos() - m_pressPosition).manhattanLength() > DRAG_THRESHOLD)
    {
        m_dragging = true;
    }

    // A drag pans the view, except the box of an area
    if (m_dragging && !(m_mode == Mode::Area && m_points.isEmpty()))
    {
        const QPoint difference = event->pos() - m_pressPosition;
        getProxy()->scrollByPixels(difference);
        m_pressPosition = event->pos();
        Q_EMIT getProxy()->repaintNeeded();
        return;
    }

    updateHover(event->pos());
}

void LightingShapeTool::setColor(const QColor& color)
{
    m_color = color;
    Q_EMIT getProxy()->repaintNeeded();
}

void LightingShapeTool::finishShape()
{
    const int minimalPoints = m_mode == Mode::Area ? 3 : 2;

    // Double click adds the same point twice
    QPolygonF points;
    for (const QPointF& point : m_points)
    {
        if (points.isEmpty() || points.back() != point)
        {
            points << point;
        }
    }

    const pdf::PDFInteger pageIndex = m_pageIndex;
    if (points.size() < minimalPoints)
    {
        Q_EMIT messageDisplayRequest(m_mode == Mode::Area ? tr("Click at least three corners of the area, or drag a box.")
                                                          : tr("Click at least two points of the run."), 4000);
        return;
    }

    cancelShape();
    Q_EMIT shapePicked(pageIndex, points);
}

void LightingShapeTool::cancelShape()
{
    m_points.clear();
    m_pageIndex = -1;
    Q_EMIT getProxy()->repaintNeeded();
}

void LightingShapeTool::removeLastPoint()
{
    if (!m_points.isEmpty())
    {
        m_points.removeLast();
        if (m_points.isEmpty())
        {
            m_pageIndex = -1;
        }
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void LightingShapeTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    m_points.clear();
    m_pageIndex = -1;
    m_hoverPageIndex = -1;
    m_leftButtonPressed = false;
    m_dragging = false;

    if (active && !m_helpMessage.isEmpty())
    {
        Q_EMIT messageDisplayRequest(m_helpMessage, 10000);
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void LightingShapeTool::updateHover(const QPoint& widgetPoint)
{
    QPointF pagePoint;
    const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(widgetPoint, &pagePoint);

    if (pageIndex != m_hoverPageIndex || pagePoint != m_hoverPoint)
    {
        m_hoverPageIndex = pageIndex;
        m_hoverPoint = pagePoint;
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void LightingShapeTool::addPoint(pdf::PDFInteger pageIndex, const QPointF& point)
{
    // A shape lies on a single sheet, a click on another sheet starts a new one
    if (!m_points.isEmpty() && pageIndex != m_pageIndex)
    {
        m_points.clear();
    }

    m_pageIndex = pageIndex;
    m_points << point;
    Q_EMIT getProxy()->repaintNeeded();
}

QPolygonF LightingShapeTool::getLiveShape() const
{
    QPolygonF shape = m_points;

    // Box being dragged
    if (m_mode == Mode::Area && m_points.isEmpty() && m_dragging && m_hoverPageIndex == m_pressPageIndex && m_hoverPageIndex != -1)
    {
        return QPolygonF(QRectF(m_pressPagePoint, m_hoverPoint).normalized()).mid(0, 4);
    }

    if (!shape.isEmpty() && m_hoverPageIndex == m_pageIndex)
    {
        shape << m_hoverPoint;
    }

    return shape;
}

}   // namespace pdfplugin
