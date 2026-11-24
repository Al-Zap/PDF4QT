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

#ifndef PDFSPLITVIEWWIDGET_H
#define PDFSPLITVIEWWIDGET_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"

#include <QWidget>

class QLabel;
class QCheckBox;
class QToolButton;

namespace pdf
{
class PDFWidget;
class PDFOptionalContentActivity;
class PDFWidgetAnnotationManager;
}

namespace pdfviewer
{

/// Second pane of the split view of the editor. It shows either the document
/// of the main pane (another place of the same document, kept up to date with
/// the edits of the main pane) or another file (for example the previous revision
/// of a drawing sheet). The pane is only for viewing, the annotations are drawn,
/// but they can be edited only in the main pane. The scrolling and the zoom
/// of both panes can be synchronized.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFSplitViewWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Creates the pane for the main widget \p mainWidget (its color management,
    /// renderer and features are used, it is the partner of the synchronization)
    explicit PDFSplitViewWidget(pdf::PDFWidget* mainWidget, QWidget* parent);
    virtual ~PDFSplitViewWidget() override;

    /// Sets the document of the main pane. It is shown in this pane, if this pane
    /// shows the same document as the main pane.
    /// \param document Document of the main pane
    /// \param fileName File name of the document of the main pane
    void setMainDocument(const pdf::PDFModifiedDocument& document, const QString& fileName);

    /// Shows the document of the main pane
    void showMainDocument();

    /// Opens another file in this pane. Returns true, if the file has been opened,
    /// otherwise the error message is returned in \p errorMessage. The scrolling
    /// is synchronized with the main pane after the file is opened.
    /// \param fileName File name
    /// \param errorMessage Error message
    bool openFile(const QString& fileName, QString* errorMessage);

    /// Returns true, if the pane shows the same document as the main pane
    bool isShowingMainDocument() const { return m_otherDocument.isNull(); }

    /// Returns the file name of the shown document
    QString getFileName() const;

    bool isScrollingSynchronized() const;
    void setScrollingSynchronized(bool synchronized);

    pdf::PDFWidget* getPdfWidget() const { return m_pdfWidget; }

    /// Copies the zoom and the position of the scroll bars of the widget
    /// \p source to the widget \p target (both must show a document)
    static void synchronizeWidgets(pdf::PDFWidget* source, pdf::PDFWidget* target);

signals:
    void closeRequested();

private:
    void onOpenFileClicked();
    void onSourceViewChanged(pdf::PDFWidget* source, pdf::PDFWidget* target);
    void setPaneDocument(const pdf::PDFModifiedDocument& document);
    void updateTitle();

    pdf::PDFWidget* m_mainWidget;
    pdf::PDFWidget* m_pdfWidget;
    pdf::PDFWidgetAnnotationManager* m_annotationManager;
    QLabel* m_titleLabel;
    QToolButton* m_sameDocumentButton;
    QToolButton* m_openFileButton;
    QCheckBox* m_synchronizeCheckBox;
    QToolButton* m_closeButton;

    pdf::PDFModifiedDocument m_mainDocument;
    QString m_mainFileName;

    pdf::PDFDocumentPointer m_otherDocument;
    pdf::PDFOptionalContentActivity* m_otherOptionalContentActivity;
    QString m_otherFileName;

    bool m_isSynchronizing;
};

}   // namespace pdfviewer

#endif // PDFSPLITVIEWWIDGET_H
