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

#ifndef FIXTURECOUNT_H
#define FIXTURECOUNT_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QColor>
#include <QPolygonF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;
}

namespace pdfplugin
{

/// Shape of the marker, which tags a fixture on the sheet. Each fixture type
/// has its own color and shape, so the types can be told apart also when
/// the colors are hard to distinguish.
enum class FixtureMarkerShape
{
    Circle,
    Square,
    Diamond,
    Triangle,
    LastShape
};

/// Fixture type, for example "A1 - 2x4 LED troffer"
struct FixtureType
{
    QString name;
    QString description;
    QColor color;
    FixtureMarkerShape shape = FixtureMarkerShape::Circle;

    bool isValid() const { return !name.isEmpty(); }

    /// Returns the localized name of the shape
    static QString getShapeName(FixtureMarkerShape shape);

    /// Returns the shape from its persistent identifier, or circle,
    /// if the identifier is unknown
    static FixtureMarkerShape getShapeFromKey(const QString& key);

    /// Returns the persistent identifier of the shape
    static QString getShapeKey(FixtureMarkerShape shape);

    /// Returns the outline of the marker of the given shape, which fills
    /// the rectangle. Points are in the same coordinate system as the rectangle,
    /// the y axis of the page points up, so the triangle points up on the page.
    /// \param shape Shape
    /// \param rect Rectangle
    static QPolygonF getShapePolygon(FixtureMarkerShape shape, const QRectF& rect);

    /// Returns a default type for a new type with the given index. The colors
    /// and shapes of consecutive types differ, so the default markers are easy
    /// to tell apart.
    /// \param index Index of the type
    static FixtureType createDefaultType(int index);
};

using FixtureTypes = std::vector<FixtureType>;

/// Single counted fixture, stored in the document as an annotation
struct FixtureMarker
{
    pdf::PDFInteger pageIndex = -1;
    pdf::PDFObjectReference annotation;
    QString typeName;
    QRectF rect;

    /// Size of the marker, as it was created (the rectangle of the annotation
    /// can be larger, because it includes the border)
    pdf::PDFReal size = 0.0;

    QPointF getCenter() const { return rect.center(); }
};

using FixtureMarkers = std::vector<FixtureMarker>;

/// Counted fixtures of the whole document. The markers are standard annotations
/// (circle, square or polygon) with an additional entry naming the fixture type,
/// so they are saved with the document, they are visible in other viewers, and
/// the count is always computed from the document itself - undo, redo and
/// deleting a marker as an ordinary annotation keep it correct.
class FixtureCountData
{
public:
    explicit FixtureCountData() = default;

    /// Key of the annotation dictionary, which holds the name of the fixture type
    static constexpr const char* FIXTURE_TYPE_KEY = "PDF4QT_FixtureType";

    /// Key of the annotation dictionary, which holds the description of the fixture type
    static constexpr const char* FIXTURE_DESCRIPTION_KEY = "PDF4QT_FixtureDescription";

    /// Key of the annotation dictionary, which holds the shape of the marker
    static constexpr const char* FIXTURE_SHAPE_KEY = "PDF4QT_FixtureShape";

    /// Key of the annotation dictionary, which holds the size of the marker
    static constexpr const char* FIXTURE_SIZE_KEY = "PDF4QT_FixtureSize";

    /// Scans all pages of the document and collects the markers
    /// \param document Document (can be nullptr)
    static FixtureCountData scan(const pdf::PDFDocument* document);

    const FixtureMarkers& getMarkers() const { return m_markers; }

    /// Returns the types used by the markers, in the order of the first use.
    /// Color, shape and description are taken from the first marker of the type.
    const FixtureTypes& getTypes() const { return m_types; }

    /// Returns the count of fixtures of the given type
    /// \param typeName Fixture type
    /// \param pageIndex Page index, or -1 for the whole document
    int getCount(const QString& typeName, pdf::PDFInteger pageIndex = -1) const;

    /// Returns the count of all fixtures
    /// \param pageIndex Page index, or -1 for the whole document
    int getTotalCount(pdf::PDFInteger pageIndex = -1) const;

    /// Returns the sorted page indices of the pages with at least one marker
    std::vector<pdf::PDFInteger> getPagesWithMarkers() const;

    /// Returns the markers of the given type
    FixtureMarkers getMarkersOfType(const QString& typeName) const;

    /// Returns the marker under the point, or nullptr. If more markers are under
    /// the point, the one with the nearest center is returned.
    /// \param pageIndex Page index
    /// \param point Point in the page coordinate system
    /// \param tolerance Tolerance in the page coordinate system
    const FixtureMarker* findMarker(pdf::PDFInteger pageIndex, const QPointF& point, pdf::PDFReal tolerance) const;

private:
    FixtureMarkers m_markers;
    FixtureTypes m_types;

    /// Counts, key is the type name, then the page index
    std::map<QString, std::map<pdf::PDFInteger, int>> m_counts;
};

/// Creates, changes and removes the markers in the document
class FixtureCountEditor
{
public:
    FixtureCountEditor() = delete;

    /// Creates a marker annotation of the given type
    /// \param builder Document builder
    /// \param pageReference Page, on which the marker is created
    /// \param center Center of the marker in the page coordinate system
    /// \param size Size of the marker in the page coordinate system
    /// \param type Fixture type
    static pdf::PDFObjectReference createMarker(pdf::PDFDocumentBuilder* builder,
                                                pdf::PDFObjectReference pageReference,
                                                const QPointF& center,
                                                pdf::PDFReal size,
                                                const FixtureType& type);

    /// Removes the marker from its page
    /// \param builder Document builder
    /// \param document Document, from which the marker was scanned
    /// \param marker Marker
    static bool removeMarker(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const FixtureMarker& marker);

    /// Replaces the marker by a marker of a different type (or by a marker of
    /// the changed type). Position and size of the marker are kept.
    /// \param builder Document builder
    /// \param document Document, from which the marker was scanned
    /// \param marker Marker
    /// \param type New type
    static bool changeMarkerType(pdf::PDFDocumentBuilder* builder,
                                 const pdf::PDFDocument* document,
                                 const FixtureMarker& marker,
                                 const FixtureType& type);

    /// Opacity of the interior of the marker, so the symbol under the marker
    /// stays visible
    static constexpr pdf::PDFReal FILL_OPACITY = 0.35;
};

/// Creates the reports of the count (table rows, CSV export)
class FixtureCountReport
{
public:
    FixtureCountReport() = delete;

    /// Returns the name of each page. The sheet number is taken from the bookmark
    /// pointing to the page (plan sets usually have one bookmark per sheet,
    /// for example "E-101 Lighting Plan"), otherwise "Page N" is used.
    /// \param document Document
    static QStringList getSheetNames(const pdf::PDFDocument* document);

    /// Creates the table of the count. The first row is the header, then
    /// there is one row per type and the last row holds the totals. Columns
    /// are type, description, one column for each sheet with at least one
    /// fixture, and the total.
    /// \param types Types in the order of the rows (types without fixtures are included)
    /// \param data Count data
    /// \param sheetNames Names of the sheets
    static std::vector<QStringList> createTable(const FixtureTypes& types,
                                                const FixtureCountData& data,
                                                const QStringList& sheetNames);

    /// Creates the CSV text from the table (comma separated, fields are quoted when needed)
    /// \param table Table
    static QString createCsv(const std::vector<QStringList>& table);

    /// Escapes the CSV field
    static QString escapeCsvField(const QString& text);
};

}   // namespace pdfplugin

#endif // FIXTURECOUNT_H
