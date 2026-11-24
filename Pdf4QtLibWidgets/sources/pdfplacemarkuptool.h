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

#ifndef PDFPLACEMARKUPTOOL_H
#define PDFPLACEMARKUPTOOL_H

#include "pdfwidgetsglobal.h"
#include "pdfwidgettool.h"

#include <QImage>

#include <functional>

namespace pdf
{
class PDFDocumentBuilder;

/// Tool, which places a prepared markup (a custom stamp, a markup of the tool
/// chest) on the page by a click. A preview of the markup follows the mouse
/// cursor. The tool is deactivated after the markup is placed, unless Shift is
/// held - then more copies can be placed.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPlaceMarkupTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Creates the markup on the page. Parameters are the builder, the page, its index
    /// and the center of the markup in the page coordinates. Returns the created annotations.
    using Creator = std::function<std::vector<PDFObjectReference>(PDFDocumentBuilder*, PDFObjectReference, PDFInteger, QPointF)>;

    /// Creates the tool
    /// \param proxy Proxy
    /// \param toolManager Tool manager (the modified document is emitted by it)
    /// \param action Checkable action of the tool (can be nullptr)
    /// \param parent Parent
    explicit PDFPlaceMarkupTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    /// Sets the markup, which is placed by the tool
    /// \param name Name of the markup (displayed in the status bar)
    /// \param creator Function creating the markup
    /// \param preview Preview image
    /// \param size Size of the markup in the page coordinates (size of the preview)
    void setMarkup(const QString& name, Creator creator, QImage preview, QSizeF size);

    /// Places the markup on the page (as a click of the user does)
    /// \param pageIndex Page index
    /// \param pagePoint Center of the markup
    /// \returns Placed annotations
    std::vector<PDFObjectReference> placeMarkup(PDFInteger pageIndex, QPointF pagePoint);

    const QString& getName() const { return m_name; }

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

signals:
    void markupPlaced();

protected:
    virtual void setActiveImpl(bool active) override;
    virtual void updateActions() override;

private:
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    QString m_name;
    Creator m_creator;
    QImage m_preview;
    QSizeF m_size;
    PDFInteger m_pageIndex = -1;
    QPointF m_pagePoint;
};

}   // namespace pdf

#endif // PDFPLACEMARKUPTOOL_H
