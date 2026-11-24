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


#ifndef LIGHTINGDIALOGS_H
#define LIGHTINGDIALOGS_H

#include "fixtureschedule.h"
#include "footcandlestats.h"
#include "wiretakeoff.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace pdfplugin
{

/// Settings of the footcandle statistics
struct FootcandleSettings
{
    bool ignoreZeros = false;
    QString unit = QStringLiteral("fc");
    QString label;
};

/// Displays the statistics of the calculation points in the selected area
class FootcandleStatsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FootcandleStatsDialog(const FootcandleValues& values, const FootcandleSettings& settings, QWidget* parent);

    /// Result code of the button, which places the table on the sheet
    static constexpr int PlaceResult = 2;

    FootcandleSettings getSettings() const;
    FootcandleStatistics getStatistics() const;

    /// Returns the text of the statistics (copied to the clipboard)
    QString getText() const;

private:
    void updateStatistics();

    FootcandleValues m_values;
    QCheckBox* m_ignoreZerosCheckBox;
    QComboBox* m_unitComboBox;
    QLineEdit* m_labelEdit;
    QLabel* m_countLabel;
    QLabel* m_averageLabel;
    QLabel* m_maximumLabel;
    QLabel* m_minimumLabel;
    QLabel* m_averageToMinimumLabel;
    QLabel* m_maximumToMinimumLabel;
    QPushButton* m_placeButton;
};

/// Displays the fixture schedule read from the sheet. The columns of the types
/// and the descriptions can be chosen, the table can be exported to a CSV file
/// and its rows can become the fixture types of the count.
class FixtureScheduleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FixtureScheduleDialog(const FixtureScheduleTable& table, QWidget* parent);

    /// Result code of the button, which creates the fixture types
    static constexpr int CreateTypesResult = 2;

    /// Returns the table (wrapped rows are joined, if requested)
    FixtureScheduleTable getTable() const;

    int getHeaderRow() const;
    int getTypeColumn() const;
    int getDescriptionColumn() const;

signals:
    /// Export of the table to a CSV file was requested
    void exportRequested(const FixtureScheduleTable& table);

private:
    void updateColumns();
    void updatePreview();

    FixtureScheduleTable m_table;
    QSpinBox* m_headerRowSpinBox;
    QComboBox* m_typeColumnComboBox;
    QComboBox* m_descriptionColumnComboBox;
    QCheckBox* m_joinWrappedRowsCheckBox;
    QTableWidget* m_previewTable;
    QLabel* m_typesLabel;
    QPushButton* m_createTypesButton;
};

/// Properties of a new wire run
class WireRunDialog : public QDialog
{
    Q_OBJECT

public:
    /// \param circuits Existing circuits (offered in the combo box)
    /// \param circuit Default circuit
    /// \param conductors Default count of the conductors
    /// \param paperLength Length of the run in the default user space
    /// \param scale Scale of the drawing (if it is invalid, the user enters it)
    /// \param userUnit User unit of the page
    /// \param parent Parent widget
    explicit WireRunDialog(const QStringList& circuits,
                           const QString& circuit,
                           int conductors,
                           pdf::PDFReal paperLength,
                           const WireScale& scale,
                           pdf::PDFReal userUnit,
                           QWidget* parent);

    QString getCircuit() const;
    int getFixtures() const;
    int getConductors() const;
    WireScale getScale() const;

    /// Sets the scale entered by the user last time (used, when the drawing has no scale)
    void setManualScale(pdf::PDFReal paperInches, pdf::PDFReal realLength, const QString& unit);

    pdf::PDFReal getPaperInches() const;
    pdf::PDFReal getRealLength() const;
    QString getRealUnit() const;

    virtual void accept() override;

private:
    void updateLength();

    pdf::PDFReal m_paperLength;
    WireScale m_drawingScale;
    pdf::PDFReal m_userUnit;
    QComboBox* m_circuitComboBox;
    QSpinBox* m_fixturesSpinBox;
    QSpinBox* m_conductorsSpinBox;
    QDoubleSpinBox* m_paperInchesSpinBox;
    QDoubleSpinBox* m_realLengthSpinBox;
    QComboBox* m_realUnitComboBox;
    QLabel* m_lengthLabel;
};

/// Wire and conduit takeoff by circuit
class WireTakeoffDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WireTakeoffDialog(const WireRuns& runs, const WireTakeoffSettings& settings, QWidget* parent);

    /// Result code of the button, which places the table on the sheet
    static constexpr int PlaceResult = 2;

    WireTakeoffSettings getSettings() const;

signals:
    /// Export of the table to a CSV file was requested
    void exportRequested(const LightingTable& table);

private:
    void updatePreview();
    LightingTable createTable() const;

    WireRuns m_runs;
    QDoubleSpinBox* m_dropLengthSpinBox;
    QDoubleSpinBox* m_wasteSpinBox;
    QSpinBox* m_conductorsSpinBox;
    QComboBox* m_unitComboBox;
    QTableWidget* m_previewTable;
    QLabel* m_infoLabel;
};

}   // namespace pdfplugin

#endif // LIGHTINGDIALOGS_H
