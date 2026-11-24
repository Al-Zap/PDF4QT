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

#ifndef FIXTURESYMBOLFINDER_H
#define FIXTURESYMBOLFINDER_H

#include "pdfrenderer.h"

#include <QImage>
#include <QPointF>
#include <QRect>

#include <vector>

namespace pdf
{
class PDFPage;
class PDFPrecompiledPage;
}

namespace pdfplugin
{

/// Finds the symbols similar to the sample symbol in a raster image of the sheet.
/// It uses the normalized cross correlation of the sample rotated by any angle
/// (fixtures on site plans point in all directions) and also mirrored, first on
/// a reduced image to find the candidates quickly, then on an image of a higher
/// resolution to confirm them and to refine the position, the angle and the size.
class FixtureSymbolFinder
{
public:
    FixtureSymbolFinder() = delete;

    struct Settings
    {
        /// Minimal similarity (normalized cross correlation, from -1 to 1). The
        /// candidates are confirmed in a sharp image of the symbols without the
        /// surrounding drawing, the same symbols have the similarity above 0.95.
        double threshold = 0.9;

        /// Search also the rotated symbols (by any angle)
        bool rotations = true;

        /// Search also the mirrored symbols (mirrored and rotated by any angle).
        /// Samples, whose mirror image is the same symbol rotated, are searched once.
        bool mirrors = true;

        /// Relative tolerance of the size of the symbols (0.1 means that symbols
        /// from 90 % to 110 % of the size of the sample are found). Symbols of
        /// the same shape, but of a different size, are usually other fixture
        /// types (a single and a double head of the same shape), so the tolerance
        /// is small.
        double scaleTolerance = 0.1;

        /// Size of the longer side of the sample in the reduced image, in pixels
        int coarseSampleSize = 12;

        /// Size of the longer side of the sample in the image, in which the
        /// candidates are confirmed, in pixels (\ref findOnPage only)
        int fineSampleSize = 48;

        /// Lines and curves larger than the sample are not drawn (\ref findOnPage only)
        bool removeLargePaths = true;
    };

    struct Match
    {
        /// Center of the found symbol in the image coordinates
        /// (in the page coordinates for \ref findOnPage)
        QPointF center;

        /// Similarity of the symbol and the sample
        double score = 0.0;

        /// Angle of the found symbol relative to the sample, in degrees
        double angle = 0.0;

        /// Size of the found symbol relative to the sample
        double scale = 1.0;

        /// The found symbol is the mirror image of the sample (mirrored
        /// horizontally, then rotated by the angle)
        bool mirrored = false;
    };

    /// Finds the symbols. The sample itself is one of the found symbols.
    /// \param image Image of the sheet (any format, it is converted to grayscale)
    /// \param sampleRect Rectangle around the sample symbol in the image
    /// \param settings Settings
    static std::vector<Match> find(const QImage& image, QRect sampleRect, const Settings& settings);

    /// Finds the symbols similar to the sample symbol on the page. Photos, scans
    /// and text are not drawn (if the sample stays visible without them), so they
    /// do not disturb the search, and neither are the lines and curves larger than
    /// the sample. The candidates are confirmed in images of the page drawn around
    /// them at a resolution, at which the sample has \ref Settings::fineSampleSize
    /// pixels, so also very small symbols (poles of a site plan of a few points)
    /// are distinguished. Returned centers are in the page coordinates.
    /// \param compiledPage Compiled page in its real colors
    /// \param page Page
    /// \param sampleRect Rectangle around the sample symbol in the page coordinates
    /// \param features Renderer features used to draw the page
    /// \param settings Settings
    static std::vector<Match> findOnPage(const pdf::PDFPrecompiledPage& compiledPage,
                                         const pdf::PDFPage* page,
                                         const QRectF& sampleRect,
                                         pdf::PDFRenderer::Features features,
                                         const Settings& settings);

    /// Returns the bounding rectangle of the ink (non-white pixels) of the image
    /// inside the rectangle. Empty rectangle is returned, if there is no ink.
    /// \param image Grayscale image
    /// \param rect Rectangle
    static QRect getInkBoundingRect(const QImage& image, const QRect& rect);

    /// Creates a grayscale image, in which only the ink of the color of the sample
    /// is dark. The color of the sample is the color of its darkest pixels. Lines
    /// of other colors (light levels, isolines, colored backgrounds) become white,
    /// so they do not disturb the search. The image of a sample without any color
    /// (black symbol) keeps black and gray ink, colored ink becomes white.
    /// \param image Color image of the sheet
    /// \param sampleRect Rectangle around the sample symbol in the image
    static QImage createInkImage(const QImage& image, const QRect& sampleRect);

private:
    /// Paths larger than this multiple of the sample are not drawn
    static constexpr double LARGE_PATH_FACTOR = 1.5;

    /// Minimal ink of the sample drawn without the large paths (relative to the
    /// ink of the sample drawn with them)
    static constexpr double MINIMAL_INK_OF_SMALL_PATHS = 0.25;

    /// Tolerance of the colors of the paths of the ink of the sample (0 - 255)
    static constexpr int INK_COLOR_TOLERANCE = 48;

    /// Minimal ink of the sample drawn only by the paths of the ink color
    /// (relative to the ink of the sample drawn by all paths)
    static constexpr double MINIMAL_INK_OF_INK_COLOR = 0.5;
};

}   // namespace pdfplugin

#endif // FIXTURESYMBOLFINDER_H
