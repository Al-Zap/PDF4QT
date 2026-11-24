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


#include "pdfprintlayout.h"
#include "pdfdocument.h"
#include "pdfrenderer.h"
#include "pdfpage.h"

#include <cmath>
#include <algorithm>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helper functions of the print layout
class PDFPrintLayoutHelper
{
public:
    PDFPrintLayoutHelper() = delete;

    /// Tolerance of the comparison of sizes in points (rounding of the paper sizes)
    static constexpr PDFReal TOLERANCE = 0.5;

    /// Returns the number of the tiles needed to cover the size
    /// \param size Size of the scaled page in points
    /// \param area Size of the printable area in points
    /// \param overlap Overlap of the tiles in points
    static int getTileCount(PDFReal size, PDFReal area, PDFReal overlap)
    {
        if (size <= area + TOLERANCE)
        {
            return 1;
        }

        return std::max(1, int(std::ceil((size - overlap - TOLERANCE) / (area - overlap))));
    }

    static bool isLandscape(const QSizeF& size) { return size.width() > size.height() + TOLERANCE; }
    static bool isPortrait(const QSizeF& size) { return size.height() > size.width() + TOLERANCE; }

    /// Candidate layout of a page (rotated or not)
    struct Candidate
    {
        bool rotated = false;
        PDFReal scale = 1.0;
        QSizeF scaledSize;
        PDFReal overlap = 0.0;
        int rows = 1;
        int columns = 1;

        int getSheetCount() const { return rows * columns; }
    };

    static Candidate evaluate(bool rotated, const QSizeF& pageSize, const QRectF& printableRect, const PDFPrintSettings& settings)
    {
        Candidate candidate;
        candidate.rotated = rotated;

        const QSizeF placedSize = rotated ? pageSize.transposed() : pageSize;
        candidate.scale = settings.getScale(placedSize, printableRect.size());
        candidate.scaledSize = placedSize * candidate.scale;

        if (settings.isTilingUsed())
        {
            const PDFReal area = std::min(printableRect.width(), printableRect.height());
            candidate.overlap = std::clamp(settings.tileOverlap, 0.0, 0.5 * area);
            candidate.columns = getTileCount(candidate.scaledSize.width(), printableRect.width(), candidate.overlap);
            candidate.rows = getTileCount(candidate.scaledSize.height(), printableRect.height(), candidate.overlap);

            if (candidate.getSheetCount() == 1)
            {
                candidate.overlap = 0.0;
            }
        }

        return candidate;
    }
};

PDFReal PDFPrintSettings::getScale(const QSizeF& pageSize, const QSizeF& printableSize) const
{
    if (pageSize.isEmpty() || printableSize.isEmpty())
    {
        return 1.0;
    }

    const PDFReal fitScale = std::min(printableSize.width() / pageSize.width(), printableSize.height() / pageSize.height());

    switch (scaleMode)
    {
        case ScaleMode::FitToPaper:
            return fitScale;

        case ScaleMode::ShrinkToFit:
            return std::min(fitScale, 1.0);

        case ScaleMode::ActualSize:
            return 1.0;

        case ScaleMode::CustomScale:
            return customScale > PDF_EPSILON ? customScale : 1.0;
    }

    Q_ASSERT(false);
    return 1.0;
}

QString PDFPrintSheet::getTileName() const
{
    QString rowName;
    int value = row;
    do
    {
        rowName.prepend(QChar('A' + value % 26));
        value = value / 26 - 1;
    }
    while (value >= 0);

    return rowName + QString::number(column + 1);
}

QTransform PDFPrintSheet::createPagePointToDeviceMatrix(const PDFPage* page, PDFReal devicePixelsPerPoint) const
{
    const QTransform pagePointToPageRect = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), pageSize));
    return pagePointToPageRect * pageToPaper * QTransform::fromScale(devicePixelsPerPoint, devicePixelsPerPoint);
}

QSizeF PDFPrintLayout::getPageSize(const PDFPage* page)
{
    if (!page)
    {
        return QSizeF();
    }

    return page->getRotatedMediaBox().size() * page->getUserUnit();
}

std::vector<PDFPrintSheet> PDFPrintLayout::layoutPage(PDFInteger pageIndex,
                                                      const QSizeF& pageSize,
                                                      const QSizeF& paperSize,
                                                      const QRectF& printableRect,
                                                      const PDFPrintSettings& settings)
{
    std::vector<PDFPrintSheet> sheets;

    if (pageSize.isEmpty() || printableRect.isEmpty())
    {
        return sheets;
    }

    using Helper = PDFPrintLayoutHelper;
    const Helper::Candidate normal = Helper::evaluate(false, pageSize, printableRect, settings);
    Helper::Candidate candidate = normal;

    if (settings.autoRotate)
    {
        const Helper::Candidate rotated = Helper::evaluate(true, pageSize, printableRect, settings);
        const bool orientationMismatch = (Helper::isLandscape(pageSize) && Helper::isPortrait(printableRect.size())) ||
                                         (Helper::isPortrait(pageSize) && Helper::isLandscape(printableRect.size()));

        if (rotated.getSheetCount() != normal.getSheetCount())
        {
            // Less sheets of paper to be taped together
            candidate = rotated.getSheetCount() < normal.getSheetCount() ? rotated : normal;
        }
        else if (orientationMismatch)
        {
            candidate = rotated;
        }
    }

    const PDFReal s = candidate.scale;
    const QSizeF scaledSize = candidate.scaledSize;
    const PDFReal stepX = printableRect.width() - candidate.overlap;
    const PDFReal stepY = printableRect.height() - candidate.overlap;

    // The tiles cover a larger area than the page, the page is centered in it
    const PDFReal coveredWidth = candidate.columns > 1 ? candidate.columns * stepX + candidate.overlap : printableRect.width();
    const PDFReal coveredHeight = candidate.rows > 1 ? candidate.rows * stepY + candidate.overlap : printableRect.height();

    sheets.reserve(candidate.getSheetCount());
    for (int row = 0; row < candidate.rows; ++row)
    {
        for (int column = 0; column < candidate.columns; ++column)
        {
            const PDFReal left = printableRect.left() + (coveredWidth - scaledSize.width()) * 0.5 - column * stepX;
            const PDFReal top = printableRect.top() + (coveredHeight - scaledSize.height()) * 0.5 - row * stepY;

            PDFPrintSheet sheet;
            sheet.pageIndex = pageIndex;
            sheet.pageSize = pageSize;
            sheet.paperSize = paperSize;
            sheet.printableRect = printableRect;
            sheet.scale = s;
            sheet.rotated = candidate.rotated;
            sheet.row = row;
            sheet.column = column;
            sheet.rows = candidate.rows;
            sheet.columns = candidate.columns;
            sheet.overlap = candidate.overlap;

            if (candidate.rotated)
            {
                // Rotation by 90 degrees clockwise: the top edge of the page is on the right
                sheet.pageToPaper = QTransform(0.0, s, -s, 0.0, left + s * pageSize.height(), top);
            }
            else
            {
                sheet.pageToPaper = QTransform(s, 0.0, 0.0, s, left, top);
            }

            sheets.push_back(sheet);
        }
    }

    return sheets;
}

std::vector<PDFPrintSheet> PDFPrintLayout::layoutDocument(const PDFDocument* document,
                                                          const std::vector<PDFInteger>& pageIndices,
                                                          const QSizeF& paperSize,
                                                          const QRectF& printableRect,
                                                          const PDFPrintSettings& settings)
{
    std::vector<PDFPrintSheet> sheets;

    if (!document)
    {
        return sheets;
    }

    const PDFCatalog* catalog = document->getCatalog();
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            continue;
        }

        std::vector<PDFPrintSheet> pageSheets = layoutPage(pageIndex, getPageSize(catalog->getPage(pageIndex)), paperSize, printableRect, settings);
        sheets.insert(sheets.end(), pageSheets.begin(), pageSheets.end());
    }

    return sheets;
}

}   // namespace pdf
