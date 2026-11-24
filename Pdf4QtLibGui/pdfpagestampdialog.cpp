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


#include "pdfpagestampdialog.h"
#include "pdfdocumentbuilder.h"
#include "pdfrenderer.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfoptionalcontent.h"
#include "pdfconstants.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QTimer>
#include <QEvent>
#include <QSpinBox>
#include <QPainter>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSettings>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QButtonGroup>
#include <QColorDialog>
#include <QImageReader>
#include <QRadioButton>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

#include <limits>

#include "pdfdbgheap.h"

namespace pdfviewer
{

class PDFPageStampDialogHelper
{
public:
    PDFPageStampDialogHelper() = delete;

    /// Creates a document with a single blank page of the geometry of the page
    static pdf::PDFDocument createBlankPageDocument(const pdf::PDFPage* page);

    /// Renders the first page of the document into an image fitting the size
    static QImage renderPage(const pdf::PDFDocument* document, QSize maximalSize);

    /// Creates a text edit of a header/footer position (two lines high)
    static QPlainTextEdit* createPositionEdit(QWidget* parent, const QString& objectName);
};

pdf::PDFDocument PDFPageStampDialogHelper::createBlankPageDocument(const pdf::PDFPage* page)
{
    pdf::PDFDocumentBuilder builder;
    const QRectF mediaBox = page ? page->getMediaBox() : QRectF(0, 0, 612, 792);
    const pdf::PDFObjectReference pageReference = builder.appendPage(mediaBox);

    if (page)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("CropBox");
        factory << page->getCropBox();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Rotate");
        factory << pdf::PDFInteger(static_cast<int>(page->getPageRotation()) * 90);
        factory.endDictionaryItem();
        if (page->getUserUnit() != 1.0)
        {
            factory.beginDictionaryItem("UserUnit");
            factory << page->getUserUnit();
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        builder.mergeTo(pageReference, factory.takeObject());
    }

    return builder.build();
}

QImage PDFPageStampDialogHelper::renderPage(const pdf::PDFDocument* document, QSize maximalSize)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(0);
    if (!page)
    {
        return QImage();
    }

    const QSizeF pageSize = page->getRotatedCropBox().size();
    if (pageSize.isEmpty())
    {
        return QImage();
    }

    const QSize imageSize = pageSize.scaled(QSizeF(maximalSize), Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity optionalContentActivity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity));
    iccProfileCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity));
    pdf::PDFCMSGeneric cms;
    pdf::PDFRenderer renderer(document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());

    // The visible part of the page (crop box) fills the image
    const QRectF mediaBox = page->getRotatedMediaBox();
    const QRectF cropBox = page->getRotatedCropBox();
    const qreal scale = imageSize.width() / cropBox.width();
    const QRectF targetRect(-(cropBox.left() - mediaBox.left()) * scale,
                            -(mediaBox.bottom() - cropBox.bottom()) * scale,
                            mediaBox.width() * scale,
                            mediaBox.height() * scale);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    renderer.render(&painter, targetRect, 0);
    painter.setPen(QPen(Qt::gray, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(QPointF(0.5, 0.5), QSizeF(imageSize) - QSizeF(1.0, 1.0)));
    painter.end();

    return image;
}

QPlainTextEdit* PDFPageStampDialogHelper::createPositionEdit(QWidget* parent, const QString& objectName)
{
    QPlainTextEdit* edit = new QPlainTextEdit(parent);
    edit->setObjectName(objectName);
    edit->setTabChangesFocus(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int lineHeight = edit->fontMetrics().lineSpacing();
    edit->setFixedHeight(lineHeight * 2 + 2 * edit->frameWidth() + 8);
    edit->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(parent, 150));
    return edit;
}

PDFPageStampDialog::PDFPageStampDialog(Mode mode, const pdf::PDFDocument* document, pdf::PDFInteger currentPageIndex, const QString& fileName, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
    m_mode(mode),
    m_document(document),
    m_currentPageIndex(currentPageIndex),
    m_fileName(fileName),
    m_color(Qt::black)
{
    switch (mode)
    {
        case Mode::HeaderFooter:
            setWindowTitle(tr("Header and Footer"));
            setObjectName("PDFHeaderFooterDialog");
            break;
        case Mode::Bates:
            setWindowTitle(tr("Bates Numbering"));
            setObjectName("PDFBatesNumberingDialog");
            break;
        case Mode::Watermark:
            setWindowTitle(tr("Watermark"));
            setObjectName("PDFWatermarkDialog");
            m_color = QColor(255, 0, 0);
            break;
    }

    QHBoxLayout* mainLayout = new QHBoxLayout();
    QVBoxLayout* settingsLayout = new QVBoxLayout();

    switch (mode)
    {
        case Mode::HeaderFooter:
            settingsLayout->addWidget(createHeaderFooterPage());
            break;
        case Mode::Bates:
            settingsLayout->addWidget(createBatesWidgets(true));
            break;
        case Mode::Watermark:
            settingsLayout->addWidget(createWatermarkPage());
            break;
    }

    settingsLayout->addWidget(createStyleWidgets());
    settingsLayout->addWidget(createMarginsWidgets());
    settingsLayout->addWidget(createPagesWidgets());
    settingsLayout->addStretch(1);
    mainLayout->addLayout(settingsLayout, 1);

    QGroupBox* previewGroupBox = new QGroupBox(tr("Preview (current page size)"), this);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroupBox);
    m_previewLabel = new QLabel(previewGroupBox);
    m_previewLabel->setObjectName("previewLabel");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(260, 300)));
    previewLayout->addWidget(m_previewLabel, 1);
    mainLayout->addWidget(previewGroupBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->setObjectName("buttonBox");
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PDFPageStampDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PDFPageStampDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(mainLayout, 1);
    layout->addWidget(buttonBox);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(150);
    connect(m_previewTimer, &QTimer::timeout, this, &PDFPageStampDialog::updatePreview);

    loadSettings();
    connectChanges();
    updateEnabledState();
    updatePreview();

    pdf::PDFWidgetUtils::style(this);
}

PDFPageStampDialog::~PDFPageStampDialog()
{

}

QWidget* PDFPageStampDialog::createHeaderFooterPage()
{
    QGroupBox* groupBox = new QGroupBox(tr("Texts"), this);
    QGridLayout* layout = new QGridLayout(groupBox);

    layout->addWidget(new QLabel(tr("Left"), groupBox), 0, 1, Qt::AlignHCenter);
    layout->addWidget(new QLabel(tr("Center"), groupBox), 0, 2, Qt::AlignHCenter);
    layout->addWidget(new QLabel(tr("Right"), groupBox), 0, 3, Qt::AlignHCenter);
    layout->addWidget(new QLabel(tr("Header"), groupBox), 1, 0);
    layout->addWidget(new QLabel(tr("Footer"), groupBox), 2, 0);

    for (int i = 0; i < pdf::PDFHeaderFooterSettings::PositionCount; ++i)
    {
        const QString name = pdf::PDFHeaderFooterSettings::getPositionName(static_cast<pdf::PDFHeaderFooterSettings::Position>(i));
        QPlainTextEdit* edit = PDFPageStampDialogHelper::createPositionEdit(groupBox, name);
        edit->installEventFilter(this);
        m_positionEdits[i] = edit;
        layout->addWidget(edit, 1 + i / 3, 1 + i % 3);
    }
    m_lastTextEdit = m_positionEdits[pdf::PDFHeaderFooterSettings::FooterCenter];

    QHBoxLayout* variablesLayout = new QHBoxLayout();
    m_variableComboBox = new QComboBox(groupBox);
    m_variableComboBox->setObjectName("variableComboBox");
    m_variableComboBox->addItem(tr("Page number"), QStringLiteral("{page}"));
    m_variableComboBox->addItem(tr("Last page number"), QStringLiteral("{pages}"));
    m_variableComboBox->addItem(tr("Page N of M"), tr("Page {page} of {pages}"));
    m_variableComboBox->addItem(tr("Bates number"), QStringLiteral("{bates}"));
    m_variableComboBox->addItem(tr("Date"), QStringLiteral("{date}"));
    m_variableComboBox->addItem(tr("Time"), QStringLiteral("{time}"));
    m_variableComboBox->addItem(tr("File name"), QStringLiteral("{file}"));
    m_variableComboBox->addItem(tr("Document title"), QStringLiteral("{title}"));
    QToolButton* insertButton = new QToolButton(groupBox);
    insertButton->setObjectName("insertVariableButton");
    insertButton->setText(tr("Insert"));
    connect(insertButton, &QToolButton::clicked, this, [this]() { insertVariable(m_variableComboBox->currentData().toString()); });
    variablesLayout->addWidget(new QLabel(tr("Variable:"), groupBox));
    variablesLayout->addWidget(m_variableComboBox, 1);
    variablesLayout->addWidget(insertButton);
    layout->addLayout(variablesLayout, 3, 1, 1, 3);

    QFormLayout* numberingLayout = new QFormLayout();
    m_startPageNumberSpinBox = new QSpinBox(groupBox);
    m_startPageNumberSpinBox->setObjectName("startPageNumberSpinBox");
    m_startPageNumberSpinBox->setRange(-999999, 999999);
    m_startPageNumberSpinBox->setValue(1);
    m_startPageNumberSpinBox->setToolTip(tr("Number of the first page, to which the header and footer is added."));
    numberingLayout->addRow(tr("Start page number:"), m_startPageNumberSpinBox);
    m_dateFormatEdit = new QLineEdit(QStringLiteral("MM/dd/yyyy"), groupBox);
    m_dateFormatEdit->setObjectName("dateFormatEdit");
    m_dateFormatEdit->setToolTip(tr("Format of the date, for example MM/dd/yyyy, dd.MM.yyyy or yyyy-MM-dd."));
    numberingLayout->addRow(tr("Date format:"), m_dateFormatEdit);
    layout->addLayout(numberingLayout, 4, 1, 1, 3);

    QWidget* widget = new QWidget(this);
    QVBoxLayout* widgetLayout = new QVBoxLayout(widget);
    widgetLayout->setContentsMargins(QMargins());
    widgetLayout->addWidget(groupBox);
    widgetLayout->addWidget(createBatesWidgets(false));
    return widget;
}

QWidget* PDFPageStampDialog::createBatesWidgets(bool withPosition)
{
    QGroupBox* groupBox = new QGroupBox(withPosition ? tr("Bates Number") : tr("Bates Number (variable {bates})"), this);
    QGridLayout* layout = new QGridLayout(groupBox);

    m_batesPrefixEdit = new QLineEdit(groupBox);
    m_batesPrefixEdit->setObjectName("batesPrefixEdit");
    m_batesSuffixEdit = new QLineEdit(groupBox);
    m_batesSuffixEdit->setObjectName("batesSuffixEdit");
    m_batesStartSpinBox = new QSpinBox(groupBox);
    m_batesStartSpinBox->setObjectName("batesStartSpinBox");
    m_batesStartSpinBox->setRange(0, 999999999);
    m_batesStartSpinBox->setValue(1);
    m_batesDigitsSpinBox = new QSpinBox(groupBox);
    m_batesDigitsSpinBox->setObjectName("batesDigitsSpinBox");
    m_batesDigitsSpinBox->setRange(1, 15);
    m_batesDigitsSpinBox->setValue(6);

    layout->addWidget(new QLabel(tr("Prefix:"), groupBox), 0, 0);
    layout->addWidget(m_batesPrefixEdit, 0, 1);
    layout->addWidget(new QLabel(tr("Suffix:"), groupBox), 0, 2);
    layout->addWidget(m_batesSuffixEdit, 0, 3);
    layout->addWidget(new QLabel(tr("Start number:"), groupBox), 1, 0);
    layout->addWidget(m_batesStartSpinBox, 1, 1);
    layout->addWidget(new QLabel(tr("Digits:"), groupBox), 1, 2);
    layout->addWidget(m_batesDigitsSpinBox, 1, 3);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(3, 1);

    if (withPosition)
    {
        m_batesPositionComboBox = new QComboBox(groupBox);
        m_batesPositionComboBox->setObjectName("batesPositionComboBox");
        m_batesPositionComboBox->addItem(tr("Header, left"), int(pdf::PDFHeaderFooterSettings::HeaderLeft));
        m_batesPositionComboBox->addItem(tr("Header, center"), int(pdf::PDFHeaderFooterSettings::HeaderCenter));
        m_batesPositionComboBox->addItem(tr("Header, right"), int(pdf::PDFHeaderFooterSettings::HeaderRight));
        m_batesPositionComboBox->addItem(tr("Footer, left"), int(pdf::PDFHeaderFooterSettings::FooterLeft));
        m_batesPositionComboBox->addItem(tr("Footer, center"), int(pdf::PDFHeaderFooterSettings::FooterCenter));
        m_batesPositionComboBox->addItem(tr("Footer, right"), int(pdf::PDFHeaderFooterSettings::FooterRight));
        m_batesPositionComboBox->setCurrentIndex(m_batesPositionComboBox->findData(int(pdf::PDFHeaderFooterSettings::FooterRight)));
        layout->addWidget(new QLabel(tr("Position:"), groupBox), 2, 0);
        layout->addWidget(m_batesPositionComboBox, 2, 1, 1, 3);

        m_batesRangeLabel = new QLabel(groupBox);
        m_batesRangeLabel->setObjectName("batesRangeLabel");
        layout->addWidget(new QLabel(tr("Numbers:"), groupBox), 3, 0);
        layout->addWidget(m_batesRangeLabel, 3, 1, 1, 3);
    }

    // Offered only in the batch mode
    m_continueBatesCheckBox = new QCheckBox(tr("Continue the numbers in the next file"), groupBox);
    m_continueBatesCheckBox->setObjectName("continueBatesCheckBox");
    m_continueBatesCheckBox->setChecked(true);
    m_continueBatesCheckBox->setToolTip(tr("The first page of the next file gets the number following the last page of the previous file."));
    m_continueBatesCheckBox->setVisible(false);
    layout->addWidget(m_continueBatesCheckBox, layout->rowCount(), 0, 1, 4);

    return groupBox;
}

QWidget* PDFPageStampDialog::createWatermarkPage()
{
    QGroupBox* groupBox = new QGroupBox(tr("Watermark"), this);
    QGridLayout* layout = new QGridLayout(groupBox);

    m_watermarkTextRadioButton = new QRadioButton(tr("Text:"), groupBox);
    m_watermarkTextRadioButton->setObjectName("watermarkTextRadioButton");
    m_watermarkTextRadioButton->setChecked(true);
    m_watermarkTextEdit = new QPlainTextEdit(QStringLiteral("DRAFT"), groupBox);
    m_watermarkTextEdit->setObjectName("watermarkTextEdit");
    m_watermarkTextEdit->setTabChangesFocus(true);
    m_watermarkTextEdit->setFixedHeight(m_watermarkTextEdit->fontMetrics().lineSpacing() * 3 + 2 * m_watermarkTextEdit->frameWidth() + 8);

    m_watermarkImageRadioButton = new QRadioButton(tr("Image:"), groupBox);
    m_watermarkImageRadioButton->setObjectName("watermarkImageRadioButton");
    m_watermarkImageEdit = new QLineEdit(groupBox);
    m_watermarkImageEdit->setObjectName("watermarkImageEdit");
    m_watermarkImageButton = new QToolButton(groupBox);
    m_watermarkImageButton->setObjectName("watermarkImageButton");
    m_watermarkImageButton->setText(tr("..."));
    connect(m_watermarkImageButton, &QToolButton::clicked, this, &PDFPageStampDialog::selectImage);

    QButtonGroup* buttonGroup = new QButtonGroup(groupBox);
    buttonGroup->addButton(m_watermarkTextRadioButton);
    buttonGroup->addButton(m_watermarkImageRadioButton);

    QHBoxLayout* imageLayout = new QHBoxLayout();
    imageLayout->addWidget(m_watermarkImageEdit, 1);
    imageLayout->addWidget(m_watermarkImageButton);

    layout->addWidget(m_watermarkTextRadioButton, 0, 0, Qt::AlignTop);
    layout->addWidget(m_watermarkTextEdit, 0, 1);
    layout->addWidget(m_watermarkImageRadioButton, 1, 0);
    layout->addLayout(imageLayout, 1, 1);

    QFormLayout* placementLayout = new QFormLayout();
    m_rotationSpinBox = new QDoubleSpinBox(groupBox);
    m_rotationSpinBox->setObjectName("rotationSpinBox");
    m_rotationSpinBox->setRange(-360.0, 360.0);
    m_rotationSpinBox->setDecimals(1);
    m_rotationSpinBox->setSuffix(tr(" °"));
    m_rotationSpinBox->setValue(45.0);
    placementLayout->addRow(tr("Rotation:"), m_rotationSpinBox);

    m_relativeSizeSpinBox = new QSpinBox(groupBox);
    m_relativeSizeSpinBox->setObjectName("relativeSizeSpinBox");
    m_relativeSizeSpinBox->setRange(1, 100);
    m_relativeSizeSpinBox->setSuffix(tr(" % of page"));
    m_relativeSizeSpinBox->setValue(50);
    m_relativeSizeSpinBox->setToolTip(tr("Size of the image, or of the text with the automatic font size, relative to the page."));
    placementLayout->addRow(tr("Size:"), m_relativeSizeSpinBox);

    m_alignmentComboBox = new QComboBox(groupBox);
    m_alignmentComboBox->setObjectName("alignmentComboBox");
    m_alignmentComboBox->addItem(tr("Center"), int(Qt::AlignCenter));
    m_alignmentComboBox->addItem(tr("Top"), int(Qt::AlignTop | Qt::AlignHCenter));
    m_alignmentComboBox->addItem(tr("Bottom"), int(Qt::AlignBottom | Qt::AlignHCenter));
    m_alignmentComboBox->addItem(tr("Left"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_alignmentComboBox->addItem(tr("Right"), int(Qt::AlignRight | Qt::AlignVCenter));
    m_alignmentComboBox->addItem(tr("Top left"), int(Qt::AlignTop | Qt::AlignLeft));
    m_alignmentComboBox->addItem(tr("Top right"), int(Qt::AlignTop | Qt::AlignRight));
    m_alignmentComboBox->addItem(tr("Bottom left"), int(Qt::AlignBottom | Qt::AlignLeft));
    m_alignmentComboBox->addItem(tr("Bottom right"), int(Qt::AlignBottom | Qt::AlignRight));
    placementLayout->addRow(tr("Position:"), m_alignmentComboBox);

    m_behindContentCheckBox = new QCheckBox(tr("Behind page content"), groupBox);
    m_behindContentCheckBox->setObjectName("behindContentCheckBox");
    m_behindContentCheckBox->setToolTip(tr("The watermark is hidden by opaque page content, for example by scanned images."));
    placementLayout->addRow(QString(), m_behindContentCheckBox);
    layout->addLayout(placementLayout, 2, 0, 1, 2);

    return groupBox;
}

QWidget* PDFPageStampDialog::createStyleWidgets()
{
    QGroupBox* groupBox = new QGroupBox(tr("Appearance"), this);
    QGridLayout* layout = new QGridLayout(groupBox);

    m_fontComboBox = new QComboBox(groupBox);
    m_fontComboBox->setObjectName("fontComboBox");
    m_fontComboBox->addItem(QStringLiteral("Helvetica"), int(pdf::PDFStampFont::Helvetica));
    m_fontComboBox->addItem(QStringLiteral("Helvetica Bold"), int(pdf::PDFStampFont::HelveticaBold));
    m_fontComboBox->addItem(QStringLiteral("Times"), int(pdf::PDFStampFont::TimesRoman));
    m_fontComboBox->addItem(QStringLiteral("Times Bold"), int(pdf::PDFStampFont::TimesBold));
    m_fontComboBox->addItem(QStringLiteral("Courier"), int(pdf::PDFStampFont::Courier));
    m_fontComboBox->addItem(QStringLiteral("Courier Bold"), int(pdf::PDFStampFont::CourierBold));
    m_fontComboBox->setCurrentIndex(m_mode == Mode::Watermark ? 1 : 0);

    m_fontSizeSpinBox = new QDoubleSpinBox(groupBox);
    m_fontSizeSpinBox->setObjectName("fontSizeSpinBox");
    m_fontSizeSpinBox->setDecimals(1);
    m_fontSizeSpinBox->setSuffix(tr(" pt"));
    if (m_mode == Mode::Watermark)
    {
        m_fontSizeSpinBox->setRange(0.0, 1000.0);
        m_fontSizeSpinBox->setSpecialValueText(tr("Fit to page"));
        m_fontSizeSpinBox->setValue(0.0);
    }
    else
    {
        m_fontSizeSpinBox->setRange(1.0, 200.0);
        m_fontSizeSpinBox->setValue(10.0);
    }

    m_colorButton = new QToolButton(groupBox);
    m_colorButton->setObjectName("colorButton");
    m_colorButton->setToolTip(tr("Color of the text"));
    connect(m_colorButton, &QToolButton::clicked, this, &PDFPageStampDialog::selectColor);

    m_opacitySpinBox = new QSpinBox(groupBox);
    m_opacitySpinBox->setObjectName("opacitySpinBox");
    m_opacitySpinBox->setRange(1, 100);
    m_opacitySpinBox->setSuffix(tr(" %"));
    m_opacitySpinBox->setValue(m_mode == Mode::Watermark ? 30 : 100);

    layout->addWidget(new QLabel(tr("Font:"), groupBox), 0, 0);
    layout->addWidget(m_fontComboBox, 0, 1);
    layout->addWidget(new QLabel(tr("Size:"), groupBox), 0, 2);
    layout->addWidget(m_fontSizeSpinBox, 0, 3);
    layout->addWidget(new QLabel(tr("Color:"), groupBox), 1, 0);
    layout->addWidget(m_colorButton, 1, 1, Qt::AlignLeft);
    layout->addWidget(new QLabel(tr("Opacity:"), groupBox), 1, 2);
    layout->addWidget(m_opacitySpinBox, 1, 3);

    updateColorButton();
    return groupBox;
}

QWidget* PDFPageStampDialog::createMarginsWidgets()
{
    QGroupBox* groupBox = new QGroupBox(tr("Margins (distance from the page edges)"), this);
    QHBoxLayout* layout = new QHBoxLayout(groupBox);

    const QStringList labels = { tr("Left:"), tr("Top:"), tr("Right:"), tr("Bottom:") };
    const QStringList names = { "marginLeftSpinBox", "marginTopSpinBox", "marginRightSpinBox", "marginBottomSpinBox" };
    for (int i = 0; i < 4; ++i)
    {
        QDoubleSpinBox* spinBox = new QDoubleSpinBox(groupBox);
        spinBox->setObjectName(names[i]);
        spinBox->setRange(0.0, 1000.0);
        spinBox->setDecimals(1);
        spinBox->setSuffix(tr(" pt"));
        spinBox->setValue(36.0);
        spinBox->setToolTip(tr("72 points are one inch (25.4 mm)."));
        m_marginSpinBoxes[i] = spinBox;
        layout->addWidget(new QLabel(labels[i], groupBox));
        layout->addWidget(spinBox);
    }

    return groupBox;
}

QWidget* PDFPageStampDialog::createPagesWidgets()
{
    QGroupBox* groupBox = new QGroupBox(tr("Pages"), this);
    QGridLayout* layout = new QGridLayout(groupBox);

    m_allPagesRadioButton = new QRadioButton(tr("All pages"), groupBox);
    m_allPagesRadioButton->setObjectName("allPagesRadioButton");
    m_allPagesRadioButton->setChecked(true);
    m_currentPageRadioButton = new QRadioButton(tr("Current page"), groupBox);
    m_currentPageRadioButton->setObjectName("currentPageRadioButton");
    m_pageRangeRadioButton = new QRadioButton(tr("Pages:"), groupBox);
    m_pageRangeRadioButton->setObjectName("pageRangeRadioButton");
    m_pageRangeEdit = new QLineEdit(groupBox);
    m_pageRangeEdit->setObjectName("pageRangeEdit");
    m_pageRangeEdit->setPlaceholderText(tr("for example 1-3, 5, 8-"));

    QButtonGroup* buttonGroup = new QButtonGroup(groupBox);
    buttonGroup->addButton(m_allPagesRadioButton);
    buttonGroup->addButton(m_currentPageRadioButton);
    buttonGroup->addButton(m_pageRangeRadioButton);

    const pdf::PDFPageStamper::StampKind kind = m_mode == Mode::Watermark ? pdf::PDFPageStamper::Watermark : pdf::PDFPageStamper::HeaderFooter;
    const pdf::PDFInteger stampedPageCount = pdf::PDFPageStamper::getStampedPageCount(m_document, kind);
    QString replaceText = m_mode == Mode::Watermark ? tr("Replace existing watermarks") : tr("Replace existing headers, footers and Bates numbers");
    if (stampedPageCount > 0)
    {
        replaceText += tr(" (on %1 pages)").arg(stampedPageCount);
    }
    m_replaceCheckBox = new QCheckBox(replaceText, groupBox);
    m_replaceCheckBox->setObjectName("replaceCheckBox");
    m_replaceCheckBox->setChecked(stampedPageCount > 0);
    m_replaceCheckBox->setEnabled(stampedPageCount > 0);
    m_replaceCheckBox->setToolTip(tr("Only the stamps added by this application are replaced."));

    layout->addWidget(m_allPagesRadioButton, 0, 0);
    layout->addWidget(m_currentPageRadioButton, 0, 1);
    layout->addWidget(m_pageRangeRadioButton, 1, 0);
    layout->addWidget(m_pageRangeEdit, 1, 1);
    layout->addWidget(m_replaceCheckBox, 2, 0, 1, 2);
    return groupBox;
}

void PDFPageStampDialog::connectChanges()
{
    auto changed = [this]() { updateEnabledState(); schedulePreview(); };

    for (QPlainTextEdit* edit : m_positionEdits)
    {
        if (edit)
        {
            connect(edit, &QPlainTextEdit::textChanged, this, changed);
        }
    }

    for (QLineEdit* edit : { m_dateFormatEdit, m_batesPrefixEdit, m_batesSuffixEdit, m_watermarkImageEdit, m_pageRangeEdit })
    {
        if (edit)
        {
            connect(edit, &QLineEdit::textChanged, this, changed);
        }
    }

    for (QSpinBox* spinBox : { m_startPageNumberSpinBox, m_batesStartSpinBox, m_batesDigitsSpinBox, m_relativeSizeSpinBox, m_opacitySpinBox })
    {
        if (spinBox)
        {
            connect(spinBox, &QSpinBox::valueChanged, this, changed);
        }
    }

    std::vector<QDoubleSpinBox*> doubleSpinBoxes = { m_rotationSpinBox, m_fontSizeSpinBox };
    doubleSpinBoxes.insert(doubleSpinBoxes.end(), m_marginSpinBoxes.cbegin(), m_marginSpinBoxes.cend());
    for (QDoubleSpinBox* spinBox : doubleSpinBoxes)
    {
        if (spinBox)
        {
            connect(spinBox, &QDoubleSpinBox::valueChanged, this, changed);
        }
    }

    for (QComboBox* comboBox : { m_batesPositionComboBox, m_alignmentComboBox, m_fontComboBox })
    {
        if (comboBox)
        {
            connect(comboBox, &QComboBox::currentIndexChanged, this, changed);
        }
    }

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ m_watermarkTextRadioButton, m_watermarkImageRadioButton, m_behindContentCheckBox,
                                                                           m_allPagesRadioButton, m_currentPageRadioButton, m_pageRangeRadioButton })
    {
        if (button)
        {
            connect(button, &QAbstractButton::toggled, this, changed);
        }
    }

    if (m_watermarkTextEdit)
    {
        connect(m_watermarkTextEdit, &QPlainTextEdit::textChanged, this, changed);
    }

    if (m_watermarkImageEdit)
    {
        connect(m_watermarkImageEdit, &QLineEdit::editingFinished, this, [this]()
        {
            m_watermarkImage = QImageReader(m_watermarkImageEdit->text()).read();
            schedulePreview();
        });
    }
}

void PDFPageStampDialog::schedulePreview()
{
    m_previewTimer->start();
}

void PDFPageStampDialog::insertVariable(const QString& variable)
{
    if (m_lastTextEdit)
    {
        m_lastTextEdit->insertPlainText(variable);
        m_lastTextEdit->setFocus();
    }
}

void PDFPageStampDialog::selectColor()
{
    const QColor color = QColorDialog::getColor(m_color, this, tr("Select Color"));
    if (color.isValid())
    {
        m_color = color;
        updateColorButton();
        schedulePreview();
    }
}

void PDFPageStampDialog::selectImage()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Select Image"), m_watermarkImageEdit->text(), tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.svg);;All files (*.*)"));
    if (!fileName.isEmpty())
    {
        m_watermarkImageEdit->setText(fileName);
        m_watermarkImage = QImageReader(fileName).read();
        m_watermarkImageRadioButton->setChecked(true);
        schedulePreview();
    }
}

void PDFPageStampDialog::updateColorButton()
{
    const int size = pdf::PDFWidgetUtils::scaleDPI_x(this, 16);
    QPixmap pixmap(size * 2, size);
    pixmap.fill(m_color);
    QPainter painter(&pixmap);
    painter.setPen(Qt::gray);
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    painter.end();
    m_colorButton->setIcon(QIcon(pixmap));
    m_colorButton->setIconSize(pixmap.size());
}

void PDFPageStampDialog::updateEnabledState()
{
    m_pageRangeEdit->setEnabled(m_pageRangeRadioButton->isChecked());
    m_currentPageRadioButton->setEnabled(m_currentPageIndex >= 0);

    if (m_mode == Mode::Watermark)
    {
        const bool isText = m_watermarkTextRadioButton->isChecked();
        m_watermarkTextEdit->setEnabled(isText);
        m_watermarkImageEdit->setEnabled(!isText);
        m_watermarkImageButton->setEnabled(!isText);
        m_fontComboBox->setEnabled(isText);
        m_fontSizeSpinBox->setEnabled(isText);
        m_colorButton->setEnabled(isText);
    }

    if (m_batesRangeLabel && m_batchMode)
    {
        const pdf::PDFStampVariables variables = getVariables();
        m_batesRangeLabel->setText(tr("from %1").arg(pdf::PDFPageStamper::formatBatesNumber(variables.batesPrefix, variables.batesStart, variables.batesDigits, variables.batesSuffix)));
    }
    else if (m_batesRangeLabel)
    {
        QString errorMessage;
        const pdf::PDFInteger count = pdf::PDFInteger(getPageIndices(&errorMessage).size());
        if (count > 0)
        {
            const pdf::PDFStampVariables variables = getVariables();
            const QString first = pdf::PDFPageStamper::formatBatesNumber(variables.batesPrefix, variables.batesStart, variables.batesDigits, variables.batesSuffix);
            const QString last = pdf::PDFPageStamper::formatBatesNumber(variables.batesPrefix, variables.batesStart + count - 1, variables.batesDigits, variables.batesSuffix);
            m_batesRangeLabel->setText(tr("%1 to %2").arg(first, last));
        }
        else
        {
            m_batesRangeLabel->setText(errorMessage);
        }
    }
}

bool PDFPageStampDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (QPlainTextEdit* edit = qobject_cast<QPlainTextEdit*>(watched))
        {
            m_lastTextEdit = edit;
        }
    }

    return QDialog::eventFilter(watched, event);
}

pdf::PDFStampTextStyle PDFPageStampDialog::getTextStyle() const
{
    pdf::PDFStampTextStyle style;
    style.font = static_cast<pdf::PDFStampFont>(m_fontComboBox->currentData().toInt());
    style.fontSize = m_fontSizeSpinBox->value();
    style.color = m_color;
    style.opacity = m_opacitySpinBox->value() / 100.0;
    return style;
}

pdf::PDFStampVariables PDFPageStampDialog::getVariables() const
{
    pdf::PDFStampVariables variables;
    variables.fileName = m_fileName;

    if (m_startPageNumberSpinBox)
    {
        variables.startPageNumber = m_startPageNumberSpinBox->value();
    }
    if (m_dateFormatEdit && !m_dateFormatEdit->text().trimmed().isEmpty())
    {
        variables.dateFormat = m_dateFormatEdit->text();
    }
    if (m_batesPrefixEdit)
    {
        variables.batesPrefix = m_batesPrefixEdit->text();
        variables.batesSuffix = m_batesSuffixEdit->text();
        variables.batesStart = m_batesStartSpinBox->value();
        variables.batesDigits = m_batesDigitsSpinBox->value();
    }

    return variables;
}

QMarginsF PDFPageStampDialog::getMargins() const
{
    return QMarginsF(m_marginSpinBoxes[0]->value(), m_marginSpinBoxes[1]->value(), m_marginSpinBoxes[2]->value(), m_marginSpinBoxes[3]->value());
}

pdf::PDFHeaderFooterSettings PDFPageStampDialog::getHeaderFooterSettings() const
{
    pdf::PDFHeaderFooterSettings settings;
    settings.style = getTextStyle();
    settings.margins = getMargins();
    settings.variables = getVariables();

    if (m_mode == Mode::Bates)
    {
        const int position = m_batesPositionComboBox->currentData().toInt();
        settings.texts[position] = QStringLiteral("{bates}");
    }
    else
    {
        for (size_t i = 0; i < settings.texts.size(); ++i)
        {
            if (m_positionEdits[i])
            {
                settings.texts[i] = m_positionEdits[i]->toPlainText();
            }
        }
    }

    return settings;
}

pdf::PDFWatermarkSettings PDFPageStampDialog::getWatermarkSettings() const
{
    pdf::PDFWatermarkSettings settings;
    settings.style = getTextStyle();
    settings.margins = getMargins();
    settings.variables = getVariables();

    if (m_mode == Mode::Watermark)
    {
        if (m_watermarkTextRadioButton->isChecked())
        {
            settings.text = m_watermarkTextEdit->toPlainText();
        }
        else
        {
            settings.image = m_watermarkImage;
        }

        settings.rotation = m_rotationSpinBox->value();
        settings.relativeSize = m_relativeSizeSpinBox->value() / 100.0;
        settings.alignment = Qt::Alignment(m_alignmentComboBox->currentData().toInt());
        settings.behindContent = m_behindContentCheckBox->isChecked();
    }

    return settings;
}

std::vector<pdf::PDFInteger> PDFPageStampDialog::getPageIndices(QString* errorMessage) const
{
    std::vector<pdf::PDFInteger> pageIndices;
    const pdf::PDFInteger pageCount = m_document ? pdf::PDFInteger(m_document->getCatalog()->getPageCount()) : 0;

    if (m_currentPageRadioButton->isChecked())
    {
        if (m_currentPageIndex >= 0 && m_currentPageIndex < pageCount)
        {
            pageIndices.push_back(m_currentPageIndex);
        }
    }
    else if (m_pageRangeRadioButton->isChecked())
    {
        QString error;
        pdf::PDFClosedIntervalSet pages = pdf::PDFClosedIntervalSet::parse(1, pageCount, m_pageRangeEdit->text(), &error);
        if (error.isEmpty())
        {
            pages.translate(-1);
            pageIndices = pages.unfold();

            auto invalidIt = std::find_if(pageIndices.cbegin(), pageIndices.cend(), [pageCount](pdf::PDFInteger index) { return index < 0 || index >= pageCount; });
            if (invalidIt != pageIndices.cend())
            {
                error = tr("Page %1 doesn't exist, the document has %2 pages.").arg(*invalidIt + 1).arg(pageCount);
                pageIndices.clear();
                if (errorMessage)
                {
                    *errorMessage = error;
                }
            }
        }
        else if (errorMessage)
        {
            *errorMessage = error;
        }
    }
    else
    {
        pageIndices.resize(pageCount);
        std::iota(pageIndices.begin(), pageIndices.end(), pdf::PDFInteger(0));
    }

    if (pageIndices.empty() && errorMessage && errorMessage->isEmpty())
    {
        *errorMessage = tr("No pages are selected.");
    }

    return pageIndices;
}

bool PDFPageStampDialog::isReplaceExisting() const
{
    return (m_batchMode || m_replaceCheckBox->isEnabled()) && m_replaceCheckBox->isChecked();
}

pdf::PDFPageStamper::Result PDFPageStampDialog::apply() const
{
    QString errorMessage;
    const std::vector<pdf::PDFInteger> pageIndices = getPageIndices(&errorMessage);
    if (pageIndices.empty())
    {
        pdf::PDFPageStamper::Result result;
        result.errorMessage = errorMessage;
        return result;
    }

    if (m_mode == Mode::Watermark)
    {
        return pdf::PDFPageStamper::addWatermark(m_document, getWatermarkSettings(), pageIndices, isReplaceExisting());
    }

    return pdf::PDFPageStamper::addHeaderFooter(m_document, getHeaderFooterSettings(), pageIndices, isReplaceExisting());
}

void PDFPageStampDialog::updatePreview()
{
    m_previewTimer->stop();

    const pdf::PDFPage* page = nullptr;
    if (m_document)
    {
        const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
        const std::vector<pdf::PDFInteger> pageIndices = getPageIndices();
        const pdf::PDFInteger pageIndex = !pageIndices.empty() ? pageIndices.front() : qBound(pdf::PDFInteger(0), m_currentPageIndex, pageCount - 1);
        if (pageIndex >= 0 && pageIndex < pageCount)
        {
            page = m_document->getCatalog()->getPage(pageIndex);
        }
    }

    // The stamp is shown on a blank sheet of the size of the page: drawing
    // of the page content would be slow for large drawings.
    const pdf::PDFDocument previewDocument = PDFPageStampDialogHelper::createBlankPageDocument(page);
    pdf::PDFPageStamper::Result result;
    const pdf::PDFInteger selectedCount = std::max(pdf::PDFInteger(getPageIndices().size()), pdf::PDFInteger(1));

    if (m_mode == Mode::Watermark)
    {
        result = pdf::PDFPageStamper::addWatermark(&previewDocument, getWatermarkSettings(), { 0 }, false);
    }
    else
    {
        pdf::PDFHeaderFooterSettings settings = getHeaderFooterSettings();
        const QString lastPageNumber = QString::number(settings.variables.startPageNumber + selectedCount - 1);
        for (QString& text : settings.texts)
        {
            text.replace(QLatin1String("{pages}"), lastPageNumber);
        }
        result = pdf::PDFPageStamper::addHeaderFooter(&previewDocument, settings, { 0 }, false);
    }

    const QSize maximalSize = m_previewLabel->contentsRect().size().expandedTo(m_previewLabel->minimumSize()) * m_previewLabel->devicePixelRatioF();
    m_previewImage = PDFPageStampDialogHelper::renderPage(result.document ? result.document.data() : &previewDocument, maximalSize);
    m_previewImage.setDevicePixelRatio(m_previewLabel->devicePixelRatioF());
    m_previewLabel->setPixmap(QPixmap::fromImage(m_previewImage));
}

QString PDFPageStampDialog::getSettingsGroup() const
{
    switch (m_mode)
    {
        case Mode::HeaderFooter:
            return QStringLiteral("PageStamp_HeaderFooter");
        case Mode::Bates:
            return QStringLiteral("PageStamp_Bates");
        case Mode::Watermark:
            return QStringLiteral("PageStamp_Watermark");
    }

    return QString();
}

void PDFPageStampDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup(getSettingsGroup());

    if (!settings.contains("font"))
    {
        settings.endGroup();
        return;
    }

    m_fontComboBox->setCurrentIndex(qMax(0, m_fontComboBox->findData(settings.value("font").toInt())));
    m_fontSizeSpinBox->setValue(settings.value("fontSize", m_fontSizeSpinBox->value()).toDouble());
    const QColor color = QColor::fromString(settings.value("color").toString());
    if (color.isValid())
    {
        m_color = color;
        updateColorButton();
    }
    m_opacitySpinBox->setValue(settings.value("opacity", m_opacitySpinBox->value()).toInt());
    const QStringList marginKeys = { "marginLeft", "marginTop", "marginRight", "marginBottom" };
    for (int i = 0; i < 4; ++i)
    {
        m_marginSpinBoxes[i]->setValue(settings.value(marginKeys[i], m_marginSpinBoxes[i]->value()).toDouble());
    }

    for (QPlainTextEdit* edit : m_positionEdits)
    {
        if (edit)
        {
            edit->setPlainText(settings.value(edit->objectName()).toString());
        }
    }

    if (m_dateFormatEdit)
    {
        m_dateFormatEdit->setText(settings.value("dateFormat", m_dateFormatEdit->text()).toString());
    }

    if (m_batesPrefixEdit)
    {
        m_batesPrefixEdit->setText(settings.value("batesPrefix").toString());
        m_batesSuffixEdit->setText(settings.value("batesSuffix").toString());
        m_batesDigitsSpinBox->setValue(settings.value("batesDigits", m_batesDigitsSpinBox->value()).toInt());
    }

    if (m_batesPositionComboBox)
    {
        m_batesPositionComboBox->setCurrentIndex(qMax(0, m_batesPositionComboBox->findData(settings.value("batesPosition", int(pdf::PDFHeaderFooterSettings::FooterRight)).toInt())));
    }

    if (m_mode == Mode::Watermark)
    {
        m_watermarkTextEdit->setPlainText(settings.value("text", m_watermarkTextEdit->toPlainText()).toString());
        m_watermarkImageEdit->setText(settings.value("imageFile").toString());
        if (settings.value("useImage", false).toBool() && !m_watermarkImageEdit->text().isEmpty())
        {
            m_watermarkImage = QImageReader(m_watermarkImageEdit->text()).read();
            m_watermarkImageRadioButton->setChecked(!m_watermarkImage.isNull());
        }
        m_rotationSpinBox->setValue(settings.value("rotation", m_rotationSpinBox->value()).toDouble());
        m_relativeSizeSpinBox->setValue(settings.value("relativeSize", m_relativeSizeSpinBox->value()).toInt());
        m_alignmentComboBox->setCurrentIndex(qMax(0, m_alignmentComboBox->findData(settings.value("alignment", int(Qt::AlignCenter)).toInt())));
        m_behindContentCheckBox->setChecked(settings.value("behindContent", false).toBool());
    }

    settings.endGroup();
}

void PDFPageStampDialog::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(getSettingsGroup());

    settings.setValue("font", m_fontComboBox->currentData().toInt());
    settings.setValue("fontSize", m_fontSizeSpinBox->value());
    settings.setValue("color", m_color.name());
    settings.setValue("opacity", m_opacitySpinBox->value());
    const QStringList marginKeys = { "marginLeft", "marginTop", "marginRight", "marginBottom" };
    for (int i = 0; i < 4; ++i)
    {
        settings.setValue(marginKeys[i], m_marginSpinBoxes[i]->value());
    }

    for (QPlainTextEdit* edit : m_positionEdits)
    {
        if (edit)
        {
            settings.setValue(edit->objectName(), edit->toPlainText());
        }
    }

    if (m_dateFormatEdit)
    {
        settings.setValue("dateFormat", m_dateFormatEdit->text());
    }

    // The Bates start number is not remembered, the next document usually continues
    // with another number (the next number is shown after the numbering)
    if (m_batesPrefixEdit)
    {
        settings.setValue("batesPrefix", m_batesPrefixEdit->text());
        settings.setValue("batesSuffix", m_batesSuffixEdit->text());
        settings.setValue("batesDigits", m_batesDigitsSpinBox->value());
    }

    if (m_batesPositionComboBox)
    {
        settings.setValue("batesPosition", m_batesPositionComboBox->currentData().toInt());
    }

    if (m_mode == Mode::Watermark)
    {
        settings.setValue("text", m_watermarkTextEdit->toPlainText());
        settings.setValue("imageFile", m_watermarkImageEdit->text());
        settings.setValue("useImage", m_watermarkImageRadioButton->isChecked());
        settings.setValue("rotation", m_rotationSpinBox->value());
        settings.setValue("relativeSize", m_relativeSizeSpinBox->value());
        settings.setValue("alignment", m_alignmentComboBox->currentData().toInt());
        settings.setValue("behindContent", m_behindContentCheckBox->isChecked());
    }

    settings.endGroup();
}

void PDFPageStampDialog::accept()
{
    QString errorMessage;
    if (m_batchMode)
    {
        // The documents of the batch are not known, only the form of the pages is checked
        if (!getPageSelection().isEmpty())
        {
            pdf::PDFClosedIntervalSet::parse(1, std::numeric_limits<int>::max(), getPageSelection(), &errorMessage);
        }
        if (!errorMessage.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), errorMessage);
            return;
        }
    }
    else if (getPageIndices(&errorMessage).empty())
    {
        QMessageBox::warning(this, windowTitle(), errorMessage);
        return;
    }

    if (m_mode == Mode::HeaderFooter && getHeaderFooterSettings().isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("Enter a text of a header or of a footer."));
        return;
    }

    if (m_mode == Mode::Watermark)
    {
        const pdf::PDFWatermarkSettings settings = getWatermarkSettings();
        if (settings.text.trimmed().isEmpty() && settings.image.isNull())
        {
            QMessageBox::warning(this, windowTitle(), m_watermarkTextRadioButton->isChecked() ? tr("Enter a text of the watermark.") : tr("Select an image file of the watermark."));
            return;
        }
    }

    if (!m_batchMode)
    {
        saveSettings();
    }
    QDialog::accept();
}

void PDFPageStampDialog::setBatchMode()
{
    m_batchMode = true;
    m_currentPageRadioButton->setVisible(false);
    m_replaceCheckBox->setText(m_mode == Mode::Watermark ? tr("Replace existing watermarks") : tr("Replace existing headers, footers and Bates numbers"));
    m_replaceCheckBox->setEnabled(true);
    m_replaceCheckBox->setChecked(true);
    if (m_continueBatesCheckBox)
    {
        m_continueBatesCheckBox->setVisible(true);
    }
    updateEnabledState();
    schedulePreview();
}

void PDFPageStampDialog::setTextStyle(const pdf::PDFStampTextStyle& style)
{
    m_fontComboBox->setCurrentIndex(qMax(0, m_fontComboBox->findData(int(style.font))));
    m_fontSizeSpinBox->setValue(style.fontSize);
    m_color = style.color;
    updateColorButton();
    m_opacitySpinBox->setValue(qRound(style.opacity * 100.0));
}

void PDFPageStampDialog::setVariables(const pdf::PDFStampVariables& variables)
{
    if (m_startPageNumberSpinBox)
    {
        m_startPageNumberSpinBox->setValue(int(variables.startPageNumber));
    }
    if (m_dateFormatEdit)
    {
        m_dateFormatEdit->setText(variables.dateFormat);
    }
    if (m_batesPrefixEdit)
    {
        m_batesPrefixEdit->setText(variables.batesPrefix);
        m_batesSuffixEdit->setText(variables.batesSuffix);
        m_batesStartSpinBox->setValue(int(variables.batesStart));
        m_batesDigitsSpinBox->setValue(variables.batesDigits);
    }
}

void PDFPageStampDialog::setMargins(const QMarginsF& margins)
{
    m_marginSpinBoxes[0]->setValue(margins.left());
    m_marginSpinBoxes[1]->setValue(margins.top());
    m_marginSpinBoxes[2]->setValue(margins.right());
    m_marginSpinBoxes[3]->setValue(margins.bottom());
}

void PDFPageStampDialog::setHeaderFooterSettings(const pdf::PDFHeaderFooterSettings& settings)
{
    setTextStyle(settings.style);
    setMargins(settings.margins);
    setVariables(settings.variables);

    if (m_mode == Mode::Bates)
    {
        for (int i = 0; i < pdf::PDFHeaderFooterSettings::PositionCount; ++i)
        {
            if (settings.texts[i].contains(QLatin1String("{bates}")))
            {
                m_batesPositionComboBox->setCurrentIndex(qMax(0, m_batesPositionComboBox->findData(i)));
                break;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < settings.texts.size(); ++i)
        {
            if (m_positionEdits[i])
            {
                m_positionEdits[i]->setPlainText(settings.texts[i]);
            }
        }
    }

    updateEnabledState();
    schedulePreview();
}

void PDFPageStampDialog::setWatermarkSettings(const pdf::PDFWatermarkSettings& settings)
{
    setTextStyle(settings.style);
    setMargins(settings.margins);
    setVariables(settings.variables);

    if (m_mode == Mode::Watermark)
    {
        const bool useImage = settings.text.isEmpty() && !settings.image.isNull();
        m_watermarkImage = settings.image;
        m_watermarkTextEdit->setPlainText(settings.text);
        m_watermarkImageEdit->setText(useImage ? tr("(image of the sequence)") : QString());
        m_watermarkTextRadioButton->setChecked(!useImage);
        m_watermarkImageRadioButton->setChecked(useImage);
        m_rotationSpinBox->setValue(settings.rotation);
        m_relativeSizeSpinBox->setValue(qRound(settings.relativeSize * 100.0));
        m_alignmentComboBox->setCurrentIndex(qMax(0, m_alignmentComboBox->findData(int(settings.alignment))));
        m_behindContentCheckBox->setChecked(settings.behindContent);
    }

    updateEnabledState();
    schedulePreview();
}

void PDFPageStampDialog::setReplaceExisting(bool replaceExisting)
{
    m_replaceCheckBox->setChecked(replaceExisting);
}

void PDFPageStampDialog::setPageSelection(const QString& pages)
{
    m_pageRangeEdit->setText(pages.trimmed());
    m_allPagesRadioButton->setChecked(pages.trimmed().isEmpty());
    m_pageRangeRadioButton->setChecked(!pages.trimmed().isEmpty());
    updateEnabledState();
}

QString PDFPageStampDialog::getPageSelection() const
{
    return m_pageRangeRadioButton->isChecked() ? m_pageRangeEdit->text().trimmed() : QString();
}

void PDFPageStampDialog::setContinueBatesNumbering(bool continueNumbering)
{
    if (m_continueBatesCheckBox)
    {
        m_continueBatesCheckBox->setChecked(continueNumbering);
    }
}

bool PDFPageStampDialog::isContinueBatesNumbering() const
{
    return m_continueBatesCheckBox && m_continueBatesCheckBox->isChecked();
}

}   // namespace pdfviewer
