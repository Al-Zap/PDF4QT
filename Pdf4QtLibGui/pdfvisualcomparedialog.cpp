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

#include "pdfvisualcomparedialog.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfprogress.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QGroupBox>
#include <QGridLayout>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QThread>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>

#include <numeric>

namespace pdfviewer
{

PDFVisualCompareDialog::PDFVisualCompareDialog(const pdf::PDFDocument* openDocument, const QString& openFileName, QWidget* parent) :
    BaseClass(parent),
    m_openDocument(openDocument),
    m_openFileName(openFileName)
{
    setWindowTitle(tr("Compare Documents"));
    setObjectName("PDFVisualCompareDialog");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* introLabel = new QLabel(tr("Overlay two revisions of a drawing. Content only in the older revision is shown in red, "
                                       "content only in the newer revision in green, unchanged content is gray. Each change gets a "
                                       "revision cloud and a bookmark in the report."), this);
    introLabel->setWordWrap(true);
    mainLayout->addWidget(introLabel);

    // Documents
    QGroupBox* documentsGroupBox = new QGroupBox(tr("Documents"), this);
    QGridLayout* documentsLayout = new QGridLayout(documentsGroupBox);
    documentsLayout->addWidget(new QLabel(tr("Pages"), documentsGroupBox), 0, 4);
    createRevisionRow(documentsLayout, 1, tr("Older revision"), "old", m_oldRevision);
    createRevisionRow(documentsLayout, 2, tr("Newer revision"), "new", m_newRevision);
    m_swapButton = new QPushButton(tr("Swap"), documentsGroupBox);
    m_swapButton->setObjectName("swapButton");
    m_swapButton->setToolTip(tr("Swap the older and the newer revision"));
    documentsLayout->addWidget(m_swapButton, 3, 0);
    documentsLayout->setColumnStretch(2, 1);
    mainLayout->addWidget(documentsGroupBox);

    // Settings
    QGroupBox* settingsGroupBox = new QGroupBox(tr("Settings"), this);
    QFormLayout* settingsLayout = new QFormLayout(settingsGroupBox);

    m_resolutionSpinBox = new QSpinBox(settingsGroupBox);
    m_resolutionSpinBox->setObjectName("resolutionSpinBox");
    m_resolutionSpinBox->setRange(50, 600);
    m_resolutionSpinBox->setSingleStep(25);
    m_resolutionSpinBox->setSuffix(tr(" ppi"));
    m_resolutionSpinBox->setToolTip(tr("Higher resolution finds smaller changes and gives a sharper report, but needs more time and memory."));
    settingsLayout->addRow(tr("Resolution"), m_resolutionSpinBox);

    m_alignmentComboBox = new QComboBox(settingsGroupBox);
    m_alignmentComboBox->setObjectName("alignmentComboBox");
    m_alignmentComboBox->addItem(tr("Automatic (find the best match)"), int(pdf::PDFVisualCompareSettings::Alignment::Automatic));
    m_alignmentComboBox->addItem(tr("Top left corners"), int(pdf::PDFVisualCompareSettings::Alignment::PageOrigin));
    m_alignmentComboBox->addItem(tr("Page centers"), int(pdf::PDFVisualCompareSettings::Alignment::PageCenter));
    settingsLayout->addRow(tr("Alignment"), m_alignmentComboBox);

    m_scaleToFitCheckBox = new QCheckBox(tr("Scale the older page to the size of the newer page"), settingsGroupBox);
    m_scaleToFitCheckBox->setObjectName("scaleToFitCheckBox");
    settingsLayout->addRow(QString(), m_scaleToFitCheckBox);

    m_toleranceSpinBox = new QSpinBox(settingsGroupBox);
    m_toleranceSpinBox->setObjectName("toleranceSpinBox");
    m_toleranceSpinBox->setRange(0, 8);
    m_toleranceSpinBox->setSuffix(tr(" px"));
    m_toleranceSpinBox->setToolTip(tr("Content moved by at most this distance is not a change."));
    settingsLayout->addRow(tr("Position tolerance"), m_toleranceSpinBox);

    m_colorToleranceSpinBox = new QSpinBox(settingsGroupBox);
    m_colorToleranceSpinBox->setObjectName("colorToleranceSpinBox");
    m_colorToleranceSpinBox->setRange(0, 255);
    m_colorToleranceSpinBox->setSpecialValueText(tr("Ignore colors"));
    m_colorToleranceSpinBox->setToolTip(tr("Content in both revisions is changed, when its color differs more. Zero ignores the color changes."));
    settingsLayout->addRow(tr("Color tolerance"), m_colorToleranceSpinBox);

    m_minimumSizeSpinBox = new QDoubleSpinBox(settingsGroupBox);
    m_minimumSizeSpinBox->setObjectName("minimumSizeSpinBox");
    m_minimumSizeSpinBox->setRange(0.0, 100.0);
    m_minimumSizeSpinBox->setDecimals(1);
    m_minimumSizeSpinBox->setSingleStep(0.5);
    m_minimumSizeSpinBox->setSuffix(tr(" mm"));
    m_minimumSizeSpinBox->setToolTip(tr("Smaller changes are ignored."));
    settingsLayout->addRow(tr("Smallest change"), m_minimumSizeSpinBox);

    m_mergeDistanceSpinBox = new QDoubleSpinBox(settingsGroupBox);
    m_mergeDistanceSpinBox->setObjectName("mergeDistanceSpinBox");
    m_mergeDistanceSpinBox->setRange(0.0, 200.0);
    m_mergeDistanceSpinBox->setDecimals(1);
    m_mergeDistanceSpinBox->setSuffix(tr(" mm"));
    m_mergeDistanceSpinBox->setToolTip(tr("Changes closer to each other are marked by a single cloud."));
    settingsLayout->addRow(tr("Group changes within"), m_mergeDistanceSpinBox);
    mainLayout->addWidget(settingsGroupBox);

    // Report
    QGroupBox* reportGroupBox = new QGroupBox(tr("Report"), this);
    QGridLayout* reportLayout = new QGridLayout(reportGroupBox);
    m_reportFileEdit = new QLineEdit(reportGroupBox);
    m_reportFileEdit->setObjectName("reportFileEdit");
    m_reportBrowseButton = new QPushButton(tr("Browse..."), reportGroupBox);
    m_reportBrowseButton->setObjectName("reportBrowseButton");
    m_openReportCheckBox = new QCheckBox(tr("Open the report when the comparison is finished"), reportGroupBox);
    m_openReportCheckBox->setObjectName("openReportCheckBox");
    reportLayout->addWidget(new QLabel(tr("Save as"), reportGroupBox), 0, 0);
    reportLayout->addWidget(m_reportFileEdit, 0, 1);
    reportLayout->addWidget(m_reportBrowseButton, 0, 2);
    reportLayout->addWidget(m_openReportCheckBox, 1, 1, 1, 2);
    mainLayout->addWidget(reportGroupBox);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("progressBar");
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_compareButton = m_buttonBox->addButton(tr("Compare"), QDialogButtonBox::AcceptRole);
    m_compareButton->setObjectName("compareButton");
    m_compareButton->setDefault(true);
    m_contentDiffButton = m_buttonBox->addButton(tr("Text and Content Diff..."), QDialogButtonBox::ActionRole);
    m_contentDiffButton->setObjectName("contentDiffButton");
    m_contentDiffButton->setToolTip(tr("Open both files in the PDF4QT Diff application, which compares texts, images and vector graphics object by object"));
    mainLayout->addWidget(m_buttonBox);

    // Defaults: the open document is the newer revision
    m_newRevision.sourceComboBox->setCurrentIndex(m_openDocument ? 1 : 0);
    m_oldRevision.sourceComboBox->setCurrentIndex(0);
    loadSettings();

    connect(m_swapButton, &QPushButton::clicked, this, &PDFVisualCompareDialog::swapRevisions);
    connect(m_reportBrowseButton, &QPushButton::clicked, this, &PDFVisualCompareDialog::browseReport);
    connect(m_contentDiffButton, &QPushButton::clicked, this, &PDFVisualCompareDialog::openContentDiff);
    connect(m_compareButton, &QPushButton::clicked, this, &PDFVisualCompareDialog::startComparison);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &PDFVisualCompareDialog::reject);
    connect(m_reportFileEdit, &QLineEdit::textEdited, this, [this]() { m_reportFileEdited = !m_reportFileEdit->text().isEmpty(); updateUi(); });

    for (Revision* revision : { &m_oldRevision, &m_newRevision })
    {
        connect(revision->sourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateDefaultReportFileName(); updateUi(); });
        connect(revision->fileEdit, &QLineEdit::textChanged, this, [this]() { updateDefaultReportFileName(); updateUi(); });
        connect(revision->browseButton, &QPushButton::clicked, this, [this, revision]() { browse(*revision); });
    }

    updateDefaultReportFileName();
    updateUi();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(760, 0));
    pdf::PDFWidgetUtils::style(this);
}

PDFVisualCompareDialog::~PDFVisualCompareDialog()
{
    if (m_thread)
    {
        m_cancelled = true;
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void PDFVisualCompareDialog::createRevisionRow(QGridLayout* layout, int row, const QString& label, const QString& name, Revision& revision)
{
    QWidget* parent = layout->parentWidget();
    revision.sourceComboBox = new QComboBox(parent);
    revision.sourceComboBox->setObjectName(name + "SourceComboBox");
    revision.sourceComboBox->addItem(tr("File"), int(Source::File));
    revision.sourceComboBox->addItem(tr("Open document"), int(Source::OpenDocument));
    revision.fileEdit = new QLineEdit(parent);
    revision.fileEdit->setObjectName(name + "FileEdit");
    revision.fileEdit->setPlaceholderText(tr("Choose a PDF file"));
    revision.fileEdit->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(parent, 280));
    revision.browseButton = new QPushButton(tr("Browse..."), parent);
    revision.browseButton->setObjectName(name + "BrowseButton");
    revision.pagesEdit = new QLineEdit(parent);
    revision.pagesEdit->setObjectName(name + "PagesEdit");
    revision.pagesEdit->setPlaceholderText(tr("All"));
    revision.pagesEdit->setToolTip(tr("Pages to compare, for example 1-3,5. The selected pages of both revisions are compared in pairs, in ascending order."));
    revision.pagesEdit->setMaximumWidth(pdf::PDFWidgetUtils::scaleDPI_x(parent, 90));

    layout->addWidget(new QLabel(label, parent), row, 0);
    layout->addWidget(revision.sourceComboBox, row, 1);
    layout->addWidget(revision.fileEdit, row, 2);
    layout->addWidget(revision.browseButton, row, 3);
    layout->addWidget(revision.pagesEdit, row, 4);
}

PDFVisualCompareDialog::Source PDFVisualCompareDialog::getSource(const Revision& revision) const
{
    if (!m_openDocument)
    {
        return Source::File;
    }

    return static_cast<Source>(revision.sourceComboBox->currentData().toInt());
}

QString PDFVisualCompareDialog::getFileName(const Revision& revision) const
{
    return getSource(revision) == Source::OpenDocument ? m_openFileName : revision.fileEdit->text().trimmed();
}

void PDFVisualCompareDialog::browse(Revision& revision)
{
    QString directory = QFileInfo(revision.fileEdit->text()).absolutePath();
    if (revision.fileEdit->text().isEmpty())
    {
        directory = QSettings().value("VisualCompare/directory", QFileInfo(m_openFileName).absolutePath()).toString();
    }

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Select Document"), directory, tr("Portable Document (*.pdf);;All files (*.*)"));
    if (!fileName.isEmpty())
    {
        revision.sourceComboBox->setCurrentIndex(0);
        revision.fileEdit->setText(QDir::toNativeSeparators(fileName));
        QSettings().setValue("VisualCompare/directory", QFileInfo(fileName).absolutePath());
    }
}

void PDFVisualCompareDialog::swapRevisions()
{
    const int oldSource = m_oldRevision.sourceComboBox->currentIndex();
    const QString oldFile = m_oldRevision.fileEdit->text();
    const QString oldPages = m_oldRevision.pagesEdit->text();

    m_oldRevision.sourceComboBox->setCurrentIndex(m_newRevision.sourceComboBox->currentIndex());
    m_oldRevision.fileEdit->setText(m_newRevision.fileEdit->text());
    m_oldRevision.pagesEdit->setText(m_newRevision.pagesEdit->text());

    m_newRevision.sourceComboBox->setCurrentIndex(oldSource);
    m_newRevision.fileEdit->setText(oldFile);
    m_newRevision.pagesEdit->setText(oldPages);
}

QString PDFVisualCompareDialog::getDefaultReportFileName(const QString& newFileName)
{
    if (newFileName.isEmpty())
    {
        return QString();
    }

    const QFileInfo info(newFileName);
    return QDir::toNativeSeparators(info.absoluteDir().filePath(tr("%1 - Compare.pdf").arg(info.completeBaseName())));
}

void PDFVisualCompareDialog::updateDefaultReportFileName()
{
    if (m_reportFileEdited)
    {
        return;
    }

    QString fileName = getFileName(m_newRevision);
    if (fileName.isEmpty())
    {
        fileName = getFileName(m_oldRevision);
    }
    m_reportFileEdit->setText(getDefaultReportFileName(fileName));
}

void PDFVisualCompareDialog::browseReport()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Save Report As"), m_reportFileEdit->text(), tr("Portable Document (*.pdf);;All files (*.*)"));
    if (!fileName.isEmpty())
    {
        m_reportFileEdit->setText(QDir::toNativeSeparators(fileName));
        m_reportFileEdited = true;
    }
}

void PDFVisualCompareDialog::updateUi()
{
    const bool running = isRunning();
    for (Revision* revision : { &m_oldRevision, &m_newRevision })
    {
        const bool isFile = getSource(*revision) == Source::File;
        revision->sourceComboBox->setEnabled(!running && m_openDocument);
        revision->fileEdit->setEnabled(!running && isFile);
        revision->browseButton->setEnabled(!running && isFile);
        revision->pagesEdit->setEnabled(!running);
        if (!isFile && revision->fileEdit->text() != m_openFileName)
        {
            const QSignalBlocker blocker(revision->fileEdit);
            revision->fileEdit->setText(QDir::toNativeSeparators(m_openFileName));
        }
    }

    const bool hasDocuments = !getFileName(m_oldRevision).isEmpty() || getSource(m_oldRevision) == Source::OpenDocument;
    const bool hasNewDocument = !getFileName(m_newRevision).isEmpty() || getSource(m_newRevision) == Source::OpenDocument;
    const bool hasReport = !m_reportFileEdit->text().trimmed().isEmpty();

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_swapButton, m_resolutionSpinBox, m_alignmentComboBox, m_scaleToFitCheckBox, m_toleranceSpinBox,
                                                            m_colorToleranceSpinBox, m_minimumSizeSpinBox, m_mergeDistanceSpinBox, m_reportFileEdit,
                                                            m_reportBrowseButton, m_openReportCheckBox })
    {
        widget->setEnabled(!running);
    }

    m_compareButton->setEnabled(!running && hasDocuments && hasNewDocument && hasReport);
    m_contentDiffButton->setEnabled(!running && !getFileName(m_oldRevision).isEmpty() && !getFileName(m_newRevision).isEmpty());
    m_progressBar->setVisible(running);
}

pdf::PDFVisualCompareSettings PDFVisualCompareDialog::getSettings() const
{
    pdf::PDFVisualCompareSettings settings;
    settings.resolution = m_resolutionSpinBox->value();
    settings.alignment = static_cast<pdf::PDFVisualCompareSettings::Alignment>(m_alignmentComboBox->currentData().toInt());
    settings.scaleToFit = m_scaleToFitCheckBox->isChecked();
    settings.tolerancePixels = m_toleranceSpinBox->value();
    settings.colorTolerance = m_colorToleranceSpinBox->value();
    settings.minimumChangeSizeMM = m_minimumSizeSpinBox->value();
    settings.mergeDistanceMM = m_mergeDistanceSpinBox->value();
    return settings;
}

QString PDFVisualCompareDialog::getReportFileName() const
{
    return m_reportFileName;
}

bool PDFVisualCompareDialog::isOpenReportRequested() const
{
    return m_openReportCheckBox->isChecked();
}

bool PDFVisualCompareDialog::isOperationCancelled() const
{
    return m_cancelled;
}

void PDFVisualCompareDialog::loadSettings()
{
    const pdf::PDFVisualCompareSettings defaults;
    QSettings settings;
    settings.beginGroup("VisualCompare");
    m_resolutionSpinBox->setValue(settings.value("resolution", int(defaults.resolution)).toInt());
    m_alignmentComboBox->setCurrentIndex(qMax(0, m_alignmentComboBox->findData(settings.value("alignment", int(defaults.alignment)).toInt())));
    m_scaleToFitCheckBox->setChecked(settings.value("scaleToFit", defaults.scaleToFit).toBool());
    m_toleranceSpinBox->setValue(settings.value("tolerance", defaults.tolerancePixels).toInt());
    m_colorToleranceSpinBox->setValue(settings.value("colorTolerance", defaults.colorTolerance).toInt());
    m_minimumSizeSpinBox->setValue(settings.value("minimumSize", defaults.minimumChangeSizeMM).toDouble());
    m_mergeDistanceSpinBox->setValue(settings.value("mergeDistance", defaults.mergeDistanceMM).toDouble());
    m_openReportCheckBox->setChecked(settings.value("openReport", true).toBool());
    settings.endGroup();
}

void PDFVisualCompareDialog::saveSettings()
{
    const pdf::PDFVisualCompareSettings current = getSettings();
    QSettings settings;
    settings.beginGroup("VisualCompare");
    settings.setValue("resolution", int(current.resolution));
    settings.setValue("alignment", int(current.alignment));
    settings.setValue("scaleToFit", current.scaleToFit);
    settings.setValue("tolerance", current.tolerancePixels);
    settings.setValue("colorTolerance", current.colorTolerance);
    settings.setValue("minimumSize", current.minimumChangeSizeMM);
    settings.setValue("mergeDistance", current.mergeDistanceMM);
    settings.setValue("openReport", m_openReportCheckBox->isChecked());
    settings.endGroup();
}

bool PDFVisualCompareDialog::getPages(const Revision& revision, const pdf::PDFDocument* document, std::vector<pdf::PDFInteger>& pages, QString* errorMessage) const
{
    return pdf::PDFVisualCompare::parsePages(revision.pagesEdit->text(), pdf::PDFInteger(document->getCatalog()->getPageCount()), pages, errorMessage);
}

void PDFVisualCompareDialog::showError(const QString& errorMessage)
{
    m_errorMessage = errorMessage;
    m_statusLabel->setText(errorMessage);
    m_statusLabel->setStyleSheet(errorMessage.isEmpty() ? QString() : QStringLiteral("color: #c00000;"));
    m_statusLabel->setVisible(!errorMessage.isEmpty());
}

void PDFVisualCompareDialog::startComparison()
{
    if (isRunning())
    {
        return;
    }

    showError(QString());
    m_result = pdf::PDFVisualCompareResult();
    m_oldDocument.reset();
    m_newDocument.reset();

    // Documents
    auto loadDocument = [this](const Revision& revision, std::optional<pdf::PDFDocument>& document) -> bool
    {
        if (getSource(revision) == Source::OpenDocument)
        {
            document = *m_openDocument;
            return true;
        }

        const QString fileName = revision.fileEdit->text().trimmed();
        if (fileName.isEmpty())
        {
            showError(tr("Choose both documents."));
            return false;
        }

        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        pdf::PDFDocument readDocument = reader.readFromFile(fileName);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            const QString reason = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled ? tr("The document is protected by a password.") : reader.getErrorMessage();
            showError(tr("Cannot open document '%1'. %2").arg(QFileInfo(fileName).fileName(), reason));
            return false;
        }

        document = std::move(readDocument);
        return true;
    };

    if (!loadDocument(m_oldRevision, m_oldDocument) || !loadDocument(m_newRevision, m_newDocument))
    {
        return;
    }

    std::vector<pdf::PDFInteger> oldPages;
    std::vector<pdf::PDFInteger> newPages;
    QString errorMessage;
    if (!getPages(m_oldRevision, &*m_oldDocument, oldPages, &errorMessage) || !getPages(m_newRevision, &*m_newDocument, newPages, &errorMessage))
    {
        showError(tr("Invalid page range. %1").arg(errorMessage));
        return;
    }

    if (oldPages.empty() && newPages.empty())
    {
        showError(tr("There are no pages to compare."));
        return;
    }

    // Report file must not overwrite the compared documents
    const QString reportFileName = m_reportFileEdit->text().trimmed();
    const QFileInfo reportInfo(reportFileName);
    for (const Revision* revision : { &m_oldRevision, &m_newRevision })
    {
        const QFileInfo info(getFileName(*revision));
        if (reportInfo.exists() && info.exists() && reportInfo.canonicalFilePath() == info.canonicalFilePath())
        {
            showError(tr("The report must be saved into another file than the compared documents."));
            return;
        }
    }

    const pdf::PDFVisualCompareSettings settings = getSettings();
    const pdf::PDFVisualCompare::PagePairs pairs = pdf::PDFVisualCompare::createPagePairs(oldPages, newPages);

    pdf::PDFVisualCompareReport::Information information;
    information.oldDocumentName = getSource(m_oldRevision) == Source::OpenDocument ? QFileInfo(m_openFileName).fileName() : QFileInfo(getFileName(m_oldRevision)).fileName();
    information.newDocumentName = getSource(m_newRevision) == Source::OpenDocument ? QFileInfo(m_openFileName).fileName() : QFileInfo(getFileName(m_newRevision)).fileName();
    information.dateTime = QDateTime::currentDateTime();

    m_cancelled = false;
    m_workerError.clear();
    m_reportFileName = reportFileName;

    m_progress = new pdf::PDFProgress(this);
    connect(m_progress, &pdf::PDFProgress::progressStep, m_progressBar, &QProgressBar::setValue, Qt::QueuedConnection);
    m_progressBar->setValue(0);
    m_statusLabel->setStyleSheet(QString());
    m_statusLabel->setText(tr("Comparing %1 pairs of pages...").arg(pairs.size()));
    m_statusLabel->setVisible(true);

    m_thread = QThread::create([this, settings, pairs, information, reportFileName]()
    {
        pdf::PDFVisualCompare compare(&*m_oldDocument, &*m_newDocument, settings);
        compare.setOperationControl(this);
        compare.setProgress(m_progress);

        pdf::PDFVisualCompareReport report(settings, information);
        m_result = compare.compare(pairs, [&report](const pdf::PDFVisualComparePageResult& page) { report.addPage(page); });

        if (!m_result.isSuccess())
        {
            return;
        }

        pdf::PDFDocument reportDocument = report.build(m_result);
        pdf::PDFDocumentWriter writer(nullptr);
        pdf::PDFOperationResult writeResult = writer.write(reportFileName, &reportDocument, true);
        if (!writeResult)
        {
            m_workerError = tr("Cannot write the report. %1").arg(writeResult.getErrorMessage());
        }
    });
    connect(m_thread, &QThread::finished, this, &PDFVisualCompareDialog::onThreadFinished, Qt::QueuedConnection);
    m_thread->start();
    updateUi();
}

void PDFVisualCompareDialog::onThreadFinished()
{
    if (!m_thread)
    {
        return;
    }

    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    delete m_progress;
    m_progress = nullptr;
    m_oldDocument.reset();
    m_newDocument.reset();
    updateUi();

    if (m_closeAfterCancel)
    {
        Q_EMIT comparisonFinished();
        BaseClass::reject();
        return;
    }

    if (m_result.cancelled)
    {
        showError(tr("The comparison was cancelled."));
        Q_EMIT comparisonFinished();
        return;
    }

    QString errorMessage = !m_workerError.isEmpty() ? m_workerError : m_result.errorMessage;
    if (!errorMessage.isEmpty())
    {
        showError(errorMessage);
        Q_EMIT comparisonFinished();
        return;
    }

    saveSettings();
    Q_EMIT comparisonFinished();
    accept();
}

void PDFVisualCompareDialog::reject()
{
    if (isRunning())
    {
        // Cancel the comparison first, the dialog is closed, when the worker stops
        m_cancelled = true;
        m_closeAfterCancel = true;
        m_statusLabel->setText(tr("Cancelling..."));
        return;
    }

    BaseClass::reject();
}

void PDFVisualCompareDialog::openContentDiff()
{
    const QString oldFileName = getFileName(m_oldRevision);
    const QString newFileName = getFileName(m_newRevision);

    QString program = QStandardPaths::findExecutable(QStringLiteral("Pdf4QtDiff"), { QCoreApplication::applicationDirPath() });
    if (program.isEmpty())
    {
        program = QStandardPaths::findExecutable(QStringLiteral("Pdf4QtDiff"));
    }

    if (program.isEmpty() || !QProcess::startDetached(program, { oldFileName, newFileName }))
    {
        showError(tr("Cannot start the PDF4QT Diff application."));
    }
}

}   // namespace pdfviewer
