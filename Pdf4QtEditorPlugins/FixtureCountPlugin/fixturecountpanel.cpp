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

#include "fixturecountpanel.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace pdfplugin
{

FixtureCountPanel::FixtureCountPanel(const std::vector<QAction*>& actions, QWidget* parent) :
    BaseClass(tr("Fixture Count"), parent),
    m_sheetLabel(nullptr),
    m_table(nullptr),
    m_markerSizeSpinBox(nullptr),
    m_typeCount(0),
    m_updating(false)
{
    setObjectName("fixturecount_Panel");

    QWidget* widget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    QToolBar* toolBar = new QToolBar(widget);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (QAction* action : actions)
    {
        if (action)
        {
            toolBar->addAction(action);
        }
        else
        {
            toolBar->addSeparator();
        }
    }
    layout->addWidget(toolBar);

    m_sheetLabel = new QLabel(widget);
    m_sheetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_sheetLabel);

    m_table = new QTableWidget(widget);
    m_table->setObjectName("fixturecount_Table");
    m_table->setColumnCount(LastColumn);
    m_table->setHorizontalHeaderLabels({ tr("Type"), tr("Description"), tr("This Sheet"), tr("Whole Set") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DescriptionColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(SheetColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(TotalColumn, QHeaderView::ResizeToContents);
    m_table->setToolTip(tr("Select the type to count. Double click edits the type."));
    layout->addWidget(m_table, 1);

    QHBoxLayout* sizeLayout = new QHBoxLayout();
    QLabel* sizeLabel = new QLabel(tr("Marker size"), widget);
    m_markerSizeSpinBox = new QDoubleSpinBox(widget);
    m_markerSizeSpinBox->setObjectName("fixturecount_MarkerSize");
    m_markerSizeSpinBox->setRange(2.0, 144.0);
    m_markerSizeSpinBox->setDecimals(0);
    m_markerSizeSpinBox->setSuffix(tr(" pt"));
    m_markerSizeSpinBox->setToolTip(tr("Size of new markers on the sheet (72 pt = 1 inch of paper)"));
    sizeLabel->setBuddy(m_markerSizeSpinBox);
    sizeLayout->addWidget(sizeLabel);
    sizeLayout->addWidget(m_markerSizeSpinBox);
    sizeLayout->addStretch(1);
    layout->addLayout(sizeLayout);

    setWidget(widget);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &FixtureCountPanel::onSelectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &FixtureCountPanel::onCellDoubleClicked);
    connect(m_markerSizeSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        if (!m_updating)
        {
            Q_EMIT markerSizeChanged(value);
        }
    });
}

void FixtureCountPanel::setData(const FixtureTypes& types,
                                const FixtureCountData& countData,
                                pdf::PDFInteger currentPageIndex,
                                const QString& currentSheetName,
                                int currentTypeIndex)
{
    m_updating = true;

    if (currentPageIndex >= 0)
    {
        m_sheetLabel->setText(tr("This sheet: %1").arg(currentSheetName));
    }
    else
    {
        m_sheetLabel->setText(tr("No document is opened."));
    }

    m_typeCount = int(types.size());
    m_table->setRowCount(m_typeCount + 1);

    auto setItem = [this](int row, int column, const QString& text, bool alignRight)
    {
        QTableWidgetItem* item = m_table->item(row, column);
        if (!item)
        {
            item = new QTableWidgetItem();
            m_table->setItem(row, column, item);
        }
        item->setText(text);
        item->setIcon(QIcon());
        item->setTextAlignment(alignRight ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QFont font = m_table->font();
        item->setFont(font);
        return item;
    };

    for (int row = 0; row < m_typeCount; ++row)
    {
        const FixtureType& type = types[row];
        QTableWidgetItem* typeItem = setItem(row, TypeColumn, type.name, false);
        typeItem->setIcon(createTypeIcon(type));
        if (row < 9)
        {
            typeItem->setToolTip(tr("Key %1 selects this type while counting").arg(row + 1));
        }
        setItem(row, DescriptionColumn, type.description, false);
        setItem(row, SheetColumn, currentPageIndex >= 0 ? QString::number(countData.getCount(type.name, currentPageIndex)) : QString(), true);
        setItem(row, TotalColumn, QString::number(countData.getCount(type.name)), true);
    }

    // Totals row
    const int totalRow = m_typeCount;
    QFont boldFont = m_table->font();
    boldFont.setBold(true);
    const QStringList totals = { tr("Total"), QString(),
                                 currentPageIndex >= 0 ? QString::number(countData.getTotalCount(currentPageIndex)) : QString(),
                                 QString::number(countData.getTotalCount()) };
    for (int column = 0; column < LastColumn; ++column)
    {
        QTableWidgetItem* item = setItem(totalRow, column, totals[column], column >= SheetColumn);
        item->setFont(boldFont);
        item->setFlags(Qt::ItemIsEnabled);
    }

    if (currentTypeIndex >= 0 && currentTypeIndex < m_typeCount)
    {
        m_table->selectRow(currentTypeIndex);
    }
    else
    {
        m_table->clearSelection();
    }

    m_updating = false;
}

void FixtureCountPanel::setMarkerSize(pdf::PDFReal markerSize)
{
    m_updating = true;
    m_markerSizeSpinBox->setValue(markerSize);
    m_updating = false;
}

QIcon FixtureCountPanel::createTypeIcon(const FixtureType& type, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fillColor = type.color;
    fillColor.setAlphaF(0.5);
    painter.setPen(QPen(type.color, 1.5));
    painter.setBrush(fillColor);

    // Pixmap has the y axis pointing down, the shape polygon expects the page
    // coordinate system with the y axis pointing up, so the polygon is flipped
    const QRectF rect(1.5, 1.5, size - 3.0, size - 3.0);
    QPolygonF polygon = FixtureType::getShapePolygon(type.shape, rect);
    for (QPointF& point : polygon)
    {
        point.setY(size - point.y());
    }
    painter.drawPolygon(polygon);
    painter.end();

    return QIcon(pixmap);
}

void FixtureCountPanel::onSelectionChanged()
{
    if (m_updating)
    {
        return;
    }

    const QList<QTableWidgetItem*> items = m_table->selectedItems();
    if (!items.isEmpty() && items.front()->row() < m_typeCount)
    {
        Q_EMIT currentTypeChanged(items.front()->row());
    }
}

void FixtureCountPanel::onCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column);

    if (row < m_typeCount)
    {
        Q_EMIT typeEditRequested(row);
    }
}

FixtureTypeDialog::FixtureTypeDialog(const FixtureType& type, const QStringList& usedNames, QWidget* parent) :
    BaseClass(parent),
    m_type(type),
    m_usedNames(usedNames),
    m_nameEdit(nullptr),
    m_descriptionEdit(nullptr),
    m_colorButton(nullptr),
    m_shapeComboBox(nullptr)
{
    setWindowTitle(tr("Fixture Type"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_nameEdit = new QLineEdit(type.name, this);
    m_nameEdit->setObjectName("fixturecount_TypeName");
    m_nameEdit->setPlaceholderText(tr("For example A1"));
    m_nameEdit->selectAll();
    formLayout->addRow(tr("&Type"), m_nameEdit);

    m_descriptionEdit = new QLineEdit(type.description, this);
    m_descriptionEdit->setObjectName("fixturecount_TypeDescription");
    m_descriptionEdit->setPlaceholderText(tr("For example 2x4 LED troffer"));
    formLayout->addRow(tr("&Description"), m_descriptionEdit);

    m_colorButton = new QPushButton(this);
    m_colorButton->setObjectName("fixturecount_TypeColor");
    formLayout->addRow(tr("&Color"), m_colorButton);

    m_shapeComboBox = new QComboBox(this);
    m_shapeComboBox->setObjectName("fixturecount_TypeShape");
    formLayout->addRow(tr("&Shape"), m_shapeComboBox);

    layout->addLayout(formLayout);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &FixtureTypeDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &FixtureTypeDialog::reject);
    connect(m_colorButton, &QPushButton::clicked, this, &FixtureTypeDialog::onColorButtonClicked);
    connect(m_shapeComboBox, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index >= 0)
        {
            m_type.shape = FixtureMarkerShape(m_shapeComboBox->itemData(index).toInt());
        }
    });

    updateColorButton();
    m_nameEdit->setFocus();
}

void FixtureTypeDialog::accept()
{
    const QString name = m_nameEdit->text().trimmed();

    if (name.isEmpty())
    {
        QMessageBox::warning(this, tr("Fixture Type"), tr("Enter the type, for example A1."));
        m_nameEdit->setFocus();
        return;
    }

    if (m_usedNames.contains(name, Qt::CaseInsensitive))
    {
        QMessageBox::warning(this, tr("Fixture Type"), tr("Type %1 already exists.").arg(name));
        m_nameEdit->setFocus();
        return;
    }

    m_type.name = name;
    m_type.description = m_descriptionEdit->text().trimmed();
    BaseClass::accept();
}

void FixtureTypeDialog::updateColorButton()
{
    m_colorButton->setText(m_type.color.name());
    m_colorButton->setIcon(FixtureCountPanel::createTypeIcon(m_type));

    const int currentShape = int(m_type.shape);
    m_shapeComboBox->blockSignals(true);
    m_shapeComboBox->clear();
    for (int i = 0; i < int(FixtureMarkerShape::LastShape); ++i)
    {
        FixtureType shapeType = m_type;
        shapeType.shape = FixtureMarkerShape(i);
        m_shapeComboBox->addItem(FixtureCountPanel::createTypeIcon(shapeType), FixtureType::getShapeName(shapeType.shape), i);
    }
    m_shapeComboBox->setCurrentIndex(currentShape);
    m_shapeComboBox->blockSignals(false);
}

void FixtureTypeDialog::onColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_type.color, this, tr("Marker Color"));
    if (color.isValid())
    {
        m_type.color = color;
        updateColorButton();
    }
}

}   // namespace pdfplugin
