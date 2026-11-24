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

#ifndef PDFEDITORTABWINDOW_H
#define PDFEDITORTABWINDOW_H

#include "pdfviewerglobal.h"

#include <QMainWindow>

class QTabWidget;
class QToolButton;

namespace pdfviewer
{
class PDFEditorMainWindow;

/// Window of the editor with several documents, each of them in its own tab.
/// Each tab is a complete editor (PDFEditorMainWindow embedded as a widget),
/// so the ribbon, the panes and the undo/redo belong to the document of the tab.
/// Documents opened by the user (open dialog, recent files, dropped files, results
/// of the dialogs) are opened in a new tab, if the current tab already shows a document.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFEditorTabWindow : public QMainWindow
{
    Q_OBJECT

private:
    using BaseClass = QMainWindow;

public:
    explicit PDFEditorTabWindow(QWidget* parent = nullptr);
    virtual ~PDFEditorTabWindow() override;

    /// Adds a new tab with an empty editor and makes it current
    PDFEditorMainWindow* addEditor();

    /// Returns the editor of the current tab
    PDFEditorMainWindow* getCurrentEditor() const;

    /// Returns the editor of the tab with index \p index, or nullptr
    PDFEditorMainWindow* getEditor(int index) const;

    /// Returns the number of the tabs
    int getEditorCount() const;

    QTabWidget* getTabWidget() const { return m_tabWidget; }

    /// Opens the document. A document, which is already opened in a tab,
    /// is shown. The document is opened in the current tab, if it is empty,
    /// otherwise in a new tab.
    /// \param fileName File name of the document
    void openDocument(const QString& fileName);

    /// Closes the tab, the user is asked to save the changes of the document.
    /// The last tab is not removed, only its document is closed.
    /// \param index Index of the tab
    /// \returns false, if the user has cancelled the closing
    bool closeEditor(int index);

    /// Returns the text of the tab of the editor
    static QString getTabText(const PDFEditorMainWindow* editor);

protected:
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    bool openDocumentElsewhere(PDFEditorMainWindow* editor, const QString& fileName);
    int findEditor(const QString& fileName) const;
    void updateTab(PDFEditorMainWindow* editor);
    void updateWindowTitle();
    void onNewTabTriggered();
    void selectNextTab(int step);
    void readGeometry();
    void writeGeometry();

    QTabWidget* m_tabWidget;
    QToolButton* m_newTabButton;
};

}   // namespace pdfviewer

#endif // PDFEDITORTABWINDOW_H
