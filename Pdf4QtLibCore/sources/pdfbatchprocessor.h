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



#ifndef PDFBATCHPROCESSOR_H
#define PDFBATCHPROCESSOR_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfpagestamper.h"
#include "pdfoptimizer.h"
#include "pdfimageoptimizer.h"
#include "pdfdocumentsanitizer.h"
#include "pdfocrconfiguration.h"
#include "pdfocrdocumentrunner.h"
#include "pdfoperationcontrol.h"

#include <QJsonObject>
#include <QStringList>

#include <map>
#include <atomic>
#include <optional>
#include <functional>

namespace pdf
{
class PDFOCRModelManager;

/// One step of a batch sequence. The steps of a sequence are applied to each
/// document in their order: the steps, which modify the document (stamps, flattening,
/// rotation, recognition, optimization, sanitization, password), work above the
/// document in the memory, the exports (Word, text, images) write the document
/// in its current state (after the steps in front of them) into their files.
/// The modified document is written once, after the last step.
struct PDF4QTLIBCORESHARED_EXPORT PDFBatchAction
{
    enum class Type
    {
        HeaderFooter,       ///< Headers and footers (PDFPageStamper::addHeaderFooter)
        BatesNumbering,     ///< Bates numbers (a header or a footer with the variable {bates})
        Watermark,          ///< Watermark (PDFPageStamper::addWatermark)
        RemoveStamps,       ///< Removal of the stamps added by PDF4QT
        FlattenForms,       ///< Values of the form fields become a part of the page content
        RotatePages,        ///< Rotation of the pages by a multiple of 90 degrees
        RecognizeText,      ///< Invisible text layer of the scanned pages (OCR)
        Optimize,           ///< Reduction of the file size (objects, streams, images)
        Sanitize,           ///< Removal of the metadata and of the hidden data
        SetPassword,        ///< Encryption by a password (AES 256)
        ExportWord,         ///< Export into a Word document (DOCX)
        ExportText,         ///< Export of the text into a text file
        ExportImages,       ///< Export of the pages into images
        TypeCount
    };

    Type type = Type::Optimize;

    /// Pages of the step (stamps, removal of the stamps, rotation, recognition and
    /// exports), in the form "1-3,5,8-" (one based); empty means all pages
    QString pages;

    // Stamps (HeaderFooter, BatesNumbering, Watermark)
    PDFHeaderFooterSettings headerFooter;
    PDFWatermarkSettings watermark;
    bool replaceExisting = true;

    /// Bates numbers continue in the next document of the batch (otherwise
    /// each document starts with the start number of the settings)
    bool continueBatesNumbering = true;

    // RemoveStamps
    PDFPageStamper::StampKinds removedStamps = PDFPageStamper::AllKinds;

    // RotatePages: angle of the rotation, clockwise (90, 180 or 270)
    int rotation = 90;

    // RecognizeText
    PDFOCRConfiguration ocrConfiguration;
    PDFOCRDocumentRunner::DecisionPolicy ocrDecisionPolicy = PDFOCRDocumentRunner::DecisionPolicy::Skip;

    /// Pages, which failed to be recognized, do not fail the document
    bool ocrAllowPageErrors = false;

    // Optimize
    PDFOptimizer::OptimizationFlags optimizationFlags = PDFOptimizer::All;
    bool optimizeImages = false;
    PDFImageOptimizer::OptimizationGoal imageGoal = PDFImageOptimizer::OptimizationGoal::PreferQuality;

    /// Images with a higher resolution are downsampled to this resolution (0 = keep)
    int imageTargetDpi = 0;

    // Sanitize
    PDFDocumentSanitizer::SanitizationFlags sanitizationFlags = PDFDocumentSanitizer::SanitizationFlags(PDFDocumentSanitizer::DocumentInfo | PDFDocumentSanitizer::Metadata);

    // SetPassword. The passwords are never written into the JSON of the sequence.
    QString userPassword;
    QString ownerPassword;

    // ExportWord
    bool wordIncludeGraphics = true;
    int wordGraphicsResolution = 150;

    // ExportImages
    QString imageFormat = QStringLiteral("png");
    int imageResolution = 150;

    /// Creates a step of the type with the default settings
    static PDFBatchAction create(Type type);

    /// Returns true, if the step changes the document (otherwise it is an export)
    bool isModifying() const { return isModifying(type); }
    static bool isModifying(Type type);

    /// Returns true, if the step works only with the selected pages
    static bool hasPages(Type type);

    /// Returns the translated name of the type
    static QString getTypeName(Type type);

    /// Returns the identifier of the type in the JSON of a sequence ("bates", "watermark", ...)
    static QString getTypeId(Type type);

    /// Returns the type of the identifier, or nothing for an unknown identifier
    static std::optional<Type> getTypeFromId(const QString& id);

    /// Returns the translated name of the step
    QString getName() const { return getTypeName(type); }

    /// Returns a short translated summary of the settings of the step
    QString getSummary() const;

    /// Returns the errors of the settings (translated), an empty list, if the step is valid
    QStringList validate() const;

    /// Returns the selected pages (zero based) of a document with \p pageCount pages,
    /// all pages for an empty selection. An invalid selection is reported by \p errorMessage.
    std::vector<PDFInteger> getPageIndices(PDFInteger pageCount, QString* errorMessage) const;

    /// Serializes the step (the passwords are not written)
    QJsonObject toJson() const;

    /// Reads the step. An unknown type is reported by \p errorMessage (the
    /// returned step is then invalid), missing values keep the defaults of the type.
    static PDFBatchAction fromJson(const QJsonObject& object, QString* errorMessage);
};

/// Settings of a batch: the sequence of the steps and the output files
struct PDF4QTLIBCORESHARED_EXPORT PDFBatchSettings
{
    enum class OutputLocation
    {
        NextToOriginal,     ///< Into the folder of the original file
        Folder              ///< Into the output folder
    };

    enum class ExistingFilePolicy
    {
        Rename,             ///< A number is added to the file name ("name (2).pdf")
        Overwrite,          ///< The existing file is replaced
        Skip                ///< The original file is not processed
    };

    /// Steps of the sequence, in the order, in which they are applied
    std::vector<PDFBatchAction> actions;

    OutputLocation outputLocation = OutputLocation::Folder;

    /// Output folder (OutputLocation::Folder)
    QString outputDirectory;

    /// Text added behind the name of the original file (an original is never overwritten)
    QString suffix = QStringLiteral("_processed");

    ExistingFilePolicy existingFilePolicy = ExistingFilePolicy::Rename;

    /// Password of the protected originals (never written into the JSON)
    QString password;

    /// Returns true, if any step modifies the document (the PDF is written then)
    bool writesDocument() const;

    /// Returns the errors of the settings (translated)
    QStringList validate() const;

    /// Serializes the sequence and the output settings (no passwords)
    QJsonObject toJson() const;

    /// Reads the settings, the errors of the unknown steps are reported by \p errorMessage
    static PDFBatchSettings fromJson(const QJsonObject& object, QString* errorMessage);
};

/// Cancellation of a running batch, it can be set from any thread
class PDF4QTLIBCORESHARED_EXPORT PDFBatchCancelToken : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_cancelled.load(std::memory_order_acquire); }
    void cancel() { m_cancelled.store(true, std::memory_order_release); }

private:
    std::atomic<bool> m_cancelled = { false };
};

/// Batch processing (Kofax Power PDF style): a sequence of steps is applied to many
/// PDF files, one file after another. The originals are never changed, the results are
/// written into new files (next to the originals with a suffix, or into a folder). A file
/// failing in any step leaves no output (the files written by its exports are removed),
/// the batch continues with the next file. The Bates numbers continue over the files.
///
/// The recognition of the text (RecognizeText) uses PDFOCRDocumentRunner, which needs
/// a thread with an event dispatcher (the main thread or a QThread); the other steps
/// have no such requirement.
class PDF4QTLIBCORESHARED_EXPORT PDFBatchProcessor
{
public:
    explicit PDFBatchProcessor(PDFBatchSettings settings);
    ~PDFBatchProcessor();

    struct FileResult
    {
        enum class Status
        {
            Succeeded,
            Skipped,
            Failed,
            Cancelled
        };

        QString inputFile;
        Status status = Status::Failed;

        /// Main message (the reason of a failure, or a summary of a success)
        QString message;

        /// Messages of the steps and warnings
        QStringList details;

        /// Written files (the PDF first, then the exports in the order of the steps)
        QStringList outputFiles;

        int pageCount = 0;
        qint64 elapsedMilliseconds = 0;

        bool isSuccess() const { return status == Status::Succeeded; }
    };

    /// Progress of a file: index of the running step, count of the steps, name of the step
    using ProgressCallback = std::function<void(int step, int stepCount, const QString& name)>;

    const PDFBatchSettings& getSettings() const { return m_settings; }

    /// Model manager of the recognition (if not set, the processor creates its own
    /// manager with the bundled catalog, when the recognition is needed)
    void setOCRModelManager(PDFOCRModelManager* modelManager) { m_modelManager = modelManager; }

    /// Returns the errors of the settings and of the availability of the OCR engine
    QStringList validate() const;

    /// Returns the planned output files of the original (before the policy of the
    /// existing files is applied): the PDF first, then the exports. For the export into
    /// images, the file of the first page is returned.
    QStringList getOutputFiles(const QString& inputFile) const;

    /// Processes the file, see the description of the class. No exception escapes.
    FileResult processFile(const QString& inputFile, const ProgressCallback& progress, const PDFOperationControl* operationControl);

    /// Restarts the numbering of the Bates numbers (a new batch)
    void restartNumbering() { m_nextBatesNumbers.clear(); }

    /// Returns the PDF files of the folder (sorted by the name), optionally of the subfolders too
    static QStringList findPdfFiles(const QString& directory, bool recursive);

    /// Returns the translated name of the status
    static QString getStatusName(FileResult::Status status);

    /// Returns a file name, which does not exist yet: the file name, or the file
    /// name with a number ("name (2).pdf") added. Names in \p reserved are avoided too.
    static QString getUniqueFileName(const QString& fileName, const QStringList& reserved = QStringList());

private:
    struct Context;

    FileResult processFileImpl(const QString& inputFile, const ProgressCallback& progress, const PDFOperationControl* operationControl, Context& context);

    /// Applies a modifying step to the document. Returns an error message (empty on success).
    QString applyStep(size_t index, const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl);

    /// Writes an export step. Returns an error message (empty on success).
    QString exportStep(const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl);

    QString recognizeText(const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl);
    QString exportImages(const PDFBatchAction& action, Context& context, const QString& firstFileName, const PDFOperationControl* operationControl);

    /// Returns the base name of the outputs of the original (folder, name and suffix, no extension)
    QString getOutputBaseName(const QString& inputFile) const;

    /// Returns the extension of the output of the step (with the dot)
    static QString getExportExtension(const PDFBatchAction& action);

    PDFOCRModelManager* getModelManager();

    PDFBatchSettings m_settings;
    PDFOCRModelManager* m_modelManager = nullptr;
    std::unique_ptr<PDFOCRModelManager> m_ownModelManager;

    /// Next Bates number of the step (index of the step), continued over the files
    std::map<size_t, PDFInteger> m_nextBatesNumbers;
};

}   // namespace pdf

#endif // PDFBATCHPROCESSOR_H
