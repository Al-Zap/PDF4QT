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

#include "pdfthumbnailslistview.h"

#include <QDropEvent>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFThumbnailsListView::PDFThumbnailsListView(QWidget* parent) :
    BaseClass(parent)
{

}

void PDFThumbnailsListView::dropEvent(QDropEvent* event)
{
    // The internal move of QListView is not used, the drop goes directly to
    // the model, which requests the move of the pages or the insertion of the files.
    // The document is changed by the receiver of the request, so the event is not
    // accepted - the view must not remove the dragged rows.
    event->ignore();

    if (QAbstractItemModel* itemModel = model())
    {
        const QPoint position = event->position().toPoint();
        const QModelIndex index = indexAt(position);

        if (index.isValid())
        {
            // Near the top or the bottom edge of the thumbnail the pages are placed
            // before or after it (as QAbstractItemView does). Dropped onto the thumbnail
            // (also onto a dragged one), the model decides, whether before or after it.
            const QRect rect = visualRect(index);
            const int margin = qBound(2, qRound(qreal(rect.height()) / 5.5), 12);
            if (position.y() - rect.top() < margin)
            {
                itemModel->dropMimeData(event->mimeData(), event->dropAction(), index.row(), 0, rootIndex());
            }
            else if (rect.bottom() - position.y() < margin)
            {
                itemModel->dropMimeData(event->mimeData(), event->dropAction(), index.row() + 1, 0, rootIndex());
            }
            else
            {
                itemModel->dropMimeData(event->mimeData(), event->dropAction(), -1, -1, index);
            }
        }
        else
        {
            itemModel->dropMimeData(event->mimeData(), event->dropAction(), getDropRowOutsideItems(position), 0, rootIndex());
        }
    }

    stopAutoScroll();
    setState(NoState);
    viewport()->update();
}

int PDFThumbnailsListView::getDropRowOutsideItems(const QPoint& position) const
{
    const QAbstractItemModel* itemModel = model();
    const int rowCount = itemModel->rowCount(rootIndex());

    // Items are ordered from left to right, top to bottom. The pages are
    // inserted before the first item, which lies below the position,
    // or to the right of it in the same line of the items.
    for (int row = 0; row < rowCount; ++row)
    {
        const QRect rect = visualRect(itemModel->index(row, 0, rootIndex()));
        if (rect.isEmpty())
        {
            continue;
        }

        const bool isBelow = rect.top() > position.y();
        const bool isInTheLine = rect.top() <= position.y() && rect.bottom() >= position.y();
        const bool isToTheRight = isRightToLeft() ? rect.right() < position.x() : rect.left() > position.x();
        if (isBelow || (isInTheLine && isToTheRight))
        {
            return row;
        }
    }

    return rowCount;
}

}   // namespace pdfviewer
