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

#ifndef PDFTHUMBNAILSLISTVIEW_H
#define PDFTHUMBNAILSLISTVIEW_H

#include "pdfviewerglobal.h"

#include <QListView>

namespace pdfviewer
{

/// List view of the page thumbnails. The pages are moved by the drop of the
/// thumbnails model (PDFThumbnailsItemModel::dropMimeData), which creates a new
/// version of the document. QListView handles drops of its own items itself:
/// it moves the dragged rows of the model row by row, and when the drop position
/// is next to one of the dragged rows, it takes the drop as finished ("move onto
/// itself") without passing it to the model; a drop onto a selected thumbnail is
/// refused, and a drop into the space between the thumbnails only moves the items
/// in the icon mode. Pages dragged together with a page, which was dropped next to
/// itself, were not moved at all. This view passes every drop to the model.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFThumbnailsListView : public QListView
{
    Q_OBJECT

private:
    using BaseClass = QListView;

public:
    explicit PDFThumbnailsListView(QWidget* parent);

protected:
    virtual void dropEvent(QDropEvent* event) override;

private:
    /// Returns the row, before which items dropped at the given position
    /// (outside of the items, in viewport coordinates) are inserted
    int getDropRowOutsideItems(const QPoint& position) const;
};

}   // namespace pdfviewer

#endif // PDFTHUMBNAILSLISTVIEW_H
