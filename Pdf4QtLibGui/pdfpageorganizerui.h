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

#ifndef PDFPAGEORGANIZERUI_H
#define PDFPAGEORGANIZERUI_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"
#include "pdfutils.h"

#include <QObject>
#include <QPointer>

#include <vector>

class QAction;
class QListView;
class QWidget;

namespace pdf
{
class PDFDrawWidgetProxy;
class PDFThumbnailsItemModel;
class PDFOptionalContentActivity;
}

namespace pdfviewer
{

/// Organizing of the pages from the main window: thumbnails can be dragged
/// to reorder the pages, PDF files dropped onto the thumbnails are inserted,
/// and actions (rotate, delete, insert, extract, split) work on the selected
/// thumbnails, or on the current page, if no thumbnail is selected.
/// Each change creates a new version of the document, so it can be undone.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFPageOrganizerUI : public QObject
{
    Q_OBJECT

public:
    explicit PDFPageOrganizerUI(pdf::PDFDrawWidgetProxy* proxy,
                                QListView* thumbnailsView,
                                pdf::PDFThumbnailsItemModel* thumbnailsModel,
                                QWidget* dialogParent);

    enum Action
    {
        RotateLeft,
        RotateRight,
        Delete,
        InsertFromFile,
        InsertBlankPage,
        Extract,
        Split,
        LastAction
    };

    /// Sets the current document
    void setDocument(const pdf::PDFModifiedDocument& document);

    /// Returns all page actions (in the order of the enumeration Action)
    std::vector<QAction*> getActions() const;

    /// Returns pages, which actions work on: selected thumbnails, or the current page
    std::vector<pdf::PDFInteger> getSelectedPages() const;

    /// Moves the pages before the given page (page count means the end)
    void movePages(std::vector<pdf::PDFInteger> pageIndices, pdf::PDFInteger insertPosition);

    /// Inserts the PDF files before the given page (page count means the end)
    void insertFiles(const QStringList& fileNames, pdf::PDFInteger insertPosition);

signals:
    void documentModified(pdf::PDFModifiedDocument document);

private:
    void updateActions();
    void onThumbnailsContextMenuRequested(const QPoint& pos);

    void onRotateLeft();
    void onRotateRight();
    void onDelete();
    void onInsertFromFile();
    void onInsertBlankPage();
    void onExtract();
    void onSplit();

    /// Reads the document, the user is asked for the password, if needed
    bool readDocument(const QString& fileName, pdf::PDFDocument* document);

    /// Writes the document into the file, reports errors to the user
    bool writeDocument(const QString& fileName, const pdf::PDFDocument* document);

    /// Sets the modified document as the new current document, if it is
    /// different from the current one, and selects the given pages.
    void applyDocument(pdf::PDFDocument document, std::vector<pdf::PDFInteger> selectedPages, pdf::PDFInteger goToPage = -1);

    /// Selects thumbnails of the given pages
    void selectPages(const std::vector<pdf::PDFInteger>& pages);

    /// Shows the error message, if operation failed, returns success of the operation
    bool checkResult(const pdf::PDFOperationResult& result);

    /// Returns the directory for the file dialogs
    QString getDirectory() const;

    pdf::PDFDrawWidgetProxy* m_proxy;
    QListView* m_thumbnailsView;
    pdf::PDFThumbnailsItemModel* m_thumbnailsModel;
    QPointer<QWidget> m_dialogParent;
    const pdf::PDFDocument* m_document = nullptr;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity = nullptr;
    std::vector<QAction*> m_actions;
    std::vector<pdf::PDFInteger> m_pendingSelection;
    QString m_lastDirectory;
};

}   // namespace pdfviewer

#endif // PDFPAGEORGANIZERUI_H
