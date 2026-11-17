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

#include <QImage>
#include <QPointF>
#include <QRect>

#include <vector>

namespace pdfplugin
{

/// Finds the symbols similar to the sample symbol in a raster image of the sheet.
/// It uses the normalized cross correlation of the sample rotated by any angle
/// (fixtures on site plans point in all directions), first on a reduced image
/// to find the candidates quickly, then on the full resolution image to confirm
/// them and to refine the position and the angle.
class FixtureSymbolFinder
{
public:
    FixtureSymbolFinder() = delete;

    struct Settings
    {
        /// Minimal similarity (normalized cross correlation, from -1 to 1)
        double threshold = 0.8;

        /// Search also the rotated symbols (by any angle)
        bool rotations = true;

        /// Size of the longer side of the sample in the reduced image, in pixels
        int coarseSampleSize = 12;
    };

    struct Match
    {
        /// Center of the found symbol in the image coordinates
        QPointF center;

        /// Similarity of the symbol and the sample
        double score = 0.0;

        /// Angle of the found symbol relative to the sample, in degrees
        double angle = 0.0;
    };

    /// Finds the symbols. The sample itself is one of the found symbols.
    /// \param image Image of the sheet (any format, it is converted to grayscale)
    /// \param sampleRect Rectangle around the sample symbol in the image
    /// \param settings Settings
    static std::vector<Match> find(const QImage& image, QRect sampleRect, const Settings& settings);

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
};

}   // namespace pdfplugin

#endif // FIXTURESYMBOLFINDER_H
