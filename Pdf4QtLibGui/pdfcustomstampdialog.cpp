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


#include "pdfcustomstampdialog.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"

#include <QFile>
#include <QLabel>
#include <QBuffer>
#include <QLineEdit>
#include <QBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QColorDialog>
#include <QFileDialog>
#include <QImageReader>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPainter>

namespace pdfviewer
{

/// Logos are scaled down to this width (in pixels), so the stamps stay small
static constexpr int MAXIMAL_LOGO_WIDTH = 600;

PDFCustomStampDialog::PDFCustomStampDialog(const pdf::PDFCustomStamps& stamps, QWidget* parent) :
    QDialog(parent),
    m_stamps(stamps)
{
    setWindowTitle(tr("Custom Stamps"));

    // List of the stamps
    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName("stampListWidget");
    m_listWidget->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(96, 40)));
    m_addButton = new QPushButton(tr("New"), this);
    m_duplicateButton = new QPushButton(tr("Duplicate"), this);
    m_removeButton = new QPushButton(tr("Delete"), this);
    m_upButton = new QToolButton(this);
    m_upButton->setArrowType(Qt::UpArrow);
    m_upButton->setToolTip(tr("Move Up"));
    m_downButton = new QToolButton(this);
    m_downButton->setArrowType(Qt::DownArrow);
    m_downButton->setToolTip(tr("Move Down"));
    m_importButton = new QPushButton(tr("Import..."), this);
    m_importButton->setToolTip(tr("Add the stamps of a file exported on another computer"));
    m_exportButton = new QPushButton(tr("Export..."), this);
    m_exportButton->setToolTip(tr("Write all stamps to a file, which can be imported on another computer"));

    QHBoxLayout* listButtonsLayout = new QHBoxLayout();
    listButtonsLayout->addWidget(m_addButton);
    listButtonsLayout->addWidget(m_duplicateButton);
    listButtonsLayout->addWidget(m_removeButton);
    listButtonsLayout->addStretch();
    listButtonsLayout->addWidget(m_upButton);
    listButtonsLayout->addWidget(m_downButton);

    QHBoxLayout* fileButtonsLayout = new QHBoxLayout();
    fileButtonsLayout->addWidget(m_importButton);
    fileButtonsLayout->addWidget(m_exportButton);
    fileButtonsLayout->addStretch();

    QVBoxLayout* listLayout = new QVBoxLayout();
    listLayout->addWidget(m_listWidget, 1);
    listLayout->addLayout(listButtonsLayout);
    listLayout->addLayout(fileButtonsLayout);

    // Editors of the current stamp
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("nameEdit");
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName("titleEdit");
    m_titleEdit->setPlaceholderText(tr("REVIEWED"));
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setObjectName("textEdit");
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setMinimumHeight(m_textEdit->fontMetrics().lineSpacing() * 4 + 12);
    m_textEdit->setMaximumHeight(m_textEdit->fontMetrics().lineSpacing() * 6 + 12);
    m_companyEdit = new QLineEdit(this);
    m_companyEdit->setObjectName("companyEdit");
    m_colorButton = new QPushButton(this);
    m_colorButton->setObjectName("colorButton");
    m_logoButton = new QPushButton(tr("Choose Logo..."), this);
    m_logoButton->setObjectName("logoButton");
    m_removeLogoButton = new QPushButton(tr("Remove Logo"), this);
    m_widthSpinBox = new QDoubleSpinBox(this);
    m_widthSpinBox->setObjectName("widthSpinBox");
    m_widthSpinBox->setRange(0.75, 12.0);
    m_widthSpinBox->setDecimals(2);
    m_widthSpinBox->setSingleStep(0.25);
    m_widthSpinBox->setSuffix(tr(" in"));

    QLabel* variablesLabel = new QLabel(tr("Variables: {date}, {time}, {author}, {company}. Empty lines are left out."), this);
    variablesLabel->setWordWrap(true);
    QPalette variablesPalette = variablesLabel->palette();
    variablesPalette.setColor(QPalette::WindowText, variablesPalette.color(QPalette::Disabled, QPalette::WindowText));
    variablesLabel->setPalette(variablesPalette);

    QHBoxLayout* logoLayout = new QHBoxLayout();
    logoLayout->addWidget(m_logoButton);
    logoLayout->addWidget(m_removeLogoButton);
    logoLayout->addStretch();

    QHBoxLayout* colorLayout = new QHBoxLayout();
    colorLayout->addWidget(m_colorButton);
    colorLayout->addStretch();

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("Name"), m_nameEdit);
    formLayout->addRow(tr("Title"), m_titleEdit);
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->addWidget(m_textEdit);
    textLayout->addWidget(variablesLabel);
    formLayout->addRow(tr("Text"), textLayout);
    formLayout->addRow(tr("Company"), m_companyEdit);
    formLayout->addRow(tr("Color"), colorLayout);
    formLayout->addRow(tr("Logo"), logoLayout);
    formLayout->addRow(tr("Width"), m_widthSpinBox);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setObjectName("previewLabel");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, 140));
    m_previewLabel->setFrameShape(QFrame::StyledPanel);
    m_previewLabel->setAutoFillBackground(true);
    QPalette previewPalette = m_previewLabel->palette();
    previewPalette.setColor(QPalette::Window, Qt::white);
    m_previewLabel->setPalette(previewPalette);

    QVBoxLayout* editorLayout = new QVBoxLayout();
    editorLayout->addLayout(formLayout);
    editorLayout->addWidget(new QLabel(tr("Preview"), this));
    editorLayout->addWidget(m_previewLabel, 1);

    QHBoxLayout* mainContentLayout = new QHBoxLayout();
    mainContentLayout->addLayout(listLayout, 2);
    mainContentLayout->addLayout(editorLayout, 3);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(mainContentLayout, 1);
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &PDFCustomStampDialog::onCurrentRowChanged);
    connect(m_addButton, &QPushButton::clicked, this, &PDFCustomStampDialog::addStamp);
    connect(m_duplicateButton, &QPushButton::clicked, this, &PDFCustomStampDialog::duplicateStamp);
    connect(m_removeButton, &QPushButton::clicked, this, &PDFCustomStampDialog::removeStamp);
    connect(m_upButton, &QToolButton::clicked, this, [this]() { moveStamp(-1); });
    connect(m_downButton, &QToolButton::clicked, this, [this]() { moveStamp(+1); });
    connect(m_importButton, &QPushButton::clicked, this, &PDFCustomStampDialog::onImportClicked);
    connect(m_exportButton, &QPushButton::clicked, this, &PDFCustomStampDialog::onExportClicked);
    connect(m_colorButton, &QPushButton::clicked, this, &PDFCustomStampDialog::onColorButtonClicked);
    connect(m_logoButton, &QPushButton::clicked, this, &PDFCustomStampDialog::onLogoButtonClicked);
    connect(m_removeLogoButton, &QPushButton::clicked, this, &PDFCustomStampDialog::removeLogo);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &PDFCustomStampDialog::onEditorChanged);
    connect(m_titleEdit, &QLineEdit::textEdited, this, &PDFCustomStampDialog::onEditorChanged);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, &PDFCustomStampDialog::onEditorChanged);
    connect(m_companyEdit, &QLineEdit::textEdited, this, &PDFCustomStampDialog::onEditorChanged);
    connect(m_widthSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFCustomStampDialog::onEditorChanged);

    updateList();
    setCurrentIndex(m_stamps.empty() ? -1 : 0);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(860, 520));
    pdf::PDFWidgetUtils::style(this);
}

PDFCustomStampDialog::~PDFCustomStampDialog() = default;

int PDFCustomStampDialog::getCurrentIndex() const
{
    const int row = m_listWidget->currentRow();
    return (row >= 0 && size_t(row) < m_stamps.size()) ? row : -1;
}

void PDFCustomStampDialog::setCurrentIndex(int index)
{
    m_listWidget->setCurrentRow(index);
    updateEditors();
}

pdf::PDFCustomStamp* PDFCustomStampDialog::getCurrentStamp()
{
    const int index = getCurrentIndex();
    return index >= 0 ? &m_stamps[index] : nullptr;
}

const pdf::PDFCustomStamp* PDFCustomStampDialog::getCurrentStamp() const
{
    const int index = getCurrentIndex();
    return index >= 0 ? &m_stamps[index] : nullptr;
}

void PDFCustomStampDialog::addStamp()
{
    pdf::PDFCustomStamp stamp;
    stamp.id = pdf::PDFCustomStampLibrary::createId();
    stamp.name = tr("New Stamp");
    stamp.title = tr("REVIEWED");
    stamp.text = QStringLiteral("{company}\n{author}\n{date} {time}");
    if (const pdf::PDFCustomStamp* currentStamp = getCurrentStamp())
    {
        // The company and the logo are usually the same for all stamps
        stamp.company = currentStamp->company;
        stamp.logo = currentStamp->logo;
    }
    m_stamps.push_back(std::move(stamp));
    updateList();
    setCurrentIndex(int(m_stamps.size()) - 1);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void PDFCustomStampDialog::duplicateStamp()
{
    const pdf::PDFCustomStamp* currentStamp = getCurrentStamp();
    if (!currentStamp)
    {
        return;
    }

    pdf::PDFCustomStamp stamp = *currentStamp;
    stamp.id = pdf::PDFCustomStampLibrary::createId();
    stamp.name = tr("%1 (Copy)").arg(stamp.name);
    const int index = getCurrentIndex() + 1;
    m_stamps.insert(std::next(m_stamps.begin(), index), std::move(stamp));
    updateList();
    setCurrentIndex(index);
}

void PDFCustomStampDialog::removeStamp()
{
    const int index = getCurrentIndex();
    if (index < 0)
    {
        return;
    }

    m_stamps.erase(std::next(m_stamps.begin(), index));
    updateList();
    setCurrentIndex(qMin(index, int(m_stamps.size()) - 1));
}

void PDFCustomStampDialog::moveStamp(int direction)
{
    const int index = getCurrentIndex();
    const int newIndex = index + direction;
    if (index < 0 || newIndex < 0 || size_t(newIndex) >= m_stamps.size())
    {
        return;
    }

    std::swap(m_stamps[index], m_stamps[newIndex]);
    updateList();
    setCurrentIndex(newIndex);
}

bool PDFCustomStampDialog::setLogoFromFile(const QString& fileName)
{
    pdf::PDFCustomStamp* stamp = getCurrentStamp();
    if (!stamp)
    {
        return false;
    }

    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        return false;
    }

    if (image.width() > MAXIMAL_LOGO_WIDTH)
    {
        image = image.scaledToWidth(MAXIMAL_LOGO_WIDTH, Qt::SmoothTransformation);
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);
    image.save(&buffer, image.hasAlphaChannel() ? "PNG" : "JPEG", image.hasAlphaChannel() ? -1 : 90);
    stamp->logo = data;
    updateEditors();
    updateList();
    return true;
}

void PDFCustomStampDialog::removeLogo()
{
    if (pdf::PDFCustomStamp* stamp = getCurrentStamp())
    {
        stamp->logo.clear();
        updateEditors();
        updateList();
    }
}

void PDFCustomStampDialog::setColor(const QColor& color)
{
    pdf::PDFCustomStamp* stamp = getCurrentStamp();
    if (stamp && color.isValid())
    {
        stamp->color = color;
        updateEditors();
        updateList();
    }
}

int PDFCustomStampDialog::importStamps(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
    {
        return -1;
    }

    bool ok = false;
    pdf::PDFCustomStamps stamps = pdf::PDFCustomStampLibrary::fromJson(file.readAll(), &ok);
    if (!ok)
    {
        return -1;
    }

    for (pdf::PDFCustomStamp& stamp : stamps)
    {
        // A stamp imported again replaces the old one
        auto it = std::find_if(m_stamps.begin(), m_stamps.end(), [&stamp](const pdf::PDFCustomStamp& existingStamp) { return existingStamp.id == stamp.id; });
        if (it != m_stamps.end())
        {
            *it = std::move(stamp);
        }
        else
        {
            m_stamps.push_back(std::move(stamp));
        }
    }

    updateList();
    setCurrentIndex(m_stamps.empty() ? -1 : qMax(getCurrentIndex(), 0));
    return int(stamps.size());
}

bool PDFCustomStampDialog::exportStamps(const QString& fileName) const
{
    return pdf::PDFCustomStampLibrary::save(m_stamps, fileName);
}

QImage PDFCustomStampDialog::getPreviewImage() const
{
    const pdf::PDFCustomStamp* stamp = getCurrentStamp();
    if (!stamp)
    {
        return QImage();
    }

    const qreal devicePixelRatio = devicePixelRatioF();
    QImage image = pdf::PDFCustomStampLibrary::renderPreview(*stamp, pdf::PDFAuthorSettings::getAuthorName(), QDateTime::currentDateTime(),
                                                             pdf::PDFWidgetUtils::scaleDPI_x(this, 1.0) * devicePixelRatio * 96.0 / 72.0);
    image.setDevicePixelRatio(devicePixelRatio);
    return image;
}

void PDFCustomStampDialog::updateList()
{
    const int row = m_listWidget->currentRow();
    const QSize iconSize = m_listWidget->iconSize() * m_listWidget->devicePixelRatioF();

    QSignalBlocker blocker(m_listWidget);
    m_listWidget->clear();
    for (const pdf::PDFCustomStamp& stamp : m_stamps)
    {
        const QImage image = pdf::PDFCustomStampLibrary::renderPreview(stamp, pdf::PDFAuthorSettings::getAuthorName(), QDateTime::currentDateTime(), 2.0);
        QPixmap pixmap(iconSize);
        pixmap.fill(Qt::transparent);
        if (!image.isNull())
        {
            QImage scaledImage = image.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter painter(&pixmap);
            painter.drawImage((iconSize.width() - scaledImage.width()) / 2, (iconSize.height() - scaledImage.height()) / 2, scaledImage);
        }
        pixmap.setDevicePixelRatio(m_listWidget->devicePixelRatioF());
        m_listWidget->addItem(new QListWidgetItem(QIcon(pixmap), stamp.name));
    }
    m_listWidget->setCurrentRow(qMin(row, m_listWidget->count() - 1));
    updateButtons();
}

void PDFCustomStampDialog::updateEditors()
{
    const pdf::PDFCustomStamp* stamp = getCurrentStamp();

    m_updatingEditors = true;
    m_nameEdit->setText(stamp ? stamp->name : QString());
    m_titleEdit->setText(stamp ? stamp->title : QString());
    if (m_textEdit->toPlainText() != (stamp ? stamp->text : QString()))
    {
        m_textEdit->setPlainText(stamp ? stamp->text : QString());
    }
    m_companyEdit->setText(stamp ? stamp->company : QString());
    m_widthSpinBox->setValue(stamp ? stamp->width / 72.0 : 3.0);

    const QColor color = stamp ? stamp->color : QColor(Qt::gray);
    QPixmap colorPixmap(m_colorButton->iconSize());
    colorPixmap.fill(color);
    m_colorButton->setIcon(QIcon(colorPixmap));
    m_colorButton->setText(color.name().toUpper());
    m_updatingEditors = false;

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_nameEdit, m_titleEdit, m_textEdit, m_companyEdit, m_widthSpinBox, m_colorButton, m_logoButton })
    {
        widget->setEnabled(stamp != nullptr);
    }
    m_removeLogoButton->setEnabled(stamp && !stamp->logo.isEmpty());

    updateButtons();
    updatePreview();
}

void PDFCustomStampDialog::updatePreview()
{
    m_previewLabel->setPixmap(QPixmap::fromImage(getPreviewImage()));
}

void PDFCustomStampDialog::updateButtons()
{
    const int index = getCurrentIndex();
    m_duplicateButton->setEnabled(index >= 0);
    m_removeButton->setEnabled(index >= 0);
    m_upButton->setEnabled(index > 0);
    m_downButton->setEnabled(index >= 0 && size_t(index + 1) < m_stamps.size());
    m_exportButton->setEnabled(!m_stamps.empty());

    // A stamp without a name can't be saved
    bool isValid = true;
    for (const pdf::PDFCustomStamp& stamp : m_stamps)
    {
        isValid = isValid && !stamp.name.trimmed().isEmpty();
    }
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isValid);
}

void PDFCustomStampDialog::onCurrentRowChanged(int row)
{
    Q_UNUSED(row);
    updateEditors();
}

void PDFCustomStampDialog::onEditorChanged()
{
    pdf::PDFCustomStamp* stamp = getCurrentStamp();
    if (m_updatingEditors || !stamp)
    {
        return;
    }

    stamp->name = m_nameEdit->text();
    stamp->title = m_titleEdit->text();
    stamp->text = m_textEdit->toPlainText();
    stamp->company = m_companyEdit->text();
    stamp->width = m_widthSpinBox->value() * 72.0;

    const int index = getCurrentIndex();
    if (QListWidgetItem* item = m_listWidget->item(index))
    {
        item->setText(stamp->name);
    }
    updateButtons();
    updatePreview();
}

void PDFCustomStampDialog::onColorButtonClicked()
{
    if (const pdf::PDFCustomStamp* stamp = getCurrentStamp())
    {
        setColor(QColorDialog::getColor(stamp->color, this, tr("Stamp Color")));
    }
}

void PDFCustomStampDialog::onLogoButtonClicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Choose Logo"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (!fileName.isEmpty() && !setLogoFromFile(fileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("The file '%1' is not an image.").arg(fileName));
    }
}

void PDFCustomStampDialog::onImportClicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Import Stamps"), QString(), tr("Stamps (*.json)"));
    if (!fileName.isEmpty() && importStamps(fileName) < 0)
    {
        QMessageBox::critical(this, tr("Error"), tr("The file '%1' is not a file of stamps.").arg(fileName));
    }
}

void PDFCustomStampDialog::onExportClicked()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Stamps"), QStringLiteral("stamps.json"), tr("Stamps (*.json)"));
    if (!fileName.isEmpty() && !exportStamps(fileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("The file '%1' can't be written.").arg(fileName));
    }
}

}   // namespace pdfviewer
