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


#ifndef LIGHTINGSHAPETOOL_H
#define LIGHTINGSHAPETOOL_H

#include "pdfwidgettool.h"

#include <QPolygonF>

#include <functional>

namespace pdfplugin
{

/// Tool for picking a place or a shape on the sheet for the lighting tools:
/// a point (where a table is placed), an area (drag a box, or click the
/// corners of a polygon) or a polyline (wire run). Polygons and polylines are
/// finished by a double click, Enter or a right click, Backspace removes the
/// last point, Esc cancels. The tool does not modify the document, it reports
/// the picked shape.
class LightingShapeTool : public pdf::PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = pdf::PDFWidgetTool;

public:
    enum class Mode
    {
        Point,      ///< Single click
        Area,       ///< Dragged box or clicked polygon
        Polyline    ///< Clicked polyline
    };

    explicit LightingShapeTool(pdf::PDFDrawWidgetProxy* proxy, Mode mode, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    Mode getMode() const { return m_mode; }

    /// Sets the message displayed, when the tool is activated
    void setHelpMessage(const QString& message) { m_helpMessage = message; }

    /// Sets the function, which returns the text displayed next to the cursor
    /// for the shape being picked (live length of a run, count of the values
    /// in an area), the shape includes the point under the cursor
    void setLabelProvider(std::function<QString(pdf::PDFInteger, const QPolygonF&)> labelProvider) { m_labelProvider = std::move(labelProvider); }

    /// Sets the color of the shape
    void setColor(const QColor& color);

    /// Returns the points picked so far
    const QPolygonF& getPoints() const { return m_points; }

    /// Returns the page of the points picked so far
    pdf::PDFInteger getPageIndex() const { return m_pageIndex; }

    /// Finishes the polygon or the polyline picked so far
    void finishShape();

    /// Cancels the shape picked so far
    void cancelShape();

    /// Removes the last picked point
    void removeLastPoint();

signals:
    /// Shape was picked (one point, polygon of the area or polyline)
    void shapePicked(pdf::PDFInteger pageIndex, QPolygonF points);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void updateHover(const QPoint& widgetPoint);
    void addPoint(pdf::PDFInteger pageIndex, const QPointF& point);

    /// Returns the shape including the point under the cursor
    QPolygonF getLiveShape() const;

    /// Distance in pixels, after which the press becomes a drag
    static constexpr int DRAG_THRESHOLD = 4;

    Mode m_mode;
    QColor m_color;
    QString m_helpMessage;
    std::function<QString(pdf::PDFInteger, const QPolygonF&)> m_labelProvider;

    pdf::PDFInteger m_pageIndex;
    QPolygonF m_points;

    pdf::PDFInteger m_hoverPageIndex;
    QPointF m_hoverPoint;

    bool m_leftButtonPressed;
    bool m_dragging;
    QPoint m_pressPosition;
    pdf::PDFInteger m_pressPageIndex;
    QPointF m_pressPagePoint;
};

}   // namespace pdfplugin

#endif // LIGHTINGSHAPETOOL_H
