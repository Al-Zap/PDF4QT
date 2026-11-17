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

#ifndef PDFPAGEORGANIZER_H
#define PDFPAGEORGANIZER_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfutils.h"

#include <QCoreApplication>

#include <vector>

namespace pdf
{

/// Organization of the pages of a single document: rotation, deletion, reordering,
/// insertion of pages of other documents or blank pages, extraction of pages
/// into a new document and splitting of the document into parts. Rotation only
/// changes the entry Rotate of the pages, so the rest of the document stays
/// untouched. Other operations assemble a new document by \p PDFDocumentManipulator,
/// which keeps annotations, form fields and the outline of the kept pages.
///
/// Each operation modifies the document only when it succeeds. Page indices
/// are zero based, invalid and duplicate indices are ignored.
class PDF4QTLIBCORESHARED_EXPORT PDFPageOrganizer
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPageOrganizer)

public:
    PDFPageOrganizer() = delete;

    /// Rotates the pages by multiples of 90 degrees
    /// \param document Document to be modified
    /// \param pageIndices Pages to be rotated
    /// \param clockwise Rotate clockwise (true), or counterclockwise (false)
    static PDFOperationResult rotatePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices, bool clockwise);

    /// Deletes the pages. At least one page must remain in the document.
    /// \param document Document to be modified
    /// \param pageIndices Pages to be deleted
    static PDFOperationResult deletePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices);

    /// Moves the pages to the given position. Position is the index of the page
    /// in the original document, before which the pages are moved (page count
    /// means the end of the document). Moved pages keep their mutual order.
    /// \param document Document to be modified
    /// \param pageIndices Pages to be moved
    /// \param insertPosition Insert position in the original document
    static PDFOperationResult movePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices, PDFInteger insertPosition);

    /// Inserts pages of other document before the given position
    /// \param document Document to be modified
    /// \param insertedDocument Document, whose pages are inserted
    /// \param insertPosition Index of the page, before which the pages are inserted (page count means the end)
    /// \param insertedPageIndices Pages of the inserted document, empty means all pages
    static PDFOperationResult insertDocument(PDFDocument* document,
                                             const PDFDocument* insertedDocument,
                                             PDFInteger insertPosition,
                                             const std::vector<PDFInteger>& insertedPageIndices = std::vector<PDFInteger>());

    /// Inserts blank pages before the given position. Blank pages have
    /// the same size and orientation, as the neighbouring page.
    /// \param document Document to be modified
    /// \param insertPosition Index of the page, before which the pages are inserted (page count means the end)
    /// \param count Number of the blank pages
    static PDFOperationResult insertBlankPages(PDFDocument* document, PDFInteger insertPosition, PDFInteger count);

    /// Creates a new document from the given pages. Source document is not changed.
    /// \param document Source document
    /// \param pageIndices Pages to be extracted, in the order of the new document
    /// \param extractedDocument Extracted document
    static PDFOperationResult extractPages(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, PDFDocument* extractedDocument);

    /// Divides pages of the document into consecutive groups of the given size
    /// (the last group can be smaller). Each group can be extracted into
    /// a separate document by \p extractPages.
    /// \param pageCount Page count
    /// \param pagesPerPart Maximal number of the pages in one group
    static std::vector<std::vector<PDFInteger>> splitPages(PDFInteger pageCount, PDFInteger pagesPerPart);

    /// Returns valid page indices sorted in ascending order without duplicates
    /// \param pageIndices Page indices
    /// \param pageCount Page count
    static std::vector<PDFInteger> getValidPageIndices(const std::vector<PDFInteger>& pageIndices, PDFInteger pageCount);

    /// Returns true, if the document permissions allow to organize the pages
    /// \param document Document
    static bool canOrganizePages(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFPAGEORGANIZER_H
