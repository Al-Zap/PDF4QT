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


#include "pdfocrmakesearchable.h"
#include "pdfocrbackgroundtask.h"
#include "pdfocrlanguagesdialog.h"
#include "pdfocrapplyprocessor.h"
#include "pdfocrmodelmanager.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QTimer>
#include <QStatusBar>
#include <QMainWindow>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QToolButton>
#include <QApplication>
#include <QProgressBar>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFOCRMakeSearchable::PDFOCRMakeSearchable(QMainWindow* mainWindow, DocumentProvider documentProvider, QObject* parent) :
    BaseClass(parent),
    m_mainWindow(mainWindow),
    m_documentProvider(std::move(documentProvider)),
    m_modelManager(new pdf::PDFOCRModelManager(this)),
    m_task(new PDFOCRBackgroundTask(this)),
    m_hideTimer(new QTimer(this))
{
    m_modelManager->loadBundledCatalog();
    m_modelManager->refresh();

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(30000);
    connect(m_hideTimer, &QTimer::timeout, this, &PDFOCRMakeSearchable::hideStatus);

    connect(m_task, &PDFOCRBackgroundTask::progressChanged, this, &PDFOCRMakeSearchable::onProgressChanged);
    connect(m_task, &PDFOCRBackgroundTask::finished, this, &PDFOCRMakeSearchable::onFinished);

    if (m_mainWindow)
    {
        m_statusWidget = new QWidget(m_mainWindow);
        m_statusWidget->setObjectName(QStringLiteral("makeSearchableStatusWidget"));
        QHBoxLayout* layout = new QHBoxLayout(m_statusWidget);
        layout->setContentsMargins(0, 0, 0, 0);

        m_statusLabel = new QLabel(m_statusWidget);
        m_statusLabel->setObjectName(QStringLiteral("makeSearchableStatusLabel"));

        m_progressBar = new QProgressBar(m_statusWidget);
        m_progressBar->setObjectName(QStringLiteral("makeSearchableProgressBar"));
        m_progressBar->setTextVisible(false);
        m_progressBar->setMaximumWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_statusWidget.data(), 140));
        m_progressBar->setMaximumHeight(pdf::PDFWidgetUtils::scaleDPI_y(m_statusWidget.data(), 14));

        m_cancelButton = new QToolButton(m_statusWidget);
        m_cancelButton->setObjectName(QStringLiteral("makeSearchableCancelButton"));
        m_cancelButton->setText(tr("Stop"));
        m_cancelButton->setToolTip(tr("Stop the text recognition. The document is not changed."));
        m_cancelButton->setAutoRaise(true);
        connect(m_cancelButton, &QToolButton::clicked, this, &PDFOCRMakeSearchable::onCancelClicked);

        m_closeButton = new QToolButton(m_statusWidget);
        m_closeButton->setObjectName(QStringLiteral("makeSearchableCloseButton"));
        m_closeButton->setText(QStringLiteral("✕"));
        m_closeButton->setToolTip(tr("Hide"));
        m_closeButton->setAutoRaise(true);
        connect(m_closeButton, &QToolButton::clicked, this, &PDFOCRMakeSearchable::hideStatus);

        layout->addWidget(m_statusLabel);
        layout->addWidget(m_progressBar);
        layout->addWidget(m_cancelButton);
        layout->addWidget(m_closeButton);

        m_mainWindow->statusBar()->addPermanentWidget(m_statusWidget);
        m_statusWidget->hide();
    }
}

PDFOCRMakeSearchable::~PDFOCRMakeSearchable()
{
    // The task is destroyed first (it waits for its worker), the model manager after it
    delete m_task;
    m_task = nullptr;
}

bool PDFOCRMakeSearchable::isRunning() const
{
    return m_task && m_task->isRunning();
}

void PDFOCRMakeSearchable::start(const QString& fileName, pdf::PDFCMSPointer cms)
{
    if (isRunning())
    {
        return;
    }

    const QString title = tr("Make Searchable");
    pdf::PDFDocumentPointer document = m_documentProvider ? m_documentProvider() : pdf::PDFDocumentPointer();
    if (!document)
    {
        return;
    }

    if (pdf::PDFOCRApplyProcessor::hasSignatureFields(document.data()) &&
        QMessageBox::question(m_mainWindow, title, tr("The document is signed. The searchable text is added as a change, which the existing signatures do not cover. Do you want to continue?")) != QMessageBox::Yes)
    {
        return;
    }

    m_modelManager->refresh();

    pdf::PDFOCRConfiguration configuration = PDFOCRBackgroundTask::loadLastConfiguration();
    QStringList missingModels;
    QString errorMessage = PDFOCRBackgroundTask::prepareConfiguration(configuration, m_modelManager, &missingModels);
    if (!errorMessage.isEmpty())
    {
        if (!missingModels.isEmpty())
        {
            if (QMessageBox::question(m_mainWindow, title, tr("%1\n\nDo you want to install them now?").arg(errorMessage)) == QMessageBox::Yes)
            {
                PDFOCRLanguagesDialog dialog(m_modelManager, m_mainWindow);
                dialog.exec();
                m_modelManager->refresh();
            }
        }
        else
        {
            QMessageBox::warning(m_mainWindow, title, tr("The text cannot be recognized.\n\n%1").arg(errorMessage));
        }
        return;
    }

    errorMessage = m_task->start(document, fileName, std::move(cms), configuration, m_modelManager);
    if (!errorMessage.isEmpty())
    {
        QMessageBox::warning(m_mainWindow, title, tr("The text cannot be recognized.\n\n%1").arg(errorMessage));
        return;
    }

    showStatus(tr("Making searchable: analyzing pages..."), true);
    Q_EMIT runningChanged(true);
}

void PDFOCRMakeSearchable::cancel()
{
    if (isRunning())
    {
        m_task->cancel();
        showStatus(tr("Stopping text recognition..."), true);
    }
}

void PDFOCRMakeSearchable::onCancelClicked()
{
    cancel();
}

void PDFOCRMakeSearchable::onProgressChanged(int finished, int total)
{
    if (!isRunning() || m_task->isCancelling() || !m_progressBar)
    {
        return;
    }

    if (total <= 0)
    {
        showStatus(tr("Making searchable: analyzing pages..."), true);
        m_progressBar->setRange(0, 0);
    }
    else if (finished >= total)
    {
        showStatus(tr("Making searchable: adding the text layer..."), true);
        m_progressBar->setRange(0, 0);
    }
    else
    {
        showStatus(tr("Making searchable: %1 of %2 pages").arg(finished).arg(total), true);
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(finished);
    }
}

void PDFOCRMakeSearchable::onFinished()
{
    PDFOCRBackgroundTask::Outcome outcome = m_task->takeOutcome();
    Q_EMIT runningChanged(false);

    if (outcome.cancelled)
    {
        showStatus(tr("Text recognition was stopped, the document was not changed."), false);
        return;
    }

    if (!outcome.errorMessage.isEmpty())
    {
        hideStatus();
        QMessageBox::warning(m_mainWindow, tr("Make Searchable"), tr("The text was not recognized.\n\n%1").arg(outcome.errorMessage));
        return;
    }

    pdf::PDFDocumentPointer resultDocument = outcome.resultDocument;
    int writtenPages = outcome.writtenPages;

    pdf::PDFDocumentPointer currentDocument = m_documentProvider ? m_documentProvider() : pdf::PDFDocumentPointer();
    if (currentDocument != outcome.sourceDocument)
    {
        // The document was changed during the recognition (an edit, undo/redo). The text is
        // written into the current revision; the pages, whose content changed, are left out.
        resultDocument.reset();
        writtenPages = 0;

        if (currentDocument && !outcome.results.empty())
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const PDFOCRBackgroundTask::WriteResult writeResult = PDFOCRBackgroundTask::writeResults(currentDocument.data(), outcome.fileName, outcome.results, nullptr, nullptr, nullptr);
            QApplication::restoreOverrideCursor();

            resultDocument = writeResult.document;
            writtenPages = writeResult.writtenPages;
            if (!writeResult.errorMessage.isEmpty())
            {
                outcome.details << writeResult.errorMessage;
            }
        }
    }

    if (resultDocument)
    {
        Q_EMIT documentReady(resultDocument);
    }

    QStringList summary;
    if (writtenPages > 0)
    {
        summary << tr("Searchable text added to %n page(s). Save the document to keep it.", nullptr, writtenPages);
        if (outcome.reviewWordCount > 0)
        {
            summary << tr("%n word(s) have low confidence (Recognize Text shows them for review).", nullptr, outcome.reviewWordCount);
        }
    }
    else if (outcome.recognizedPages == 0 && outcome.failedPages == 0 && outcome.skippedPages > 0)
    {
        summary << tr("The pages already have searchable text, nothing was changed.");
    }
    else
    {
        summary << tr("No text was found to add, the document was not changed.");
    }

    if (outcome.failedPages > 0)
    {
        summary << tr("%n page(s) failed.", nullptr, outcome.failedPages);
    }

    showStatus(summary.join(QChar(' ')), false);

    if (m_statusWidget)
    {
        QStringList toolTip;
        toolTip << tr("Pages: %1, recognized: %2, with the text added: %3, skipped (with text): %4.").arg(outcome.pageCount).arg(outcome.recognizedPages).arg(writtenPages).arg(outcome.skippedPages);
        if (outcome.drawingPages > 0)
        {
            toolTip << tr("%n large sheet(s) recognized as drawings (sparse text).", nullptr, outcome.drawingPages);
        }
        toolTip << tr("Words: %1, time: %2 s.").arg(outcome.wordCount).arg(QString::number(outcome.elapsedMilliseconds / 1000.0, 'f', 1));
        toolTip << outcome.details;
        m_statusWidget->setToolTip(toolTip.join(QChar('\n')));
    }
}

void PDFOCRMakeSearchable::showStatus(const QString& text, bool running)
{
    if (!m_statusWidget || !m_statusLabel || !m_progressBar || !m_cancelButton || !m_closeButton)
    {
        return;
    }

    m_statusLabel->setText(text);
    m_progressBar->setVisible(running);
    m_cancelButton->setVisible(running);
    m_cancelButton->setEnabled(running && !m_task->isCancelling());
    m_closeButton->setVisible(!running);

    if (running)
    {
        m_hideTimer->stop();
        m_statusWidget->setToolTip(QString());
        if (!m_statusWidget->isVisible())
        {
            m_progressBar->setRange(0, 0);
        }
    }
    else
    {
        m_hideTimer->start();
    }

    m_statusWidget->show();
}

void PDFOCRMakeSearchable::hideStatus()
{
    m_hideTimer->stop();

    if (m_statusWidget && !isRunning())
    {
        m_statusWidget->hide();
    }
}

}   // namespace pdfviewer
