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

#ifndef LIGHTINGTABLE_H
#define LIGHTINGTABLE_H

#include "fixturecount.h"
#include "pdfpage.h"

#include <QSizeF>
#include <QTransform>

namespace pdfplugin
{

/// Row of a table placed on the sheet
struct LightingTableRow
{
    QStringList cells;

    /// Marker symbol drawn in front of the cells (legend rows), it is drawn
    /// only when the table has the symbol column and the type is valid
    FixtureType symbol;

    /// Row is written in the bold font (header and total rows)
    bool bold = false;
};

/// Table placed on the sheet (count legend, footcandle statistics, wire
/// takeoff). Numbers are right aligned, other cells are left aligned.
struct LightingTable
{
    /// Title written above the rows (an empty title has no row)
    QString title;

    std::vector<LightingTableRow> rows;

    /// The first column holds the marker symbols of the rows
    bool hasSymbolColumn = false;

    /// Returns the count of the columns of the text cells
    int getColumnCount() const;
};

/// Kind of the table placed on the sheet
enum class LightingTableKind
{
    Invalid,
    Legend,             ///< Count legend (fixture types and their counts)
    FootcandleStats,    ///< Statistics of the calculation points
    WireTakeoff         ///< Wire and conduit takeoff by circuit
};

/// Scope of a count legend
enum class LightingLegendScope
{
    Sheet,      ///< Counts of the sheet, on which the legend is placed
    Document    ///< Counts of the whole plan set
};

/// Table annotation found in the document
struct LightingTableAnnotation
{
    pdf::PDFInteger pageIndex = -1;
    pdf::PDFObjectReference annotation;
    LightingTableKind kind = LightingTableKind::Invalid;
    LightingLegendScope scope = LightingLegendScope::Sheet;
    QRectF rect;

    /// Font size, with which the table was written (zero, if unknown)
    pdf::PDFReal fontSize = 0.0;
};

using LightingTableAnnotations = std::vector<LightingTableAnnotation>;

/// Writes tables onto the sheets. A table is a stamp annotation with its own
/// appearance stream (standard Helvetica fonts, lines and marker symbols),
/// so it is visible in every viewer, it can be moved like any markup, it is
/// printed and saved with the document. The text of the table is also in the
/// contents of the annotation (one row per line, cells separated by tabs), so
/// it is listed in the comments. The table is written upright on rotated
/// sheets (the appearance stream has the rotation of the page).
class LightingTableWriter
{
public:
    LightingTableWriter() = delete;

    /// Key of the annotation dictionary, which holds the kind of the table
    static constexpr const char* TABLE_KIND_KEY = "PDF4QT_LightingTable";

    /// Key of the annotation dictionary, which holds the scope of a legend
    static constexpr const char* LEGEND_SCOPE_KEY = "PDF4QT_LegendScope";

    /// Key of the annotation dictionary, which holds the font size of the table
    static constexpr const char* FONT_SIZE_KEY = "PDF4QT_TableFontSize";

    /// Name of the stamp of the tables
    static constexpr const char* STAMP_NAME = "PDF4QT_LightingTable";

    /// Returns the font size of the tables for the sheet of the given size
    /// (larger sheets get larger text, so the table is readable when printed)
    /// \param pageSize Size of the page in points
    static pdf::PDFReal getDefaultFontSize(const QSizeF& pageSize);

    /// Returns the size of the table (upright, before the rotation of the page)
    /// \param table Table
    /// \param fontSize Font size
    static QSizeF getTableSize(const LightingTable& table, pdf::PDFReal fontSize);

    /// Creates the content stream drawing the table into the box
    /// (0, 0, width, height) of the table size
    /// \param table Table
    /// \param fontSize Font size
    static QByteArray createContentStream(const LightingTable& table, pdf::PDFReal fontSize);

    /// Returns the text of the table (rows on separate lines, cells separated by tabs)
    static QString getText(const LightingTable& table);

    /// Returns the matrix of the appearance stream, which keeps the table
    /// upright on the page with the given rotation
    static QTransform getRotationMatrix(pdf::PageRotation rotation);

    /// Returns the rectangle of the annotation, whose upright top left corner
    /// is at the given point of the page
    /// \param topLeft Top left corner as displayed (page coordinate system)
    /// \param size Size of the table
    /// \param rotation Rotation of the page
    static QRectF getAnnotationRect(const QPointF& topLeft, const QSizeF& size, pdf::PageRotation rotation);

    /// Returns the point of the rectangle of the annotation, which is
    /// the upright top left corner of the table (inverse of getAnnotationRect)
    static QPointF getTopLeft(const QRectF& rect, pdf::PageRotation rotation);

    /// Creates the table annotation on the page
    /// \param builder Document builder
    /// \param pageReference Page
    /// \param rotation Rotation of the page
    /// \param topLeft Top left corner as displayed (page coordinate system)
    /// \param table Table
    /// \param kind Kind of the table
    /// \param scope Scope of a legend (ignored for other kinds)
    /// \param fontSize Font size
    static pdf::PDFObjectReference createTable(pdf::PDFDocumentBuilder* builder,
                                               pdf::PDFObjectReference pageReference,
                                               pdf::PageRotation rotation,
                                               const QPointF& topLeft,
                                               const LightingTable& table,
                                               LightingTableKind kind,
                                               LightingLegendScope scope,
                                               pdf::PDFReal fontSize);

    /// Replaces the contents of an existing table annotation. The upright top
    /// left corner of the table stays where it is (also when the table was moved).
    /// \param builder Document builder
    /// \param document Document, from which the table was scanned
    /// \param tableAnnotation Table annotation
    /// \param table New table
    static bool updateTable(pdf::PDFDocumentBuilder* builder,
                            const pdf::PDFDocument* document,
                            const LightingTableAnnotation& tableAnnotation,
                            const LightingTable& table);

    /// Scans all pages of the document and returns the table annotations
    /// \param document Document (can be nullptr)
    /// \param kind Only tables of this kind are returned (Invalid returns all)
    static LightingTableAnnotations scan(const pdf::PDFDocument* document, LightingTableKind kind = LightingTableKind::Invalid);

    /// Returns the persistent identifier of the kind
    static QByteArray getKindKey(LightingTableKind kind);

    /// Returns the kind from its persistent identifier (Invalid, if unknown)
    static LightingTableKind getKindFromKey(const QByteArray& key);

private:
    /// Creates the appearance stream and the entries of the table annotation
    static void writeTable(pdf::PDFDocumentBuilder* builder,
                           pdf::PDFObjectReference annotation,
                           pdf::PageRotation rotation,
                           const QPointF& topLeft,
                           const LightingTable& table,
                           LightingTableKind kind,
                           LightingLegendScope scope,
                           pdf::PDFReal fontSize);
};

}   // namespace pdfplugin

#endif // LIGHTINGTABLE_H
