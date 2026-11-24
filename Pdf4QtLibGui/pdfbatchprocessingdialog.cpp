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



#include "pdfbatchprocessingdialog.h"
#include "pdfpagestampdialog.h"
#include "pdfocrmodelmanager.h"
#include "pdfwidgetutils.h"

#include <QDir>
#include <QMenu>
#include <QFile>
#include <QLabel>
#include <QThread>
#include <QSpinBox>
#include <QMimeData>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QFileInfo>
#include <QGroupBox>
#include <QDropEvent>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QImageWriter>
#include <QRadioButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QRegularExpression>

#include "pdfdbgheap.h"

namespace pdfviewer
{

// -------------------------------------------------------------------------
// PDFBatchActionDialog
// -------------------------------------------------------------------------

PDFBatchActionDialog::PDFBatchActionDialog(const pdf::PDFBatchAction& action, QWidget* parent) :
    BaseClass(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
    m_action(action)
{
    using Type = pdf::PDFBatchAction::Type;

    setWindowTitle(action.getName());
    setObjectName("PDFBatchActionDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();
    layout->addLayout(formLayout);

    switch (action.type)
    {
        case Type::RemoveStamps:
            m_removeHeaderFooterCheckBox = new QCheckBox(tr("Headers, footers and Bates numbers"), this);
            m_removeHeaderFooterCheckBox->setObjectName("removeHeaderFooterCheckBox");
            m_removeHeaderFooterCheckBox->setChecked(action.removedStamps.testFlag(pdf::PDFPageStamper::HeaderFooter));
            m_removeWatermarkCheckBox = new QCheckBox(tr("Watermarks"), this);
            m_removeWatermarkCheckBox->setObjectName("removeWatermarkCheckBox");
            m_removeWatermarkCheckBox->setChecked(action.removedStamps.testFlag(pdf::PDFPageStamper::Watermark));
            formLayout->addRow(tr("Remove:"), m_removeHeaderFooterCheckBox);
            formLayout->addRow(QString(), m_removeWatermarkCheckBox);
            formLayout->addRow(new QLabel(tr("Only the stamps added by this application are removed."), this));
            break;

        case Type::RotatePages:
            m_rotationComboBox = new QComboBox(this);
            m_rotationComboBox->setObjectName("rotationComboBox");
            m_rotationComboBox->addItem(tr("90° clockwise"), 90);
            m_rotationComboBox->addItem(tr("180°"), 180);
            m_rotationComboBox->addItem(tr("90° counterclockwise"), 270);
            m_rotationComboBox->setCurrentIndex(qMax(0, m_rotationComboBox->findData(action.rotation)));
            formLayout->addRow(tr("Rotation:"), m_rotationComboBox);
            break;

        case Type::RecognizeText:
        {
            m_languagesEdit = new QLineEdit(action.ocrConfiguration.languages.join(QChar('+')), this);
            m_languagesEdit->setObjectName("languagesEdit");
            m_languagesEdit->setToolTip(tr("Languages of the text separated by '+', for example eng+deu. The languages are installed in Convert > Languages."));
            formLayout->addRow(tr("Languages:"), m_languagesEdit);

            m_profileComboBox = new QComboBox(this);
            m_profileComboBox->setObjectName("profileComboBox");
            for (pdf::PDFOCRModelProfile profile : pdf::PDFOCRConfiguration::getProfiles())
            {
                m_profileComboBox->addItem(pdf::PDFOCRConfiguration::getProfileName(profile), int(profile));
            }
            m_profileComboBox->setCurrentIndex(qMax(0, m_profileComboBox->findData(int(action.ocrConfiguration.profile))));
            formLayout->addRow(tr("Models:"), m_profileComboBox);

            m_existingTextComboBox = new QComboBox(this);
            m_existingTextComboBox->setObjectName("existingTextComboBox");
            m_existingTextComboBox->addItem(tr("Only pages without text"), int(pdf::PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
            m_existingTextComboBox->addItem(tr("Replace the text layer of PDF4QT"), int(pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer));
            m_existingTextComboBox->setCurrentIndex(qMax(0, m_existingTextComboBox->findData(int(action.ocrConfiguration.existingTextPolicy))));
            formLayout->addRow(tr("Existing text:"), m_existingTextComboBox);

            m_decisionComboBox = new QComboBox(this);
            m_decisionComboBox->setObjectName("decisionComboBox");
            m_decisionComboBox->addItem(tr("Skip pages with both text and images"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::Skip));
            m_decisionComboBox->addItem(tr("Recognize scans with a little text (the text is masked)"), int(pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText));
            m_decisionComboBox->setCurrentIndex(qMax(0, m_decisionComboBox->findData(int(action.ocrDecisionPolicy))));
            formLayout->addRow(tr("Mixed pages:"), m_decisionComboBox);

            m_allowPageErrorsCheckBox = new QCheckBox(tr("Write the file, even if some pages fail"), this);
            m_allowPageErrorsCheckBox->setObjectName("allowPageErrorsCheckBox");
            m_allowPageErrorsCheckBox->setChecked(action.ocrAllowPageErrors);
            formLayout->addRow(QString(), m_allowPageErrorsCheckBox);
            break;
        }

        case Type::Optimize:
        {
            m_optimizeObjectsCheckBox = new QCheckBox(tr("Merge duplicate objects, remove unused objects, recompress streams"), this);
            m_optimizeObjectsCheckBox->setObjectName("optimizeObjectsCheckBox");
            m_optimizeObjectsCheckBox->setChecked(action.optimizationFlags != pdf::PDFOptimizer::None);
            formLayout->addRow(tr("Objects:"), m_optimizeObjectsCheckBox);

            m_optimizeImagesCheckBox = new QCheckBox(tr("Recompress the images"), this);
            m_optimizeImagesCheckBox->setObjectName("optimizeImagesCheckBox");
            m_optimizeImagesCheckBox->setChecked(action.optimizeImages);
            formLayout->addRow(tr("Images:"), m_optimizeImagesCheckBox);

            m_imageGoalComboBox = new QComboBox(this);
            m_imageGoalComboBox->setObjectName("imageGoalComboBox");
            m_imageGoalComboBox->addItem(tr("Best quality"), int(pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality));
            m_imageGoalComboBox->addItem(tr("Smallest size"), int(pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize));
            m_imageGoalComboBox->setCurrentIndex(qMax(0, m_imageGoalComboBox->findData(int(action.imageGoal))));
            formLayout->addRow(tr("Goal:"), m_imageGoalComboBox);

            m_imageDpiSpinBox = new QSpinBox(this);
            m_imageDpiSpinBox->setObjectName("imageDpiSpinBox");
            m_imageDpiSpinBox->setRange(0, 2400);
            m_imageDpiSpinBox->setSpecialValueText(tr("Keep"));
            m_imageDpiSpinBox->setSuffix(tr(" DPI"));
            m_imageDpiSpinBox->setValue(action.imageTargetDpi);
            formLayout->addRow(tr("Downsample to:"), m_imageDpiSpinBox);

            auto updateImageWidgets = [this]()
            {
                m_imageGoalComboBox->setEnabled(m_optimizeImagesCheckBox->isChecked());
                m_imageDpiSpinBox->setEnabled(m_optimizeImagesCheckBox->isChecked());
            };
            connect(m_optimizeImagesCheckBox, &QCheckBox::toggled, this, updateImageWidgets);
            updateImageWidgets();
            break;
        }

        case Type::Sanitize:
        {
            const std::vector<std::pair<QString, pdf::PDFDocumentSanitizer::SanitizationFlag>> flags = {
                { tr("Document information (author, title, ...)"), pdf::PDFDocumentSanitizer::DocumentInfo },
                { tr("Metadata"), pdf::PDFDocumentSanitizer::Metadata },
                { tr("Bookmarks"), pdf::PDFDocumentSanitizer::Outline },
                { tr("File attachments"), pdf::PDFDocumentSanitizer::FileAttachments },
                { tr("Embedded search index"), pdf::PDFDocumentSanitizer::EmbeddedSearchIndex },
                { tr("Comments and markups"), pdf::PDFDocumentSanitizer::MarkupAnnotations },
                { tr("Page thumbnails"), pdf::PDFDocumentSanitizer::PageThumbnails },
                { tr("Page labels"), pdf::PDFDocumentSanitizer::PageLabels },
                { tr("Invisible text (also an OCR text layer)"), pdf::PDFDocumentSanitizer::InvisibleText }
            };

            bool first = true;
            for (const auto& [text, flag] : flags)
            {
                QCheckBox* checkBox = new QCheckBox(text, this);
                checkBox->setObjectName(QStringLiteral("sanitizeCheckBox%1").arg(int(flag)));
                checkBox->setChecked(action.sanitizationFlags.testFlag(flag));
                formLayout->addRow(first ? tr("Remove:") : QString(), checkBox);
                m_sanitizeCheckBoxes.emplace_back(checkBox, flag);
                first = false;
            }
            break;
        }

        case Type::SetPassword:
            m_userPasswordEdit = new QLineEdit(action.userPassword, this);
            m_userPasswordEdit->setObjectName("userPasswordEdit");
            m_userPasswordEdit->setEchoMode(QLineEdit::Password);
            m_ownerPasswordEdit = new QLineEdit(action.ownerPassword, this);
            m_ownerPasswordEdit->setObjectName("ownerPasswordEdit");
            m_ownerPasswordEdit->setEchoMode(QLineEdit::Password);
            formLayout->addRow(tr("Password to open:"), m_userPasswordEdit);
            formLayout->addRow(tr("Password to change:"), m_ownerPasswordEdit);
            formLayout->addRow(new QLabel(tr("The files are encrypted by AES 256. Passwords are not stored in saved sequences."), this));
            break;

        case Type::ExportWord:
            m_wordGraphicsCheckBox = new QCheckBox(tr("Keep the graphics as a picture behind the text"), this);
            m_wordGraphicsCheckBox->setObjectName("wordGraphicsCheckBox");
            m_wordGraphicsCheckBox->setChecked(action.wordIncludeGraphics);
            m_wordResolutionSpinBox = new QSpinBox(this);
            m_wordResolutionSpinBox->setObjectName("wordResolutionSpinBox");
            m_wordResolutionSpinBox->setRange(36, 600);
            m_wordResolutionSpinBox->setSuffix(tr(" DPI"));
            m_wordResolutionSpinBox->setValue(action.wordGraphicsResolution);
            formLayout->addRow(tr("Graphics:"), m_wordGraphicsCheckBox);
            formLayout->addRow(tr("Resolution:"), m_wordResolutionSpinBox);
            connect(m_wordGraphicsCheckBox, &QCheckBox::toggled, m_wordResolutionSpinBox, &QSpinBox::setEnabled);
            m_wordResolutionSpinBox->setEnabled(action.wordIncludeGraphics);
            break;

        case Type::ExportImages:
        {
            m_imageFormatComboBox = new QComboBox(this);
            m_imageFormatComboBox->setObjectName("imageFormatComboBox");
            const QList<QByteArray> supportedFormats = QImageWriter::supportedImageFormats();
            for (const char* format : { "png", "jpg", "tiff", "bmp", "webp" })
            {
                if (supportedFormats.contains(QByteArray(format)))
                {
                    m_imageFormatComboBox->addItem(QString::fromLatin1(format).toUpper(), QString::fromLatin1(format));
                }
            }
            m_imageFormatComboBox->setCurrentIndex(qMax(0, m_imageFormatComboBox->findData(action.imageFormat)));
            m_imageResolutionSpinBox = new QSpinBox(this);
            m_imageResolutionSpinBox->setObjectName("imageResolutionSpinBox");
            m_imageResolutionSpinBox->setRange(10, 1200);
            m_imageResolutionSpinBox->setSuffix(tr(" DPI"));
            m_imageResolutionSpinBox->setValue(action.imageResolution);
            formLayout->addRow(tr("Format:"), m_imageFormatComboBox);
            formLayout->addRow(tr("Resolution:"), m_imageResolutionSpinBox);
            break;
        }

        default:
            break;
    }

    if (pdf::PDFBatchAction::hasPages(action.type))
    {
        m_pagesEdit = new QLineEdit(action.pages, this);
        m_pagesEdit->setObjectName("pagesEdit");
        m_pagesEdit->setPlaceholderText(tr("all pages, or for example 1-3, 5, 8-"));
        formLayout->addRow(tr("Pages:"), m_pagesEdit);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->setObjectName("buttonBox");
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PDFBatchActionDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PDFBatchActionDialog::reject);
    layout->addStretch(1);
    layout->addWidget(buttonBox);

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 420));
    pdf::PDFWidgetUtils::style(this);
}

PDFBatchActionDialog::~PDFBatchActionDialog()
{

}

bool PDFBatchActionDialog::hasSettings(pdf::PDFBatchAction::Type type)
{
    return type != pdf::PDFBatchAction::Type::FlattenForms;
}

pdf::PDFBatchAction PDFBatchActionDialog::getAction() const
{
    pdf::PDFBatchAction action = m_action;

    if (m_pagesEdit)
    {
        action.pages = m_pagesEdit->text().trimmed();
    }

    if (m_removeHeaderFooterCheckBox)
    {
        pdf::PDFPageStamper::StampKinds kinds = pdf::PDFPageStamper::None;
        if (m_removeHeaderFooterCheckBox->isChecked())
        {
            kinds |= pdf::PDFPageStamper::HeaderFooter;
        }
        if (m_removeWatermarkCheckBox->isChecked())
        {
            kinds |= pdf::PDFPageStamper::Watermark;
        }
        action.removedStamps = kinds;
    }

    if (m_rotationComboBox)
    {
        action.rotation = m_rotationComboBox->currentData().toInt();
    }

    if (m_languagesEdit)
    {
        action.ocrConfiguration.languages.clear();
        for (const QString& language : m_languagesEdit->text().split(QRegularExpression(QStringLiteral("[+,\\s]")), Qt::SkipEmptyParts))
        {
            if (!action.ocrConfiguration.languages.contains(language))
            {
                action.ocrConfiguration.languages << language;
            }
        }
        action.ocrConfiguration.profile = pdf::PDFOCRModelProfile(m_profileComboBox->currentData().toInt());
        action.ocrConfiguration.existingTextPolicy = pdf::PDFOCRExistingTextPolicy(m_existingTextComboBox->currentData().toInt());
        action.ocrDecisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy(m_decisionComboBox->currentData().toInt());
        action.ocrAllowPageErrors = m_allowPageErrorsCheckBox->isChecked();
    }

    if (m_optimizeObjectsCheckBox)
    {
        action.optimizationFlags = m_optimizeObjectsCheckBox->isChecked() ? pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::All) : pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::None);
        action.optimizeImages = m_optimizeImagesCheckBox->isChecked();
        action.imageGoal = pdf::PDFImageOptimizer::OptimizationGoal(m_imageGoalComboBox->currentData().toInt());
        action.imageTargetDpi = m_imageDpiSpinBox->value();
    }

    if (!m_sanitizeCheckBoxes.empty())
    {
        pdf::PDFDocumentSanitizer::SanitizationFlags flags = pdf::PDFDocumentSanitizer::None;
        for (const auto& [checkBox, flag] : m_sanitizeCheckBoxes)
        {
            if (checkBox->isChecked())
            {
                flags |= flag;
            }
        }
        action.sanitizationFlags = flags;
    }

    if (m_userPasswordEdit)
    {
        action.userPassword = m_userPasswordEdit->text();
        action.ownerPassword = m_ownerPasswordEdit->text();
    }

    if (m_wordGraphicsCheckBox)
    {
        action.wordIncludeGraphics = m_wordGraphicsCheckBox->isChecked();
        action.wordGraphicsResolution = m_wordResolutionSpinBox->value();
    }

    if (m_imageFormatComboBox)
    {
        action.imageFormat = m_imageFormatComboBox->currentData().toString();
        action.imageResolution = m_imageResolutionSpinBox->value();
    }

    return action;
}

void PDFBatchActionDialog::accept()
{
    const QStringList errors = getAction().validate();
    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), errors.join(QChar('\n')));
        return;
    }

    BaseClass::accept();
}

// -------------------------------------------------------------------------
// PDFBatchProcessingDialog
// -------------------------------------------------------------------------

PDFBatchProcessingDialog::PDFBatchProcessingDialog(QWidget* parent) :
    BaseClass(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint),
    m_modelManager(new pdf::PDFOCRModelManager(this))
{
    setWindowTitle(tr("Batch Process"));
    setObjectName("PDFBatchProcessingDialog");
    setAcceptDrops(true);

    m_modelManager->loadBundledCatalog();
    m_modelManager->refresh();

    createUi();
    loadSettings();
    updateUi();

    resize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(1000, 720)));
    pdf::PDFWidgetUtils::style(this);
}

PDFBatchProcessingDialog::~PDFBatchProcessingDialog()
{
    if (m_thread)
    {
        m_cancelToken->cancel();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void PDFBatchProcessingDialog::createUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* topLayout = new QHBoxLayout();
    mainLayout->addLayout(topLayout, 1);

    // Files
    QGroupBox* filesGroupBox = new QGroupBox(tr("Files"), this);
    QVBoxLayout* filesLayout = new QVBoxLayout(filesGroupBox);
    m_filesTable = new QTableWidget(0, ColumnCount, filesGroupBox);
    m_filesTable->setObjectName("filesTable");
    m_filesTable->setHorizontalHeaderLabels({ tr("File"), tr("Status"), tr("Pages"), tr("Time"), tr("Message") });
    m_filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_filesTable->verticalHeader()->setVisible(false);
    m_filesTable->horizontalHeader()->setSectionResizeMode(ColumnFile, QHeaderView::Interactive);
    m_filesTable->horizontalHeader()->setStretchLastSection(true);
    m_filesTable->setColumnWidth(ColumnFile, pdf::PDFWidgetUtils::scaleDPI_x(this, 220));
    filesLayout->addWidget(m_filesTable, 1);

    QHBoxLayout* filesButtonsLayout = new QHBoxLayout();
    m_addFilesButton = new QPushButton(tr("Add Files..."), filesGroupBox);
    m_addFilesButton->setObjectName("addFilesButton");
    m_addFolderButton = new QPushButton(tr("Add Folder..."), filesGroupBox);
    m_addFolderButton->setObjectName("addFolderButton");
    m_includeSubfoldersCheckBox = new QCheckBox(tr("Include subfolders"), filesGroupBox);
    m_includeSubfoldersCheckBox->setObjectName("includeSubfoldersCheckBox");
    m_removeFilesButton = new QPushButton(tr("Remove"), filesGroupBox);
    m_removeFilesButton->setObjectName("removeFilesButton");
    m_clearFilesButton = new QPushButton(tr("Clear"), filesGroupBox);
    m_clearFilesButton->setObjectName("clearFilesButton");
    filesButtonsLayout->addWidget(m_addFilesButton);
    filesButtonsLayout->addWidget(m_addFolderButton);
    filesButtonsLayout->addWidget(m_includeSubfoldersCheckBox);
    filesButtonsLayout->addStretch(1);
    filesButtonsLayout->addWidget(m_removeFilesButton);
    filesButtonsLayout->addWidget(m_clearFilesButton);
    filesLayout->addLayout(filesButtonsLayout);
    filesLayout->addWidget(new QLabel(tr("Drop PDF files or folders here. The original files are never changed."), filesGroupBox));
    topLayout->addWidget(filesGroupBox, 3);

    // Sequence
    QGroupBox* sequenceGroupBox = new QGroupBox(tr("Actions (applied from top to bottom)"), this);
    QVBoxLayout* sequenceLayout = new QVBoxLayout(sequenceGroupBox);
    m_actionsList = new QListWidget(sequenceGroupBox);
    m_actionsList->setObjectName("actionsList");
    m_actionsList->setWordWrap(true);
    m_actionsList->setAlternatingRowColors(true);
    sequenceLayout->addWidget(m_actionsList, 1);

    QGridLayout* sequenceButtonsLayout = new QGridLayout();
    m_addActionButton = new QToolButton(sequenceGroupBox);
    m_addActionButton->setObjectName("addActionButton");
    m_addActionButton->setText(tr("Add Action"));
    m_addActionButton->setPopupMode(QToolButton::InstantPopup);
    m_addActionButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_addActionButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QMenu* addMenu = new QMenu(m_addActionButton);
    addMenu->setObjectName("addActionMenu");
    const std::vector<std::vector<pdf::PDFBatchAction::Type>> groups = {
        { pdf::PDFBatchAction::Type::HeaderFooter, pdf::PDFBatchAction::Type::BatesNumbering, pdf::PDFBatchAction::Type::Watermark, pdf::PDFBatchAction::Type::RemoveStamps },
        { pdf::PDFBatchAction::Type::FlattenForms, pdf::PDFBatchAction::Type::RotatePages, pdf::PDFBatchAction::Type::RecognizeText },
        { pdf::PDFBatchAction::Type::Optimize, pdf::PDFBatchAction::Type::Sanitize, pdf::PDFBatchAction::Type::SetPassword },
        { pdf::PDFBatchAction::Type::ExportWord, pdf::PDFBatchAction::Type::ExportText, pdf::PDFBatchAction::Type::ExportImages }
    };
    for (const auto& group : groups)
    {
        if (!addMenu->isEmpty())
        {
            addMenu->addSeparator();
        }
        for (pdf::PDFBatchAction::Type type : group)
        {
            QAction* action = addMenu->addAction(pdf::PDFBatchAction::getTypeName(type));
            action->setObjectName(QStringLiteral("addAction_") + pdf::PDFBatchAction::getTypeId(type));
            connect(action, &QAction::triggered, this, [this, type]() { onAddAction(type); });
        }
    }
    m_addActionButton->setMenu(addMenu);

    m_editActionButton = new QPushButton(tr("Settings..."), sequenceGroupBox);
    m_editActionButton->setObjectName("editActionButton");
    m_removeActionButton = new QPushButton(tr("Remove"), sequenceGroupBox);
    m_removeActionButton->setObjectName("removeActionButton");
    m_moveUpButton = new QPushButton(tr("Move Up"), sequenceGroupBox);
    m_moveUpButton->setObjectName("moveUpButton");
    m_moveDownButton = new QPushButton(tr("Move Down"), sequenceGroupBox);
    m_moveDownButton->setObjectName("moveDownButton");
    m_loadSequenceButton = new QPushButton(tr("Load Sequence..."), sequenceGroupBox);
    m_loadSequenceButton->setObjectName("loadSequenceButton");
    m_saveSequenceButton = new QPushButton(tr("Save Sequence..."), sequenceGroupBox);
    m_saveSequenceButton->setObjectName("saveSequenceButton");
    sequenceButtonsLayout->addWidget(m_addActionButton, 0, 0);
    sequenceButtonsLayout->addWidget(m_editActionButton, 0, 1);
    sequenceButtonsLayout->addWidget(m_removeActionButton, 0, 2);
    sequenceButtonsLayout->addWidget(m_moveUpButton, 1, 0);
    sequenceButtonsLayout->addWidget(m_moveDownButton, 1, 1);
    sequenceButtonsLayout->addWidget(m_loadSequenceButton, 2, 0, 1, 2);
    sequenceButtonsLayout->addWidget(m_saveSequenceButton, 2, 2);
    sequenceLayout->addLayout(sequenceButtonsLayout);
    topLayout->addWidget(sequenceGroupBox, 2);

    // Output
    QGroupBox* outputGroupBox = new QGroupBox(tr("Output"), this);
    QGridLayout* outputLayout = new QGridLayout(outputGroupBox);
    m_outputFolderRadioButton = new QRadioButton(tr("Folder:"), outputGroupBox);
    m_outputFolderRadioButton->setObjectName("outputFolderRadioButton");
    m_nextToOriginalRadioButton = new QRadioButton(tr("Next to the original files"), outputGroupBox);
    m_nextToOriginalRadioButton->setObjectName("nextToOriginalRadioButton");
    m_outputDirectoryEdit = new QLineEdit(outputGroupBox);
    m_outputDirectoryEdit->setObjectName("outputDirectoryEdit");
    m_browseOutputDirectoryButton = new QPushButton(tr("Browse..."), outputGroupBox);
    m_browseOutputDirectoryButton->setObjectName("browseOutputDirectoryButton");
    m_suffixEdit = new QLineEdit(outputGroupBox);
    m_suffixEdit->setObjectName("suffixEdit");
    m_suffixEdit->setToolTip(tr("Text added to the name of the original file, for example plan.pdf becomes plan_processed.pdf."));
    m_existingFilesComboBox = new QComboBox(outputGroupBox);
    m_existingFilesComboBox->setObjectName("existingFilesComboBox");
    m_existingFilesComboBox->addItem(tr("Add a number to the new file name"), int(pdf::PDFBatchSettings::ExistingFilePolicy::Rename));
    m_existingFilesComboBox->addItem(tr("Overwrite the existing file"), int(pdf::PDFBatchSettings::ExistingFilePolicy::Overwrite));
    m_existingFilesComboBox->addItem(tr("Skip the original file"), int(pdf::PDFBatchSettings::ExistingFilePolicy::Skip));
    m_passwordEdit = new QLineEdit(outputGroupBox);
    m_passwordEdit->setObjectName("passwordEdit");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("only for protected files"));

    outputLayout->addWidget(m_outputFolderRadioButton, 0, 0);
    outputLayout->addWidget(m_outputDirectoryEdit, 0, 1);
    outputLayout->addWidget(m_browseOutputDirectoryButton, 0, 2);
    outputLayout->addWidget(m_nextToOriginalRadioButton, 1, 0, 1, 3);
    outputLayout->addWidget(new QLabel(tr("File name suffix:"), outputGroupBox), 0, 3);
    outputLayout->addWidget(m_suffixEdit, 0, 4);
    outputLayout->addWidget(new QLabel(tr("Existing files:"), outputGroupBox), 1, 3);
    outputLayout->addWidget(m_existingFilesComboBox, 1, 4);
    outputLayout->addWidget(new QLabel(tr("Password of the files:"), outputGroupBox), 2, 3);
    outputLayout->addWidget(m_passwordEdit, 2, 4);
    outputLayout->setColumnStretch(1, 2);
    outputLayout->setColumnStretch(4, 1);
    mainLayout->addWidget(outputGroupBox);

    // Progress
    QHBoxLayout* progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("progressBar");
    m_progressBar->setValue(0);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName("progressLabel");
    m_startButton = new QPushButton(tr("Start"), this);
    m_startButton->setObjectName("startButton");
    m_startButton->setDefault(true);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setObjectName("stopButton");
    m_openResultButton = new QPushButton(tr("Open Result"), this);
    m_openResultButton->setObjectName("openResultButton");
    progressLayout->addWidget(m_progressBar, 1);
    progressLayout->addWidget(m_startButton);
    progressLayout->addWidget(m_stopButton);
    progressLayout->addWidget(m_openResultButton);
    mainLayout->addLayout(progressLayout);
    mainLayout->addWidget(m_progressLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_buttonBox->setObjectName("buttonBox");
    mainLayout->addWidget(m_buttonBox);

    connect(m_addFilesButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onAddFiles);
    connect(m_addFolderButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onAddFolder);
    connect(m_removeFilesButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onRemoveFiles);
    connect(m_clearFilesButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onClearFiles);
    connect(m_editActionButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onEditAction);
    connect(m_actionsList, &QListWidget::itemDoubleClicked, this, &PDFBatchProcessingDialog::onEditAction);
    connect(m_removeActionButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onRemoveAction);
    connect(m_moveUpButton, &QPushButton::clicked, this, [this]() { onMoveAction(-1); });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this]() { onMoveAction(1); });
    connect(m_saveSequenceButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onSaveSequence);
    connect(m_loadSequenceButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onLoadSequence);
    connect(m_browseOutputDirectoryButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onBrowseOutputDirectory);
    connect(m_outputFolderRadioButton, &QRadioButton::toggled, this, &PDFBatchProcessingDialog::updateUi);
    connect(m_startButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onStart);
    connect(m_stopButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::stopBatch);
    connect(m_openResultButton, &QPushButton::clicked, this, &PDFBatchProcessingDialog::onOpenResult);
    connect(m_actionsList, &QListWidget::currentRowChanged, this, &PDFBatchProcessingDialog::updateUi);
    connect(m_filesTable, &QTableWidget::itemSelectionChanged, this, &PDFBatchProcessingDialog::updateUi);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &PDFBatchProcessingDialog::reject);
}

void PDFBatchProcessingDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("BatchProcessing"));

    pdf::PDFBatchSettings batchSettings;
    const QByteArray sequence = settings.value(QStringLiteral("sequence")).toByteArray();
    if (!sequence.isEmpty())
    {
        batchSettings = pdf::PDFBatchSettings::fromJson(QJsonDocument::fromJson(sequence).object(), nullptr);
    }
    if (batchSettings.outputDirectory.isEmpty())
    {
        batchSettings.outputDirectory = QDir::toNativeSeparators(QDir::home().absoluteFilePath(QStringLiteral("PDF4QT Batch")));
    }
    setSettings(batchSettings);

    m_includeSubfoldersCheckBox->setChecked(settings.value(QStringLiteral("includeSubfolders"), false).toBool());
    settings.endGroup();
}

void PDFBatchProcessingDialog::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("BatchProcessing"));
    settings.setValue(QStringLiteral("sequence"), QJsonDocument(getSettings().toJson()).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("includeSubfolders"), m_includeSubfoldersCheckBox->isChecked());
    settings.endGroup();
}

pdf::PDFBatchSettings PDFBatchProcessingDialog::getSettings() const
{
    pdf::PDFBatchSettings settings;
    settings.actions = m_actions;
    settings.outputLocation = m_outputFolderRadioButton->isChecked() ? pdf::PDFBatchSettings::OutputLocation::Folder : pdf::PDFBatchSettings::OutputLocation::NextToOriginal;
    settings.outputDirectory = QDir::fromNativeSeparators(m_outputDirectoryEdit->text().trimmed());
    settings.suffix = m_suffixEdit->text();
    settings.existingFilePolicy = pdf::PDFBatchSettings::ExistingFilePolicy(m_existingFilesComboBox->currentData().toInt());
    settings.password = m_passwordEdit->text();
    return settings;
}

void PDFBatchProcessingDialog::setSettings(const pdf::PDFBatchSettings& settings)
{
    m_actions = settings.actions;
    m_outputFolderRadioButton->setChecked(settings.outputLocation == pdf::PDFBatchSettings::OutputLocation::Folder);
    m_nextToOriginalRadioButton->setChecked(settings.outputLocation == pdf::PDFBatchSettings::OutputLocation::NextToOriginal);
    m_outputDirectoryEdit->setText(QDir::toNativeSeparators(settings.outputDirectory));
    m_suffixEdit->setText(settings.suffix);
    m_existingFilesComboBox->setCurrentIndex(qMax(0, m_existingFilesComboBox->findData(int(settings.existingFilePolicy))));
    m_passwordEdit->setText(settings.password);
    updateActionList();
    updateUi();
}

void PDFBatchProcessingDialog::addAction(const pdf::PDFBatchAction& action)
{
    m_actions.push_back(action);
    updateActionList();
    m_actionsList->setCurrentRow(int(m_actions.size()) - 1);
    updateUi();
}

void PDFBatchProcessingDialog::setAction(int row, const pdf::PDFBatchAction& action)
{
    if (row >= 0 && row < int(m_actions.size()))
    {
        m_actions[row] = action;
        updateActionList();
        m_actionsList->setCurrentRow(row);
    }
}

void PDFBatchProcessingDialog::updateActionList()
{
    const int currentRow = m_actionsList->currentRow();
    m_actionsList->clear();

    for (size_t i = 0; i < m_actions.size(); ++i)
    {
        const pdf::PDFBatchAction& action = m_actions[i];
        const QStringList errors = action.validate();
        QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1. %2\n%3").arg(i + 1).arg(action.getName(), action.getSummary()), m_actionsList);
        item->setToolTip(errors.isEmpty() ? action.getSummary() : errors.join(QChar('\n')));
        if (!errors.isEmpty())
        {
            item->setForeground(Qt::red);
        }
    }

    m_actionsList->setCurrentRow(qMin(currentRow, m_actionsList->count() - 1));
}

void PDFBatchProcessingDialog::updateUi()
{
    const bool running = isRunning();
    const int actionRow = m_actionsList->currentRow();
    const bool hasAction = actionRow >= 0 && actionRow < int(m_actions.size());

    m_addFilesButton->setEnabled(!running);
    m_addFolderButton->setEnabled(!running);
    m_includeSubfoldersCheckBox->setEnabled(!running);
    m_removeFilesButton->setEnabled(!running && !m_filesTable->selectionModel()->selectedRows().isEmpty());
    m_clearFilesButton->setEnabled(!running && m_filesTable->rowCount() > 0);

    m_actionsList->setEnabled(!running);
    m_addActionButton->setEnabled(!running);
    m_editActionButton->setEnabled(!running && hasAction && PDFBatchActionDialog::hasSettings(m_actions[actionRow].type));
    m_removeActionButton->setEnabled(!running && hasAction);
    m_moveUpButton->setEnabled(!running && hasAction && actionRow > 0);
    m_moveDownButton->setEnabled(!running && hasAction && actionRow + 1 < int(m_actions.size()));
    m_loadSequenceButton->setEnabled(!running);
    m_saveSequenceButton->setEnabled(!running && !m_actions.empty());

    m_outputFolderRadioButton->setEnabled(!running);
    m_nextToOriginalRadioButton->setEnabled(!running);
    m_outputDirectoryEdit->setEnabled(!running && m_outputFolderRadioButton->isChecked());
    m_browseOutputDirectoryButton->setEnabled(!running && m_outputFolderRadioButton->isChecked());
    m_suffixEdit->setEnabled(!running);
    m_existingFilesComboBox->setEnabled(!running);
    m_passwordEdit->setEnabled(!running);

    m_startButton->setEnabled(!running && m_filesTable->rowCount() > 0 && !m_actions.empty());
    m_stopButton->setEnabled(running && !m_cancelToken->isOperationCancelled());

    bool canOpen = false;
    const QModelIndexList selectedRows = m_filesTable->selectionModel()->selectedRows();
    if (!running && selectedRows.size() == 1)
    {
        const QString outputFile = m_filesTable->item(selectedRows.front().row(), ColumnFile)->data(OutputFileRole).toString();
        canOpen = outputFile.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) && QFileInfo::exists(outputFile);
    }
    m_openResultButton->setEnabled(canOpen);
}

void PDFBatchProcessingDialog::addFiles(const QStringList& files)
{
    QStringList pdfFiles;
    for (const QString& file : files)
    {
        const QFileInfo fileInfo(file);
        if (fileInfo.isDir())
        {
            pdfFiles << pdf::PDFBatchProcessor::findPdfFiles(fileInfo.absoluteFilePath(), m_includeSubfoldersCheckBox->isChecked());
        }
        else if (fileInfo.isFile())
        {
            pdfFiles << QDir::cleanPath(fileInfo.absoluteFilePath());
        }
    }

    QStringList existingFiles = getFiles();
    for (const QString& file : pdfFiles)
    {
        if (existingFiles.contains(file, Qt::CaseInsensitive))
        {
            continue;
        }
        existingFiles << file;

        const int row = m_filesTable->rowCount();
        m_filesTable->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column)
        {
            m_filesTable->setItem(row, column, new QTableWidgetItem());
        }

        QTableWidgetItem* fileItem = m_filesTable->item(row, ColumnFile);
        fileItem->setText(QFileInfo(file).fileName());
        fileItem->setToolTip(QDir::toNativeSeparators(file));
        fileItem->setData(InputFileRole, file);
    }

    updateUi();
}

QStringList PDFBatchProcessingDialog::getFiles() const
{
    QStringList files;
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        files << m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString();
    }
    return files;
}

void PDFBatchProcessingDialog::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files"), QString(), tr("PDF documents (*.pdf)"));
    addFiles(files);
}

void PDFBatchProcessingDialog::onAddFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
    if (!directory.isEmpty())
    {
        addFiles({ directory });
    }
}

void PDFBatchProcessingDialog::onRemoveFiles()
{
    QModelIndexList rows = m_filesTable->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) { return left.row() > right.row(); });
    for (const QModelIndex& index : rows)
    {
        m_filesTable->removeRow(index.row());
    }
    updateUi();
}

void PDFBatchProcessingDialog::onClearFiles()
{
    m_filesTable->setRowCount(0);
    updateUi();
}

bool PDFBatchProcessingDialog::editAction(pdf::PDFBatchAction& action)
{
    using Type = pdf::PDFBatchAction::Type;

    switch (action.type)
    {
        case Type::HeaderFooter:
        case Type::BatesNumbering:
        case Type::Watermark:
        {
            PDFPageStampDialog::Mode mode = PDFPageStampDialog::Mode::HeaderFooter;
            if (action.type == Type::BatesNumbering)
            {
                mode = PDFPageStampDialog::Mode::Bates;
            }
            else if (action.type == Type::Watermark)
            {
                mode = PDFPageStampDialog::Mode::Watermark;
            }

            PDFPageStampDialog dialog(mode, nullptr, -1, QString(), this);
            dialog.setBatchMode();
            if (action.type == Type::Watermark)
            {
                dialog.setWatermarkSettings(action.watermark);
            }
            else
            {
                dialog.setHeaderFooterSettings(action.headerFooter);
            }
            dialog.setPageSelection(action.pages);
            dialog.setReplaceExisting(action.replaceExisting);
            dialog.setContinueBatesNumbering(action.continueBatesNumbering);

            if (dialog.exec() != QDialog::Accepted)
            {
                return false;
            }

            if (action.type == Type::Watermark)
            {
                action.watermark = dialog.getWatermarkSettings();
            }
            else
            {
                action.headerFooter = dialog.getHeaderFooterSettings();
            }
            action.pages = dialog.getPageSelection();
            action.replaceExisting = dialog.isReplaceExisting();
            action.continueBatesNumbering = dialog.isContinueBatesNumbering();
            return true;
        }

        default:
        {
            if (!PDFBatchActionDialog::hasSettings(action.type))
            {
                return true;
            }

            PDFBatchActionDialog dialog(action, this);
            if (dialog.exec() != QDialog::Accepted)
            {
                return false;
            }
            action = dialog.getAction();
            return true;
        }
    }
}

void PDFBatchProcessingDialog::onAddAction(pdf::PDFBatchAction::Type type)
{
    pdf::PDFBatchAction action = pdf::PDFBatchAction::create(type);
    if (editAction(action))
    {
        addAction(action);
    }
}

void PDFBatchProcessingDialog::onEditAction()
{
    const int row = m_actionsList->currentRow();
    if (isRunning() || row < 0 || row >= int(m_actions.size()))
    {
        return;
    }

    pdf::PDFBatchAction action = m_actions[row];
    if (editAction(action))
    {
        setAction(row, action);
    }
}

void PDFBatchProcessingDialog::onRemoveAction()
{
    const int row = m_actionsList->currentRow();
    if (row >= 0 && row < int(m_actions.size()))
    {
        m_actions.erase(m_actions.begin() + row);
        updateActionList();
        updateUi();
    }
}

void PDFBatchProcessingDialog::onMoveAction(int offset)
{
    const int row = m_actionsList->currentRow();
    const int newRow = row + offset;
    if (row >= 0 && row < int(m_actions.size()) && newRow >= 0 && newRow < int(m_actions.size()))
    {
        std::swap(m_actions[row], m_actions[newRow]);
        updateActionList();
        m_actionsList->setCurrentRow(newRow);
        updateUi();
    }
}

QString PDFBatchProcessingDialog::saveSequence(const QString& fileName) const
{
    pdf::PDFBatchSettings settings = getSettings();
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return tr("The file '%1' cannot be written. %2").arg(QDir::toNativeSeparators(fileName), file.errorString());
    }

    file.write(QJsonDocument(settings.toJson()).toJson(QJsonDocument::Indented));
    file.close();
    return QString();
}

QString PDFBatchProcessingDialog::loadSequence(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return tr("The file '%1' cannot be read. %2").arg(QDir::toNativeSeparators(fileName), file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject())
    {
        return tr("The file '%1' is not a sequence. %2").arg(QDir::toNativeSeparators(fileName), parseError.errorString());
    }

    QString errorMessage;
    pdf::PDFBatchSettings settings = pdf::PDFBatchSettings::fromJson(document.object(), &errorMessage);
    settings.password = m_passwordEdit->text();
    setSettings(settings);
    return errorMessage;
}

void PDFBatchProcessingDialog::onSaveSequence()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Save Sequence"), QString(), tr("Batch sequence (*.json)"));
    if (fileName.isEmpty())
    {
        return;
    }

    const QString errorMessage = saveSequence(fileName);
    if (!errorMessage.isEmpty())
    {
        QMessageBox::critical(this, windowTitle(), errorMessage);
    }
}

void PDFBatchProcessingDialog::onLoadSequence()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Load Sequence"), QString(), tr("Batch sequence (*.json)"));
    if (fileName.isEmpty())
    {
        return;
    }

    const QString errorMessage = loadSequence(fileName);
    if (!errorMessage.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), errorMessage);
    }
}

void PDFBatchProcessingDialog::onBrowseOutputDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Output Folder"), m_outputDirectoryEdit->text());
    if (!directory.isEmpty())
    {
        m_outputDirectoryEdit->setText(QDir::toNativeSeparators(directory));
    }
}

bool PDFBatchProcessingDialog::isRunning() const
{
    return m_thread != nullptr;
}

void PDFBatchProcessingDialog::setRowStatus(int row, const QString& status, const QString& message)
{
    m_filesTable->item(row, ColumnStatus)->setText(status);
    m_filesTable->item(row, ColumnMessage)->setText(QString(message).replace(QChar('\n'), QChar(' ')));
    m_filesTable->item(row, ColumnMessage)->setToolTip(message);
}

QStringList PDFBatchProcessingDialog::startBatch()
{
    if (isRunning())
    {
        return { tr("The batch is already running.") };
    }

    const pdf::PDFBatchSettings settings = getSettings();
    auto processor = std::make_shared<pdf::PDFBatchProcessor>(settings);
    processor->setOCRModelManager(m_modelManager);

    QStringList errors;
    if (m_filesTable->rowCount() == 0)
    {
        errors << tr("Add the files to process.");
    }
    errors << processor->validate();
    if (!errors.isEmpty())
    {
        return errors;
    }

    saveSettings();

    std::vector<std::pair<int, QString>> tasks;
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        for (int column = ColumnStatus; column < ColumnCount; ++column)
        {
            m_filesTable->item(row, column)->setText(QString());
            m_filesTable->item(row, column)->setToolTip(QString());
        }
        m_filesTable->item(row, ColumnFile)->setData(OutputFileRole, QString());
        setRowStatus(row, tr("Waiting"), QString());
        tasks.emplace_back(row, m_filesTable->item(row, ColumnFile)->data(InputFileRole).toString());
    }

    m_results.clear();
    m_finishedFiles = 0;
    m_totalFiles = int(tasks.size());
    m_progressBar->setRange(0, qMax(1, m_totalFiles));
    m_progressBar->setValue(0);

    m_cancelToken = std::make_shared<pdf::PDFBatchCancelToken>();
    std::shared_ptr<pdf::PDFBatchCancelToken> cancelToken = m_cancelToken;

    // The files are processed one after another on a worker thread, which has
    // its own event loop (needed by the recognition of the text)
    m_thread = QThread::create([this, tasks, processor, cancelToken]()
    {
        for (const auto& [row, file] : tasks)
        {
            if (cancelToken->isOperationCancelled())
            {
                break;
            }

            const int fileRow = row;
            QMetaObject::invokeMethod(this, [this, fileRow]() { onFileStarted(fileRow); }, Qt::QueuedConnection);

            auto progress = [this, fileRow](int step, int stepCount, const QString& name)
            {
                QMetaObject::invokeMethod(this, [this, fileRow, step, stepCount, name]() { onFileProgress(fileRow, step, stepCount, name); }, Qt::QueuedConnection);
            };

            pdf::PDFBatchProcessor::FileResult result = processor->processFile(file, progress, cancelToken.get());
            QMetaObject::invokeMethod(this, [this, fileRow, result]() { onFileFinished(fileRow, result); }, Qt::QueuedConnection);
        }
    });
    connect(m_thread, &QThread::finished, this, &PDFBatchProcessingDialog::onThreadFinished, Qt::QueuedConnection);
    m_thread->start();

    m_progressLabel->setText(tr("Processing of %n file(s) started.", nullptr, m_totalFiles));
    updateUi();
    return QStringList();
}

void PDFBatchProcessingDialog::onStart()
{
    const QStringList errors = startBatch();
    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("The batch cannot be started:\n\n%1").arg(errors.join(QChar('\n'))));
    }
}

void PDFBatchProcessingDialog::stopBatch()
{
    if (isRunning())
    {
        m_cancelToken->cancel();
        m_progressLabel->setText(tr("Stopping..."));
        updateUi();
    }
}

void PDFBatchProcessingDialog::onFileStarted(int row)
{
    setRowStatus(row, tr("Processing"), QString());
    m_filesTable->scrollToItem(m_filesTable->item(row, ColumnFile));
}

void PDFBatchProcessingDialog::onFileProgress(int row, int step, int stepCount, QString name)
{
    setRowStatus(row, tr("Processing"), tr("%1 (%2 of %3)").arg(name).arg(qMin(step + 1, stepCount)).arg(stepCount));
}

void PDFBatchProcessingDialog::onFileFinished(int row, pdf::PDFBatchProcessor::FileResult result)
{
    QString message = result.message;
    if (!result.details.isEmpty())
    {
        message += QChar('\n') + result.details.join(QChar('\n'));
    }
    if (!result.outputFiles.isEmpty())
    {
        QStringList files;
        for (const QString& file : std::as_const(result.outputFiles))
        {
            files << QDir::toNativeSeparators(file);
        }
        message += QChar('\n') + files.join(QChar('\n'));
    }

    setRowStatus(row, pdf::PDFBatchProcessor::getStatusName(result.status), message);
    m_filesTable->item(row, ColumnPages)->setText(result.pageCount > 0 ? QString::number(result.pageCount) : QString());
    m_filesTable->item(row, ColumnTime)->setText(tr("%1 s").arg(QString::number(result.elapsedMilliseconds / 1000.0, 'f', 1)));
    m_filesTable->item(row, ColumnFile)->setData(OutputFileRole, result.outputFiles.isEmpty() ? QString() : result.outputFiles.front());

    m_results.push_back(std::move(result));
    m_progressBar->setValue(++m_finishedFiles);
    updateUi();
}

void PDFBatchProcessingDialog::onThreadFinished()
{
    if (m_thread)
    {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    int succeeded = 0;
    int failed = 0;
    int skipped = 0;
    for (const pdf::PDFBatchProcessor::FileResult& result : m_results)
    {
        switch (result.status)
        {
            case pdf::PDFBatchProcessor::FileResult::Status::Succeeded:
                ++succeeded;
                break;
            case pdf::PDFBatchProcessor::FileResult::Status::Skipped:
                ++skipped;
                break;
            default:
                ++failed;
                break;
        }
    }

    // Files, which were not started because of the stop, are marked
    for (int row = 0; row < m_filesTable->rowCount(); ++row)
    {
        if (m_filesTable->item(row, ColumnStatus)->text() == tr("Waiting"))
        {
            setRowStatus(row, pdf::PDFBatchProcessor::getStatusName(pdf::PDFBatchProcessor::FileResult::Status::Cancelled), QString());
        }
    }

    const bool stopped = m_cancelToken && m_cancelToken->isOperationCancelled();
    QString text = tr("Finished: %1 done, %2 failed, %3 skipped.").arg(succeeded).arg(failed).arg(skipped);
    if (stopped)
    {
        text = tr("Stopped: %1 done, %2 failed, %3 skipped.").arg(succeeded).arg(failed).arg(skipped);
    }
    m_progressLabel->setText(text);
    updateUi();

    Q_EMIT batchFinished();

    if (m_closeRequested)
    {
        BaseClass::reject();
    }
}

void PDFBatchProcessingDialog::onOpenResult()
{
    const QModelIndexList selectedRows = m_filesTable->selectionModel()->selectedRows();
    if (isRunning() || selectedRows.size() != 1)
    {
        return;
    }

    const QString outputFile = m_filesTable->item(selectedRows.front().row(), ColumnFile)->data(OutputFileRole).toString();
    if (!outputFile.isEmpty() && QFileInfo::exists(outputFile))
    {
        m_documentToOpen = outputFile;
        saveSettings();
        accept();
    }
}

void PDFBatchProcessingDialog::reject()
{
    if (isRunning())
    {
        // The dialog is closed, when the current file is stopped
        m_closeRequested = true;
        stopBatch();
        return;
    }

    saveSettings();
    BaseClass::reject();
}

void PDFBatchProcessingDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (!isRunning() && event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void PDFBatchProcessingDialog::dropEvent(QDropEvent* event)
{
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
        {
            files << url.toLocalFile();
        }
    }
    addFiles(files);
    event->acceptProposedAction();
}

}   // namespace pdfviewer
