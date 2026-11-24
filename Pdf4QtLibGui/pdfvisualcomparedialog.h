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

#ifndef PDFVISUALCOMPAREDIALOG_H
#define PDFVISUALCOMPAREDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfvisualcompare.h"
#include "pdfoperationcontrol.h"

#include <QDialog>

#include <atomic>
#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;
class QThread;
class QDialogButtonBox;
class QGridLayout;

namespace pdf
{
class PDFProgress;
}

namespace pdfviewer
{

/// Dialog of the visual comparison of two revisions of a document (Compare Documents
/// in the ribbon). The user chooses the older and the newer revision (a file, or the
/// document open in the window), the pages and the sensitivity. The comparison runs
/// in a worker thread and writes the report document, which can then be opened.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFVisualCompareDialog : public QDialog, public pdf::PDFOperationControl
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// \param openDocument Document open in the window (can be nullptr)
    /// \param openFileName File name of the open document (can be empty)
    /// \param parent Parent widget
    explicit PDFVisualCompareDialog(const pdf::PDFDocument* openDocument, const QString& openFileName, QWidget* parent);
    virtual ~PDFVisualCompareDialog() override;

    enum class Source
    {
        File,
        OpenDocument
    };

    virtual bool isOperationCancelled() const override;

    /// Settings of the comparison read from the widgets
    pdf::PDFVisualCompareSettings getSettings() const;

    /// Result of the last finished comparison
    const pdf::PDFVisualCompareResult& getResult() const { return m_result; }

    /// File name of the written report
    QString getReportFileName() const;

    /// Returns true, if the user wants to open the report after the comparison
    bool isOpenReportRequested() const;

    /// Returns true, while the comparison is running
    bool isRunning() const { return m_thread != nullptr; }

    /// Returns the last error shown in the dialog
    QString getErrorMessage() const { return m_errorMessage; }

    /// Starts the comparison (the same as the button Compare)
    void startComparison();

    /// Closes the dialog. A running comparison is cancelled first, the dialog
    /// is closed, when the worker stops.
    virtual void reject() override;

    /// Default name of the report document for the given new document
    static QString getDefaultReportFileName(const QString& newFileName);

signals:
    /// Emitted, when the comparison finished, failed or was cancelled
    void comparisonFinished();

private:
    struct Revision
    {
        QComboBox* sourceComboBox = nullptr;
        QLineEdit* fileEdit = nullptr;
        QPushButton* browseButton = nullptr;
        QLineEdit* pagesEdit = nullptr;
    };

    void createRevisionRow(QGridLayout* layout, int row, const QString& label, const QString& name, Revision& revision);
    void browse(Revision& revision);
    void swapRevisions();
    void updateUi();
    void updateDefaultReportFileName();
    void browseReport();
    void openContentDiff();
    void onThreadFinished();
    void showError(const QString& errorMessage);
    void loadSettings();
    void saveSettings();

    Source getSource(const Revision& revision) const;
    QString getFileName(const Revision& revision) const;
    bool getPages(const Revision& revision, const pdf::PDFDocument* document, std::vector<pdf::PDFInteger>& pages, QString* errorMessage) const;

    const pdf::PDFDocument* m_openDocument;
    QString m_openFileName;

    Revision m_oldRevision;
    Revision m_newRevision;
    QPushButton* m_swapButton = nullptr;
    QSpinBox* m_resolutionSpinBox = nullptr;
    QComboBox* m_alignmentComboBox = nullptr;
    QCheckBox* m_scaleToFitCheckBox = nullptr;
    QSpinBox* m_toleranceSpinBox = nullptr;
    QSpinBox* m_colorToleranceSpinBox = nullptr;
    QDoubleSpinBox* m_minimumSizeSpinBox = nullptr;
    QDoubleSpinBox* m_mergeDistanceSpinBox = nullptr;
    QLineEdit* m_reportFileEdit = nullptr;
    QPushButton* m_reportBrowseButton = nullptr;
    QCheckBox* m_openReportCheckBox = nullptr;
    QPushButton* m_contentDiffButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QPushButton* m_compareButton = nullptr;

    bool m_reportFileEdited = false;
    bool m_updatingReportFileName = false;
    QString m_errorMessage;
    QString m_reportFileName;

    // Data of the running comparison
    QThread* m_thread = nullptr;
    pdf::PDFProgress* m_progress = nullptr;
    std::atomic_bool m_cancelled = false;
    std::optional<pdf::PDFDocument> m_oldDocument;
    std::optional<pdf::PDFDocument> m_newDocument;
    pdf::PDFVisualCompareResult m_result;
    QString m_workerError;
    bool m_closeAfterCancel = false;
};

}   // namespace pdfviewer

#endif // PDFVISUALCOMPAREDIALOG_H
