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

#include "pdfsplitviewwidget.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfdocumentreader.h"
#include "pdfoptionalcontent.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QToolButton>
#include <QScrollBar>
#include <QBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFSplitViewWidget::PDFSplitViewWidget(pdf::PDFWidget* mainWidget, QWidget* parent) :
    BaseClass(parent),
    m_mainWidget(mainWidget),
    m_pdfWidget(nullptr),
    m_annotationManager(nullptr),
    m_titleLabel(nullptr),
    m_sameDocumentButton(nullptr),
    m_openFileButton(nullptr),
    m_synchronizeCheckBox(nullptr),
    m_closeButton(nullptr),
    m_otherOptionalContentActivity(nullptr),
    m_isSynchronizing(false)
{
    Q_ASSERT(m_mainWidget);

    setObjectName("splitViewWidget");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* bar = new QWidget(this);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    const int margin = pdf::PDFWidgetUtils::scaleDPI_x(this, 4);
    barLayout->setContentsMargins(margin, margin / 2, margin, margin / 2);

    m_titleLabel = new QLabel(bar);
    m_titleLabel->setObjectName("splitViewTitleLabel");
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_sameDocumentButton = new QToolButton(bar);
    m_sameDocumentButton->setObjectName("splitViewSameDocumentButton");
    m_sameDocumentButton->setText(tr("Same Document"));
    m_sameDocumentButton->setToolTip(tr("Show another place of the document of the main pane"));
    m_sameDocumentButton->setCheckable(true);
    m_sameDocumentButton->setAutoRaise(true);

    m_openFileButton = new QToolButton(bar);
    m_openFileButton->setObjectName("splitViewOpenFileButton");
    m_openFileButton->setText(tr("Open File..."));
    m_openFileButton->setToolTip(tr("Show another file next to the document, for example the previous revision of a sheet"));
    m_openFileButton->setAutoRaise(true);

    m_synchronizeCheckBox = new QCheckBox(tr("Sync Scrolling"), bar);
    m_synchronizeCheckBox->setObjectName("splitViewSynchronizeCheckBox");
    m_synchronizeCheckBox->setToolTip(tr("Scroll and zoom both panes together"));

    m_closeButton = new QToolButton(bar);
    m_closeButton->setObjectName("splitViewCloseButton");
    m_closeButton->setText(QStringLiteral("✕"));
    m_closeButton->setToolTip(tr("Close the split view"));
    m_closeButton->setAutoRaise(true);

    barLayout->addWidget(m_titleLabel, 1);
    barLayout->addWidget(m_sameDocumentButton);
    barLayout->addWidget(m_openFileButton);
    barLayout->addWidget(m_synchronizeCheckBox);
    barLayout->addWidget(m_closeButton);
    layout->addWidget(bar);

    pdf::PDFDrawWidgetProxy* mainProxy = m_mainWidget->getDrawWidgetProxy();
    m_pdfWidget = new pdf::PDFWidget(m_mainWidget->getCMSManager(), mainProxy->getRendererEngine(), this);
    m_pdfWidget->setObjectName("splitViewPdfWidget");
    m_pdfWidget->getDrawWidgetProxy()->setFeatures(mainProxy->getFeatures());
    m_pdfWidget->getDrawWidgetProxy()->setPageLayout(mainProxy->getPageLayout());
    layout->addWidget(m_pdfWidget, 1);

    // Annotations are drawn, but they are not edited in this pane: the annotation
    // manager does not receive the input of the widget.
    m_annotationManager = new pdf::PDFWidgetAnnotationManager(m_pdfWidget->getDrawWidgetProxy(), this);
    m_pdfWidget->setAnnotationManager(m_annotationManager);
    m_pdfWidget->removeInputInterface(m_annotationManager);

    connect(m_sameDocumentButton, &QToolButton::clicked, this, &PDFSplitViewWidget::showMainDocument);
    connect(m_openFileButton, &QToolButton::clicked, this, &PDFSplitViewWidget::onOpenFileClicked);
    connect(m_closeButton, &QToolButton::clicked, this, &PDFSplitViewWidget::closeRequested);
    connect(m_synchronizeCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (checked)
        {
            onSourceViewChanged(m_mainWidget, m_pdfWidget);
        }
    });

    auto connectWidgets = [this](pdf::PDFWidget* source, pdf::PDFWidget* target)
    {
        auto onChanged = [this, source, target]() { onSourceViewChanged(source, target); };
        connect(source->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, onChanged);
        connect(source->getHorizontalScrollbar(), &QScrollBar::valueChanged, this, onChanged);
        connect(source->getVerticalScrollbar(), &QScrollBar::valueChanged, this, onChanged);
    };
    connectWidgets(m_mainWidget, m_pdfWidget);
    connectWidgets(m_pdfWidget, m_mainWidget);
    connect(mainProxy, &pdf::PDFDrawWidgetProxy::pageLayoutChanged, this, [this]()
    {
        m_pdfWidget->getDrawWidgetProxy()->setPageLayout(m_mainWidget->getDrawWidgetProxy()->getPageLayout());
    });

    updateTitle();
}

PDFSplitViewWidget::~PDFSplitViewWidget()
{
    // The annotation manager is registered in the proxy of the widget,
    // so it is destroyed before the widget.
    m_pdfWidget->setAnnotationManager(nullptr);
    delete m_annotationManager;
    m_annotationManager = nullptr;

    m_pdfWidget->setDocument(pdf::PDFModifiedDocument(), {});
}

void PDFSplitViewWidget::setMainDocument(const pdf::PDFModifiedDocument& document, const QString& fileName)
{
    m_mainDocument = document;
    m_mainFileName = fileName;

    if (isShowingMainDocument())
    {
        if (document.getDocument() && !document.hasReset() && m_pdfWidget->getDrawWidgetProxy()->getDocument())
        {
            // Edits of the document, the view of the pane is kept
            setPaneDocument(document);
        }
        else if (document.getDocument())
        {
            setPaneDocument(pdf::PDFModifiedDocument(document.getDocument(), document.getOptionalContentActivity()));
        }
        else
        {
            setPaneDocument(pdf::PDFModifiedDocument());
        }
    }

    updateTitle();
}

void PDFSplitViewWidget::showMainDocument()
{
    const bool wasShowingOtherDocument = !isShowingMainDocument();

    if (pdf::PDFDocument* document = m_mainDocument.getDocument())
    {
        setPaneDocument(pdf::PDFModifiedDocument(document, m_mainDocument.getOptionalContentActivity()));
    }
    else
    {
        setPaneDocument(pdf::PDFModifiedDocument());
    }

    // The other document is released after the widget does not show it
    m_otherDocument.reset();
    m_otherFileName.clear();
    delete m_otherOptionalContentActivity;
    m_otherOptionalContentActivity = nullptr;

    if (wasShowingOtherDocument)
    {
        // The same document shown in both panes is not scrolled together by default
        m_synchronizeCheckBox->setChecked(false);
    }

    updateTitle();
}

bool PDFSplitViewWidget::openFile(const QString& fileName, QString* errorMessage)
{
    auto queryPassword = [this, &fileName](bool* ok) -> QString
    {
        return QInputDialog::getText(this, tr("Encrypted Document"), tr("Enter password to access document '%1'").arg(QFileInfo(fileName).fileName()), QLineEdit::Password, QString(), ok);
    };

    pdf::PDFDocumentReader reader(nullptr, queryPassword, true, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);

    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        if (errorMessage)
        {
            *errorMessage = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled ? QString() : reader.getErrorMessage();
        }
        return false;
    }

    pdf::PDFDocumentPointer oldDocument = m_otherDocument;
    pdf::PDFOptionalContentActivity* oldOptionalContentActivity = m_otherOptionalContentActivity;

    m_otherDocument.reset(new pdf::PDFDocument(std::move(document)));
    m_otherOptionalContentActivity = new pdf::PDFOptionalContentActivity(m_otherDocument.data(), pdf::OCUsage::View, this);
    m_otherFileName = fileName;
    setPaneDocument(pdf::PDFModifiedDocument(m_otherDocument.data(), m_otherOptionalContentActivity));

    delete oldOptionalContentActivity;
    oldDocument.reset();

    // Revisions of a sheet are compared at the same place
    if (m_synchronizeCheckBox->isChecked())
    {
        onSourceViewChanged(m_mainWidget, m_pdfWidget);
    }
    else
    {
        m_synchronizeCheckBox->setChecked(true);
    }

    updateTitle();
    return true;
}

QString PDFSplitViewWidget::getFileName() const
{
    return isShowingMainDocument() ? m_mainFileName : m_otherFileName;
}

bool PDFSplitViewWidget::isScrollingSynchronized() const
{
    return m_synchronizeCheckBox->isChecked();
}

void PDFSplitViewWidget::setScrollingSynchronized(bool synchronized)
{
    m_synchronizeCheckBox->setChecked(synchronized);
}

void PDFSplitViewWidget::synchronizeWidgets(pdf::PDFWidget* source, pdf::PDFWidget* target)
{
    pdf::PDFDrawWidgetProxy* sourceProxy = source->getDrawWidgetProxy();
    pdf::PDFDrawWidgetProxy* targetProxy = target->getDrawWidgetProxy();

    if (!sourceProxy->getDocument() || !targetProxy->getDocument())
    {
        return;
    }

    if (!qFuzzyCompare(sourceProxy->getZoom(), targetProxy->getZoom()))
    {
        targetProxy->zoom(sourceProxy->getZoom());
    }

    target->getHorizontalScrollbar()->setValue(source->getHorizontalScrollbar()->value());
    target->getVerticalScrollbar()->setValue(source->getVerticalScrollbar()->value());
}

void PDFSplitViewWidget::onOpenFileClicked()
{
    QString directory = QFileInfo(getFileName()).absolutePath();
    if (getFileName().isEmpty())
    {
        directory = QFileInfo(m_mainFileName).absolutePath();
    }

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open File in the Split View"), directory, tr("PDF document (*.pdf)"));
    if (fileName.isEmpty())
    {
        return;
    }

    QString errorMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool opened = openFile(fileName, &errorMessage);
    QApplication::restoreOverrideCursor();

    if (!opened && !errorMessage.isEmpty())
    {
        QMessageBox::critical(this, tr("Split View"), tr("Document '%1' cannot be opened: %2").arg(QFileInfo(fileName).fileName(), errorMessage));
    }
}

void PDFSplitViewWidget::onSourceViewChanged(pdf::PDFWidget* source, pdf::PDFWidget* target)
{
    if (m_isSynchronizing || !m_synchronizeCheckBox->isChecked() || !isVisible())
    {
        return;
    }

    m_isSynchronizing = true;
    synchronizeWidgets(source, target);
    m_isSynchronizing = false;
}

void PDFSplitViewWidget::setPaneDocument(const pdf::PDFModifiedDocument& document)
{
    m_annotationManager->setDocument(document);
    m_pdfWidget->setDocument(document, {});
}

void PDFSplitViewWidget::updateTitle()
{
    const QString fileName = getFileName();
    if (isShowingMainDocument())
    {
        m_titleLabel->setText(m_mainDocument.getDocument() ? tr("Same document") : tr("No document"));
    }
    else
    {
        m_titleLabel->setText(QFileInfo(fileName).fileName());
    }
    m_titleLabel->setToolTip(QDir::toNativeSeparators(fileName));
    m_sameDocumentButton->setChecked(isShowingMainDocument());
}

}   // namespace pdfviewer
