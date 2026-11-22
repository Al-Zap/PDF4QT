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

#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfitemmodels.h"
#include "pdfcms.h"

#include "pdfthumbnailslistview.h"

#include <QtTest>
#include <QMimeData>
#include <QItemSelectionModel>

#include <memory>
#include <vector>

using namespace pdf;

/// Drag and drop of the page thumbnails (the strip of the pages in the sidebar): every drop is passed
/// to the model, which requests the move of the pages. QListView handles a drop of its own items by
/// itself and never passed a drop next to one of the dragged pages to the model. The offscreen platform
/// can't run a real drag (QDrag::exec does nothing there), so the drag events are sent to the viewport.
class ThumbnailsDragDropTest : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
    }

private slots:
    void dragPages_data();
    void dragPages();

private:
    /// Where the dragged thumbnails are dropped
    enum class DropPlace
    {
        Center,     ///< Center of the thumbnail
        TopEdge,    ///< Top edge of the thumbnail
        BottomEdge, ///< Bottom edge of the thumbnail
        GapAbove,   ///< Space between the thumbnail and the thumbnail above it
        BelowAll    ///< Empty space below the last thumbnail
    };
};

Q_DECLARE_METATYPE(std::vector<PDFInteger>)

/// The thumbnails of a document of 6 pages are shown in a narrow view - one thumbnail in a line,
/// configured like the thumbnails of the sidebar and of the page organizing.
struct ThumbnailsFixture
{
    PDFDocument document;
    PDFCMSManager cms{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFThumbnailsItemModel model{ widget.getDrawWidgetProxy(), nullptr };
    std::unique_ptr<pdfviewer::PDFThumbnailsListView> view;

    /// Requests of the model to move the pages: pages and the insert position
    std::vector<std::pair<std::vector<PDFInteger>, PDFInteger>> moveRequests;

    explicit ThumbnailsFixture()
    {
        PDFDocumentBuilder builder;
        for (int i = 0; i < 6; ++i)
        {
            builder.appendPage(QRectF(0, 0, 200, 200));
        }
        document = builder.build();

        widget.resize(400, 400);
        widget.setDocument(PDFModifiedDocument(&document, nullptr), {});
        model.setThumbnailsSize(60);
        model.setExtraItemSizeHint(4, 20);
        model.setDocument(PDFModifiedDocument(&document, nullptr));
        model.setPageOrganizingEnabled(true);

        view.reset(new pdfviewer::PDFThumbnailsListView(nullptr));
        // The same order as in the form of the sidebar (the movement changes the drops of the viewport)
        view->setMovement(QListView::Static);
        view->setResizeMode(QListView::Adjust);
        view->setViewMode(QListView::IconMode);
        view->setSpacing(10);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setDragEnabled(true);
        view->setAcceptDrops(true);
        view->setDropIndicatorShown(true);
        view->setDragDropMode(QAbstractItemView::DragDrop);
        view->setDefaultDropAction(Qt::MoveAction);
        view->setModel(&model);
        view->resize(120, 900);
        view->show();

        QObject::connect(&model, &PDFThumbnailsItemModel::pagesMoveRequested, &model, [this](std::vector<PDFInteger> pages, PDFInteger insertPosition)
        {
            moveRequests.emplace_back(std::move(pages), insertPosition);
        });
    }

    ~ThumbnailsFixture()
    {
        view.reset();
        model.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
    }

    QRect rect(int row) const
    {
        return view->visualRect(model.index(row, 0, QModelIndex()));
    }

    /// Selects the pages and drags them over the viewport to the given point, where they are dropped.
    /// Returns false, if the view refuses the drag.
    bool drag(const std::vector<PDFInteger>& pages, QPoint dropPoint)
    {
        QWidget* viewport = view->viewport();
        QModelIndexList indexes;
        view->selectionModel()->clearSelection();
        for (const PDFInteger page : pages)
        {
            const QModelIndex index = model.index(int(page), 0, QModelIndex());
            view->selectionModel()->select(index, QItemSelectionModel::Select);
            indexes << index;
        }

        std::unique_ptr<QMimeData> mimeData(model.mimeData(indexes));
        const Qt::DropActions actions = Qt::MoveAction | Qt::CopyAction;

        // Qt 6.12 deprecates the constructors of the drag events taking QPoint, older versions don't have the others
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        using EventPoint = QPointF;
#else
        using EventPoint = QPoint;
#endif

        QDragEnterEvent enterEvent(EventPoint(rect(int(pages.front())).center()), actions, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &enterEvent);
        if (!enterEvent.isAccepted())
        {
            return false;
        }

        QDragMoveEvent moveEvent(EventPoint(dropPoint), actions, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &moveEvent);
        if (!moveEvent.isAccepted())
        {
            return false;
        }

        QDropEvent dropEvent(QPointF(dropPoint), actions, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
        dropEvent.setDropAction(Qt::MoveAction);
        QApplication::sendEvent(viewport, &dropEvent);

        // The document is changed by the receiver of the request, the view must not remove the rows
        return !dropEvent.isAccepted();
    }
};

void ThumbnailsDragDropTest::dragPages_data()
{
    QTest::addColumn<std::vector<PDFInteger>>("pages");
    QTest::addColumn<int>("target");
    QTest::addColumn<int>("place");
    QTest::addColumn<PDFInteger>("insertPosition");

    // Pages dropped next to the first dragged page (the other pages join it)
    QTest::newRow("next to the first dragged page") << std::vector<PDFInteger>{ 0, 2 } << 0 << int(DropPlace::BottomEdge) << PDFInteger(1);
    QTest::newRow("before the first dragged page") << std::vector<PDFInteger>{ 1, 4 } << 1 << int(DropPlace::TopEdge) << PDFInteger(1);
    QTest::newRow("after the last dragged page") << std::vector<PDFInteger>{ 1, 4 } << 4 << int(DropPlace::BottomEdge) << PDFInteger(5);
    QTest::newRow("onto the first dragged page") << std::vector<PDFInteger>{ 0, 2 } << 0 << int(DropPlace::Center) << PDFInteger(0);
    QTest::newRow("onto a dragged page in the middle") << std::vector<PDFInteger>{ 1, 3, 5 } << 3 << int(DropPlace::Center) << PDFInteger(3);
    QTest::newRow("gap next to the dragged page") << std::vector<PDFInteger>{ 2, 4 } << 3 << int(DropPlace::GapAbove) << PDFInteger(3);

    // Ordinary drops
    QTest::newRow("onto a page below") << std::vector<PDFInteger>{ 0 } << 3 << int(DropPlace::Center) << PDFInteger(4);
    QTest::newRow("onto a page above") << std::vector<PDFInteger>{ 4 } << 1 << int(DropPlace::Center) << PDFInteger(1);
    QTest::newRow("gap between pages") << std::vector<PDFInteger>{ 5 } << 2 << int(DropPlace::GapAbove) << PDFInteger(2);
    QTest::newRow("below all pages") << std::vector<PDFInteger>{ 1 } << 5 << int(DropPlace::BelowAll) << PDFInteger(6);
}

void ThumbnailsDragDropTest::dragPages()
{
    QFETCH(std::vector<PDFInteger>, pages);
    QFETCH(int, target);
    QFETCH(int, place);
    QFETCH(PDFInteger, insertPosition);

    ThumbnailsFixture fixture;
    QVERIFY(QTest::qWaitForWindowExposed(fixture.view.get()));

    // One thumbnail in a line, with the space between the lines
    const QRect targetRect = fixture.rect(target);
    QVERIFY(targetRect.isValid());
    QVERIFY(fixture.rect(1).top() > fixture.rect(0).bottom() + 4);
    QCOMPARE(fixture.rect(1).left(), fixture.rect(0).left());

    QPoint dropPoint = targetRect.center();
    switch (DropPlace(place))
    {
        case DropPlace::Center:
            break;
        case DropPlace::TopEdge:
            dropPoint.setY(targetRect.top() + 1);
            break;
        case DropPlace::BottomEdge:
            dropPoint.setY(targetRect.bottom() - 1);
            break;
        case DropPlace::GapAbove:
            dropPoint.setY((fixture.rect(target - 1).bottom() + targetRect.top()) / 2);
            QVERIFY(!fixture.view->indexAt(dropPoint).isValid());
            break;
        case DropPlace::BelowAll:
            dropPoint.setY(targetRect.bottom() + 30);
            QVERIFY(fixture.view->viewport()->rect().contains(dropPoint));
            QVERIFY(!fixture.view->indexAt(dropPoint).isValid());
            break;
    }

    QVERIFY(fixture.drag(pages, dropPoint));
    QCOMPARE(fixture.moveRequests.size(), size_t(1));
    QCOMPARE(fixture.moveRequests.front().first, pages);
    QCOMPARE(fixture.moveRequests.front().second, insertPosition);

    // The thumbnails are not removed by the view, the move is done by changing the document
    QCOMPARE(fixture.model.rowCount(QModelIndex()), 6);
}

QTEST_MAIN(ThumbnailsDragDropTest)

#include "tst_thumbnailsdragdroptest.moc"
