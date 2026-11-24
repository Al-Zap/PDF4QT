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


#include "pdfprintdialog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfannotation.h"
#include "pdfrenderer.h"
#include "pdfcms.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QGroupBox>
#include <QFileDialog>
#include <QPushButton>
#include <QToolButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPrintDialog>
#include <QPrinterInfo>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>

#include <set>
#include <cmath>
#include <numeric>

#include "pdfdbgheap.h"

namespace pdfviewer
{

/// Helper functions of the print dialog
class PDFPrintDialogHelper
{
public:
    PDFPrintDialogHelper() = delete;

    /// Scale choices of the scale combo box
    enum ScaleChoice
    {
        FitToPaper = 0,
        ShrinkToFit = 1,
        ActualSize = 2,
        HalfSize = 3,
        CustomScale = 4
    };

    static constexpr pdf::PDFReal POINTS_PER_INCH = 72.0;
    static constexpr pdf::PDFReal POINTS_PER_MM = 72.0 / 25.4;

    /// Paper sizes offered, when the printer doesn't report its paper sizes (PDF file)
    static QList<QPageSize> getStandardPageSizes()
    {
        QList<QPageSize> pageSizes;
        for (QPageSize::PageSizeId id : { QPageSize::Letter, QPageSize::Legal, QPageSize::Tabloid,
                                          QPageSize::AnsiC, QPageSize::AnsiD, QPageSize::AnsiE,
                                          QPageSize::ArchA, QPageSize::ArchB, QPageSize::ArchC, QPageSize::ArchD, QPageSize::ArchE,
                                          QPageSize::A4, QPageSize::A3, QPageSize::A2, QPageSize::A1, QPageSize::A0 })
        {
            pageSizes.push_back(QPageSize(id));
        }
        return pageSizes;
    }
};

// -------------------------------------------------------------------------
// PDFPrintRenderer
// -------------------------------------------------------------------------

void PDFPrintRenderer::drawSheet(QPainter* painter,
                                 const pdf::PDFPrintSheet& sheet,
                                 const QTransform& paperToDevice,
                                 const pdf::PDFDocument* document,
                                 const pdf::PDFDrawWidgetProxy* proxy,
                                 bool tileMarks)
{
    if (!document || !proxy)
    {
        return;
    }

    const pdf::PDFPage* page = document->getCatalog()->getPage(sheet.pageIndex);
    if (!page)
    {
        return;
    }

    painter->save();
    painter->setWorldTransform(QTransform());
    painter->setClipRect(paperToDevice.mapRect(sheet.printableRect), Qt::IntersectClip);

    const QTransform pagePointToDevicePointMatrix = sheet.createPagePointToDeviceMatrix(page, 1.0) * paperToDevice;

    // Paper gets the colors of the document, not the colors of the screen (inverted
    // colors of the dark mode, high contrast etc.), and the comments are printed too.
    const pdf::PDFRenderer::Features printFeatures = (proxy->getFeatures() & ~pdf::PDFRenderer::getColorFeatures()) | pdf::PDFRenderer::DisplayAnnotations;

    // The document is only read, the annotation manager wants a modifiable document
    pdf::PDFDocument* printedDocument = const_cast<pdf::PDFDocument*>(document);
    pdf::PDFOptionalContentActivity optionalContentActivity(printedDocument, pdf::OCUsage::Print, nullptr);
    pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFRenderer renderer(document, proxy->getFontCache(), proxy->getIccProfileCache(), cms.data(), &optionalContentActivity, printFeatures, proxy->getMeshQualitySettings());
    renderer.render(painter, pagePointToDevicePointMatrix, sheet.pageIndex);

    // Annotations (comments, clouds, measurements, fixture count markers, stamps,
    // filled form fields) are not a part of the page content, they are drawn over it.
    // Annotations marked as not printed are skipped by the print target.
    pdf::PDFAnnotationManager annotationManager(proxy->getFontCache(), proxy->getIccProfileCache(), proxy->getCMSManager(), &optionalContentActivity, proxy->getMeshQualitySettings(), printFeatures, pdf::PDFAnnotationManager::Target::Print, nullptr);
    annotationManager.setDocument(pdf::PDFModifiedDocument(printedDocument, &optionalContentActivity));

    QList<pdf::PDFRenderError> errors;
    pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, sheet.pageIndex);
    annotationManager.drawPage(painter, sheet.pageIndex, nullptr, textLayoutGetter, pagePointToDevicePointMatrix, cms->getColorConvertor(), errors);

    if (tileMarks && sheet.isTile())
    {
        painter->setWorldTransform(QTransform());
        painter->setRenderHint(QPainter::Antialiasing, true);

        const qreal pixelsPerPoint = std::sqrt(std::abs(paperToDevice.determinant()));
        const QRectF printable = sheet.printableRect;

        // Trim lines: the left and top edges of a tile are cut off at the overlap,
        // the tile is laid over the right / bottom edge of its neighbour.
        QPen pen(QColor(96, 96, 96));
        pen.setWidthF(0.5 * pixelsPerPoint);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);

        if (sheet.column > 0)
        {
            const qreal x = printable.left() + sheet.overlap;
            painter->drawLine(paperToDevice.map(QLineF(x, printable.top(), x, printable.bottom())));
        }
        if (sheet.row > 0)
        {
            const qreal y = printable.top() + sheet.overlap;
            painter->drawLine(paperToDevice.map(QLineF(printable.left(), y, printable.right(), y)));
        }

        // Label in the bottom right corner (covered by the neighbouring tiles, when they overlap)
        QFont font = painter->font();
        font.setPixelSize(qMax(1, qRound(7.0 * pixelsPerPoint)));
        painter->setFont(font);

        const QString label = getSheetDescription(sheet);
        const QFontMetricsF fontMetrics(font, painter->device());
        const qreal padding = 2.0 * pixelsPerPoint;
        const QRectF deviceRect = paperToDevice.mapRect(printable);
        QRectF labelRect(QPointF(0, 0), fontMetrics.size(Qt::TextSingleLine, label) + QSizeF(2 * padding, 2 * padding));
        labelRect.moveBottomRight(deviceRect.bottomRight() - QPointF(padding, padding));

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 220));
        painter->drawRect(labelRect);
        painter->setPen(QColor(64, 64, 64));
        painter->drawText(labelRect, Qt::AlignCenter, label);
    }

    painter->restore();
}

bool PDFPrintRenderer::print(QPrinter* printer,
                             const std::vector<pdf::PDFPrintSheet>& sheets,
                             const pdf::PDFDocument* document,
                             const pdf::PDFDrawWidgetProxy* proxy,
                             bool tileMarks,
                             const std::function<bool(void)>& progress)
{
    if (sheets.empty())
    {
        return false;
    }

    // Origin of the painter is the top left corner of the paper
    printer->setFullPage(true);

    QPainter painter;
    if (!painter.begin(printer))
    {
        return false;
    }

    const qreal pixelsPerPoint = printer->resolution() / PDFPrintDialogHelper::POINTS_PER_INCH;
    const QTransform paperToDevice = QTransform::fromScale(pixelsPerPoint, pixelsPerPoint);

    bool result = true;
    for (size_t i = 0; i < sheets.size(); ++i)
    {
        if (i > 0 && !printer->newPage())
        {
            result = false;
            break;
        }

        drawSheet(&painter, sheets[i], paperToDevice, document, proxy, tileMarks);

        if (progress && !progress())
        {
            result = false;
            break;
        }
    }

    painter.end();
    return result;
}

QString PDFPrintRenderer::getSheetDescription(const pdf::PDFPrintSheet& sheet)
{
    const QString scale = QString("%1 %").arg(QLocale().toString(sheet.scale * 100.0, 'f', sheet.scale * 100.0 == qRound(sheet.scale * 100.0) ? 0 : 1));
    if (sheet.isTile())
    {
        return PDFPrintDialog::tr("Page %1, tile %2 of %3 (%4 rows, %5 columns), %6").arg(sheet.pageIndex + 1).arg(sheet.getTileName()).arg(sheet.rows * sheet.columns).arg(sheet.rows).arg(sheet.columns).arg(scale);
    }

    return PDFPrintDialog::tr("Page %1, %2").arg(sheet.pageIndex + 1).arg(scale);
}

// -------------------------------------------------------------------------
// PDFPrintPreviewWidget
// -------------------------------------------------------------------------

PDFPrintPreviewWidget::PDFPrintPreviewWidget(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName("printPreviewWidget");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PDFPrintPreviewWidget::setSheet(const pdf::PDFPrintSheet& sheet, const pdf::PDFDocument* document, const pdf::PDFDrawWidgetProxy* proxy, bool tileMarks)
{
    m_sheet = sheet;
    m_document = document;
    m_proxy = proxy;
    m_tileMarks = tileMarks;
    m_image = QImage();
    update();
}

void PDFPrintPreviewWidget::clear()
{
    m_sheet.reset();
    m_image = QImage();
    update();
}

QImage PDFPrintPreviewWidget::getPaperImage()
{
    updateImage();
    return m_image;
}

QSize PDFPrintPreviewWidget::sizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(420, 480));
}

QSize PDFPrintPreviewWidget::minimumSizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(240, 240));
}

QRectF PDFPrintPreviewWidget::getPaperRect() const
{
    if (!m_sheet || m_sheet->paperSize.isEmpty())
    {
        return QRectF();
    }

    const qreal margin = pdf::PDFWidgetUtils::scaleDPI_x(this, 12);
    const QRectF area = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
    if (area.isEmpty())
    {
        return QRectF();
    }

    QRectF paperRect(QPointF(0, 0), m_sheet->paperSize.scaled(area.size(), Qt::KeepAspectRatio));
    paperRect.moveCenter(area.center());
    return paperRect;
}

void PDFPrintPreviewWidget::updateImage()
{
    const QRectF paperRect = getPaperRect();
    if (paperRect.isEmpty())
    {
        m_image = QImage();
        return;
    }

    const qreal ratio = devicePixelRatioF();
    const QSize imageSize = (paperRect.size() * ratio).toSize();
    if (!m_image.isNull() && m_image.size() == imageSize)
    {
        return;
    }

    m_image = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::white);

    const qreal pixelsPerPoint = qreal(imageSize.width()) / m_sheet->paperSize.width();
    const QTransform paperToDevice = QTransform::fromScale(pixelsPerPoint, pixelsPerPoint);

    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    PDFPrintRenderer::drawSheet(&painter, *m_sheet, paperToDevice, m_document, m_proxy, m_tileMarks);
    painter.end();

    m_image.setDevicePixelRatio(ratio);
}

void PDFPrintPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));

    const QRectF paperRect = getPaperRect();
    if (paperRect.isEmpty())
    {
        return;
    }

    updateImage();

    // Shadow of the paper
    const qreal shadow = pdf::PDFWidgetUtils::scaleDPI_x(this, 3);
    painter.fillRect(paperRect.translated(shadow, shadow), QColor(0, 0, 0, 80));
    painter.drawImage(paperRect.topLeft(), m_image);

    // Printable area (parts outside of it are not printed)
    const qreal scale = paperRect.width() / m_sheet->paperSize.width();
    const QRectF printable(paperRect.topLeft() + m_sheet->printableRect.topLeft() * scale, m_sheet->printableRect.size() * scale);
    if (printable.adjusted(-1, -1, 1, 1).contains(paperRect) == false)
    {
        QPen pen(QColor(0, 120, 215));
        pen.setStyle(Qt::DotLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(printable);
    }

    painter.setPen(QColor(128, 128, 128));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(paperRect);
}

// -------------------------------------------------------------------------
// PDFPrintDialog
// -------------------------------------------------------------------------

PDFPrintDialog::PDFPrintDialog(const pdf::PDFDocument* document,
                               pdf::PDFInteger currentPageIndex,
                               const pdf::PDFDrawWidgetProxy* proxy,
                               QPrinter::PrinterMode printerMode,
                               QWidget* parent) :
    BaseClass(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint),
    m_document(document),
    m_currentPageIndex(currentPageIndex),
    m_proxy(proxy),
    m_printer(std::make_unique<QPrinter>(printerMode))
{
    setWindowTitle(tr("Print"));
    setObjectName("PDFPrintDialog");

    using Helper = PDFPrintDialogHelper;

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();
    mainLayout->addLayout(contentLayout, 1);

    QVBoxLayout* settingsLayout = new QVBoxLayout();
    contentLayout->addLayout(settingsLayout, 0);

    // Printer and paper
    QGroupBox* printerGroupBox = new QGroupBox(tr("Printer"), this);
    QFormLayout* printerLayout = new QFormLayout(printerGroupBox);
    QHBoxLayout* printerRowLayout = new QHBoxLayout();
    m_printerComboBox = new QComboBox(printerGroupBox);
    m_printerComboBox->setObjectName("printerComboBox");
    for (const QString& printerName : QPrinterInfo::availablePrinterNames())
    {
        m_printerComboBox->addItem(printerName, printerName);
    }
    m_printerComboBox->addItem(tr("Save as PDF file"), QString());
    m_propertiesButton = new QPushButton(tr("Properties..."), printerGroupBox);
    m_propertiesButton->setObjectName("propertiesButton");
    m_propertiesButton->setToolTip(tr("Settings of the printer driver (quality, color, paper source)"));
    printerRowLayout->addWidget(m_printerComboBox, 1);
    printerRowLayout->addWidget(m_propertiesButton);
    printerLayout->addRow(tr("Printer:"), printerRowLayout);

    m_paperComboBox = new QComboBox(printerGroupBox);
    m_paperComboBox->setObjectName("paperComboBox");
    printerLayout->addRow(tr("Paper:"), m_paperComboBox);

    m_orientationComboBox = new QComboBox(printerGroupBox);
    m_orientationComboBox->setObjectName("orientationComboBox");
    m_orientationComboBox->addItem(tr("Portrait"), int(QPageLayout::Portrait));
    m_orientationComboBox->addItem(tr("Landscape"), int(QPageLayout::Landscape));
    printerLayout->addRow(tr("Orientation:"), m_orientationComboBox);

    m_copiesSpinBox = new QSpinBox(printerGroupBox);
    m_copiesSpinBox->setObjectName("copiesSpinBox");
    m_copiesSpinBox->setRange(1, 999);
    printerLayout->addRow(tr("Copies:"), m_copiesSpinBox);
    settingsLayout->addWidget(printerGroupBox);

    // Pages
    QGroupBox* pagesGroupBox = new QGroupBox(tr("Pages"), this);
    QGridLayout* pagesLayout = new QGridLayout(pagesGroupBox);
    m_allPagesRadioButton = new QRadioButton(tr("All pages"), pagesGroupBox);
    m_allPagesRadioButton->setObjectName("allPagesRadioButton");
    m_currentPageRadioButton = new QRadioButton(tr("Current page"), pagesGroupBox);
    m_currentPageRadioButton->setObjectName("currentPageRadioButton");
    m_currentPageRadioButton->setEnabled(m_currentPageIndex >= 0);
    m_pageRangeRadioButton = new QRadioButton(tr("Pages:"), pagesGroupBox);
    m_pageRangeRadioButton->setObjectName("pageRangeRadioButton");
    m_pageRangeEdit = new QLineEdit(pagesGroupBox);
    m_pageRangeEdit->setObjectName("pageRangeEdit");
    m_pageRangeEdit->setPlaceholderText(tr("for example 1-3, 5"));
    m_allPagesRadioButton->setChecked(true);
    pagesLayout->addWidget(m_allPagesRadioButton, 0, 0, 1, 2);
    pagesLayout->addWidget(m_currentPageRadioButton, 1, 0, 1, 2);
    pagesLayout->addWidget(m_pageRangeRadioButton, 2, 0);
    pagesLayout->addWidget(m_pageRangeEdit, 2, 1);
    settingsLayout->addWidget(pagesGroupBox);

    // Size and scale
    QGroupBox* scaleGroupBox = new QGroupBox(tr("Size"), this);
    QFormLayout* scaleLayout = new QFormLayout(scaleGroupBox);
    m_scaleComboBox = new QComboBox(scaleGroupBox);
    m_scaleComboBox->setObjectName("scaleComboBox");
    m_scaleComboBox->addItem(tr("Fit to paper"), int(Helper::FitToPaper));
    m_scaleComboBox->addItem(tr("Shrink oversized pages"), int(Helper::ShrinkToFit));
    m_scaleComboBox->addItem(tr("Actual size (100 %)"), int(Helper::ActualSize));
    m_scaleComboBox->addItem(tr("Half size (50 %)"), int(Helper::HalfSize));
    m_scaleComboBox->addItem(tr("Custom scale"), int(Helper::CustomScale));
    m_scaleComboBox->setToolTip(tr("Actual size prints a drawing at its true scale, for example a 24 x 36 in sheet 1:1 on a plotter. "
                                   "Half size prints it at 50 %, so 1\" = 20' on the sheet becomes 1\" = 40' on the paper."));
    scaleLayout->addRow(tr("Scale:"), m_scaleComboBox);

    m_customScaleSpinBox = new QDoubleSpinBox(scaleGroupBox);
    m_customScaleSpinBox->setObjectName("customScaleSpinBox");
    m_customScaleSpinBox->setRange(1.0, 400.0);
    m_customScaleSpinBox->setDecimals(1);
    m_customScaleSpinBox->setSingleStep(5.0);
    m_customScaleSpinBox->setSuffix(tr(" %"));
    m_customScaleSpinBox->setValue(50.0);
    scaleLayout->addRow(tr("Custom scale:"), m_customScaleSpinBox);

    m_autoRotateCheckBox = new QCheckBox(tr("Rotate pages to fit the paper"), scaleGroupBox);
    m_autoRotateCheckBox->setObjectName("autoRotateCheckBox");
    m_autoRotateCheckBox->setChecked(true);
    scaleLayout->addRow(m_autoRotateCheckBox);
    settingsLayout->addWidget(scaleGroupBox);

    // Tiles
    QGroupBox* tilesGroupBox = new QGroupBox(tr("Large pages"), this);
    QFormLayout* tilesLayout = new QFormLayout(tilesGroupBox);
    m_tilingCheckBox = new QCheckBox(tr("Print on several sheets (tiles)"), tilesGroupBox);
    m_tilingCheckBox->setObjectName("tilingCheckBox");
    m_tilingCheckBox->setToolTip(tr("A page larger than the paper is printed in parts, which are taped together "
                                    "(for example a 24 x 36 in sheet at 100 % on letter paper). Not used with Fit to paper."));
    tilesLayout->addRow(m_tilingCheckBox);

    m_overlapSpinBox = new QDoubleSpinBox(tilesGroupBox);
    m_overlapSpinBox->setObjectName("overlapSpinBox");
    if (isImperial())
    {
        m_overlapSpinBox->setRange(0.0, 4.0);
        m_overlapSpinBox->setDecimals(2);
        m_overlapSpinBox->setSingleStep(0.25);
        m_overlapSpinBox->setSuffix(tr(" in"));
        m_overlapSpinBox->setValue(0.5);
    }
    else
    {
        m_overlapSpinBox->setRange(0.0, 100.0);
        m_overlapSpinBox->setDecimals(0);
        m_overlapSpinBox->setSingleStep(5.0);
        m_overlapSpinBox->setSuffix(tr(" mm"));
        m_overlapSpinBox->setValue(10.0);
    }
    tilesLayout->addRow(tr("Overlap:"), m_overlapSpinBox);

    m_tileMarksCheckBox = new QCheckBox(tr("Trim lines and tile labels"), tilesGroupBox);
    m_tileMarksCheckBox->setObjectName("tileMarksCheckBox");
    m_tileMarksCheckBox->setChecked(true);
    tilesLayout->addRow(m_tileMarksCheckBox);
    settingsLayout->addWidget(tilesGroupBox);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("summaryLabel");
    m_summaryLabel->setWordWrap(true);
    settingsLayout->addWidget(m_summaryLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setWordWrap(true);
    QPalette errorPalette = m_errorLabel->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(192, 0, 0));
    m_errorLabel->setPalette(errorPalette);
    settingsLayout->addWidget(m_errorLabel);
    settingsLayout->addStretch(1);

    // Preview
    QVBoxLayout* previewLayout = new QVBoxLayout();
    contentLayout->addLayout(previewLayout, 1);
    m_previewWidget = new PDFPrintPreviewWidget(this);
    previewLayout->addWidget(m_previewWidget, 1);

    QHBoxLayout* navigationLayout = new QHBoxLayout();
    m_previousSheetButton = new QToolButton(this);
    m_previousSheetButton->setObjectName("previousSheetButton");
    m_previousSheetButton->setArrowType(Qt::LeftArrow);
    m_previousSheetButton->setToolTip(tr("Previous sheet"));
    m_nextSheetButton = new QToolButton(this);
    m_nextSheetButton->setObjectName("nextSheetButton");
    m_nextSheetButton->setArrowType(Qt::RightArrow);
    m_nextSheetButton->setToolTip(tr("Next sheet"));
    m_sheetLabel = new QLabel(this);
    m_sheetLabel->setObjectName("sheetLabel");
    m_sheetLabel->setAlignment(Qt::AlignCenter);
    navigationLayout->addWidget(m_previousSheetButton);
    navigationLayout->addWidget(m_sheetLabel, 1);
    navigationLayout->addWidget(m_nextSheetButton);
    previewLayout->addLayout(navigationLayout);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_printButton = buttonBox->addButton(tr("Print"), QDialogButtonBox::AcceptRole);
    m_printButton->setObjectName("printButton");
    m_printButton->setDefault(true);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &PDFPrintDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PDFPrintDialog::reject);

    loadSettings();
    onPrinterChanged();

    connect(m_printerComboBox, &QComboBox::currentIndexChanged, this, &PDFPrintDialog::onPrinterChanged);
    connect(m_paperComboBox, &QComboBox::currentIndexChanged, this, &PDFPrintDialog::onPaperChanged);
    connect(m_orientationComboBox, &QComboBox::currentIndexChanged, this, &PDFPrintDialog::onPaperChanged);
    connect(m_propertiesButton, &QPushButton::clicked, this, &PDFPrintDialog::onPrinterProperties);
    connect(m_allPagesRadioButton, &QRadioButton::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_currentPageRadioButton, &QRadioButton::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_pageRangeRadioButton, &QRadioButton::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_pageRangeEdit, &QLineEdit::textChanged, this, [this]()
    {
        if (!m_pageRangeRadioButton->isChecked())
        {
            m_pageRangeRadioButton->setChecked(true);
        }
        updateUi();
    });
    connect(m_scaleComboBox, &QComboBox::currentIndexChanged, this, &PDFPrintDialog::updateUi);
    connect(m_customScaleSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFPrintDialog::updateUi);
    connect(m_autoRotateCheckBox, &QCheckBox::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_tilingCheckBox, &QCheckBox::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_overlapSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFPrintDialog::updateUi);
    connect(m_tileMarksCheckBox, &QCheckBox::toggled, this, &PDFPrintDialog::updateUi);
    connect(m_previousSheetButton, &QToolButton::clicked, this, [this]() { setCurrentSheet(m_currentSheet - 1); });
    connect(m_nextSheetButton, &QToolButton::clicked, this, [this]() { setCurrentSheet(m_currentSheet + 1); });

    pdf::PDFWidgetUtils::style(this);
    resize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(900, 620)));
}

PDFPrintDialog::~PDFPrintDialog()
{

}

pdf::PDFPrintSettings PDFPrintDialog::getPrintSettings() const
{
    using Helper = PDFPrintDialogHelper;

    pdf::PDFPrintSettings settings;
    switch (m_scaleComboBox->currentData().toInt())
    {
        case Helper::FitToPaper:
            settings.scaleMode = pdf::PDFPrintSettings::ScaleMode::FitToPaper;
            break;

        case Helper::ShrinkToFit:
            settings.scaleMode = pdf::PDFPrintSettings::ScaleMode::ShrinkToFit;
            break;

        case Helper::ActualSize:
            settings.scaleMode = pdf::PDFPrintSettings::ScaleMode::ActualSize;
            break;

        case Helper::HalfSize:
            settings.scaleMode = pdf::PDFPrintSettings::ScaleMode::CustomScale;
            settings.customScale = 0.5;
            break;

        case Helper::CustomScale:
        default:
            settings.scaleMode = pdf::PDFPrintSettings::ScaleMode::CustomScale;
            settings.customScale = m_customScaleSpinBox->value() / 100.0;
            break;
    }

    settings.autoRotate = m_autoRotateCheckBox->isChecked();
    settings.tiling = m_tilingCheckBox->isChecked();
    settings.tileOverlap = m_overlapSpinBox->value() * (isImperial() ? Helper::POINTS_PER_INCH : Helper::POINTS_PER_MM);
    settings.tileMarks = m_tileMarksCheckBox->isChecked();
    return settings;
}

std::vector<pdf::PDFInteger> PDFPrintDialog::getPageIndices(QString* errorMessage) const
{
    std::vector<pdf::PDFInteger> pageIndices;
    const pdf::PDFInteger pageCount = m_document ? pdf::PDFInteger(m_document->getCatalog()->getPageCount()) : 0;
    QString error;

    if (m_currentPageRadioButton->isChecked())
    {
        if (m_currentPageIndex >= 0 && m_currentPageIndex < pageCount)
        {
            pageIndices.push_back(m_currentPageIndex);
        }
    }
    else if (m_pageRangeRadioButton->isChecked())
    {
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
            }
        }
    }
    else
    {
        pageIndices.resize(pageCount);
        std::iota(pageIndices.begin(), pageIndices.end(), pdf::PDFInteger(0));
    }

    if (pageIndices.empty() && error.isEmpty())
    {
        error = tr("No pages are selected.");
    }

    if (errorMessage)
    {
        *errorMessage = error;
    }

    return pageIndices;
}

std::vector<pdf::PDFPrintSheet> PDFPrintDialog::getSheets() const
{
    return pdf::PDFPrintLayout::layoutDocument(m_document, getPageIndices(), getPaperSize(m_printer.get()), getPrintableRect(m_printer.get()), getPrintSettings());
}

QSizeF PDFPrintDialog::getPaperSize(const QPrinter* printer)
{
    QPageLayout layout = printer->pageLayout();
    layout.setUnits(QPageLayout::Point);
    return layout.fullRect().size();
}

QRectF PDFPrintDialog::getPrintableRect(const QPrinter* printer)
{
    QPageLayout layout = printer->pageLayout();
    layout.setUnits(QPageLayout::Point);
    return layout.fullRect().marginsRemoved(layout.minimumMargins());
}

void PDFPrintDialog::setOutputFileName(const QString& fileName)
{
    m_outputFileName = fileName;
}

void PDFPrintDialog::setCurrentSheet(int index)
{
    if (m_sheets.empty())
    {
        m_currentSheet = 0;
    }
    else
    {
        m_currentSheet = qBound(0, index, int(m_sheets.size()) - 1);
    }

    updatePreview();
}

void PDFPrintDialog::accept()
{
    QString errorMessage;
    if (getPageIndices(&errorMessage).empty())
    {
        m_errorLabel->setText(errorMessage);
        return;
    }

    if (isPdfOutput())
    {
        QString fileName = m_outputFileName;
        if (fileName.isEmpty())
        {
            fileName = QFileDialog::getSaveFileName(this, tr("Save as PDF File"), QString(), tr("Portable Document Format (*.pdf)"));
        }

        if (fileName.isEmpty())
        {
            return;
        }

        m_printer->setOutputFormat(QPrinter::PdfFormat);
        m_printer->setOutputFileName(fileName);
    }

    m_printer->setCopyCount(m_copiesSpinBox->value());
    saveSettings();
    BaseClass::accept();
}

void PDFPrintDialog::onPrinterChanged()
{
    if (isPdfOutput())
    {
        m_printer->setOutputFormat(QPrinter::PdfFormat);
    }
    else
    {
        m_printer->setOutputFormat(QPrinter::NativeFormat);
        m_printer->setPrinterName(m_printerComboBox->currentData().toString());
    }

    m_propertiesButton->setEnabled(!isPdfOutput());
    updatePaperSizes();
    onPaperChanged();
}

void PDFPrintDialog::onPaperChanged()
{
    if (m_updatingUi)
    {
        return;
    }

    const int index = m_paperComboBox->currentIndex();
    if (index >= 0 && index < m_pageSizes.size())
    {
        m_printer->setPageSize(m_pageSizes[index]);
    }
    m_printer->setPageOrientation(QPageLayout::Orientation(m_orientationComboBox->currentData().toInt()));
    updateUi();
}

void PDFPrintDialog::onPrinterProperties()
{
    m_printer->setCopyCount(m_copiesSpinBox->value());

    QPrintDialog printDialog(m_printer.get(), this);
    printDialog.setOptions(QPrintDialog::PrintShowPageSize | QPrintDialog::PrintCollateCopies);
    if (printDialog.exec() != QPrintDialog::Accepted)
    {
        return;
    }

    // Printer, paper, orientation and copies might be changed in the dialog of the printer
    const QPageSize pageSize = m_printer->pageLayout().pageSize();
    const QPageLayout::Orientation orientation = m_printer->pageLayout().orientation();

    m_updatingUi = true;
    const int printerIndex = m_printerComboBox->findData(m_printer->printerName());
    if (printerIndex >= 0 && printerIndex != m_printerComboBox->currentIndex())
    {
        m_printerComboBox->blockSignals(true);
        m_printerComboBox->setCurrentIndex(printerIndex);
        m_printerComboBox->blockSignals(false);
        updatePaperSizes();
    }

    for (int i = 0; i < m_pageSizes.size(); ++i)
    {
        if (m_pageSizes[i].isEquivalentTo(pageSize))
        {
            m_paperComboBox->setCurrentIndex(i);
            break;
        }
    }
    m_orientationComboBox->setCurrentIndex(qMax(0, m_orientationComboBox->findData(int(orientation))));
    m_copiesSpinBox->setValue(qMax(1, m_printer->copyCount()));
    m_updatingUi = false;

    m_printer->setPageSize(pageSize);
    m_printer->setPageOrientation(orientation);
    updateUi();
}

void PDFPrintDialog::updatePaperSizes()
{
    // Keep the selected paper, if the printer has it, otherwise use the paper of the printer
    const QString selectedPaper = m_paperComboBox->count() > 0 ? m_paperComboBox->currentText() : m_paperComboBox->property("preferredPaper").toString();
    const QPageSize printerPageSize = m_printer->pageLayout().pageSize();

    m_pageSizes.clear();
    if (!isPdfOutput())
    {
        m_pageSizes = QPrinterInfo::printerInfo(m_printer->printerName()).supportedPageSizes();
    }
    if (m_pageSizes.isEmpty())
    {
        m_pageSizes = PDFPrintDialogHelper::getStandardPageSizes();
    }

    const bool updatingUi = m_updatingUi;
    m_updatingUi = true;
    m_paperComboBox->clear();

    int selectedIndex = -1;
    int printerIndex = -1;
    for (int i = 0; i < m_pageSizes.size(); ++i)
    {
        const QPageSize& pageSize = m_pageSizes[i];
        const QSizeF size = pageSize.size(QPageSize::Inch);
        const QString text = tr("%1 (%2 x %3 in)").arg(pageSize.name(), QLocale().toString(size.width(), 'g', 4), QLocale().toString(size.height(), 'g', 4));
        m_paperComboBox->addItem(text, pageSize.name());

        if (selectedIndex == -1 && !selectedPaper.isEmpty() && (text == selectedPaper || pageSize.name() == selectedPaper))
        {
            selectedIndex = i;
        }
        if (printerIndex == -1 && pageSize.isEquivalentTo(printerPageSize))
        {
            printerIndex = i;
        }
    }

    if (selectedIndex == -1)
    {
        selectedIndex = printerIndex;
    }
    m_paperComboBox->setCurrentIndex(qMax(0, selectedIndex));
    m_updatingUi = updatingUi;
}

void PDFPrintDialog::updateUi()
{
    if (m_updatingUi)
    {
        return;
    }

    using Helper = PDFPrintDialogHelper;
    const int scaleChoice = m_scaleComboBox->currentData().toInt();
    m_customScaleSpinBox->setEnabled(scaleChoice == Helper::CustomScale);
    m_tilingCheckBox->setEnabled(scaleChoice != Helper::FitToPaper);
    m_overlapSpinBox->setEnabled(m_tilingCheckBox->isEnabled() && m_tilingCheckBox->isChecked());
    m_tileMarksCheckBox->setEnabled(m_overlapSpinBox->isEnabled());
    m_pageRangeEdit->setEnabled(true);

    QString errorMessage;
    const std::vector<pdf::PDFInteger> pageIndices = getPageIndices(&errorMessage);
    m_sheets = pdf::PDFPrintLayout::layoutDocument(m_document, pageIndices, getPaperSize(m_printer.get()), getPrintableRect(m_printer.get()), getPrintSettings());

    if (!pageIndices.empty() && m_sheets.empty())
    {
        errorMessage = tr("The pages can't be printed on this paper.");
    }

    // Parts of the pages outside of the paper
    if (errorMessage.isEmpty())
    {
        std::set<pdf::PDFInteger> clippedPages;
        for (const pdf::PDFPrintSheet& sheet : m_sheets)
        {
            if (!sheet.isTile() && !sheet.printableRect.adjusted(-1, -1, 1, 1).contains(sheet.getPageRectOnPaper()))
            {
                clippedPages.insert(sheet.pageIndex);
            }
        }

        if (!clippedPages.empty())
        {
            errorMessage = tr("Some pages are larger than the paper and their edges are not printed (first one is page %1). "
                              "Choose a larger paper or a smaller scale, or print the pages on several sheets.").arg(*clippedPages.begin() + 1);
        }
    }

    m_errorLabel->setText(errorMessage);
    m_errorLabel->setVisible(!errorMessage.isEmpty());

    if (!m_sheets.empty())
    {
        const pdf::PDFPrintSheet& sheet = m_sheets[qBound(0, m_currentSheet, int(m_sheets.size()) - 1)];
        const QSizeF printedSize = sheet.pageSize * sheet.scale;
        QString summary = tr("Pages: %1, sheets of paper: %2.").arg(pageIndices.size()).arg(m_sheets.size());
        summary += QChar(' ');
        summary += tr("Page %1 is %2 and it is printed at %3 % as %4.").arg(sheet.pageIndex + 1)
                   .arg(formatLength(sheet.pageSize.width()) + " x " + formatLength(sheet.pageSize.height()))
                   .arg(QLocale().toString(sheet.scale * 100.0, 'f', 1))
                   .arg(formatLength(printedSize.width()) + " x " + formatLength(printedSize.height()));
        m_summaryLabel->setText(summary);
    }
    else
    {
        m_summaryLabel->clear();
    }

    m_printButton->setEnabled(!m_sheets.empty());
    setCurrentSheet(m_currentSheet);
}

void PDFPrintDialog::updatePreview()
{
    if (m_sheets.empty())
    {
        m_previewWidget->clear();
        m_sheetLabel->setText(tr("Nothing to print"));
        m_previousSheetButton->setEnabled(false);
        m_nextSheetButton->setEnabled(false);
        return;
    }

    const pdf::PDFPrintSheet& sheet = m_sheets[m_currentSheet];
    m_previewWidget->setSheet(sheet, m_document, m_proxy, getPrintSettings().tileMarks);
    m_sheetLabel->setText(tr("Sheet %1 of %2: %3").arg(m_currentSheet + 1).arg(m_sheets.size()).arg(PDFPrintRenderer::getSheetDescription(sheet)));
    m_previousSheetButton->setEnabled(m_currentSheet > 0);
    m_nextSheetButton->setEnabled(m_currentSheet + 1 < int(m_sheets.size()));
}

void PDFPrintDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup("PrintDialog");

    const QString printerName = settings.value("printer", QPrinterInfo::defaultPrinterName()).toString();
    int printerIndex = m_printerComboBox->findData(printerName);
    if (printerIndex < 0)
    {
        printerIndex = m_printerComboBox->findData(QPrinterInfo::defaultPrinterName());
    }
    m_printerComboBox->setCurrentIndex(printerIndex >= 0 ? printerIndex : m_printerComboBox->count() - 1);

    m_paperComboBox->setProperty("preferredPaper", settings.value("paper").toString());
    m_orientationComboBox->setCurrentIndex(qMax(0, m_orientationComboBox->findData(settings.value("orientation", int(QPageLayout::Portrait)).toInt())));
    m_scaleComboBox->setCurrentIndex(qMax(0, m_scaleComboBox->findData(settings.value("scale", int(PDFPrintDialogHelper::FitToPaper)).toInt())));
    m_customScaleSpinBox->setValue(settings.value("customScale", 50.0).toDouble());
    m_autoRotateCheckBox->setChecked(settings.value("autoRotate", true).toBool());
    m_tilingCheckBox->setChecked(settings.value("tiling", false).toBool());
    if (settings.contains("tileOverlap"))
    {
        const pdf::PDFReal overlapPoints = settings.value("tileOverlap").toDouble();
        m_overlapSpinBox->setValue(overlapPoints / (isImperial() ? PDFPrintDialogHelper::POINTS_PER_INCH : PDFPrintDialogHelper::POINTS_PER_MM));
    }
    m_tileMarksCheckBox->setChecked(settings.value("tileMarks", true).toBool());

    settings.endGroup();
}

void PDFPrintDialog::saveSettings() const
{
    const pdf::PDFPrintSettings printSettings = getPrintSettings();

    QSettings settings;
    settings.beginGroup("PrintDialog");
    settings.setValue("printer", m_printerComboBox->currentData().toString());
    settings.setValue("paper", m_paperComboBox->currentData().toString());
    settings.setValue("orientation", m_orientationComboBox->currentData().toInt());
    settings.setValue("scale", m_scaleComboBox->currentData().toInt());
    settings.setValue("customScale", m_customScaleSpinBox->value());
    settings.setValue("autoRotate", printSettings.autoRotate);
    settings.setValue("tiling", printSettings.tiling);
    settings.setValue("tileOverlap", printSettings.tileOverlap);
    settings.setValue("tileMarks", printSettings.tileMarks);
    settings.endGroup();
}

bool PDFPrintDialog::isPdfOutput() const
{
    return m_printerComboBox->currentData().toString().isEmpty();
}

bool PDFPrintDialog::isImperial() const
{
    return QLocale().measurementSystem() != QLocale::MetricSystem;
}

QString PDFPrintDialog::formatLength(pdf::PDFReal points) const
{
    if (isImperial())
    {
        return tr("%1 in").arg(QLocale().toString(points / PDFPrintDialogHelper::POINTS_PER_INCH, 'g', 4));
    }

    return tr("%1 mm").arg(QLocale().toString(points / PDFPrintDialogHelper::POINTS_PER_MM, 'f', 0));
}

}   // namespace pdfviewer
