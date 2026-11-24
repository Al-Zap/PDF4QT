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


#ifndef LIGHTINGTOOLS_H
#define LIGHTINGTOOLS_H

#include "lightingdialogs.h"

#include <QObject>

#include <functional>
#include <map>

class QAction;

namespace pdf
{
class PDFDocument;
class PDFDocumentModifier;
class PDFTextLayout;
class PDFWidget;
class IPluginDataExchange;
}

namespace pdfplugin
{

class LightingShapeTool;

/// Lighting markup tools of the Lighting tab, which are not the fixture count
/// itself: count legends on the sheets, statistics of the footcandle grids,
/// fixture schedules read from the sheet, and the wire and conduit takeoff.
/// The fixture count plugin owns this object and provides the fixture types.
class LightingTools : public QObject
{
    Q_OBJECT

public:
    explicit LightingTools(QObject* parent);

    /// Creates the actions and the tools
    /// \param widget Widget of the application
    /// \param dataExchange Interface of the application
    void initialize(pdf::PDFWidget* widget, pdf::IPluginDataExchange* dataExchange);

    /// Sets the document (can be nullptr)
    void setDocument(pdf::PDFDocument* document);

    /// Sets the function providing the fixture types of the count
    void setTypesProvider(std::function<FixtureTypes()> typesProvider) { m_typesProvider = std::move(typesProvider); }

    /// Returns all actions, in the order of the groups of the Lighting tab
    std::vector<QAction*> getActions() const;

    /// Rewrites the tables on the sheets (count legends and wire takeoffs),
    /// which depend on the markers and runs of the modified document. It is
    /// called before each modification of the count or of the runs is finalized,
    /// so the tables change in the same step of the undo history.
    /// \param builder Builder of the modified document
    /// \param types Fixture types
    /// \param takeoffSettings Settings of the wire takeoff
    /// \returns Count of the rewritten tables
    static int updateTables(pdf::PDFDocumentBuilder* builder, const FixtureTypes& types, const WireTakeoffSettings& takeoffSettings);

    /// Loads the settings of the takeoff
    static WireTakeoffSettings loadTakeoffSettings();

    /// Returns the top left corner (as displayed) of a table placed next to
    /// the area, so it does not cover it: on the right side of the area, or
    /// on its left side, if the table does not fit onto the page there
    /// \param area Area in the page coordinate system
    /// \param tableSize Size of the table
    /// \param pageBox Box of the page (crop box)
    /// \param rotation Rotation of the page
    static QPointF getTablePositionNextToArea(const QPolygonF& area, const QSizeF& tableSize, const QRectF& pageBox, pdf::PageRotation rotation);

signals:
    /// Fixture types were created from a fixture schedule
    void typesImported(const FixtureTypes& types);

private:
    enum class Placement
    {
        None,
        SheetLegend,
        SetLegend,
        Takeoff
    };

    void onPlaceSheetLegendTriggered();
    void onPlaceSetLegendTriggered();
    void onUpdateTablesTriggered();
    void onWireTakeoffTriggered();
    void onPlacePointPicked(pdf::PDFInteger pageIndex, QPolygonF points);
    void onFootcandleAreaPicked(pdf::PDFInteger pageIndex, QPolygonF points);
    void onScheduleAreaPicked(pdf::PDFInteger pageIndex, QPolygonF points);
    void onWireRunPicked(pdf::PDFInteger pageIndex, QPolygonF points);

    /// Starts the placement of a table by a click on the sheet
    void startPlacement(Placement placement);

    /// Returns the text layout of the page (cached for the current document)
    const pdf::PDFTextLayout& getTextLayout(pdf::PDFInteger pageIndex);

    /// Returns true, if the document can be modified (if not, the user is told)
    bool canModifyDocument() const;

    /// Finalizes the modification and sends it to the application
    void finishModification(pdf::PDFDocumentModifier& modifier);

    /// Writes the table into a CSV file chosen by the user
    void exportCsv(const std::vector<QStringList>& rows, const QString& suffix, const QString& title);

    /// Displays the message in the status bar
    void showMessage(const QString& text, int timeout);

    /// Returns the circuits of the runs of the document
    QStringList getCircuits() const;

    void updateActions();

    void loadSettings();
    void saveSettings() const;
    static void saveTakeoffSettings(const WireTakeoffSettings& settings);

    pdf::PDFWidget* m_widget;
    pdf::IPluginDataExchange* m_dataExchange;
    pdf::PDFDocument* m_document;
    std::function<FixtureTypes()> m_typesProvider;

    QAction* m_placeSheetLegendAction;
    QAction* m_placeSetLegendAction;
    QAction* m_updateTablesAction;
    QAction* m_footcandleAction;
    QAction* m_scheduleAction;
    QAction* m_wireRunAction;
    QAction* m_wireTakeoffAction;
    QAction* m_placeAction;

    LightingShapeTool* m_placeTool;
    LightingShapeTool* m_footcandleTool;
    LightingShapeTool* m_scheduleTool;
    LightingShapeTool* m_wireRunTool;

    Placement m_placement;
    LightingTable m_pendingTable;

    /// Text layouts of the pages of the current document
    std::map<pdf::PDFInteger, pdf::PDFTextLayout> m_textLayouts;

    /// Values of the calculation points of the pages (live readout of the area)
    std::map<pdf::PDFInteger, FootcandleValues> m_pageValues;

    FootcandleSettings m_footcandleSettings;
    QString m_lastCircuit;
    int m_lastConductors;
    pdf::PDFReal m_manualPaperInches;
    pdf::PDFReal m_manualRealLength;
    QString m_manualRealUnit;
};

}   // namespace pdfplugin

#endif // LIGHTINGTOOLS_H
