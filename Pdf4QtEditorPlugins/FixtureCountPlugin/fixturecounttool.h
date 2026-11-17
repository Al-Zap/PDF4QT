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

#ifndef FIXTURECOUNTTOOL_H
#define FIXTURECOUNTTOOL_H

#include "fixturecount.h"

#include "pdfwidgettool.h"

namespace pdfplugin
{

/// Tool for counting the fixtures. A click places a marker of the current
/// fixture type, Shift + click (or a right click) removes the marker under
/// the cursor, dragging pans the view, keys 1 - 9 select the fixture type.
/// Ctrl + drag selects a rectangle around a sample symbol, the symbols
/// similar to the sample are then searched on the sheet.
/// The tool itself does not modify the document, it only reports the requests.
class FixtureCountTool : public pdf::PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = pdf::PDFWidgetTool;

public:
    explicit FixtureCountTool(pdf::PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

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
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    /// Sets the type of the placed markers (it is used for the preview under the cursor)
    void setCurrentType(const FixtureType& type);

    /// Sets the size of the placed markers in the page coordinate system
    void setMarkerSize(pdf::PDFReal markerSize);

signals:
    /// Requests a new marker of the current type
    void markerRequested(pdf::PDFInteger pageIndex, QPointF pagePoint);

    /// Requests the removal of the marker under the point
    void markerRemoveRequested(pdf::PDFInteger pageIndex, QPointF pagePoint);

    /// Requests the type with the given index (0 for the key 1)
    void typeRequested(int index);

    /// Requests the search of the symbols similar to the sample symbol
    /// \param pageIndex Page index
    /// \param sampleRect Rectangle around the sample symbol in the page coordinate system
    void sampleSelected(pdf::PDFInteger pageIndex, QRectF sampleRect);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void updateHover(const QPoint& widgetPoint);

    /// Distance in pixels, after which the click becomes the panning of the view
    static constexpr int DRAG_THRESHOLD = 4;

    FixtureType m_currentType;
    pdf::PDFReal m_markerSize;

    pdf::PDFInteger m_hoverPageIndex;
    QPointF m_hoverPagePoint;

    bool m_leftButtonPressed;
    bool m_dragging;
    bool m_selectingSample;
    pdf::PDFInteger m_samplePageIndex;
    QPointF m_sampleStartPoint;
    QPointF m_sampleEndPoint;
    QPoint m_pressPosition;
    QPoint m_lastMousePosition;
};

}   // namespace pdfplugin

#endif // FIXTURECOUNTTOOL_H
