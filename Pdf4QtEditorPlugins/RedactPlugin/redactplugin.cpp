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

#include "redactplugin.h"
#include "createredacteddocumentdialog.h"
#include "searchredactdialog.h"

#include "pdfdrawwidget.h"
#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfredact.h"
#include "pdfdocumentwriter.h"
#include "pdfselectpagesdialog.h"
#include "pdfcompiler.h"

#include <QAction>
#include <QFileInfo>
#include <QMessageBox>
#include <QGuiApplication>
#include <QCursor>

namespace pdfplugin
{

RedactPlugin::RedactPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionRedactRectangle(nullptr),
    m_actionRedactText(nullptr),
    m_actionRedactTextSelection(nullptr),
    m_actionRedactPage(nullptr),
    m_actionCreateRedactedDocument(nullptr),
    m_actionSearchRedact(nullptr),
    m_actionApplyRedactions(nullptr)
{

}

void RedactPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionRedactRectangle = new QAction(QIcon(":/pdfplugins/redactplugin/redact-rectangle.svg"), tr("Redact &Rectangle"), this);
    m_actionRedactText = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text.svg"), tr("Redact &Text"), this);
    m_actionRedactTextSelection = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text-selection.svg"), tr("Redact Text &Selection"), this);
    m_actionRedactPage = new QAction(QIcon(":/pdfplugins/redactplugin/redact-page.svg"), tr("Redact &Page(s)"), this);
    m_actionCreateRedactedDocument = new QAction(QIcon(":/pdfplugins/redactplugin/redact-create-document.svg"), tr("Create Redacted &Document"), this);
    m_actionSearchRedact = new QAction(QIcon(":/pdfplugins/redactplugin/redact-search.svg"), tr("Search && R&edact..."), this);
    m_actionApplyRedactions = new QAction(QIcon(":/pdfplugins/redactplugin/redact-apply.svg"), tr("&Apply Redactions"), this);

    m_actionSearchRedact->setToolTip(tr("Find words, phrases or patterns (social security numbers, phone numbers, e-mail addresses, credit card numbers, dates) and mark them for redaction"));
    m_actionApplyRedactions->setToolTip(tr("Remove the content under all redaction marks permanently (text, images and drawings) and fill the marked areas"));
    m_actionCreateRedactedDocument->setToolTip(tr("Save a copy of the document with all redaction marks applied"));

    m_actionRedactRectangle->setObjectName("redactplugin_RedactRectangle");
    m_actionRedactText->setObjectName("redactplugin_RedactText");
    m_actionRedactTextSelection->setObjectName("redactplugin_RedactTextSelection");
    m_actionRedactPage->setObjectName("redactplugin_RedactPage");
    m_actionCreateRedactedDocument->setObjectName("redactplugin_CreateRedactedDocument");
    m_actionSearchRedact->setObjectName("redactplugin_SearchRedact");
    m_actionApplyRedactions->setObjectName("redactplugin_ApplyRedactions");

    m_actionRedactRectangle->setCheckable(true);
    m_actionRedactText->setCheckable(true);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    pdf::PDFCreateRedactRectangleTool* redactRectangleTool = new pdf::PDFCreateRedactRectangleTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactRectangle, this);
    pdf::PDFCreateRedactTextTool* redactTextTool = new pdf::PDFCreateRedactTextTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactText, this);

    toolManager->addTool(redactRectangleTool);
    toolManager->addTool(redactTextTool);

    connect(m_actionRedactTextSelection, &QAction::triggered, this, &RedactPlugin::onRedactTextSelectionTriggered);
    connect(m_actionRedactPage, &QAction::triggered, this, &RedactPlugin::onRedactPageTriggered);
    connect(m_actionCreateRedactedDocument, &QAction::triggered, this, &RedactPlugin::onCreateRedactedDocumentTriggered);
    connect(m_actionSearchRedact, &QAction::triggered, this, &RedactPlugin::onSearchRedactTriggered);
    connect(m_actionApplyRedactions, &QAction::triggered, this, &RedactPlugin::onApplyRedactionsTriggered);

    updateActions();
}

void RedactPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    // Text layouts belong to a single version of the document
    m_textLayouts.reset();

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> RedactPlugin::getActions() const
{
    return { m_actionRedactRectangle, m_actionRedactText, m_actionSearchRedact, m_actionRedactTextSelection, m_actionRedactPage, m_actionApplyRedactions, m_actionCreateRedactedDocument };
}

QString RedactPlugin::getPluginMenuName() const
{
    return tr("Redac&t");
}

void RedactPlugin::updateActions()
{
    m_actionRedactTextSelection->setEnabled(m_document);
    m_actionRedactPage->setEnabled(m_document);
    m_actionCreateRedactedDocument->setEnabled(m_document);
    m_actionSearchRedact->setEnabled(m_document);
    m_actionApplyRedactions->setEnabled(m_document);
}

void RedactPlugin::onRedactTextSelectionTriggered()
{
    pdf::PDFTextSelection selectedText = m_dataExchangeInterface->getSelectedText();

    if (selectedText.isEmpty())
    {
        QMessageBox::information(m_widget, tr("Information"), tr("Select text via 'Advanced Search' tool, and then redact it using this tool. Select rows in 'Result' table to select particular results."));
        return;
    }

    const QColor redactColor = pdf::PDFCreateRedactRectangleTool::getRedactColor();

    pdf::PDFDocumentModifier modifier(m_document);

    for (auto it = selectedText.begin(); it != selectedText.end(); it = selectedText.nextPageRange(it))
    {
        const pdf::PDFTextSelectionColoredItem& item = *it;
        const pdf::PDFInteger pageIndex = item.start.pageIndex;

        pdf::PDFTextLayoutGetter textLayoutGetter = m_widget->getDrawWidgetProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);

        QPolygonF quadrilaterals;
        pdf::PDFTextSelectionPainter textSelectionPainter(&selectedText);
        QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

        if (!path.isEmpty())
        {
            pdf::PDFObjectReference page = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
            modifier.getBuilder()->createAnnotationRedact(page, quadrilaterals, redactColor, redactColor);
            modifier.markAnnotationsChanged();
        }
    }

    if (modifier.finalize())
    {
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void RedactPlugin::onRedactPageTriggered()
{
    pdf::PDFSelectPagesDialog dialog(tr("Redact Pages"), tr("Page Range to be Redacted"), m_document->getCatalog()->getPageCount(), m_widget->getDrawWidget()->getCurrentPages(), m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        std::vector<pdf::PDFInteger> selectedPages = dialog.getSelectedPages();

        if (selectedPages.empty())
        {
            // Jakub Melka: no pages are selected, just return
            return;
        }

        pdf::PDFDocumentModifier modifier(m_document);

        std::vector<pdf::PDFInteger> pageIndices;
        for (pdf::PDFInteger pageIndex : selectedPages)
        {
            pageIndices.push_back(pageIndex - 1);
        }

        pdf::PDFRedactSearch::createPageMarks(modifier.getBuilder(), m_document, pageIndices, pdf::PDFCreateRedactRectangleTool::getRedactColor());
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void RedactPlugin::onCreateRedactedDocumentTriggered()
{
    CreateRedactedDocumentDialog dialog(getRedactedFileName(), Qt::black, m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
        pdf::PDFRedact redactProcessor(m_document,
                                       m_widget->getDrawWidgetProxy()->getFontCache(),
                                       m_widget->getDrawWidgetProxy()->getIccProfileCache(),
                                       cms.data(),
                                       m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                       &m_widget->getDrawWidgetProxy()->getMeshQualitySettings(),
                                       dialog.getRedactColor());

        pdf::PDFRedact::Options options;
        options.setFlag(pdf::PDFRedact::CopyTitle, dialog.isCopyingTitle());
        options.setFlag(pdf::PDFRedact::CopyMetadata, dialog.isCopyingMetadata());
        options.setFlag(pdf::PDFRedact::CopyOutline, dialog.isCopyingOutline());

        QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        pdf::PDFDocument redactedDocument = redactProcessor.perform(options);
        QGuiApplication::restoreOverrideCursor();

        pdf::PDFDocumentWriter writer(m_widget->getDrawWidgetProxy()->getProgress());
        pdf::PDFOperationResult result = writer.write(dialog.getFileName(), &redactedDocument, false);
        if (!result)
        {
            QMessageBox::critical(m_widget, tr("Error"), result.getErrorMessage());
        }
    }
}

void RedactPlugin::onSearchRedactTriggered()
{
    SearchRedactDialog dialog([this]() { return getTextLayouts(); }, m_widget);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    pdf::PDFRedactSearch::Matches matches = dialog.getSelectedMatches();
    if (matches.empty())
    {
        return;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    if (pdf::PDFRedactSearch::createMarks(modifier.getBuilder(), m_document, matches, pdf::PDFCreateRedactRectangleTool::getRedactColor()) > 0)
    {
        modifier.markAnnotationsChanged();
    }

    if (modifier.finalize())
    {
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void RedactPlugin::onApplyRedactionsTriggered()
{
    const int markCount = pdf::PDFRedact::getRedactionMarkCount(m_document);
    if (markCount == 0)
    {
        QMessageBox::information(m_widget, tr("Apply Redactions"), tr("The document has no redaction marks. Mark the content to be removed first (Redact Rectangle, Redact Text, Search & Redact or Redact Page)."));
        return;
    }

    const QString question = tr("Apply %1 redaction mark(s)?<br><br>The text, images and drawings under the marks will be removed and the marked areas will be filled. "
                                "Annotations and form fields in the marked areas are removed too. You can undo this until the document is saved; "
                                "after saving, the removed content can't be recovered from the file.").arg(markCount);
    if (QMessageBox::question(m_widget, tr("Apply Redactions"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
    pdf::PDFRedact redactProcessor(m_document,
                                   m_widget->getDrawWidgetProxy()->getFontCache(),
                                   m_widget->getDrawWidgetProxy()->getIccProfileCache(),
                                   cms.data(),
                                   m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                   &m_widget->getDrawWidgetProxy()->getMeshQualitySettings(),
                                   QColor());

    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    pdf::PDFDocument redactedDocument = redactProcessor.perform(pdf::PDFRedact::KeepDocumentData);
    QGuiApplication::restoreOverrideCursor();

    pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(redactedDocument)));
    Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(pointer, nullptr, pdf::PDFModifiedDocument::Reset));

    const pdf::PDFRedact::Statistics& statistics = redactProcessor.getStatistics();
    QString message = tr("Applied %1 redaction mark(s) on %2 page(s). Removed %3 character(s), %4 image(s), %5 drawing(s) and %6 annotation(s); cut %7 image(s) and %8 drawing(s).")
                      .arg(statistics.markCount)
                      .arg(statistics.pageCount)
                      .arg(statistics.removedCharacterCount)
                      .arg(statistics.removedImageCount)
                      .arg(statistics.removedPathCount)
                      .arg(statistics.removedAnnotationCount)
                      .arg(statistics.redactedImageCount)
                      .arg(statistics.redactedPathCount + statistics.redactedShadingCount);

    if (!redactProcessor.getErrors().isEmpty())
    {
        QStringList errors = redactProcessor.getErrors();
        const qsizetype errorCount = errors.size();
        errors = errors.mid(0, 5);
        message += QString("<br><br>") + tr("Problems occurred while processing the page content (%1):").arg(errorCount) + QString("<br>") + errors.join("<br>");
        QMessageBox::warning(m_widget, tr("Apply Redactions"), message);
    }
    else
    {
        QMessageBox::information(m_widget, tr("Apply Redactions"), message);
    }
}

const pdf::PDFTextLayoutStorage* RedactPlugin::getTextLayouts()
{
    if (const pdf::PDFTextLayoutStorage* storage = m_widget->getDrawWidgetProxy()->getTextLayoutCompiler()->getTextLayoutStorage())
    {
        return storage;
    }

    if (!m_textLayouts)
    {
        pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
        pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
        proxy->getFontCache()->setCacheShrinkEnabled(this, false);
        m_textLayouts = pdf::PDFRedactSearch::createTextLayouts(m_document, proxy->getFontCache(), proxy->getIccProfileCache(), cms.data(), proxy->getOptionalContentActivity(), proxy->getMeshQualitySettings());
        proxy->getFontCache()->setCacheShrinkEnabled(this, true);
    }

    return &m_textLayouts.value();
}

QString RedactPlugin::getRedactedFileName() const
{
    QFileInfo fileInfo(m_dataExchangeInterface->getOriginalFileName());

    return fileInfo.path() + "/" + fileInfo.baseName() + "_REDACTED.pdf";
}

}
