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

#ifndef DIMENSIONSETTINGS_H
#define DIMENSIONSETTINGS_H

#include "dimensionunits.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QString>

#include <map>
#include <vector>

namespace pdf
{
class PDFDocument;
}

namespace pdfplugin
{

/// Identification of a document, under which its scale is remembered. A document
/// is primarily identified by the path of its file, because the scale is
/// remembered for each file. The permanent identifier from the trailer dictionary
/// is stored as well, so the scale is not lost, when the file is renamed or moved,
/// and it is the only identification of a document, which has no file name at all.
///
/// The permanent identifier is not unique - a copy of a document keeps it - so
/// it is used only when it cannot lead to a different, still existing file. As
/// a consequence, a document saved under a new name while the original still
/// exists is treated as a new document and has to be calibrated again.
struct DocumentIdentity
{
    QString filePath;       ///< Canonical path of the file, empty if it is not known
    QString permanentId;    ///< Hexadecimal form of the first part of the trailer ID, empty if the document has none

    bool isValid() const { return !filePath.isEmpty() || !permanentId.isEmpty(); }
    bool operator==(const DocumentIdentity&) const = default;

    /// Returns the identity of the document. Returns an invalid identity,
    /// if the document cannot be identified at all.
    /// \param document Document
    /// \param fileName Original file name of the document
    static DocumentIdentity create(const pdf::PDFDocument* document, const QString& fileName);

    /// Returns the key of a sheet (page), under which its scale is remembered.
    /// The key is derived from the content of the page (its media box, content
    /// streams and the streams of its XObjects), not from its position in the
    /// document, so the scale follows the sheet, when the pages are reordered,
    /// deleted or inserted, and when the objects are renumbered by saving.
    /// Annotations and the rotation of the page are not a part of the key,
    /// so measuring or rotating a sheet keeps its scale. Returns an empty
    /// string for an invalid page.
    /// \param document Document
    /// \param pageIndex Page index
    static QString createPageKey(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex);
};

/// Scale set by the user either for the whole document, or for a single sheet
struct SheetScale
{
    /// Scale of the parts of the sheet, which do not define their own scale.
    /// It can be invalid, if only the drawing correction is set.
    DimensionScale scale;

    /// Correction of the scales defined by the document itself (viewports with
    /// a measure dictionary, typically from CAD or Bluebeam). The real values
    /// of these scales are multiplied by it, so for example a sheet printed
    /// at the half size has the correction 2. Zero means, that no correction
    /// is set here (a sheet then uses the correction of the document).
    pdf::PDFReal drawingCorrection = 0.0;

    bool hasDrawingCorrection() const { return drawingCorrection > 0.0; }
    bool isValid() const { return scale.isValid() || hasDrawingCorrection(); }
};

struct DimensionsPluginSettings
{
    /// Determines, how the created measurements are stored
    enum class StorageMode
    {
        Temporary,      ///< Measurements are drawn by the plugin and are lost when the document is closed
        Annotations     ///< Measurements are stored in the document as measurement annotations
    };

    DimensionUnit lengthUnit;
    DimensionUnit areaUnit;
    DimensionUnit angleUnit;

    /// Scale used for documents, which have no scale of their own. It is never
    /// inherited from another document - the scale currently in effect is held
    /// by the plugin, not by the settings.
    DimensionScale defaultScale;

    StorageMode storageMode = StorageMode::Annotations;
    bool isScaleStoredPerDocument = true;

    /// If true, then a scale selected from the presets applies to the current
    /// sheet (page) only, otherwise to all sheets of the document
    bool isScaleAppliedPerSheet = false;

    /// If true, then the picked points snap to the geometry of the drawing
    /// (end points, intersections, midpoints and points on the lines)
    bool isSnapToDrawing = true;

    /// True, if the units were selected by the user in the settings. Until then,
    /// the units follow the drawing - a drawing with a scale in feet is measured
    /// in feet, even if the locale of the system is metric, and vice versa.
    bool areUnitsSelectedByUser = false;
    QFont font;
    QColor textColor = QColor(Qt::black);

    /// Color, by which is the interior of the area and the perimeter measurements
    /// filled and by which is the displayed value backed. Its alpha channel is
    /// respected, a fully transparent color means, that nothing is filled.
    QColor backgroundColor = QColor(0, 0, 0, 25);

    /// Returns the settings with the default values
    static DimensionsPluginSettings createDefault();
};

/// Persistent storage of the dimension plugin settings, of the scale presets
/// and of the scales, which were calibrated for the particular documents.
/// The data are stored in the application settings, so they survive the restart
/// of the application.
class DimensionsSettingsStorage
{
public:
    explicit DimensionsSettingsStorage();

    /// Loads everything from the application settings
    void load();

    /// Stores everything to the application settings
    void save() const;

    DimensionsPluginSettings& getSettings() { return m_settings; }
    const DimensionsPluginSettings& getSettings() const { return m_settings; }
    void setSettings(DimensionsPluginSettings settings) { m_settings = qMove(settings); }

    const std::vector<DimensionScale>& getPresets() const { return m_presets; }
    void setPresets(std::vector<DimensionScale> presets) { m_presets = qMove(presets); }

    /// Adds the scale to the presets. If a preset with the same name already
    /// exists, then it is replaced.
    /// \param scale Named scale
    void addPreset(const DimensionScale& scale);

    /// Returns the scale, which was stored for the document, or an invalid scale,
    /// if no scale is known for it. The file path has a priority over the permanent
    /// identifier, so a document, which was calibrated under its own path, is not
    /// affected by another document sharing the identifier with it.
    /// \param identity Document identification
    SheetScale getDocumentScale(const DocumentIdentity& identity) const;

    /// Stores the scale for the document. An invalid scale removes the scale
    /// of the whole document. The oldest records are discarded, so the settings
    /// file does not grow indefinitely.
    /// \param identity Document identification
    /// \param scale Scale of the document
    /// \param keepPageScales If false, the scales of the single sheets are removed,
    ///        so the scale applies to all sheets of the document
    void setDocumentScale(const DocumentIdentity& identity, const SheetScale& scale, bool keepPageScales);

    /// Returns the scales of the single sheets (pages) of the document, which
    /// differ from the scale of the whole document. The key is the key of the
    /// sheet, see \p DocumentIdentity::createPageKey.
    /// \param identity Document identification
    std::map<QString, SheetScale> getPageScales(const DocumentIdentity& identity) const;

    /// Stores the scale of a single sheet (page) of the document. An invalid
    /// scale removes the scale of the sheet, so the scale of the document applies.
    /// \param identity Document identification
    /// \param pageKey Key of the sheet, see \p DocumentIdentity::createPageKey
    /// \param scale Scale of the sheet
    void setPageScale(const DocumentIdentity& identity, const QString& pageKey, const SheetScale& scale);

    /// Copies all the scales remembered for a document to another document,
    /// for example when the document was saved under a new name. Nothing
    /// happens, if nothing is remembered for the source document.
    /// \param source Identification of the original document
    /// \param target Identification of the new document
    void copyDocumentScales(const DocumentIdentity& source, const DocumentIdentity& target);

private:
    /// Maximum count of the documents, for which the scale is remembered
    static constexpr size_t MAXIMUM_DOCUMENT_SCALES = 200;

    struct DocumentScale
    {
        DocumentIdentity identity;
        SheetScale scale;   ///< Scale of the whole document, it can be invalid, if only sheets were calibrated
        std::map<QString, SheetScale> pageScales;   ///< Scales of the single sheets
        QDateTime lastUsed;

        bool isValid() const { return identity.isValid() && (scale.isValid() || !pageScales.empty()); }
    };

    /// Returns the record of the document, creates a new one, if it does
    /// not exist yet, and marks it as used
    /// \param identity Document identification
    DocumentScale* getOrCreateDocumentScale(const DocumentIdentity& identity);

    /// Discards the records, which were not used for the longest time
    void discardOldDocumentScales();

    /// Returns the record of the document, or nullptr, if it does not exist
    /// \param identity Document identification
    const DocumentScale* findDocumentScale(const DocumentIdentity& identity) const;

    DimensionsPluginSettings m_settings;
    std::vector<DimensionScale> m_presets;
    std::vector<DocumentScale> m_documentScales;
};

}   // namespace pdfplugin

#endif // DIMENSIONSETTINGS_H
