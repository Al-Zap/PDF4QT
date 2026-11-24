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


#include "lightingdialogs.h"

#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pdfplugin
{

/// Helper functions of the dialogs
class LightingDialogsHelper
{
public:
    LightingDialogsHelper() = delete;

    /// Fills the table widget by the rows
    static void fillTable(QTableWidget* tableWidget, const std::vector<QStringList>& rows, int boldRow);
};

void LightingDialogsHelper::fillTable(QTableWidget* tableWidget, const std::vector<QStringList>& rows, int boldRow)
{
    int columnCount = 0;
    for (const QStringList& row : rows)
    {
        columnCount = qMax(columnCount, int(row.size()));
    }

    tableWidget->clear();
    tableWidget->setRowCount(int(rows.size()));
    tableWidget->setColumnCount(columnCount);

    for (int i = 0; i < int(rows.size()); ++i)
    {
        for (int j = 0; j < rows[i].size(); ++j)
        {
            QTableWidgetItem* item = new QTableWidgetItem(rows[i][j]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (i == boldRow)
            {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            tableWidget->setItem(i, j, item);
        }
    }

    tableWidget->resizeColumnsToContents();
}

FootcandleStatsDialog::FootcandleStatsDialog(const FootcandleValues& values, const FootcandleSettings& settings, QWidget* parent) :
    QDialog(parent),
    m_values(values)
{
    setWindowTitle(tr("Footcandle Grid Statistics"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_labelEdit = new QLineEdit(settings.label, this);
    m_labelEdit->setPlaceholderText(tr("Parking Lot"));
    m_labelEdit->setObjectName("labelEdit");
    formLayout->addRow(tr("Area name:"), m_labelEdit);

    m_unitComboBox = new QComboBox(this);
    m_unitComboBox->setObjectName("unitComboBox");
    m_unitComboBox->addItem(tr("Footcandles (fc)"), QStringLiteral("fc"));
    m_unitComboBox->addItem(tr("Lux (lx)"), QStringLiteral("lx"));
    m_unitComboBox->setCurrentIndex(qMax(0, m_unitComboBox->findData(settings.unit)));
    formLayout->addRow(tr("Values are in:"), m_unitComboBox);

    m_ignoreZerosCheckBox = new QCheckBox(tr("Leave out the points with the value 0.0"), this);
    m_ignoreZerosCheckBox->setObjectName("ignoreZerosCheckBox");
    m_ignoreZerosCheckBox->setChecked(settings.ignoreZeros);
    formLayout->addRow(QString(), m_ignoreZerosCheckBox);
    layout->addLayout(formLayout);

    QGroupBox* resultsGroupBox = new QGroupBox(tr("Statistics"), this);
    QFormLayout* resultsLayout = new QFormLayout(resultsGroupBox);
    auto createLabel = [this](const char* name)
    {
        QLabel* label = new QLabel(this);
        label->setObjectName(name);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        return label;
    };
    m_countLabel = createLabel("countLabel");
    m_averageLabel = createLabel("averageLabel");
    m_maximumLabel = createLabel("maximumLabel");
    m_minimumLabel = createLabel("minimumLabel");
    m_averageToMinimumLabel = createLabel("averageToMinimumLabel");
    m_maximumToMinimumLabel = createLabel("maximumToMinimumLabel");
    resultsLayout->addRow(tr("Calculation points:"), m_countLabel);
    resultsLayout->addRow(tr("Average:"), m_averageLabel);
    resultsLayout->addRow(tr("Maximum:"), m_maximumLabel);
    resultsLayout->addRow(tr("Minimum:"), m_minimumLabel);
    resultsLayout->addRow(tr("Avg:Min:"), m_averageToMinimumLabel);
    resultsLayout->addRow(tr("Max:Min:"), m_maximumToMinimumLabel);
    layout->addWidget(resultsGroupBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_placeButton = buttonBox->addButton(tr("Place Table on Sheet"), QDialogButtonBox::AcceptRole);
    m_placeButton->setObjectName("placeButton");
    m_placeButton->setDefault(true);
    connect(m_placeButton, &QPushButton::clicked, this, [this]() { done(PlaceResult); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_ignoreZerosCheckBox, &QCheckBox::toggled, this, &FootcandleStatsDialog::updateStatistics);
    connect(m_unitComboBox, &QComboBox::currentIndexChanged, this, &FootcandleStatsDialog::updateStatistics);

    updateStatistics();
    pdf::PDFWidgetUtils::style(this);
}

FootcandleSettings FootcandleStatsDialog::getSettings() const
{
    FootcandleSettings settings;
    settings.ignoreZeros = m_ignoreZerosCheckBox->isChecked();
    settings.unit = m_unitComboBox->currentData().toString();
    settings.label = m_labelEdit->text().trimmed();
    return settings;
}

FootcandleStatistics FootcandleStatsDialog::getStatistics() const
{
    return FootcandleStatistics::compute(m_values, m_ignoreZerosCheckBox->isChecked());
}

QString FootcandleStatsDialog::getText() const
{
    const FootcandleSettings settings = getSettings();
    const LightingTable table = FootcandleGrid::createTable(getStatistics(), settings.label, settings.unit);
    return LightingTableWriter::getText(table);
}

void FootcandleStatsDialog::updateStatistics()
{
    const FootcandleStatistics statistics = getStatistics();
    const QString unit = m_unitComboBox->currentData().toString();

    m_countLabel->setText(QString::number(statistics.count));
    m_placeButton->setEnabled(statistics.isValid());

    if (!statistics.isValid())
    {
        const QString none = tr("-");
        m_averageLabel->setText(none);
        m_maximumLabel->setText(none);
        m_minimumLabel->setText(none);
        m_averageToMinimumLabel->setText(none);
        m_maximumToMinimumLabel->setText(none);
        return;
    }

    m_averageLabel->setText(QString("%1 %2").arg(statistics.average, 0, 'f', statistics.decimals).arg(unit));
    m_maximumLabel->setText(QString("%1 %2").arg(statistics.maximum, 0, 'f', statistics.decimals).arg(unit));
    m_minimumLabel->setText(QString("%1 %2").arg(statistics.minimum, 0, 'f', statistics.decimals).arg(unit));
    m_averageToMinimumLabel->setText(FootcandleGrid::formatRatio(statistics.getAverageToMinimum(), statistics.hasRatios()));
    m_maximumToMinimumLabel->setText(FootcandleGrid::formatRatio(statistics.getMaximumToMinimum(), statistics.hasRatios()));
}

FixtureScheduleDialog::FixtureScheduleDialog(const FixtureScheduleTable& table, QWidget* parent) :
    QDialog(parent),
    m_table(table)
{
    setWindowTitle(tr("Fixture Schedule"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_headerRowSpinBox = new QSpinBox(this);
    m_headerRowSpinBox->setObjectName("headerRowSpinBox");
    m_headerRowSpinBox->setRange(0, qMax(int(table.size()), 1));
    m_headerRowSpinBox->setSpecialValueText(tr("None"));
    m_headerRowSpinBox->setValue(FixtureSchedule::findHeaderRow(table) + 1);
    formLayout->addRow(tr("Header row:"), m_headerRowSpinBox);

    m_typeColumnComboBox = new QComboBox(this);
    m_typeColumnComboBox->setObjectName("typeColumnComboBox");
    formLayout->addRow(tr("Fixture type column:"), m_typeColumnComboBox);

    m_descriptionColumnComboBox = new QComboBox(this);
    m_descriptionColumnComboBox->setObjectName("descriptionColumnComboBox");
    formLayout->addRow(tr("Description column:"), m_descriptionColumnComboBox);

    m_joinWrappedRowsCheckBox = new QCheckBox(tr("Join rows without a type to the row above (wrapped text)"), this);
    m_joinWrappedRowsCheckBox->setObjectName("joinWrappedRowsCheckBox");
    m_joinWrappedRowsCheckBox->setChecked(true);
    formLayout->addRow(QString(), m_joinWrappedRowsCheckBox);
    layout->addLayout(formLayout);

    m_previewTable = new QTableWidget(this);
    m_previewTable->setObjectName("previewTable");
    m_previewTable->setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 260)));
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_previewTable, 1);

    m_typesLabel = new QLabel(this);
    m_typesLabel->setObjectName("typesLabel");
    m_typesLabel->setWordWrap(true);
    layout->addWidget(m_typesLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* exportButton = buttonBox->addButton(tr("Export to CSV..."), QDialogButtonBox::ActionRole);
    exportButton->setObjectName("exportButton");
    m_createTypesButton = buttonBox->addButton(tr("Create Count Types"), QDialogButtonBox::AcceptRole);
    m_createTypesButton->setObjectName("createTypesButton");
    connect(exportButton, &QPushButton::clicked, this, [this]() { Q_EMIT exportRequested(getTable()); });
    connect(m_createTypesButton, &QPushButton::clicked, this, [this]() { done(CreateTypesResult); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_headerRowSpinBox, &QSpinBox::valueChanged, this, &FixtureScheduleDialog::updateColumns);
    connect(m_typeColumnComboBox, &QComboBox::currentIndexChanged, this, &FixtureScheduleDialog::updatePreview);
    connect(m_descriptionColumnComboBox, &QComboBox::currentIndexChanged, this, &FixtureScheduleDialog::updatePreview);
    connect(m_joinWrappedRowsCheckBox, &QCheckBox::toggled, this, &FixtureScheduleDialog::updatePreview);

    updateColumns();
    pdf::PDFWidgetUtils::style(this);
}

FixtureScheduleTable FixtureScheduleDialog::getTable() const
{
    if (m_joinWrappedRowsCheckBox->isChecked())
    {
        return FixtureSchedule::joinWrappedRows(m_table, getHeaderRow(), getTypeColumn());
    }

    return m_table;
}

int FixtureScheduleDialog::getHeaderRow() const
{
    return m_headerRowSpinBox->value() - 1;
}

int FixtureScheduleDialog::getTypeColumn() const
{
    return m_typeColumnComboBox->currentData().toInt();
}

int FixtureScheduleDialog::getDescriptionColumn() const
{
    return m_descriptionColumnComboBox->currentData().toInt();
}

void FixtureScheduleDialog::updateColumns()
{
    const int headerRow = getHeaderRow();
    int columnCount = 0;
    for (const QStringList& row : m_table)
    {
        columnCount = qMax(columnCount, int(row.size()));
    }

    QSignalBlocker typeBlocker(m_typeColumnComboBox);
    QSignalBlocker descriptionBlocker(m_descriptionColumnComboBox);
    m_typeColumnComboBox->clear();
    m_descriptionColumnComboBox->clear();
    m_descriptionColumnComboBox->addItem(tr("None"), -1);

    for (int i = 0; i < columnCount; ++i)
    {
        QString name = tr("Column %1").arg(i + 1);
        if (headerRow >= 0 && headerRow < int(m_table.size()) && i < m_table[headerRow].size() && !m_table[headerRow][i].isEmpty())
        {
            name = QString("%1: %2").arg(name, m_table[headerRow][i]);
        }
        m_typeColumnComboBox->addItem(name, i);
        m_descriptionColumnComboBox->addItem(name, i);
    }

    const int typeColumn = FixtureSchedule::findTypeColumn(m_table, headerRow);
    m_typeColumnComboBox->setCurrentIndex(qMax(0, m_typeColumnComboBox->findData(typeColumn)));
    m_descriptionColumnComboBox->setCurrentIndex(qMax(0, m_descriptionColumnComboBox->findData(FixtureSchedule::findDescriptionColumn(m_table, headerRow, typeColumn))));

    updatePreview();
}

void FixtureScheduleDialog::updatePreview()
{
    const FixtureScheduleTable table = getTable();
    LightingDialogsHelper::fillTable(m_previewTable, table, getHeaderRow());

    const FixtureTypes types = FixtureSchedule::createTypes(table, getHeaderRow(), getTypeColumn(), getDescriptionColumn(), FixtureTypes());
    QStringList names;
    for (const FixtureType& type : types)
    {
        names << type.name;
    }

    m_createTypesButton->setEnabled(!types.empty());
    m_typesLabel->setText(types.empty() ? tr("No fixture types were found in the selected column.")
                                        : tr("Fixture types: %1").arg(names.join(QStringLiteral(", "))));
}

WireRunDialog::WireRunDialog(const QStringList& circuits,
                             const QString& circuit,
                             int conductors,
                             pdf::PDFReal paperLength,
                             const WireScale& scale,
                             pdf::PDFReal userUnit,
                             QWidget* parent) :
    QDialog(parent),
    m_paperLength(paperLength),
    m_drawingScale(scale),
    m_userUnit(userUnit),
    m_paperInchesSpinBox(nullptr),
    m_realLengthSpinBox(nullptr),
    m_realUnitComboBox(nullptr)
{
    setWindowTitle(tr("Wire Run"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_circuitComboBox = new QComboBox(this);
    m_circuitComboBox->setObjectName("circuitComboBox");
    m_circuitComboBox->setEditable(true);
    m_circuitComboBox->addItems(circuits);
    m_circuitComboBox->setCurrentText(circuit);
    formLayout->addRow(tr("Circuit:"), m_circuitComboBox);

    m_fixturesSpinBox = new QSpinBox(this);
    m_fixturesSpinBox->setObjectName("fixturesSpinBox");
    m_fixturesSpinBox->setRange(0, 999);
    m_fixturesSpinBox->setValue(1);
    m_fixturesSpinBox->setToolTip(tr("Count of the fixtures fed by this run, each of them adds a drop to the wire length"));
    formLayout->addRow(tr("Fixtures fed by the run:"), m_fixturesSpinBox);

    m_conductorsSpinBox = new QSpinBox(this);
    m_conductorsSpinBox->setObjectName("conductorsSpinBox");
    m_conductorsSpinBox->setRange(1, 99);
    m_conductorsSpinBox->setValue(qBound(1, conductors, 99));
    m_conductorsSpinBox->setToolTip(tr("Count of the conductors pulled through the run (for example 2 hots and a ground)"));
    formLayout->addRow(tr("Conductors:"), m_conductorsSpinBox);

    if (!scale.isValid())
    {
        // The drawing defines no scale and nothing was measured on the sheet yet
        QWidget* scaleWidget = new QWidget(this);
        QHBoxLayout* scaleLayout = new QHBoxLayout(scaleWidget);
        scaleLayout->setContentsMargins(0, 0, 0, 0);
        m_paperInchesSpinBox = new QDoubleSpinBox(scaleWidget);
        m_paperInchesSpinBox->setObjectName("paperInchesSpinBox");
        m_paperInchesSpinBox->setRange(0.001, 1000.0);
        m_paperInchesSpinBox->setDecimals(3);
        m_paperInchesSpinBox->setValue(1.0);
        m_realLengthSpinBox = new QDoubleSpinBox(scaleWidget);
        m_realLengthSpinBox->setObjectName("realLengthSpinBox");
        m_realLengthSpinBox->setRange(0.001, 1000000.0);
        m_realLengthSpinBox->setDecimals(3);
        m_realLengthSpinBox->setValue(20.0);
        m_realUnitComboBox = new QComboBox(scaleWidget);
        m_realUnitComboBox->setObjectName("realUnitComboBox");
        m_realUnitComboBox->addItem(QStringLiteral("ft"));
        m_realUnitComboBox->addItem(QStringLiteral("m"));
        scaleLayout->addWidget(m_paperInchesSpinBox);
        scaleLayout->addWidget(new QLabel(tr("in on paper ="), scaleWidget));
        scaleLayout->addWidget(m_realLengthSpinBox);
        scaleLayout->addWidget(m_realUnitComboBox);
        formLayout->addRow(tr("Scale:"), scaleWidget);

        connect(m_paperInchesSpinBox, &QDoubleSpinBox::valueChanged, this, &WireRunDialog::updateLength);
        connect(m_realLengthSpinBox, &QDoubleSpinBox::valueChanged, this, &WireRunDialog::updateLength);
        connect(m_realUnitComboBox, &QComboBox::currentIndexChanged, this, &WireRunDialog::updateLength);
    }

    m_lengthLabel = new QLabel(this);
    m_lengthLabel->setObjectName("lengthLabel");
    formLayout->addRow(tr("Measured length:"), m_lengthLabel);
    layout->addLayout(formLayout);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    updateLength();
    pdf::PDFWidgetUtils::style(this);
}

QString WireRunDialog::getCircuit() const
{
    return m_circuitComboBox->currentText().trimmed();
}

int WireRunDialog::getFixtures() const
{
    return m_fixturesSpinBox->value();
}

int WireRunDialog::getConductors() const
{
    return m_conductorsSpinBox->value();
}

WireScale WireRunDialog::getScale() const
{
    if (m_drawingScale.isValid())
    {
        return m_drawingScale;
    }

    return WireScale::fromPaperScale(getPaperInches(), getRealLength(), getRealUnit(), m_userUnit);
}

void WireRunDialog::setManualScale(pdf::PDFReal paperInches, pdf::PDFReal realLength, const QString& unit)
{
    if (m_paperInchesSpinBox && paperInches > 0.0 && realLength > 0.0)
    {
        m_paperInchesSpinBox->setValue(paperInches);
        m_realLengthSpinBox->setValue(realLength);
        m_realUnitComboBox->setCurrentIndex(qMax(0, m_realUnitComboBox->findText(unit)));
    }
}

pdf::PDFReal WireRunDialog::getPaperInches() const
{
    return m_paperInchesSpinBox ? m_paperInchesSpinBox->value() : 0.0;
}

pdf::PDFReal WireRunDialog::getRealLength() const
{
    return m_realLengthSpinBox ? m_realLengthSpinBox->value() : 0.0;
}

QString WireRunDialog::getRealUnit() const
{
    return m_realUnitComboBox ? m_realUnitComboBox->currentText() : m_drawingScale.unit;
}

void WireRunDialog::accept()
{
    if (getCircuit().isEmpty())
    {
        QMessageBox::warning(this, tr("Wire Run"), tr("Enter the circuit of the run."));
        m_circuitComboBox->setFocus();
        return;
    }

    QDialog::accept();
}

void WireRunDialog::updateLength()
{
    const WireScale scale = getScale();
    if (!scale.isValid())
    {
        m_lengthLabel->setText(tr("unknown scale"));
        return;
    }

    const pdf::PDFReal length = m_paperLength * scale.unitsPerUserSpaceUnit;
    QString text = QString("%1 %2").arg(length, 0, 'f', 1).arg(scale.unit);
    if (!scale.ratio.isEmpty())
    {
        text += tr(" (scale %1)").arg(scale.ratio);
    }
    m_lengthLabel->setText(text);
}

WireTakeoffDialog::WireTakeoffDialog(const WireRuns& runs, const WireTakeoffSettings& settings, QWidget* parent) :
    QDialog(parent),
    m_runs(runs)
{
    setWindowTitle(tr("Wire Takeoff"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_unitComboBox = new QComboBox(this);
    m_unitComboBox->setObjectName("unitComboBox");
    m_unitComboBox->addItem(tr("Feet"), false);
    m_unitComboBox->addItem(tr("Meters"), true);
    m_unitComboBox->setCurrentIndex(settings.metric ? 1 : 0);
    formLayout->addRow(tr("Report in:"), m_unitComboBox);

    m_dropLengthSpinBox = new QDoubleSpinBox(this);
    m_dropLengthSpinBox->setObjectName("dropLengthSpinBox");
    m_dropLengthSpinBox->setRange(0.0, 1000.0);
    m_dropLengthSpinBox->setDecimals(1);
    m_dropLengthSpinBox->setValue(settings.metric ? settings.dropLength * WireTakeoff::METERS_PER_FOOT : settings.dropLength);
    m_dropLengthSpinBox->setToolTip(tr("Wire added for each fixture: pole height or wall drop plus the loop in the handhole"));
    formLayout->addRow(tr("Drop per fixture:"), m_dropLengthSpinBox);

    m_wasteSpinBox = new QDoubleSpinBox(this);
    m_wasteSpinBox->setObjectName("wasteSpinBox");
    m_wasteSpinBox->setRange(0.0, 200.0);
    m_wasteSpinBox->setDecimals(1);
    m_wasteSpinBox->setSuffix(QStringLiteral(" %"));
    m_wasteSpinBox->setValue(settings.wastePercent);
    formLayout->addRow(tr("Waste and slack:"), m_wasteSpinBox);

    m_conductorsSpinBox = new QSpinBox(this);
    m_conductorsSpinBox->setObjectName("conductorsSpinBox");
    m_conductorsSpinBox->setRange(1, 99);
    m_conductorsSpinBox->setValue(qBound(1, settings.defaultConductors, 99));
    m_conductorsSpinBox->setToolTip(tr("Conductors of the measured polylines, which were not drawn as wire runs"));
    formLayout->addRow(tr("Conductors of other runs:"), m_conductorsSpinBox);
    layout->addLayout(formLayout);

    m_previewTable = new QTableWidget(this);
    m_previewTable->setObjectName("previewTable");
    m_previewTable->setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(620, 220)));
    layout->addWidget(m_previewTable, 1);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName("infoLabel");
    m_infoLabel->setWordWrap(true);
    layout->addWidget(m_infoLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* exportButton = buttonBox->addButton(tr("Export to CSV..."), QDialogButtonBox::ActionRole);
    exportButton->setObjectName("exportButton");
    QPushButton* placeButton = buttonBox->addButton(tr("Place Table on Sheet"), QDialogButtonBox::AcceptRole);
    placeButton->setObjectName("placeButton");
    connect(exportButton, &QPushButton::clicked, this, [this]() { Q_EMIT exportRequested(createTable()); });
    connect(placeButton, &QPushButton::clicked, this, [this]() { done(PlaceResult); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_unitComboBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        // Drop length is converted, so it stays the same length
        const bool metric = m_unitComboBox->currentData().toBool();
        QSignalBlocker blocker(m_dropLengthSpinBox);
        const pdf::PDFReal value = m_dropLengthSpinBox->value();
        m_dropLengthSpinBox->setValue(metric ? value * WireTakeoff::METERS_PER_FOOT : value / WireTakeoff::METERS_PER_FOOT);
        updatePreview();
    });
    connect(m_dropLengthSpinBox, &QDoubleSpinBox::valueChanged, this, &WireTakeoffDialog::updatePreview);
    connect(m_wasteSpinBox, &QDoubleSpinBox::valueChanged, this, &WireTakeoffDialog::updatePreview);
    connect(m_conductorsSpinBox, &QSpinBox::valueChanged, this, &WireTakeoffDialog::updatePreview);

    updatePreview();
    pdf::PDFWidgetUtils::style(this);
}

WireTakeoffSettings WireTakeoffDialog::getSettings() const
{
    WireTakeoffSettings settings;
    settings.metric = m_unitComboBox->currentData().toBool();
    settings.dropLength = settings.metric ? m_dropLengthSpinBox->value() / WireTakeoff::METERS_PER_FOOT : m_dropLengthSpinBox->value();
    settings.wastePercent = m_wasteSpinBox->value();
    settings.defaultConductors = m_conductorsSpinBox->value();
    return settings;
}

LightingTable WireTakeoffDialog::createTable() const
{
    const WireTakeoffSettings settings = getSettings();
    return WireTakeoff::createTable(WireTakeoff::createTotals(m_runs, settings), settings);
}

void WireTakeoffDialog::updatePreview()
{
    m_dropLengthSpinBox->setSuffix(m_unitComboBox->currentData().toBool() ? QStringLiteral(" m") : QStringLiteral(" ft"));

    const LightingTable table = createTable();
    std::vector<QStringList> rows;
    for (const LightingTableRow& row : table.rows)
    {
        rows.push_back(row.cells);
    }
    LightingDialogsHelper::fillTable(m_previewTable, rows, 0);

    const int unscaledRuns = int(std::count_if(m_runs.cbegin(), m_runs.cend(), [](const WireRun& run) { return !run.scale.isValid(); }));
    QString info = tr("%n run(s) on the sheets.", nullptr, int(m_runs.size()));
    if (unscaledRuns > 0)
    {
        info += QChar(' ') + tr("%n run(s) without a scale are not counted.", nullptr, unscaledRuns);
    }
    m_infoLabel->setText(info);
}

}   // namespace pdfplugin
