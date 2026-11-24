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


#ifndef COUNTLEGEND_H
#define COUNTLEGEND_H

#include "lightingtable.h"

namespace pdfplugin
{

/// Count legend placed on the sheet: a table of the fixture types with their
/// marker symbols and counts. A legend counts either the sheet, on which it is
/// placed, or the whole plan set. Legends are rewritten together with each
/// change of the count made by the count tools (in the same step of the undo
/// history), so they always match the markers.
class CountLegend
{
public:
    CountLegend() = delete;

    /// Creates the table of the legend. Types without fixtures in the scope
    /// are left out. The last row holds the total.
    /// \param types Types in the order of the rows (types of the document,
    ///        which are not in the list, are appended)
    /// \param data Count data
    /// \param scope Scope of the legend
    /// \param pageIndex Page, on which the legend is placed
    /// \param sheetName Name of the sheet (used in the title of a sheet legend)
    static LightingTable createTable(const FixtureTypes& types,
                                     const FixtureCountData& data,
                                     LightingLegendScope scope,
                                     pdf::PDFInteger pageIndex,
                                     const QString& sheetName);

    /// Rewrites all legends of the document with the current count
    /// \param builder Builder of the modified document
    /// \param types Types in the order of the rows
    /// \returns Count of the rewritten legends
    static int updateLegends(pdf::PDFDocumentBuilder* builder, const FixtureTypes& types);
};

}   // namespace pdfplugin

#endif // COUNTLEGEND_H
