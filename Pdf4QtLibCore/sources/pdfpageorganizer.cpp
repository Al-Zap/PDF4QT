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

#include "pdfpageorganizer.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentmanipulator.h"
#include "pdfsecurityhandler.h"

#include <algorithm>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helper functions of the page organizer
class PDFPageOrganizerHelper
{
public:
    PDFPageOrganizerHelper() = delete;

    /// Returns the assembled page of the given page of the document
    static PDFDocumentManipulator::AssembledPage createPage(int documentIndex, const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        Q_ASSERT(page);
        return PDFDocumentManipulator::createDocumentPage(documentIndex, int(pageIndex), page->getMediaBox().size(), page->getPageRotation());
    }

    /// Returns all pages of the document
    static PDFDocumentManipulator::AssembledPages createPages(int documentIndex, const PDFDocument* document)
    {
        return PDFDocumentManipulator::createAllDocumentPages(documentIndex, document);
    }

    /// Assembles the pages into a new document, which replaces the given document
    static PDFOperationResult assemble(PDFDocument* document,
                                       const std::vector<const PDFDocument*>& sourceDocuments,
                                       const PDFDocumentManipulator::AssembledPages& pages)
    {
        if (pages.empty())
        {
            return PDFPageOrganizer::tr("Document must contain at least one page.");
        }

        PDFDocumentManipulator manipulator;
        for (size_t i = 0; i < sourceDocuments.size(); ++i)
        {
            manipulator.addDocument(int(i), sourceDocuments[i]);
        }
        manipulator.setOutlineMode(sourceDocuments.size() > 1 ? PDFDocumentManipulator::OutlineMode::Merge : PDFDocumentManipulator::OutlineMode::NoOutline);

        PDFOperationResult result = manipulator.assemble(pages);
        if (!result)
        {
            return result;
        }

        *document = manipulator.takeAssembledDocument();
        return true;
    }

    /// Returns valid page indices in their original order without duplicates
    static std::vector<PDFInteger> getValidPageIndicesInOrder(const std::vector<PDFInteger>& pageIndices, PDFInteger pageCount)
    {
        std::vector<PDFInteger> result;
        result.reserve(pageIndices.size());

        std::set<PDFInteger> usedIndices;
        for (const PDFInteger pageIndex : pageIndices)
        {
            if (pageIndex >= 0 && pageIndex < pageCount && usedIndices.insert(pageIndex).second)
            {
                result.push_back(pageIndex);
            }
        }

        return result;
    }

    static PDFOperationResult checkDocument(const PDFDocument* document)
    {
        if (!document || !document->getCatalog())
        {
            return PDFPageOrganizer::tr("Invalid document.");
        }

        return true;
    }
};

PDFOperationResult PDFPageOrganizer::rotatePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices, bool clockwise)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    const std::vector<PDFInteger> indices = getValidPageIndices(pageIndices, PDFInteger(document->getCatalog()->getPageCount()));
    if (indices.empty())
    {
        return true;
    }

    PDFDocumentModifier modifier(document);
    PDFDocumentBuilder* builder = modifier.getBuilder();

    for (const PDFInteger pageIndex : indices)
    {
        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        const PageRotation rotation = clockwise ? getPageRotationRotatedRight(page->getPageRotation()) : getPageRotationRotatedLeft(page->getPageRotation());
        builder->setPageRotation(page->getPageReference(), rotation);
    }

    modifier.markReset();
    if (!modifier.finalize())
    {
        return tr("Pages can't be rotated.");
    }

    *document = *modifier.getDocument();
    return true;
}

PDFOperationResult PDFPageOrganizer::deletePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    const std::vector<PDFInteger> indices = getValidPageIndices(pageIndices, pageCount);
    if (indices.empty())
    {
        return true;
    }

    if (PDFInteger(indices.size()) >= pageCount)
    {
        return tr("All pages can't be deleted, at least one page must remain in the document.");
    }

    PDFDocumentManipulator::AssembledPages pages;
    pages.reserve(pageCount - indices.size());
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (!std::binary_search(indices.cbegin(), indices.cend(), i))
        {
            pages.push_back(PDFPageOrganizerHelper::createPage(0, document, i));
        }
    }

    const PDFDocument sourceDocument = *document;
    return PDFPageOrganizerHelper::assemble(document, { &sourceDocument }, pages);
}

PDFOperationResult PDFPageOrganizer::movePages(PDFDocument* document, const std::vector<PDFInteger>& pageIndices, PDFInteger insertPosition)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    const std::vector<PDFInteger> indices = getValidPageIndices(pageIndices, pageCount);
    if (indices.empty())
    {
        return true;
    }

    insertPosition = qBound(PDFInteger(0), insertPosition, pageCount);

    std::vector<PDFInteger> order;
    order.reserve(pageCount);
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (!std::binary_search(indices.cbegin(), indices.cend(), i))
        {
            order.push_back(i);
        }
    }

    // Position in the list of the remaining pages
    const PDFInteger movedBeforePosition = std::distance(indices.cbegin(), std::lower_bound(indices.cbegin(), indices.cend(), insertPosition));
    const PDFInteger targetPosition = insertPosition - movedBeforePosition;
    order.insert(std::next(order.begin(), targetPosition), indices.cbegin(), indices.cend());

    bool isIdentity = true;
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (order[i] != i)
        {
            isIdentity = false;
            break;
        }
    }

    if (isIdentity)
    {
        // Nothing to do, pages are not moved
        return true;
    }

    PDFDocumentManipulator::AssembledPages pages;
    pages.reserve(pageCount);
    for (const PDFInteger pageIndex : order)
    {
        pages.push_back(PDFPageOrganizerHelper::createPage(0, document, pageIndex));
    }

    const PDFDocument sourceDocument = *document;
    return PDFPageOrganizerHelper::assemble(document, { &sourceDocument }, pages);
}

PDFOperationResult PDFPageOrganizer::insertDocument(PDFDocument* document,
                                                    const PDFDocument* insertedDocument,
                                                    PDFInteger insertPosition,
                                                    const std::vector<PDFInteger>& insertedPageIndices)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    checkResult = PDFPageOrganizerHelper::checkDocument(insertedDocument);
    if (!checkResult)
    {
        return checkResult;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    const PDFInteger insertedPageCount = PDFInteger(insertedDocument->getCatalog()->getPageCount());
    insertPosition = qBound(PDFInteger(0), insertPosition, pageCount);

    PDFDocumentManipulator::AssembledPages insertedPages;
    if (insertedPageIndices.empty())
    {
        insertedPages = PDFPageOrganizerHelper::createPages(1, insertedDocument);
    }
    else
    {
        for (const PDFInteger pageIndex : PDFPageOrganizerHelper::getValidPageIndicesInOrder(insertedPageIndices, insertedPageCount))
        {
            insertedPages.push_back(PDFPageOrganizerHelper::createPage(1, insertedDocument, pageIndex));
        }
    }

    if (insertedPages.empty())
    {
        return tr("Inserted document has no pages.");
    }

    PDFDocumentManipulator::AssembledPages pages = PDFPageOrganizerHelper::createPages(0, document);
    pages.insert(std::next(pages.begin(), insertPosition), insertedPages.cbegin(), insertedPages.cend());

    const PDFDocument sourceDocument = *document;
    return PDFPageOrganizerHelper::assemble(document, { &sourceDocument, insertedDocument }, pages);
}

PDFOperationResult PDFPageOrganizer::insertBlankPages(PDFDocument* document, PDFInteger insertPosition, PDFInteger count)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    if (count <= 0)
    {
        return true;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    insertPosition = qBound(PDFInteger(0), insertPosition, pageCount);

    // Blank page has the size of the neighbouring page (A4, if document has no pages)
    QSizeF pageSize(210.0, 297.0);
    PageRotation pageRotation = PageRotation::None;
    if (pageCount > 0)
    {
        const PDFPage* page = document->getCatalog()->getPage(qMin(insertPosition, pageCount - 1));
        pageSize = page->getMediaBoxMM().size();
        pageRotation = page->getPageRotation();
    }

    PDFDocumentManipulator::AssembledPages pages = PDFPageOrganizerHelper::createPages(0, document);
    PDFDocumentManipulator::AssembledPages blankPages(count, PDFDocumentManipulator::createBlankPage(pageSize, pageRotation));
    pages.insert(std::next(pages.begin(), insertPosition), blankPages.cbegin(), blankPages.cend());

    const PDFDocument sourceDocument = *document;
    return PDFPageOrganizerHelper::assemble(document, { &sourceDocument }, pages);
}

PDFOperationResult PDFPageOrganizer::extractPages(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, PDFDocument* extractedDocument)
{
    PDFOperationResult checkResult = PDFPageOrganizerHelper::checkDocument(document);
    if (!checkResult)
    {
        return checkResult;
    }

    if (!extractedDocument)
    {
        return tr("Invalid document.");
    }

    PDFDocumentManipulator::AssembledPages pages;
    for (const PDFInteger pageIndex : PDFPageOrganizerHelper::getValidPageIndicesInOrder(pageIndices, PDFInteger(document->getCatalog()->getPageCount())))
    {
        pages.push_back(PDFPageOrganizerHelper::createPage(0, document, pageIndex));
    }

    if (pages.empty())
    {
        return tr("No pages selected.");
    }

    PDFDocument result;
    PDFOperationResult assembleResult = PDFPageOrganizerHelper::assemble(&result, { document }, pages);
    if (assembleResult)
    {
        *extractedDocument = std::move(result);
    }
    return assembleResult;
}

std::vector<std::vector<PDFInteger>> PDFPageOrganizer::splitPages(PDFInteger pageCount, PDFInteger pagesPerPart)
{
    std::vector<std::vector<PDFInteger>> parts;

    if (pageCount <= 0 || pagesPerPart <= 0)
    {
        return parts;
    }

    for (PDFInteger first = 0; first < pageCount; first += pagesPerPart)
    {
        std::vector<PDFInteger> part;
        const PDFInteger last = qMin(first + pagesPerPart, pageCount);
        part.reserve(last - first);
        for (PDFInteger i = first; i < last; ++i)
        {
            part.push_back(i);
        }
        parts.emplace_back(std::move(part));
    }

    return parts;
}

std::vector<PDFInteger> PDFPageOrganizer::getValidPageIndices(const std::vector<PDFInteger>& pageIndices, PDFInteger pageCount)
{
    std::vector<PDFInteger> result = PDFPageOrganizerHelper::getValidPageIndicesInOrder(pageIndices, pageCount);
    std::sort(result.begin(), result.end());
    return result;
}

bool PDFPageOrganizer::canOrganizePages(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    return !securityHandler || securityHandler->isAllowed(PDFSecurityHandler::Permission::Assemble);
}

}   // namespace pdf
