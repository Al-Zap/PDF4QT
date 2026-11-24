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


#ifndef FIXTURESCHEDULE_H
#define FIXTURESCHEDULE_H

#include "fixturecount.h"

#include <QPolygonF>

namespace pdf
{
class PDFTextLayout;
}

namespace pdfplugin
{

/// Table read from the text of the sheet, rows from the top
using FixtureScheduleTable = std::vector<QStringList>;

/// Reads the fixture schedule (luminaire schedule) drawn on the sheet as a
/// table of text, so it can be exported to a spreadsheet and its rows can
/// become the fixture types of the count.
class FixtureSchedule
{
public:
    FixtureSchedule() = delete;

    /// Reads the table from the text in the area. Rows are the baselines of the
    /// text, columns are the vertical strips of the page, which are covered by
    /// the words of the rows (words of a single wide row, like the title of the
    /// schedule, do not join the columns, they are written into the first cell).
    /// \param layout Text layout of the page (page coordinate system)
    /// \param area Area of the schedule in the page coordinate system
    static FixtureScheduleTable readTable(const pdf::PDFTextLayout& layout, const QPolygonF& area);

    /// Returns the index of the header row (the first row with a header of the
    /// type column, like "TYPE", "MARK" or "TAG"), or -1
    static int findHeaderRow(const FixtureScheduleTable& table);

    /// Returns the column of the fixture types: the column with the header of
    /// the type, otherwise the first column
    /// \param table Table
    /// \param headerRow Header row, or -1
    static int findTypeColumn(const FixtureScheduleTable& table, int headerRow);

    /// Returns the column of the descriptions: the column with the header of
    /// a description, otherwise the column with the longest text, or -1
    /// \param table Table
    /// \param headerRow Header row, or -1
    /// \param typeColumn Column of the types
    static int findDescriptionColumn(const FixtureScheduleTable& table, int headerRow, int typeColumn);

    /// Joins the rows with an empty type cell to the row above them (text
    /// of a cell wrapped onto more lines). Rows above the header row are kept.
    /// \param table Table
    /// \param headerRow Header row, or -1
    /// \param typeColumn Column of the types
    static FixtureScheduleTable joinWrappedRows(const FixtureScheduleTable& table, int headerRow, int typeColumn);

    /// Creates the fixture types from the rows of the schedule below the header
    /// row. Rows with an empty type and duplicated types are skipped, types
    /// get the default colors and shapes following the existing types.
    /// \param table Table
    /// \param headerRow Header row, or -1
    /// \param typeColumn Column of the types
    /// \param descriptionColumn Column of the descriptions, or -1
    /// \param existingTypes Types, which already exist (their names are skipped)
    static FixtureTypes createTypes(const FixtureScheduleTable& table,
                                    int headerRow,
                                    int typeColumn,
                                    int descriptionColumn,
                                    const FixtureTypes& existingTypes);

    /// Maximal length of a fixture type name (longer cells are not types)
    static constexpr int MAXIMAL_TYPE_LENGTH = 12;
};

}   // namespace pdfplugin

#endif // FIXTURESCHEDULE_H
