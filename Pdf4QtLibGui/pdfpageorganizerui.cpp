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

#include "pdfpageorganizerui.h"
#include "pdfpageorganizer.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfitemmodels.h"
#include "pdfoptionalcontent.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>

#include <algorithm>

#include "pdfdbgheap.h"

namespace pdfviewer
{

/// Shows the wait cursor during the lifetime of the object
class PDFPageOrganizerUIWaitCursor
{
public:
    PDFPageOrganizerUIWaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~PDFPageOrganizerUIWaitCursor() { QApplication::restoreOverrideCursor(); }

    PDFPageOrganizerUIWaitCursor(const PDFPageOrganizerUIWaitCursor&) = delete;
    PDFPageOrganizerUIWaitCursor& operator=(const PDFPageOrganizerUIWaitCursor&) = delete;
};

PDFPageOrganizerUI::PDFPageOrganizerUI(pdf::PDFDrawWidgetProxy* proxy,
                                       QListView* thumbnailsView,
                                       pdf::PDFThumbnailsItemModel* thumbnailsModel,
                                       QWidget* dialogParent) :
    QObject(dialogParent),
    m_proxy(proxy),
    m_thumbnailsView(thumbnailsView),
    m_thumbnailsModel(thumbnailsModel),
    m_dialogParent(dialogParent),
    m_lastDirectory(QDir::homePath())
{
    m_actions.resize(LastAction, nullptr);

    auto createAction = [this](Action action, const QString& iconName, const QString& text, const QString& toolTip, void (PDFPageOrganizerUI::*slot)())
    {
        QAction* qaction = new QAction(text, this);
        if (!iconName.isEmpty())
        {
            qaction->setIcon(QIcon(iconName));
        }
        qaction->setToolTip(toolTip);
        connect(qaction, &QAction::triggered, this, slot);
        m_actions[action] = qaction;
    };

    createAction(RotateLeft, ":/resources/rotate-left.svg", tr("Rotate Pages &Left"), tr("Rotate the selected pages 90° counterclockwise"), &PDFPageOrganizerUI::onRotateLeft);
    createAction(RotateRight, ":/resources/rotate-right.svg", tr("Rotate Pages &Right"), tr("Rotate the selected pages 90° clockwise"), &PDFPageOrganizerUI::onRotateRight);
    createAction(Delete, ":/resources/page-delete.svg", tr("&Delete Pages"), tr("Delete the selected pages"), &PDFPageOrganizerUI::onDelete);
    createAction(InsertFromFile, ":/resources/open.svg", tr("&Insert Pages from File..."), tr("Insert pages of other PDF files after the selected pages"), &PDFPageOrganizerUI::onInsertFromFile);
    createAction(InsertBlankPage, ":/resources/page.svg", tr("Insert &Blank Page"), tr("Insert a blank page after the selected pages"), &PDFPageOrganizerUI::onInsertBlankPage);
    createAction(Extract, ":/resources/save-as.svg", tr("&Extract Pages..."), tr("Save the selected pages as a new PDF file"), &PDFPageOrganizerUI::onExtract);
    createAction(Split, ":/resources/page-split.svg", tr("&Split Document..."), tr("Split the document into several PDF files"), &PDFPageOrganizerUI::onSplit);

    m_actions[RotateLeft]->setObjectName("actionPagesRotateLeft");
    m_actions[RotateRight]->setObjectName("actionPagesRotateRight");
    m_actions[Delete]->setObjectName("actionPagesDelete");
    m_actions[InsertFromFile]->setObjectName("actionPagesInsertFromFile");
    m_actions[InsertBlankPage]->setObjectName("actionPagesInsertBlankPage");
    m_actions[Extract]->setObjectName("actionPagesExtract");
    m_actions[Split]->setObjectName("actionPagesSplit");

    // Delete key deletes the selected thumbnails only, when thumbnails have focus,
    // otherwise it would collide with the deletion of the annotations.
    m_actions[Delete]->setShortcut(QKeySequence::Delete);
    m_actions[Delete]->setShortcutContext(Qt::WidgetShortcut);
    m_thumbnailsView->addAction(m_actions[Delete]);

    m_thumbnailsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_thumbnailsView->setDragEnabled(true);
    m_thumbnailsView->setAcceptDrops(true);
    m_thumbnailsView->setDropIndicatorShown(true);
    m_thumbnailsView->setDragDropMode(QAbstractItemView::DragDrop);
    m_thumbnailsView->setDefaultDropAction(Qt::MoveAction);
    m_thumbnailsView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_thumbnailsView, &QListView::customContextMenuRequested, this, &PDFPageOrganizerUI::onThumbnailsContextMenuRequested);

    // Requests from the drag and drop are processed after the drop event is finished,
    // because the document change resets the model of the view handling the drop.
    connect(m_thumbnailsModel, &pdf::PDFThumbnailsItemModel::pagesMoveRequested, this, [this](std::vector<pdf::PDFInteger> pageIndices, pdf::PDFInteger insertPosition)
    {
        QTimer::singleShot(0, this, [this, pageIndices, insertPosition]() { movePages(pageIndices, insertPosition); });
    });
    connect(m_thumbnailsModel, &pdf::PDFThumbnailsItemModel::filesInsertRequested, this, [this](QStringList fileNames, pdf::PDFInteger insertPosition)
    {
        QTimer::singleShot(0, this, [this, fileNames, insertPosition]() { insertFiles(fileNames, insertPosition); });
    });

    // Selection of the thumbnails is used by the actions only, if the user worked
    // with the thumbnails last. When the user continues in the document itself,
    // the actions work with the page displayed in the document.
    connect(qApp, &QApplication::focusChanged, this, &PDFPageOrganizerUI::onFocusChanged);

    updateActions();
}

void PDFPageOrganizerUI::setDocument(const pdf::PDFModifiedDocument& document)
{
    m_document = document;
    m_optionalContentActivity = document.getOptionalContentActivity();
    m_thumbnailsModel->setPageOrganizingEnabled(pdf::PDFPageOrganizer::canOrganizePages(m_document));

    if (!m_pendingSelection.empty())
    {
        selectPages(m_pendingSelection);
        m_pendingSelection.clear();
    }

    updateActions();
}

std::vector<QAction*> PDFPageOrganizerUI::getActions() const
{
    return m_actions;
}

std::vector<pdf::PDFInteger> PDFPageOrganizerUI::getSelectedPages() const
{
    std::vector<pdf::PDFInteger> pages;

    if (!m_document)
    {
        return pages;
    }

    // Selection of the thumbnails is used only, when the user can see it and works with it
    if (m_thumbnailsActive && m_thumbnailsView->isVisible() && m_thumbnailsView->selectionModel())
    {
        for (const QModelIndex& index : m_thumbnailsView->selectionModel()->selectedIndexes())
        {
            pages.push_back(m_thumbnailsModel->getPageIndex(index));
        }
    }

    if (pages.empty())
    {
        std::vector<pdf::PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();
        if (!currentPages.empty())
        {
            pages.push_back(currentPages.front());
        }
    }

    return pdf::PDFPageOrganizer::getValidPageIndices(pages, pdf::PDFInteger(m_document->getCatalog()->getPageCount()));
}

void PDFPageOrganizerUI::movePages(std::vector<pdf::PDFInteger> pageIndices, pdf::PDFInteger insertPosition)
{
    if (!m_document || !pdf::PDFPageOrganizer::canOrganizePages(m_document))
    {
        return;
    }

    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
    pageIndices = pdf::PDFPageOrganizer::getValidPageIndices(pageIndices, pageCount);
    if (pageIndices.empty())
    {
        return;
    }

    PDFPageOrganizerUIWaitCursor waitCursor;
    pdf::PDFDocument document = *m_document;
    if (!checkResult(pdf::PDFPageOrganizer::movePages(&document, pageIndices, insertPosition)))
    {
        return;
    }

    // Moved pages are selected at their new position
    insertPosition = qBound(pdf::PDFInteger(0), insertPosition, pageCount);
    const pdf::PDFInteger movedBefore = std::count_if(pageIndices.cbegin(), pageIndices.cend(), [insertPosition](pdf::PDFInteger page) { return page < insertPosition; });
    const pdf::PDFInteger firstPage = insertPosition - movedBefore;

    std::vector<pdf::PDFInteger> selection;
    for (size_t i = 0; i < pageIndices.size(); ++i)
    {
        selection.push_back(firstPage + pdf::PDFInteger(i));
    }

    applyDocument(std::move(document), std::move(selection));
}

void PDFPageOrganizerUI::insertFiles(const QStringList& fileNames, pdf::PDFInteger insertPosition)
{
    if (!m_document || fileNames.isEmpty() || !pdf::PDFPageOrganizer::canOrganizePages(m_document))
    {
        return;
    }

    pdf::PDFDocument document = *m_document;
    insertPosition = qBound(pdf::PDFInteger(0), insertPosition, pdf::PDFInteger(document.getCatalog()->getPageCount()));
    const pdf::PDFInteger firstInsertedPage = insertPosition;

    for (const QString& fileName : fileNames)
    {
        // File, which can't be inserted, is reported and skipped, other files are inserted
        pdf::PDFDocument insertedDocument;
        if (!readDocument(fileName, &insertedDocument))
        {
            continue;
        }

        PDFPageOrganizerUIWaitCursor waitCursor;
        pdf::PDFDocument updatedDocument = document;
        if (!checkResult(pdf::PDFPageOrganizer::insertDocument(&updatedDocument, &insertedDocument, insertPosition)))
        {
            continue;
        }
        document = std::move(updatedDocument);
        insertPosition += pdf::PDFInteger(insertedDocument.getCatalog()->getPageCount());
        m_lastDirectory = QFileInfo(fileName).absolutePath();
    }

    if (insertPosition == firstInsertedPage)
    {
        return;
    }

    std::vector<pdf::PDFInteger> selection;
    for (pdf::PDFInteger i = firstInsertedPage; i < insertPosition; ++i)
    {
        selection.push_back(i);
    }

    applyDocument(std::move(document), std::move(selection), firstInsertedPage);
}

void PDFPageOrganizerUI::onFocusChanged(QWidget* old, QWidget* now)
{
    Q_UNUSED(old);

    if (!now)
    {
        return;
    }

    // Other widgets (menus, toolbars, dialogs) do not change, what the user works with
    QWidget* documentWidget = m_proxy->getWidget();
    if (now == m_thumbnailsView || m_thumbnailsView->isAncestorOf(now))
    {
        m_thumbnailsActive = true;
    }
    else if (documentWidget && (now == documentWidget || documentWidget->isAncestorOf(now)))
    {
        m_thumbnailsActive = false;
    }
}

QString PDFPageOrganizerUI::getPagesText(const std::vector<pdf::PDFInteger>& pages)
{
    // Pages are sorted, ranges of consecutive pages are joined
    QStringList ranges;
    size_t i = 0;
    while (i < pages.size())
    {
        size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1)
        {
            ++j;
        }

        ranges << ((i == j) ? QString::number(pages[i] + 1) : QString("%1-%2").arg(pages[i] + 1).arg(pages[j] + 1));
        i = j + 1;
    }

    const int maxRanges = 6;
    if (ranges.size() > maxRanges)
    {
        ranges = ranges.mid(0, maxRanges);
        ranges << QString("...");
    }

    return ranges.join(", ");
}

void PDFPageOrganizerUI::updateActions()
{
    const bool enabled = m_document && pdf::PDFPageOrganizer::canOrganizePages(m_document);
    for (QAction* action : m_actions)
    {
        action->setEnabled(enabled);
    }
}

void PDFPageOrganizerUI::onThumbnailsContextMenuRequested(const QPoint& pos)
{
    if (!m_document)
    {
        return;
    }

    // Right click on the thumbnail, which is not selected, selects it
    const QModelIndex index = m_thumbnailsView->indexAt(pos);
    if (index.isValid() && !m_thumbnailsView->selectionModel()->isSelected(index))
    {
        m_thumbnailsView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        m_thumbnailsView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }

    m_thumbnailsActive = true;

    QMenu menu(m_thumbnailsView);
    menu.addAction(m_actions[RotateLeft]);
    menu.addAction(m_actions[RotateRight]);
    menu.addSeparator();
    menu.addAction(m_actions[InsertFromFile]);
    menu.addAction(m_actions[InsertBlankPage]);
    menu.addSeparator();
    menu.addAction(m_actions[Extract]);
    menu.addAction(m_actions[Split]);
    menu.addSeparator();
    menu.addAction(m_actions[Delete]);
    menu.exec(m_thumbnailsView->viewport()->mapToGlobal(pos));
}

void PDFPageOrganizerUI::onRotateLeft()
{
    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    pdf::PDFDocument document = *m_document;
    if (checkResult(pdf::PDFPageOrganizer::rotatePages(&document, pages, false)))
    {
        applyDocument(std::move(document), pages);
    }
}

void PDFPageOrganizerUI::onRotateRight()
{
    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    pdf::PDFDocument document = *m_document;
    if (checkResult(pdf::PDFPageOrganizer::rotatePages(&document, pages, true)))
    {
        applyDocument(std::move(document), pages);
    }
}

void PDFPageOrganizerUI::onDelete()
{
    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    if (pages.empty())
    {
        return;
    }

    const pdf::PDFInteger documentPageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
    if (pdf::PDFInteger(pages.size()) >= documentPageCount)
    {
        QMessageBox::information(m_dialogParent, tr("Delete Pages"), tr("All pages of the document can't be deleted."));
        return;
    }

    const QString question = (pages.size() == 1) ? tr("Do you want to delete page %1?").arg(pages.front() + 1)
                                                 : tr("Do you want to delete %1 pages (%2)?").arg(pages.size()).arg(getPagesText(pages));
    if (QMessageBox::question(m_dialogParent, tr("Delete Pages"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
    {
        return;
    }

    PDFPageOrganizerUIWaitCursor waitCursor;
    pdf::PDFDocument document = *m_document;
    if (checkResult(pdf::PDFPageOrganizer::deletePages(&document, pages)))
    {
        // Page following the deleted pages is selected
        const pdf::PDFInteger pageCount = pdf::PDFInteger(document.getCatalog()->getPageCount());
        const pdf::PDFInteger selectedPage = qMin(pages.front(), pageCount - 1);
        applyDocument(std::move(document), { selectedPage });
    }
}

void PDFPageOrganizerUI::onInsertFromFile()
{
    const QStringList fileNames = QFileDialog::getOpenFileNames(m_dialogParent, tr("Insert Pages from File"), getDirectory(), tr("PDF document (*.pdf)"));
    if (fileNames.isEmpty())
    {
        return;
    }

    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    const pdf::PDFInteger insertPosition = pages.empty() ? pdf::PDFInteger(m_document->getCatalog()->getPageCount()) : pages.back() + 1;
    insertFiles(fileNames, insertPosition);
}

void PDFPageOrganizerUI::onInsertBlankPage()
{
    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    const pdf::PDFInteger insertPosition = pages.empty() ? pdf::PDFInteger(m_document->getCatalog()->getPageCount()) : pages.back() + 1;

    pdf::PDFDocument document = *m_document;
    if (checkResult(pdf::PDFPageOrganizer::insertBlankPages(&document, insertPosition, 1)))
    {
        applyDocument(std::move(document), { insertPosition }, insertPosition);
    }
}

void PDFPageOrganizerUI::onExtract()
{
    const std::vector<pdf::PDFInteger> pages = getSelectedPages();
    if (pages.empty())
    {
        return;
    }

    QString suggestedName = (pages.size() == 1) ? tr("Page %1.pdf").arg(pages.front() + 1)
                                                : tr("Pages %1.pdf").arg(getPagesText(pages));
    QString fileName = QFileDialog::getSaveFileName(m_dialogParent, tr("Extract Pages"), QDir(getDirectory()).filePath(suggestedName), tr("PDF document (*.pdf)"));
    if (fileName.isEmpty())
    {
        return;
    }
    m_lastDirectory = QFileInfo(fileName).absolutePath();

    PDFPageOrganizerUIWaitCursor waitCursor;
    pdf::PDFDocument extractedDocument;
    if (checkResult(pdf::PDFPageOrganizer::extractPages(m_document, pages, &extractedDocument)))
    {
        writeDocument(fileName, &extractedDocument);
    }
}

void PDFPageOrganizerUI::onSplit()
{
    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
    if (pageCount < 2)
    {
        QMessageBox::information(m_dialogParent, tr("Split Document"), tr("Document has only one page, it can't be split."));
        return;
    }

    bool ok = false;
    const int pagesPerFile = QInputDialog::getInt(m_dialogParent, tr("Split Document"), tr("Pages per file:"), 1, 1, int(pageCount - 1), 1, &ok);
    if (!ok)
    {
        return;
    }

    QString baseFileName = QFileDialog::getSaveFileName(m_dialogParent, tr("Split Document - Name of the Files"), QDir(getDirectory()).filePath(tr("Document.pdf")), tr("PDF document (*.pdf)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (baseFileName.isEmpty())
    {
        return;
    }

    QFileInfo baseFileInfo(baseFileName);
    m_lastDirectory = baseFileInfo.absolutePath();
    const QDir directory = baseFileInfo.absoluteDir();
    const QString baseName = baseFileInfo.completeBaseName();

    const std::vector<std::vector<pdf::PDFInteger>> parts = pdf::PDFPageOrganizer::splitPages(pageCount, pagesPerFile);
    QStringList fileNames;
    bool anyFileExists = false;
    for (const std::vector<pdf::PDFInteger>& part : parts)
    {
        QString name = (part.size() == 1) ? tr("%1 - page %2.pdf").arg(baseName).arg(part.front() + 1)
                                          : tr("%1 - pages %2-%3.pdf").arg(baseName).arg(part.front() + 1).arg(part.back() + 1);
        QString fileName = directory.filePath(name);
        anyFileExists = anyFileExists || QFileInfo::exists(fileName);
        fileNames << fileName;
    }

    if (anyFileExists && QMessageBox::question(m_dialogParent, tr("Split Document"), tr("Some of the files already exist in the folder '%1'. Do you want to replace them?").arg(QDir::toNativeSeparators(directory.absolutePath()))) != QMessageBox::Yes)
    {
        return;
    }

    {
        PDFPageOrganizerUIWaitCursor waitCursor;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            pdf::PDFDocument partDocument;
            if (!checkResult(pdf::PDFPageOrganizer::extractPages(m_document, parts[i], &partDocument)) ||
                !writeDocument(fileNames[int(i)], &partDocument))
            {
                return;
            }
        }
    }

    QMessageBox::information(m_dialogParent, tr("Split Document"), tr("Document was split into %1 files in the folder '%2'.").arg(parts.size()).arg(QDir::toNativeSeparators(directory.absolutePath())));
}

bool PDFPageOrganizerUI::readDocument(const QString& fileName, pdf::PDFDocument* document)
{
    auto queryPassword = [this, fileName](bool* ok)
    {
        return QInputDialog::getText(m_dialogParent, tr("Encrypted Document"), tr("Enter password to open the document '%1':").arg(QFileInfo(fileName).fileName()), QLineEdit::Password, QString(), ok);
    };

    pdf::PDFDocumentReader reader(nullptr, queryPassword, true, false);
    *document = reader.readFromFile(fileName);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
            break;

        case pdf::PDFDocumentReader::Result::Cancelled:
            return false;

        default:
            QMessageBox::critical(m_dialogParent, tr("Error"), tr("Document '%1' can't be opened: %2").arg(QDir::toNativeSeparators(fileName), reader.getErrorMessage()));
            return false;
    }

    if (!pdf::PDFPageOrganizer::canOrganizePages(document))
    {
        QMessageBox::critical(m_dialogParent, tr("Error"), tr("Document '%1' doesn't allow to copy its pages into other documents.").arg(QDir::toNativeSeparators(fileName)));
        return false;
    }

    return true;
}

bool PDFPageOrganizerUI::writeDocument(const QString& fileName, const pdf::PDFDocument* document)
{
    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(fileName, document, true);
    if (!result)
    {
        QMessageBox::critical(m_dialogParent, tr("Error"), tr("File '%1' can't be saved: %2").arg(QDir::toNativeSeparators(fileName), result.getErrorMessage()));
        return false;
    }

    return true;
}

void PDFPageOrganizerUI::applyDocument(pdf::PDFDocument document, std::vector<pdf::PDFInteger> selectedPages, pdf::PDFInteger goToPage)
{
    if (!m_document || document == *m_document)
    {
        return;
    }

    m_pendingSelection = std::move(selectedPages);

    pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(document)));
    const pdf::PDFModifiedDocument::ModificationFlags flags(pdf::PDFModifiedDocument::Reset | pdf::PDFModifiedDocument::PreserveUndoRedo | pdf::PDFModifiedDocument::PreserveView);
    Q_EMIT documentModified(pdf::PDFModifiedDocument(std::move(pointer), m_optionalContentActivity, flags));

    if (goToPage >= 0)
    {
        m_proxy->goToPage(goToPage);
    }
}

void PDFPageOrganizerUI::selectPages(const std::vector<pdf::PDFInteger>& pages)
{
    QItemSelectionModel* selectionModel = m_thumbnailsView->selectionModel();
    if (!selectionModel)
    {
        return;
    }

    QItemSelection selection;
    for (const pdf::PDFInteger page : pages)
    {
        const QModelIndex index = m_thumbnailsModel->index(int(page), 0, QModelIndex());
        if (index.isValid())
        {
            selection.select(index, index);
        }
    }

    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    if (!selection.isEmpty())
    {
        const QModelIndex firstIndex = selection.indexes().front();
        selectionModel->setCurrentIndex(firstIndex, QItemSelectionModel::NoUpdate);
        m_thumbnailsView->scrollTo(firstIndex, QAbstractItemView::EnsureVisible);
    }
}

bool PDFPageOrganizerUI::checkResult(const pdf::PDFOperationResult& result)
{
    if (!result)
    {
        QMessageBox::critical(m_dialogParent, tr("Error"), result.getErrorMessage());
        return false;
    }

    return true;
}

QString PDFPageOrganizerUI::getDirectory() const
{
    return m_lastDirectory;
}

}   // namespace pdfviewer
