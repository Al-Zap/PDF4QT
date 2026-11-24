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

#ifndef FIXTURECOUNTPLUGIN_H
#define FIXTURECOUNTPLUGIN_H

#include "fixturecount.h"

#include "pdfplugin.h"

#include <QObject>

namespace pdf
{
class PDFDocumentModifier;
}

namespace pdfplugin
{

class FixtureCountPanel;
class FixtureCountTool;
class LightingTools;

/// Tools of the Lighting tab. Counts the fixtures on lighting plans: fixtures
/// are tagged by a click with a marker of the fixture type (A1, B2, ...). Each
/// type has its own color and shape. The panel displays the counts on the
/// current sheet and in the whole set and the count can be exported to a CSV
/// file. Markers are annotations of the document, so they are saved with it
/// and the undo works for them. The other lighting tools (count legends,
/// footcandle grid statistics, fixture schedules, wire takeoff) are provided
/// by \p LightingTools.
class FixtureCountPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.FixtureCountPlugin" FILE "FixtureCountPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    Q_INVOKABLE FixtureCountPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual std::vector<QAction*> getToolbarActions() const override;
    virtual QString getPluginMenuName() const override;
    virtual PluginMenuLocation getPluginMenuLocation() const override;
    virtual QString getToolbarSection() const override;

private:
    void onCountToolActivityChanged(bool active);
    void onMarkerRequested(pdf::PDFInteger pageIndex, QPointF pagePoint);
    void onMarkerRemoveRequested(pdf::PDFInteger pageIndex, QPointF pagePoint);
    void onTypeRequested(int index);
    void onAddTypeTriggered();
    void onEditTypeTriggered();
    void onRemoveTypeTriggered();
    void onExportTriggered();
    void onShowPanelTriggered();
    void onCurrentTypeChanged(int index);
    void onMarkerSizeChanged(pdf::PDFReal markerSize);
    void onDrawSpaceChanged();
    void onSampleSelected(pdf::PDFInteger pageIndex, QRectF sampleRect);
    void onFindSimilarTriggered();
    void onTypesImported(const FixtureTypes& types);

    /// Renders the page and finds the symbols similar to the sample symbol.
    /// Returns the centers of the found symbols in the page coordinate system.
    /// \param pageIndex Page index
    /// \param sampleRect Rectangle around the sample in the page coordinate system
    std::vector<QPointF> findSimilarSymbols(pdf::PDFInteger pageIndex, const QRectF& sampleRect) const;

    /// Edits the type with the given index, if the user confirms the dialog,
    /// then the markers of the type are changed too
    void editType(int index);

    /// Creates the panel, if it does not exist yet, and shows it
    void showPanel();

    /// Updates the panel and the actions
    void updateUI();

    /// Merges the types found in the document into the list of the types
    void mergeDocumentTypes();

    /// Returns the current page (the sheet in the middle of the view), or -1
    pdf::PDFInteger getCurrentPageIndex() const;

    /// Returns the names of the types except the given one
    QStringList getTypeNames(int exceptIndex) const;

    /// Returns true, if the document can be modified
    bool canModifyDocument() const;

    /// Finalizes the modification of the document and sends it to the application
    /// (it becomes one step of the undo history)
    void finishModification(pdf::PDFDocumentModifier& modifier);

    void loadSettings();
    void saveSettings() const;

    FixtureCountTool* m_countTool;
    FixtureCountPanel* m_panel;
    LightingTools* m_lightingTools;

    QAction* m_countAction;
    QAction* m_addTypeAction;
    QAction* m_editTypeAction;
    QAction* m_removeTypeAction;
    QAction* m_exportAction;
    QAction* m_showPanelAction;
    QAction* m_findSimilarAction;

    FixtureTypes m_types;
    int m_currentTypeIndex;
    /// Initial width of the panel
    static constexpr int PANEL_WIDTH_IN_CHARACTERS = 44;

    /// Maximal number of the symbols found by a single search of similar symbols
    static constexpr size_t MAXIMAL_FOUND_SYMBOLS = 400;

    pdf::PDFReal m_markerSize;

    FixtureCountData m_data;
    QStringList m_sheetNames;
};

}   // namespace pdfplugin

#endif // FIXTURECOUNTPLUGIN_H
