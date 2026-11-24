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

#ifndef PDFSHEETLINKS_H
#define PDFSHEETLINKS_H

#include "pdfdocument.h"
#include "pdftextlayout.h"

#include <QRectF>
#include <QRegularExpression>

namespace pdf
{
class PDFDocumentBuilder;

/// Links the sheet references of a plan set to their sheets. The sheet number of each
/// page (E-101, LT-2) is taken from its page label or from the title block (the largest
/// sheet number alone on its line in the bottom right corner of the displayed page), then every occurrence
/// of the number of another sheet in the text of the pages ("SEE SHEET LT-2", sheet index
/// of the cover sheet) gets a link annotation, which goes to that sheet. The links are
/// marked by the entry PDF4QT_SheetLink, so they can be replaced and removed later, links
/// of other origin are kept.
class PDF4QTLIBCORESHARED_EXPORT PDFSheetLinks
{
public:
    PDFSheetLinks() = delete;

    /// Sheet number found on a page
    struct Sheet
    {
        QString number;             ///< Sheet number, empty, if the page has none
        bool fromPageLabel = false; ///< Number was taken from the page label
        QRectF rectangle;           ///< Number in the title block (page space), empty for a page label
    };
    using Sheets = std::vector<Sheet>;

    /// Reference to a sheet found in the text of a page
    struct Link
    {
        PDFInteger pageIndex = -1;          ///< Page with the reference
        PDFInteger targetPageIndex = -1;    ///< Referenced sheet
        QString text;                       ///< Text of the reference (sheet number)
        QRectF rectangle;                   ///< Rectangle of the reference (page space)
    };
    using Links = std::vector<Link>;

    /// Name of the entry of the link annotation, which marks the links created by this class
    static constexpr const char* SHEET_LINK_KEY = "PDF4QT_SheetLink";

    /// Returns the expression of a sheet number: one to three capital letters, an optional
    /// dash or dot, one to three digits, optional decimals and an optional letter (E-101,
    /// LT2, A1.01, E-3B). The number must not be a part of a longer word.
    static QRegularExpression getSheetNumberExpression();

    /// Returns true, if the whole text is a sheet number
    static bool isSheetNumber(const QString& text);

    /// Finds the sheet number of each page
    /// \param document Document
    /// \param storage Text layouts of all pages of the document
    static Sheets findSheets(const PDFDocument* document, const PDFTextLayoutStorage& storage);

    /// Finds the references to the sheets. A sheet number, which belongs to more pages,
    /// is ambiguous and it is not linked, a page does not link to itself, and a place
    /// covered by a link of another origin is skipped.
    /// \param document Document
    /// \param storage Text layouts of all pages of the document
    /// \param sheets Sheet numbers of the pages (one item for each page)
    static Links findLinks(const PDFDocument* document, const PDFTextLayoutStorage& storage, const Sheets& sheets);

    /// Creates the link annotations (without a border, the target sheet is fitted to the window)
    /// \returns Number of the created links
    static int createLinks(PDFDocumentBuilder* builder, const PDFDocument* document, const Links& links);

    /// Removes the links created by this class (the other links are kept)
    /// \returns Number of the removed links
    static int removeLinks(PDFDocumentBuilder* builder, const PDFDocument* document);

    /// Returns the number of the links created by this class
    static int getLinkCount(const PDFDocument* document);

private:
    static QRectF getRectangle(const QPolygonF& quadrilaterals);
    static std::vector<QRectF> getForeignLinkRectangles(const PDFDocument* document, PDFInteger pageIndex);
    static std::vector<std::pair<PDFObjectReference, PDFObjectReference>> getSheetLinkAnnotations(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFSHEETLINKS_H
