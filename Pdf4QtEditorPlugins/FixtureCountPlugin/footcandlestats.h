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


#ifndef FOOTCANDLESTATS_H
#define FOOTCANDLESTATS_H

#include "lightingtable.h"

#include <QPolygonF>

namespace pdf
{
class PDFTextLayout;
}

namespace pdfplugin
{

/// Calculation point value found in the text of the sheet
struct FootcandleValue
{
    pdf::PDFReal value = 0.0;

    /// Center of the value in the page coordinate system
    QPointF position;

    /// Count of the decimal places of the written value
    int decimals = 0;
};

using FootcandleValues = std::vector<FootcandleValue>;

/// Statistics of the calculation points (the same values, which AGi32
/// and Visual write into their statistics tables)
struct FootcandleStatistics
{
    int count = 0;
    pdf::PDFReal average = 0.0;
    pdf::PDFReal maximum = 0.0;
    pdf::PDFReal minimum = 0.0;

    /// Count of the decimal places used to display the values
    int decimals = 1;

    bool isValid() const { return count > 0; }

    /// Returns true, if the ratios to the minimum are defined (minimum is not zero)
    bool hasRatios() const { return isValid() && minimum > 0.0; }

    pdf::PDFReal getAverageToMinimum() const { return hasRatios() ? average / minimum : 0.0; }
    pdf::PDFReal getMaximumToMinimum() const { return hasRatios() ? maximum / minimum : 0.0; }

    /// Computes the statistics of the values
    /// \param values Values
    /// \param ignoreZeros Values equal to zero are left out (points outside
    ///        of the paved area are often written as 0.0)
    static FootcandleStatistics compute(const FootcandleValues& values, bool ignoreZeros);
};

/// Finds the calculation point values (footcandles or lux) in the text
/// of the sheet. Photometric plans exported from AGi32, Visual or other
/// lighting software have the values as real text, often laid out
/// in a regular grid - the text layout can join the values of one row
/// into a single line, so the lines are split into separate numbers
/// by the gaps between the characters.
class FootcandleGrid
{
public:
    FootcandleGrid() = delete;

    /// Returns the values, whose centers lie in the area
    /// \param layout Text layout of the page (page coordinate system)
    /// \param area Area in the page coordinate system (a rectangle or a polygon)
    static FootcandleValues collectValues(const pdf::PDFTextLayout& layout, const QPolygonF& area);

    /// Parses the text of a single calculation point ("2.4", "+0.6", ".5").
    /// Text with other characters (labels, mounting heights, fixture tags
    /// with letters) is not a value.
    /// \param text Text
    /// \param value Parsed value
    /// \param decimals Count of the decimal places
    static bool parseValue(const QString& text, pdf::PDFReal* value, int* decimals);

    /// Formats a ratio ("3.2:1"), or "N/A", if it is not defined
    static QString formatRatio(pdf::PDFReal ratio, bool isDefined);

    /// Creates the statistics table placed on the sheet
    /// \param statistics Statistics
    /// \param label Name of the area ("Parking Lot")
    /// \param unit Unit of the values ("fc" or "lx")
    static LightingTable createTable(const FootcandleStatistics& statistics, const QString& label, const QString& unit);

    /// Gap between the characters (relative to the font size), which separates two values
    static constexpr pdf::PDFReal VALUE_GAP = 0.3;
};

}   // namespace pdfplugin

#endif // FOOTCANDLESTATS_H
