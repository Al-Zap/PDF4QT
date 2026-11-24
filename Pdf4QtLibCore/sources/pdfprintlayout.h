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


#ifndef PDFPRINTLAYOUT_H
#define PDFPRINTLAYOUT_H

#include "pdfglobal.h"

#include <QRectF>
#include <QString>
#include <QTransform>

#include <vector>

namespace pdf
{
class PDFPage;
class PDFDocument;

/// Settings of the printing: scale of the pages, rotation and tiling of the pages,
/// which are larger than the paper.
struct PDF4QTLIBCORESHARED_EXPORT PDFPrintSettings
{
    enum class ScaleMode
    {
        FitToPaper,     ///< Page is enlarged or reduced to fill the printable area of the paper
        ShrinkToFit,    ///< Pages larger than the printable area are reduced, smaller pages are printed at 100 %
        ActualSize,     ///< 100 %, the page is printed at its true size (1:1 drawings on a plotter)
        CustomScale     ///< Scale given by customScale (for example 50 % for a half size set)
    };

    ScaleMode scaleMode = ScaleMode::FitToPaper;

    /// Scale of the mode CustomScale, 1.0 is 100 %
    PDFReal customScale = 0.5;

    /// Pages are rotated by 90 degrees, when it fits the paper better
    /// (landscape sheet on a portrait paper)
    bool autoRotate = true;

    /// Pages larger than the printable area are printed on more sheets of paper
    /// (tiles), which are taped together. Not used by the mode FitToPaper.
    bool tiling = false;

    /// Overlap of the neighbouring tiles in points (1/72 inch)
    PDFReal tileOverlap = 36.0;

    /// Tiles get trim lines at the overlap and a label (page, tile name, scale)
    bool tileMarks = true;

    bool operator==(const PDFPrintSettings&) const = default;

    /// Returns the scale (1.0 is 100 %), which is used for a page
    /// of the given size printed into the printable area
    /// \param pageSize Size of the page in points (already rotated as it is placed on the paper)
    /// \param printableSize Size of the printable area of the paper in points
    PDFReal getScale(const QSizeF& pageSize, const QSizeF& printableSize) const;

    /// Returns true, if the tiles are used (tiling is not used for the mode FitToPaper)
    bool isTilingUsed() const { return tiling && scaleMode != ScaleMode::FitToPaper; }
};

/// One sheet of paper of the print job. A page is printed on a single sheet, or on more
/// sheets (tiles), when the tiling is on and the scaled page is larger than the paper.
struct PDF4QTLIBCORESHARED_EXPORT PDFPrintSheet
{
    /// Index of the page (zero based)
    PDFInteger pageIndex = -1;

    /// Physical size of the page in points (rotated media box multiplied by the user unit)
    QSizeF pageSize;

    /// Size of the paper in points
    QSizeF paperSize;

    /// Printable area of the paper in points (the paper minus the margins of the printer)
    QRectF printableRect;

    /// Maps the page rectangle (0, 0, pageSize) with the origin in the top left corner
    /// and the y axis going down onto the paper (points, origin in the top left corner
    /// of the paper, y axis going down). Parts outside of the printable rectangle are
    /// not printed.
    QTransform pageToPaper;

    /// Scale of the page, 1.0 is 100 %
    PDFReal scale = 1.0;

    /// Page is rotated by 90 degrees clockwise on the paper
    bool rotated = false;

    /// Tile of the page (zero based row and column) and the number of the tiles
    int row = 0;
    int column = 0;
    int rows = 1;
    int columns = 1;

    /// Overlap of the tiles in points (zero, when the page is not tiled)
    PDFReal overlap = 0.0;

    bool isTile() const { return rows * columns > 1; }

    /// Returns the name of the tile: letter of the row and number of the column (A1, A2, B1, ...)
    QString getTileName() const;

    /// Returns the rectangle of the page on the paper (points), it can exceed the paper
    QRectF getPageRectOnPaper() const { return pageToPaper.mapRect(QRectF(QPointF(0, 0), pageSize)); }

    /// Returns the matrix, which maps the points of the page (PDF user space of the page)
    /// onto the device, whose origin is the top left corner of the paper.
    /// \param page Page
    /// \param devicePixelsPerPoint Resolution of the device (pixels per one point, i.e. dpi / 72)
    QTransform createPagePointToDeviceMatrix(const PDFPage* page, PDFReal devicePixelsPerPoint) const;
};

/// Layout of the printed pages on the paper: scale, rotation, centering, tiles.
/// It is independent of the printer, so the print preview and the printing
/// use the same sheets.
class PDF4QTLIBCORESHARED_EXPORT PDFPrintLayout
{
public:
    PDFPrintLayout() = delete;

    /// Returns physical size of the page in points: the rotated media box (the box
    /// printed by the renderer) multiplied by the user unit
    static QSizeF getPageSize(const PDFPage* page);

    /// Creates the sheets of a single page
    /// \param pageIndex Index of the page
    /// \param pageSize Physical size of the page in points
    /// \param paperSize Size of the paper in points
    /// \param printableRect Printable area of the paper in points
    /// \param settings Settings
    static std::vector<PDFPrintSheet> layoutPage(PDFInteger pageIndex,
                                                 const QSizeF& pageSize,
                                                 const QSizeF& paperSize,
                                                 const QRectF& printableRect,
                                                 const PDFPrintSettings& settings);

    /// Creates the sheets of the pages of the document (invalid page indices are skipped)
    static std::vector<PDFPrintSheet> layoutDocument(const PDFDocument* document,
                                                     const std::vector<PDFInteger>& pageIndices,
                                                     const QSizeF& paperSize,
                                                     const QRectF& printableRect,
                                                     const PDFPrintSettings& settings);
};

}   // namespace pdf

#endif // PDFPRINTLAYOUT_H
