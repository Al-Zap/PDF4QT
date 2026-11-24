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



#ifndef PDFBATCHPROCESSINGDIALOG_H
#define PDFBATCHPROCESSINGDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfbatchprocessor.h"

#include <QDialog>

#include <memory>

class QLabel;
class QThread;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;
class QRadioButton;
class QTableWidget;
class QProgressBar;
class QDialogButtonBox;

namespace pdf
{
class PDFOCRModelManager;
}

namespace pdfviewer
{

/// Editor of the settings of a step of a batch, which is not a stamp (the stamps
/// are edited by PDFPageStampDialog in the batch mode)
class PDF4QTLIBGUILIBSHARED_EXPORT PDFBatchActionDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFBatchActionDialog(const pdf::PDFBatchAction& action, QWidget* parent);
    virtual ~PDFBatchActionDialog() override;

    /// Returns the step with the settings of the widgets
    pdf::PDFBatchAction getAction() const;

    /// Returns true, if the step has any settings to edit
    static bool hasSettings(pdf::PDFBatchAction::Type type);

    virtual void accept() override;

private:
    pdf::PDFBatchAction m_action;

    QLineEdit* m_pagesEdit = nullptr;

    // RotatePages
    QComboBox* m_rotationComboBox = nullptr;

    // RecognizeText
    QLineEdit* m_languagesEdit = nullptr;
    QComboBox* m_profileComboBox = nullptr;
    QComboBox* m_existingTextComboBox = nullptr;
    QComboBox* m_decisionComboBox = nullptr;
    QCheckBox* m_allowPageErrorsCheckBox = nullptr;

    // Optimize
    QCheckBox* m_optimizeObjectsCheckBox = nullptr;
    QCheckBox* m_optimizeImagesCheckBox = nullptr;
    QComboBox* m_imageGoalComboBox = nullptr;
    QSpinBox* m_imageDpiSpinBox = nullptr;

    // Sanitize
    std::vector<std::pair<QCheckBox*, pdf::PDFDocumentSanitizer::SanitizationFlag>> m_sanitizeCheckBoxes;

    // SetPassword
    QLineEdit* m_userPasswordEdit = nullptr;
    QLineEdit* m_ownerPasswordEdit = nullptr;

    // ExportWord
    QCheckBox* m_wordGraphicsCheckBox = nullptr;
    QSpinBox* m_wordResolutionSpinBox = nullptr;

    // ExportImages
    QComboBox* m_imageFormatComboBox = nullptr;
    QSpinBox* m_imageResolutionSpinBox = nullptr;

    // RemoveStamps
    QCheckBox* m_removeHeaderFooterCheckBox = nullptr;
    QCheckBox* m_removeWatermarkCheckBox = nullptr;
};

/// Batch processing of many PDF files (Kofax Power PDF style), Convert > Batch Process.
/// The user picks the files (or folders, optionally with the subfolders) and a sequence
/// of actions (stamps, flattening, rotation, OCR, optimization, removal of the hidden
/// information, password, exports to Word, text and images). The sequence can be saved
/// into a JSON file and loaded again (also by the command line tool, PdfTool batch).
/// The files are processed by PDFBatchProcessor on a worker thread, one after another;
/// the originals are never changed.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFBatchProcessingDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFBatchProcessingDialog(QWidget* parent);
    virtual ~PDFBatchProcessingDialog() override;

    /// Adds the PDF files; folders are searched for the PDF files (also in the
    /// subfolders, when the check box is checked). Files already listed are ignored.
    void addFiles(const QStringList& files);

    /// Returns the files of the list
    QStringList getFiles() const;

    /// Returns the settings of the batch (sequence and output) given by the widgets
    pdf::PDFBatchSettings getSettings() const;

    /// Sets the sequence and the output settings into the widgets
    void setSettings(const pdf::PDFBatchSettings& settings);

    /// Appends a step to the sequence
    void addAction(const pdf::PDFBatchAction& action);

    /// Replaces the step of the row of the sequence
    void setAction(int row, const pdf::PDFBatchAction& action);

    /// Returns the steps of the sequence
    const std::vector<pdf::PDFBatchAction>& getActions() const { return m_actions; }

    /// Saves the sequence into a JSON file (the passwords are not saved). Returns an error message.
    QString saveSequence(const QString& fileName) const;

    /// Loads the sequence from a JSON file. Returns an error message.
    QString loadSequence(const QString& fileName);

    /// Starts the batch. Returns the errors, which prevent the start (empty list on start).
    /// Existing outputs are handled by the policy of the settings, no question is asked.
    QStringList startBatch();

    /// Stops the batch (the current file leaves no output)
    void stopBatch();

    /// Returns true, while the batch is running
    bool isRunning() const;

    /// Returns the results of the processed files of the last batch
    const std::vector<pdf::PDFBatchProcessor::FileResult>& getResults() const { return m_results; }

    /// Returns the result, which the user wants to open in the editor (the dialog
    /// is accepted then), or an empty string
    const QString& getDocumentToOpen() const { return m_documentToOpen; }

    virtual void reject() override;

signals:
    /// The batch has finished (all files processed, or stopped)
    void batchFinished();

protected:
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    enum Column
    {
        ColumnFile,
        ColumnStatus,
        ColumnPages,
        ColumnTime,
        ColumnMessage,
        ColumnCount
    };

    static constexpr int InputFileRole = Qt::UserRole;
    static constexpr int OutputFileRole = Qt::UserRole + 1;

    void createUi();
    void loadSettings();
    void saveSettings() const;
    void updateUi();
    void updateActionList();

    void onAddFiles();
    void onAddFolder();
    void onRemoveFiles();
    void onClearFiles();
    void onAddAction(pdf::PDFBatchAction::Type type);
    void onEditAction();
    void onRemoveAction();
    void onMoveAction(int offset);
    void onSaveSequence();
    void onLoadSequence();
    void onBrowseOutputDirectory();
    void onStart();
    void onOpenResult();
    void onFileStarted(int row);
    void onFileProgress(int row, int step, int stepCount, QString name);
    void onFileFinished(int row, pdf::PDFBatchProcessor::FileResult result);
    void onThreadFinished();

    /// Edits the step by its dialog. Returns true, if the step was accepted.
    bool editAction(pdf::PDFBatchAction& action);

    void setRowStatus(int row, const QString& status, const QString& message);

    pdf::PDFOCRModelManager* m_modelManager = nullptr;
    std::vector<pdf::PDFBatchAction> m_actions;

    // Files
    QTableWidget* m_filesTable = nullptr;
    QPushButton* m_addFilesButton = nullptr;
    QPushButton* m_addFolderButton = nullptr;
    QCheckBox* m_includeSubfoldersCheckBox = nullptr;
    QPushButton* m_removeFilesButton = nullptr;
    QPushButton* m_clearFilesButton = nullptr;

    // Sequence
    QListWidget* m_actionsList = nullptr;
    QToolButton* m_addActionButton = nullptr;
    QPushButton* m_editActionButton = nullptr;
    QPushButton* m_removeActionButton = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QPushButton* m_saveSequenceButton = nullptr;
    QPushButton* m_loadSequenceButton = nullptr;

    // Output
    QRadioButton* m_outputFolderRadioButton = nullptr;
    QRadioButton* m_nextToOriginalRadioButton = nullptr;
    QLineEdit* m_outputDirectoryEdit = nullptr;
    QPushButton* m_browseOutputDirectoryButton = nullptr;
    QLineEdit* m_suffixEdit = nullptr;
    QComboBox* m_existingFilesComboBox = nullptr;
    QLineEdit* m_passwordEdit = nullptr;

    // Progress
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_openResultButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    QThread* m_thread = nullptr;
    std::shared_ptr<pdf::PDFBatchCancelToken> m_cancelToken;
    std::vector<pdf::PDFBatchProcessor::FileResult> m_results;
    int m_totalFiles = 0;
    int m_finishedFiles = 0;
    bool m_closeRequested = false;
    QString m_documentToOpen;
};

}   // namespace pdfviewer

#endif // PDFBATCHPROCESSINGDIALOG_H
