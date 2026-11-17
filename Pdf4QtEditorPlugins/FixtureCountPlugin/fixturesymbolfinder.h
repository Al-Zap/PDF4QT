#ifndef FIXTURESYMBOLFINDER_H
#define FIXTURESYMBOLFINDER_H

#include <QImage>
#include <QPointF>
#include <QRect>

#include <vector>

namespace pdfplugin
{

/// Finds the symbols similar to the sample symbol in a raster image of the sheet.
/// It uses the normalized cross correlation, first on a reduced image to find
/// the candidates quickly, then on the full resolution image to confirm them.
/// The symbol is searched also rotated by 90, 180 and 270 degrees, because
/// fixtures on plans are often rotated.
class FixtureSymbolFinder
{
public:
    FixtureSymbolFinder() = delete;

    struct Settings
    {
        /// Minimal similarity (normalized cross correlation, from -1 to 1)
        double threshold = 0.75;

        /// Search also the symbols rotated by 90, 180 and 270 degrees
        bool rotations = true;

        /// Size of the longer side of the sample in the reduced image, in pixels
        int coarseSampleSize = 24;
    };

    struct Match
    {
        /// Center of the found symbol in the image coordinates
        QPointF center;

        /// Similarity of the symbol and the sample
        double score = 0.0;
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
};

}   // namespace pdfplugin

#endif // FIXTURESYMBOLFINDER_H
