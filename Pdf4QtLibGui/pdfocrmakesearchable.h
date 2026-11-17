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


#ifndef PDFOCRMAKESEARCHABLE_H
#define PDFOCRMAKESEARCHABLE_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"
#include "pdfcms.h"

#include <QObject>
#include <QPointer>

#include <functional>

class QLabel;
class QTimer;
class QWidget;
class QMainWindow;
class QToolButton;
class QProgressBar;

namespace pdf
{
class PDFOCRModelManager;
}

namespace pdfviewer
{
class PDFOCRBackgroundTask;

/// Command "Make Searchable" of the editor: one click recognition of the scanned
/// pages of the current document (PDFOCRBackgroundTask). The progress is shown in
/// a compact widget in the status bar with a button to stop the recognition, so the
/// user can keep viewing, measuring and marking up the document meanwhile. The
/// result is passed back by the signal documentReady as one change of the document
/// (one undo step). When the document was changed during the recognition, the text
/// is written into the current revision; the changed pages are left out.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRMakeSearchable : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Returns the current document of the application
    using DocumentProvider = std::function<pdf::PDFDocumentPointer()>;

    explicit PDFOCRMakeSearchable(QMainWindow* mainWindow, DocumentProvider documentProvider, QObject* parent);
    virtual ~PDFOCRMakeSearchable() override;

    /// Starts the recognition of the current document (the questions and the errors
    /// before the start are displayed in message boxes)
    void start(const QString& fileName, pdf::PDFCMSPointer cms);

    /// Stops the recognition (for example, when the document is closed), its result is discarded
    void cancel();

    bool isRunning() const;

signals:
    /// New revision of the current document with the searchable text
    void documentReady(pdf::PDFDocumentPointer document);

    /// The recognition started or finished
    void runningChanged(bool running);

private:
    void onProgressChanged(int finished, int total);
    void onFinished();
    void onCancelClicked();
    void showStatus(const QString& text, bool running);
    void hideStatus();

    QMainWindow* m_mainWindow;
    DocumentProvider m_documentProvider;
    pdf::PDFOCRModelManager* m_modelManager;
    PDFOCRBackgroundTask* m_task;
    QTimer* m_hideTimer;

    // The widgets belong to the status bar of the main window
    QPointer<QWidget> m_statusWidget;
    QPointer<QLabel> m_statusLabel;
    QPointer<QProgressBar> m_progressBar;
    QPointer<QToolButton> m_cancelButton;
    QPointer<QToolButton> m_closeButton;
};

}   // namespace pdfviewer

#endif // PDFOCRMAKESEARCHABLE_H
