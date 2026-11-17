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


#ifndef PDFOCRBACKGROUNDTASK_H
#define PDFOCRBACKGROUNDTASK_H

#include "pdfviewerglobal.h"
#include "pdfocrdocumentrunner.h"
#include "pdfcms.h"

#include <QObject>

#include <map>
#include <memory>
#include <vector>

class QThread;

namespace pdf
{
class PDFPage;
class PDFOCRCancelToken;
class PDFOCRModelManager;
}

namespace pdfviewer
{

/// One click OCR of the current document ("Make Searchable"). The pages without
/// text are recognized on a worker thread with defaults chosen for the document
/// and the invisible text layer is written into a new revision of the document,
/// which is passed back to the application as a single change. The user interface
/// is not blocked; the progress is reported by the signals.
///
/// Defaults: the settings of the last use of the dialog Recognize Text (languages,
/// resolution, preprocessing, user words), with these changes:
///   - only the pages without text are recognized; a scan with a little digital
///     text (a stamp, a page number) is recognized with the text masked,
///   - large sheets (drawings, 11x17 inch and larger) are recognized with the
///     sparse text layout, which finds the scattered labels and dimensions of
///     a drawing; the other pages use the layout of the settings,
///   - the images are never re-encoded, the pages too large for the resolution
///     are recognized with a reduced resolution, small skew is corrected,
///   - when the models of the selected profile are not installed, the built-in
///     fast models are used.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRBackgroundTask : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFOCRBackgroundTask(QObject* parent);
    virtual ~PDFOCRBackgroundTask() override;

    /// Result of the task, see takeOutcome
    struct Outcome
    {
        /// Recognized document
        pdf::PDFDocumentPointer sourceDocument;

        /// Document with the text layer (null, if nothing was written or on error)
        pdf::PDFDocumentPointer resultDocument;

        /// File name of the recognized document
        QString fileName;

        /// Results of the pages with a result (to write them into a newer revision
        /// of the document, if the document was changed during the recognition)
        std::vector<pdf::PDFOCRPageResult> results;

        int pageCount = 0;          ///< Number of the pages of the document
        int drawingPages = 0;       ///< Pages recognized as drawings (sparse text layout)
        int recognizedPages = 0;    ///< Pages with a result
        int writtenPages = 0;       ///< Pages, where the text layer was written
        int skippedPages = 0;       ///< Pages with existing text and the pages not recognized
        int failedPages = 0;        ///< Pages with an error
        int wordCount = 0;
        int reviewWordCount = 0;    ///< Words with a low confidence
        qint64 elapsedMilliseconds = 0;

        bool cancelled = false;

        /// Critical error (translated), nothing was written then
        QString errorMessage;

        /// Warnings and the messages of the skipped/failed pages (translated)
        QStringList details;
    };

    /// Result of writing the recognized text into a document
    struct WriteResult
    {
        pdf::PDFDocumentPointer document;
        int writtenPages = 0;
        bool nothingToWrite = false;
        QString errorMessage;
        QStringList messages;
    };

    /// Returns the configuration of the last use of the dialog Recognize Text
    /// (the default configuration with English, if the dialog was never used)
    static pdf::PDFOCRConfiguration loadLastConfiguration();

    /// Adjusts the configuration for the one click recognition (see the description
    /// of the class) and checks the engine and the language models. Returns an error
    /// (translated) or an empty string. The missing models are filled, if they are
    /// the reason of the error (the user can install them in the language manager).
    static QString prepareConfiguration(pdf::PDFOCRConfiguration& configuration,
                                        pdf::PDFOCRModelManager* modelManager,
                                        QStringList* missingModels);

    /// Returns true, if the page is a large sheet (11x17 inch and larger), typically
    /// a drawing, whose text is recognized with the sparse text layout
    static bool isDrawingSheet(const pdf::PDFPage* page);

    /// Writes the results into the document as one transaction (PDFOCRApplyProcessor).
    /// The results of the changed pages are not written (their fingerprint differs).
    /// The analysis and the fingerprints of the pages are optional (computed, if missing).
    static WriteResult writeResults(const pdf::PDFDocument* document,
                                    const QString& fileName,
                                    const std::vector<pdf::PDFOCRPageResult>& results,
                                    const std::map<pdf::PDFInteger, pdf::PDFOCRPageAnalysis>* analysis,
                                    const std::map<pdf::PDFInteger, QByteArray>* fingerprints,
                                    const pdf::PDFOperationControl* operationControl);

    /// Starts the recognition of the document. The configuration must be prepared by
    /// prepareConfiguration. Returns an error (translated, nothing is started then)
    /// or an empty string. The model manager must outlive the task.
    QString start(pdf::PDFDocumentPointer document,
                  const QString& fileName,
                  pdf::PDFCMSPointer cms,
                  const pdf::PDFOCRConfiguration& configuration,
                  pdf::PDFOCRModelManager* modelManager);

    /// Stops the recognition, its result is discarded (the outcome is marked as cancelled)
    void cancel();

    /// Returns true, if the recognition is running (also while it is being cancelled)
    bool isRunning() const { return m_thread != nullptr; }

    /// Returns true, if the running recognition is being cancelled
    bool isCancelling() const;

    /// Takes the outcome of the finished recognition
    Outcome takeOutcome();

signals:
    /// Progress of the recognition: finished pages and the total number of the recognized
    /// pages (total is 0, while the pages are being analyzed)
    void progressChanged(int finished, int total);

    /// The recognition finished, was cancelled or failed, see takeOutcome
    void finished();

private:
    void onThreadFinished();

    QThread* m_thread = nullptr;
    std::shared_ptr<pdf::PDFOCRCancelToken> m_cancelToken;
    std::shared_ptr<Outcome> m_runningOutcome;
    Outcome m_outcome;
    bool m_discarded = false;
};

}   // namespace pdfviewer

#endif // PDFOCRBACKGROUNDTASK_H
