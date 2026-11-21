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

#ifndef FIXTURECOUNTPANEL_H
#define FIXTURECOUNTPANEL_H

#include "fixturecount.h"

#include <QDialog>
#include <QDockWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace pdfplugin
{

/// Panel with the fixture types and their counts on the current sheet
/// and in the whole set
class FixtureCountPanel : public QDockWidget
{
    Q_OBJECT

private:
    using BaseClass = QDockWidget;

public:
    explicit FixtureCountPanel(const std::vector<QAction*>& actions, QWidget* parent);

    /// Column of the type table
    enum Column
    {
        TypeColumn,
        DescriptionColumn,
        SheetColumn,
        TotalColumn,
        LastColumn
    };

    /// Displays the count
    /// \param types Types (rows of the table)
    /// \param countData Count data
    /// \param currentPageIndex Current page (sheet), or -1
    /// \param currentSheetName Name of the current sheet
    /// \param currentTypeIndex Index of the selected type, or -1
    void setData(const FixtureTypes& types,
                 const FixtureCountData& countData,
                 pdf::PDFInteger currentPageIndex,
                 const QString& currentSheetName,
                 int currentTypeIndex);

    /// Sets the marker size displayed in the panel
    void setMarkerSize(pdf::PDFReal markerSize);

    /// Returns the icon of the type (marker of its shape and color)
    static QIcon createTypeIcon(const FixtureType& type, int size = 16);

signals:
    void currentTypeChanged(int index);
    void typeEditRequested(int index);
    void markerSizeChanged(pdf::PDFReal markerSize);

private:
    void onSelectionChanged();
    void onCellDoubleClicked(int row, int column);

    QLabel* m_sheetLabel;
    QTableWidget* m_table;
    QDoubleSpinBox* m_markerSizeSpinBox;
    int m_typeCount;
    bool m_updating;
};

/// Dialog for creating and editing the fixture type
class FixtureTypeDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// Creates the dialog
    /// \param type Edited type
    /// \param usedNames Names of the other types, which cannot be used
    /// \param parent Parent widget
    explicit FixtureTypeDialog(const FixtureType& type, const QStringList& usedNames, QWidget* parent);

    const FixtureType& getType() const { return m_type; }

    virtual void accept() override;

private:
    void updateColorButton();
    void onColorButtonClicked();

    FixtureType m_type;
    QStringList m_usedNames;
    QLineEdit* m_nameEdit;
    QLineEdit* m_descriptionEdit;
    QPushButton* m_colorButton;
    QComboBox* m_shapeComboBox;
};

}   // namespace pdfplugin

#endif // FIXTURECOUNTPANEL_H
