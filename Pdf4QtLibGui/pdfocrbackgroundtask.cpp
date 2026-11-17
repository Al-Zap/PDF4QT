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


#include "pdfocrbackgroundtask.h"
#include "pdfocrdocumentdialog.h"
#include "pdfocrapplyprocessor.h"
#include "pdfocrjobcontroller.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrsession.h"
#include "pdfocrengine.h"
#include "pdfcatalog.h"
#include "pdfpage.h"

#include <QThread>
#include <QSettings>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QCoreApplication>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFOCRBackgroundTask::PDFOCRBackgroundTask(QObject* parent) :
    BaseClass(parent)
{

}

PDFOCRBackgroundTask::~PDFOCRBackgroundTask()
{
    // The worker posts its progress to this object, so it must finish first
    // (the posted events are discarded together with the object)
    if (m_thread)
    {
        m_cancelToken->cancel();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

pdf::PDFOCRConfiguration PDFOCRBackgroundTask::loadLastConfiguration()
{
    pdf::PDFOCRConfiguration configuration;

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(PDFOCRDocumentDialog::getSettingsGroup());
    const QByteArray configurationData = settings.value(QStringLiteral("configuration")).toByteArray();
    settings.endGroup();

    if (!configurationData.isEmpty())
    {
        configuration = pdf::PDFOCRConfiguration::fromJson(QJsonDocument::fromJson(configurationData).object());
    }

    if (configuration.languages.isEmpty())
    {
        configuration.languages << QStringLiteral("eng");
    }

    return configuration;
}

QString PDFOCRBackgroundTask::prepareConfiguration(pdf::PDFOCRConfiguration& configuration,
                                                   pdf::PDFOCRModelManager* modelManager,
                                                   QStringList* missingModels)
{
    if (missingModels)
    {
        missingModels->clear();
    }

    // One click always means: recognize the scanned pages and write the text into the PDF.
    // The pages with text are left alone, the images are never re-encoded and the page
    // specific settings of the dialog (perspective of a photo) are not used.
    configuration.existingTextPolicy = pdf::PDFOCRExistingTextPolicy::OnlyPagesWithoutText;
    configuration.compression = pdf::PDFOCRCompressionSettings();
    configuration.preprocessing.perspective.reset();
    configuration.preprocessing.deskew = true;

    if (configuration.languages.isEmpty())
    {
        configuration.languages << QStringLiteral("eng");
    }

    std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
    QString reason;
    if (!factory || !factory->isAvailable(&reason))
    {
        return tr("The OCR engine '%1' is not available. %2").arg(configuration.engineId, reason).trimmed();
    }

    if (factory->usesManagedModels())
    {
        if (!modelManager)
        {
            return tr("The language models cannot be found.");
        }

        QStringList missing = modelManager->getMissingModels(configuration.engineId, configuration.languages, configuration.profile);
        if (!missing.isEmpty() && configuration.profile != pdf::PDFOCRModelProfile::Fast &&
            modelManager->getMissingModels(configuration.engineId, configuration.languages, pdf::PDFOCRModelProfile::Fast).isEmpty())
        {
            // The more accurate models were not downloaded; the built-in fast models are used
            configuration.profile = pdf::PDFOCRModelProfile::Fast;
            configuration.modelSetId.clear();
            missing.clear();
        }

        if (!missing.isEmpty())
        {
            if (missingModels)
            {
                *missingModels = missing;
            }
            return tr("The language models are not installed: %1.").arg(missing.join(QStringLiteral(", ")));
        }

        // Scanned sheets are often fed sideways, the orientation is detected, when it is possible
        configuration.preprocessing.autoOrientation = modelManager->isOrientationDataUsable(configuration.engineId, configuration.profile);
    }

    const QStringList errors = configuration.validate();
    if (!errors.isEmpty())
    {
        return errors.join(QChar('\n'));
    }

    return QString();
}

bool PDFOCRBackgroundTask::isDrawingSheet(const pdf::PDFPage* page)
{
    if (!page)
    {
        return false;
    }

    const QRectF box = page->getRotatedCropBox();
    const double userUnit = page->getUserUnit() > 0.0 ? page->getUserUnit() : 1.0;
    const double longerSide = qMax(box.width(), box.height()) * userUnit;
    const double shorterSide = qMin(box.width(), box.height()) * userUnit;

    // ANSI B (11x17 inch) is 792 x 1224 points, A3 is 842 x 1191 points. Letter,
    // legal and A4 are smaller. The sheets of a drawing set are ANSI B to E / ARCH A to E.
    return longerSide >= 1190.0 && shorterSide >= 700.0;
}

PDFOCRBackgroundTask::WriteResult PDFOCRBackgroundTask::writeResults(const pdf::PDFDocument* document,
                                                                     const QString& fileName,
                                                                     const std::vector<pdf::PDFOCRPageResult>& results,
                                                                     const std::map<pdf::PDFInteger, pdf::PDFOCRPageAnalysis>* analysis,
                                                                     const std::map<pdf::PDFInteger, QByteArray>* fingerprints,
                                                                     const pdf::PDFOperationControl* operationControl)
{
    WriteResult writeResult;

    if (!document)
    {
        writeResult.errorMessage = tr("There is no document to write.");
        return writeResult;
    }

    const pdf::PDFOCRApplyProcessor::Context context = pdf::PDFOCRApplyProcessor::createContext(document, fileName);
    const QString permissionError = pdf::PDFOCRApplyProcessor::checkPermissions(context, pdf::PDFOCRApplyProcessor::OutputMode::ModifyDocument);
    if (!permissionError.isEmpty())
    {
        writeResult.errorMessage = permissionError;
        return writeResult;
    }

    pdf::PDFOCRApplyProcessor::Request request;
    request.outputMode = pdf::PDFOCRApplyProcessor::OutputMode::ModifyDocument;
    request.results = results;
    if (analysis)
    {
        request.analysis = *analysis;
    }
    if (fingerprints)
    {
        request.fingerprints = *fingerprints;
    }

    const pdf::PDFOCRApplyProcessor::Plan plan = pdf::PDFOCRApplyProcessor::createPlan(context, request);
    if (!plan.hasRequests())
    {
        // Blank pages, pages without any recognized word
        writeResult.nothingToWrite = true;
        writeResult.messages = plan.excluded;
        return writeResult;
    }

    const pdf::PDFOCRApplyProcessor::Result applyResult = pdf::PDFOCRApplyProcessor::execute(context, plan, operationControl);
    writeResult.messages << applyResult.report.messages;
    if (!applyResult.isSuccess())
    {
        writeResult.errorMessage = applyResult.errorMessage;
        return writeResult;
    }

    writeResult.document = applyResult.document;
    writeResult.writtenPages = int(applyResult.report.writtenPages.size());
    writeResult.nothingToWrite = !applyResult.document;
    return writeResult;
}

QString PDFOCRBackgroundTask::start(pdf::PDFDocumentPointer document,
                                    const QString& fileName,
                                    pdf::PDFCMSPointer cms,
                                    const pdf::PDFOCRConfiguration& configuration,
                                    pdf::PDFOCRModelManager* modelManager)
{
    if (isRunning())
    {
        return tr("Text recognition is already running.");
    }

    if (!document || document->getCatalog()->getPageCount() == 0)
    {
        return tr("The document has no page to recognize.");
    }

    // The permissions are checked before the work, so the user does not wait for nothing
    const pdf::PDFOCRApplyProcessor::Context context = pdf::PDFOCRApplyProcessor::createContext(document.data(), fileName);
    const QString permissionError = pdf::PDFOCRApplyProcessor::checkPermissions(context, pdf::PDFOCRApplyProcessor::OutputMode::ModifyDocument);
    if (!permissionError.isEmpty())
    {
        return permissionError;
    }

    // The pages are recognized in two groups: the large drawing sheets with the sparse
    // text layout (scattered labels, dimensions, title block), the other pages with the
    // layout of the configuration (paragraphs and tables of a spec sheet)
    struct PageGroup
    {
        std::vector<pdf::PDFInteger> pages;
        pdf::PDFOCRConfiguration configuration;
    };

    const pdf::PDFCatalog* catalog = document->getCatalog();
    const int pageCount = int(catalog->getPageCount());

    PageGroup documentPages;
    PageGroup drawingPages;
    documentPages.configuration = configuration;
    drawingPages.configuration = configuration;
    if (configuration.layout == pdf::PDFOCRLayout::Automatic || configuration.layout == pdf::PDFOCRLayout::AutomaticWithOrientation)
    {
        drawingPages.configuration.layout = pdf::PDFOCRLayout::SparseText;
    }

    for (int i = 0; i < pageCount; ++i)
    {
        (isDrawingSheet(catalog->getPage(size_t(i))) ? drawingPages : documentPages).pages.push_back(pdf::PDFInteger(i));
    }

    std::vector<PageGroup> groups;
    for (const PageGroup* group : { &documentPages, &drawingPages })
    {
        if (!group->pages.empty())
        {
            groups.push_back(*group);
        }
    }

    m_discarded = false;
    m_outcome = Outcome();
    m_cancelToken = std::make_shared<pdf::PDFOCRCancelToken>();
    m_runningOutcome = std::make_shared<Outcome>();
    m_runningOutcome->sourceDocument = document;
    m_runningOutcome->fileName = fileName;
    m_runningOutcome->pageCount = pageCount;
    m_runningOutcome->drawingPages = int(drawingPages.pages.size());

    std::shared_ptr<pdf::PDFOCRCancelToken> cancelToken = m_cancelToken;
    std::shared_ptr<Outcome> outcome = m_runningOutcome;

    // The runner needs a thread with an event dispatcher (the job controller delivers
    // its results by queued signals); the progress is posted into this object
    m_thread = QThread::create([this, document, fileName, cms, groups, cancelToken, outcome, modelManager, pageCount]()
    {
        QElapsedTimer timer;
        timer.start();

        try
        {
            std::map<pdf::PDFInteger, pdf::PDFOCRPageAnalysis> analysis;
            std::map<pdf::PDFInteger, QByteArray> fingerprints;

            // Pages of the following groups are counted as pages to recognize, until
            // their group is analyzed (so the progress does not run backwards much)
            int finishedBefore = 0;
            int remainingPages = pageCount;

            for (const PageGroup& group : groups)
            {
                if (cancelToken->isOperationCancelled())
                {
                    outcome->cancelled = true;
                    break;
                }

                remainingPages -= int(group.pages.size());

                pdf::PDFOCRDocumentRunner::Settings settings;
                settings.configuration = group.configuration;
                settings.pages = group.pages;
                settings.decisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;
                settings.allowReducedResolution = true;
                settings.modelManager = modelManager;
                settings.cms = cms.data();
                settings.fileName = fileName;

                auto progress = [this, finishedBefore, remainingPages](int finishedPages, int totalPages, const pdf::PDFOCRPageResult*)
                {
                    const int overallFinished = finishedBefore + finishedPages;
                    const int overallTotal = totalPages > 0 ? finishedBefore + totalPages + remainingPages : 0;
                    QMetaObject::invokeMethod(this, [this, overallFinished, overallTotal]() { Q_EMIT progressChanged(overallFinished, overallTotal); }, Qt::QueuedConnection);
                };

                const pdf::PDFOCRDocumentRunner::Result result = pdf::PDFOCRDocumentRunner::run(document.data(), settings, progress, cancelToken.get());
                if (result.cancelled || cancelToken->isOperationCancelled())
                {
                    outcome->cancelled = true;
                    break;
                }

                if (!result.errorMessage.isEmpty())
                {
                    outcome->errorMessage = result.errorMessage;
                    break;
                }

                outcome->details << result.warnings;
                outcome->failedPages += result.getFailedPageCount();
                for (const pdf::PDFOCRDocumentRunner::PageRecord& record : result.records)
                {
                    if (record.source == pdf::PDFOCRDocumentRunner::PageSource::Skipped)
                    {
                        ++outcome->skippedPages;
                    }
                    else if (record.source == pdf::PDFOCRDocumentRunner::PageSource::Recognized)
                    {
                        ++finishedBefore;
                    }

                    if ((record.state == pdf::PDFOCRPageState::Error || record.state == pdf::PDFOCRPageState::Cancelled) && !record.message.isEmpty())
                    {
                        outcome->details << tr("Page %1: %2").arg(record.pageIndex + 1).arg(record.message);
                    }
                }

                for (pdf::PDFInteger page : result.pages)
                {
                    const pdf::PDFOCRPageResult* pageResult = result.session ? result.session->getPage(page) : nullptr;
                    if (pageResult && pageResult->hasResult())
                    {
                        outcome->results.push_back(*pageResult);
                        ++outcome->recognizedPages;
                    }
                }

                analysis.insert(result.analysis.cbegin(), result.analysis.cend());
                fingerprints.insert(result.fingerprints.cbegin(), result.fingerprints.cend());

                const pdf::PDFOCRConfidenceStatistics statistics = result.getStatistics();
                outcome->wordCount += statistics.wordCount;
                outcome->reviewWordCount += statistics.reviewRequiredCount;
            }

            if (!outcome->cancelled && outcome->errorMessage.isEmpty() && !outcome->results.empty())
            {
                const int recognizedPages = outcome->recognizedPages;
                QMetaObject::invokeMethod(this, [this, recognizedPages]() { Q_EMIT progressChanged(recognizedPages, recognizedPages); }, Qt::QueuedConnection);

                const WriteResult writeResult = writeResults(document.data(), fileName, outcome->results, &analysis, &fingerprints, cancelToken.get());
                if (cancelToken->isOperationCancelled())
                {
                    outcome->cancelled = true;
                }
                else if (!writeResult.errorMessage.isEmpty())
                {
                    outcome->errorMessage = writeResult.errorMessage;
                }
                else
                {
                    outcome->resultDocument = writeResult.document;
                    outcome->writtenPages = writeResult.writtenPages;
                }
            }
        }
        catch (...)
        {
            outcome->errorMessage = pdf::PDFOCRDocumentRunner::getCurrentExceptionMessage();
        }

        outcome->elapsedMilliseconds = timer.elapsed();
    });

    connect(m_thread, &QThread::finished, this, &PDFOCRBackgroundTask::onThreadFinished, Qt::QueuedConnection);
    m_thread->start();
    return QString();
}

void PDFOCRBackgroundTask::cancel()
{
    if (isRunning())
    {
        // The pages are stopped cooperatively, nothing of the result is used
        m_discarded = true;
        m_cancelToken->cancel();
    }
}

bool PDFOCRBackgroundTask::isCancelling() const
{
    return isRunning() && m_cancelToken && m_cancelToken->isOperationCancelled();
}

PDFOCRBackgroundTask::Outcome PDFOCRBackgroundTask::takeOutcome()
{
    Outcome outcome = std::move(m_outcome);
    m_outcome = Outcome();
    return outcome;
}

void PDFOCRBackgroundTask::onThreadFinished()
{
    if (!m_thread)
    {
        return;
    }

    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;

    m_outcome = std::move(*m_runningOutcome);
    m_runningOutcome.reset();
    m_cancelToken.reset();

    if (m_discarded)
    {
        m_outcome.cancelled = true;
        m_outcome.resultDocument.reset();
        m_outcome.results.clear();
    }

    Q_EMIT finished();
}

}   // namespace pdfviewer
