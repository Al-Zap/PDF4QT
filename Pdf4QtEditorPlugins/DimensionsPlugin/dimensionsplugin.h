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

#ifndef DIMENSIONSPLUGIN_H
#define DIMENSIONSPLUGIN_H

#include "dimensionsettings.h"
#include "dimensiontool.h"

#include "pdfplugin.h"

#include <QObject>

class QMenu;
class QToolButton;
class ScaleDialog;

namespace pdf
{
class PDFDocumentBuilder;
class PDFViewport;
}

namespace pdfplugin
{

class DimensionsPlugin : public pdf::PDFPlugin, public pdf::IDocumentDrawInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.DimensionsPlugin" FILE "DimensionsPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    Q_INVOKABLE DimensionsPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;
    virtual QString getToolbarSection() const override;
    virtual std::vector<QAction*> getToolbarActions() const override;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override;

    /// Origin of the scale, which applies at some place of the document
    enum class ScaleSource
    {
        Default,    ///< Nothing is known, the default scale from the settings is used
        Drawing,    ///< Scale is defined by the document itself (viewport with a measure dictionary)
        Sheet,      ///< Scale was set by the user for the single sheet (page)
        Document    ///< Scale was set by the user for the whole document
    };

    /// Returns the scale of the drawing at the given point. This is the single
    /// place, where the scale is resolved: the scale defined by the document
    /// itself (a viewport, typically from CAD or Bluebeam) has the highest
    /// priority, then the scale set by the user for the sheet, and finally
    /// the scale of the whole document. Other tools, which need to convert
    /// distances on a sheet, should use this function.
    /// \param pageIndex Page index
    /// \param point Point in the default user space of the page
    /// \param source If not nullptr, the origin of the scale is returned in it
    DimensionScale getScaleForPoint(pdf::PDFInteger pageIndex, const QPointF& point, ScaleSource* source = nullptr) const;

    /// Returns the scale set by the user for the sheet (page), or the scale
    /// of the whole document, if the sheet has no scale of its own. Viewports
    /// are not taken into account.
    /// \param pageIndex Page index
    /// \param source If not nullptr, the origin of the scale is returned in it
    DimensionScale getScaleForPage(pdf::PDFInteger pageIndex, ScaleSource* source = nullptr) const;

private:
    void onShowDimensionsTriggered();
    void onClearDimensionsTriggered();
    void onConvertToAnnotationsTriggered();
    void onSettingsTriggered();
    void onExportTriggered();
    void onManagePresetsTriggered();
    void onCustomScaleTriggered();
    void onScaleMenuAboutToShow();
    void onDrawSpaceChanged();
    void onCalibrationLinePicked(pdf::PDFInteger pageIndex, std::vector<QPointF> points, pdf::PDFReal measuredLength);
    void onWindowTitleChanged();
    void onDimensionCreated(Dimension dimension);

    void updateActions();
    void updateGraphics();
    void updateScaleAction();

    /// Creates the scale button in the status bar of the main window. The scale
    /// is then always visible and it can be changed by a single click.
    void createStatusBarButton();

    /// Returns the page, which the user is currently looking at - the page
    /// in the middle of the view, or the page, which occupies the largest
    /// part of the view
    pdf::PDFInteger getCurrentPageIndex() const;

    /// Returns the key of the sheet, under which its scale is remembered,
    /// see \p DocumentIdentity::createPageKey. The keys are cached.
    QString getPageKey(pdf::PDFInteger pageIndex) const;

    /// Returns the scale set by the user for the sheet, or nullptr
    const SheetScale* findPageScale(pdf::PDFInteger pageIndex) const;

    /// Returns the correction of the scales defined by the drawing on the page
    /// (1.0, if the drawing is not corrected)
    pdf::PDFReal getDrawingCorrection(pdf::PDFInteger pageIndex) const;

    /// Returns true, if the scales of the drawing on the page are corrected
    bool isDrawingCorrected(pdf::PDFInteger pageIndex) const;

    /// Returns the single scale defined by the viewports of the page (without
    /// the correction), or an invalid scale, if the page defines no scale or
    /// more different scales
    DimensionScale getSingleRawViewportScale(pdf::PDFInteger pageIndex) const;

    /// Measurement annotation created by this plugin, which displays a value
    /// computed with the scale in effect
    struct MeasurementAnnotation
    {
        pdf::PDFObjectReference reference;
        Dimension dimension;    ///< Dimension, which the annotation displays
        QString contents;       ///< Displayed text
    };

    /// Returns the measurement annotations of the document, which display
    /// exactly the value computed with the scale currently in effect, i.e.
    /// the annotations created by this plugin and not edited by the user
    std::vector<MeasurementAnnotation> getMeasurementAnnotations() const;

    /// Offers the user to update the measurement annotations, whose displayed
    /// value has changed by the change of the scale
    /// \param annotations Annotations collected before the scale was changed
    void offerMeasurementAnnotationsUpdate(const std::vector<MeasurementAnnotation>& annotations);

    /// Returns the text describing the scale of the current sheet, which
    /// is displayed in the status bar, for example "1 in = 20 ft (from drawing)"
    QString getCurrentScaleText() const;

    /// Returns the distinct scales defined by the viewports of the page
    std::vector<DimensionScale> getViewportScales(pdf::PDFInteger pageIndex, bool* isUnitAssumed = nullptr) const;

    /// Shows the scale dialog for the scale either calibrated or entered by the user
    /// and applies the result
    /// \param dialog Scale dialog
    /// \param pageIndex Sheet, on which the scale was calibrated or entered
    /// \param drawingScale Scale defined by the drawing at the calibrated line
    ///        (without the correction), see \p setScale
    /// \param isCalibration The scale was calibrated by a picked line
    void execScaleDialog(ScaleDialog& dialog, pdf::PDFInteger pageIndex, const DimensionScale& drawingScale, bool isCalibration);

    const DimensionsPluginSettings& getSettings() const { return m_settingsStorage.getSettings(); }

    /// Sets the scale currently in effect, remembers it for the current document,
    /// if it is requested by the settings, and stores everything to the application
    /// settings
    /// \param scale New scale
    /// \param isThisSheetOnly Apply the scale to the sheet (page) only
    /// \param pageIndex Sheet, on which the scale was calibrated or selected
    /// \param drawingScale Scale defined by the drawing itself (a viewport) at the
    ///        place, where the scale was calibrated, without the correction. If it is
    ///        valid, then the user overrides the scale of the drawing, for example
    ///        because the sheet was printed at a different size, so the scales of the
    ///        drawing are corrected in the same ratio.
    void setScale(const DimensionScale& scale, bool isThisSheetOnly, pdf::PDFInteger pageIndex, const DimensionScale& drawingScale);

    /// Stores the scale of the whole document and of the sheets in the settings
    void storeDocumentScale(bool keepPageScales);

    /// Selects the scale for the currently opened document. The scale remembered
    /// for the document has the highest priority, then the scale defined by the
    /// measure dictionary of the document, and finally the default scale from
    /// the settings. The scale of the previously opened document is never reused.
    void applyDocumentScale();

    /// Selects the units of the measurements by the scale defined by the
    /// drawing (feet for a drawing in feet), unless the user has selected
    /// the units in the settings
    void applyUnitsOfDrawing();

    /// Returns the scale defined by the measure dictionaries of the document.
    /// If the document defines more than one different scale, then a single
    /// scale cannot represent it and an invalid scale is returned - such
    /// measurements are resolved for each measurement separately,
    /// see \p getScaleFromViewport.
    DimensionScale getScaleFromDocument() const;

    /// Returns the scale prescribed by the viewport, in which the point lies,
    /// or an invalid scale, if the point does not lie in a viewport with
    /// a usable measure. The correction of the drawing set by the user is applied.
    /// \param pageIndex Page index
    /// \param point Point in the default user space of the page
    DimensionScale getScaleFromViewport(pdf::PDFInteger pageIndex, const QPointF& point) const;

    /// Returns the scale prescribed by the viewport, in which the point lies,
    /// exactly as it is defined by the document (without the correction)
    /// \param pageIndex Page index
    /// \param point Point in the default user space of the page
    DimensionScale getRawScaleFromViewport(pdf::PDFInteger pageIndex, const QPointF& point) const;

    /// Returns the scale with the real value multiplied by the correction
    static DimensionScale applyDrawingCorrection(const DimensionScale& scale, pdf::PDFReal correction);

    /// Returns the scale defined by a viewport of a page, without the calibration.
    /// Returns an invalid scale, if the viewport does not define a usable scale.
    /// \param viewport Viewport
    /// \param userUnit Value of the UserUnit entry of the page
    /// \param[out] isUnitAssumed Set to true, if the drawing does not tell its
    ///             unit and the unit was estimated
    static DimensionScale getRawViewportScale(const pdf::PDFViewport& viewport, pdf::PDFReal userUnit, bool* isUnitAssumed = nullptr);

    /// Returns the scale, which applies to the dimension - either the scale
    /// prescribed by the document, or the scale currently selected by the user
    /// \param dimension Dimension
    DimensionScale getEffectiveScale(const Dimension& dimension) const;

    /// Returns true, if the measurements can be stored in the document
    /// as annotations
    bool canCreateAnnotations() const;

    /// Stores the measurements in the document as measurement annotations.
    /// All of them are written in a single modification of the document, so
    /// the whole operation can be undone at once. Returns true, if at least
    /// one annotation was created and the document was really modified.
    /// \param dimensions Dimensions to be stored
    /// \param notCreated If it is not nullptr, then the dimensions, which were
    ///        not stored in the document, are returned in it
    bool createDimensionAnnotations(const std::vector<Dimension>& dimensions, std::vector<Dimension>* notCreated = nullptr);

    /// Creates a single measurement annotation using the builder. The caller is
    /// responsible for finalizing the modification of the document. Returns true,
    /// if the annotation was really created.
    /// \param builder Document builder
    /// \param dimension Dimension
    bool createDimensionAnnotation(pdf::PDFDocumentBuilder* builder, const Dimension& dimension);

    /// Creates the measure dictionary, which describes the scale and units used
    /// for the dimension, so external applications can interpret the created
    /// annotation
    /// \param dimension Dimension, for which is the measure created
    pdf::PDFObject createMeasureDictionary(const Dimension& dimension) const;

    /// Returns the text displayed for the dimension, for example "12.50 m"
    /// \param dimension Dimension
    QString getDimensionText(const Dimension& dimension) const;

    /// Returns the localized name of the dimension type
    /// \param type Dimension type
    static QString getDimensionTypeName(Dimension::Type type);

    /// Collects the measurements, which are stored in the document
    /// as measurement annotations
    std::vector<QStringList> getAnnotationExportRows() const;

    std::array<DimensionTool*, DimensionTool::LastStyle> m_dimensionTools;
    std::vector<Dimension> m_dimensions;

    QAction* m_showDimensionsAction;
    QAction* m_clearDimensionsAction;
    QAction* m_convertToAnnotationsAction;
    QAction* m_exportAction;
    QAction* m_scaleAction;
    QAction* m_settingsAction;
    QMenu* m_scaleMenu;
    QAction* m_scalePerSheetAction;
    QAction* m_snapToDrawingAction;
    QToolButton* m_statusBarScaleButton;
    pdf::PDFInteger m_statusBarPageIndex;

    DimensionsSettingsStorage m_settingsStorage;

    /// Identification of the currently opened document, under which its scale
    /// is remembered in the application settings
    DocumentIdentity m_documentIdentity;

    /// Scale currently in effect. It is not a part of the settings, because it
    /// belongs to the opened document and must not leak to another one.
    DimensionScale m_scale;

    /// Origin of the scale of the whole document
    ScaleSource m_scaleSource = ScaleSource::Default;

    /// Correction of the scales defined by the drawing for the whole document,
    /// zero, if it is not set (see \p SheetScale::drawingCorrection)
    pdf::PDFReal m_drawingCorrection = 0.0;

    /// Scales set by the user for single sheets (pages) of the current document,
    /// the key is the key of the sheet (see \p DocumentIdentity::createPageKey)
    std::map<QString, SheetScale> m_pageScales;

    /// Cached keys of the sheets of the current document, by page index
    mutable std::vector<QString> m_pageKeys;
};

}   // namespace pdfplugin

#endif // DIMENSIONSPLUGIN_H
