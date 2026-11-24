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

#include "fixturesymbolfinder.h"

#include "pdfexecutionpolicy.h"
#include "pdfpage.h"
#include "pdfpainter.h"

#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>

namespace pdfplugin
{

/// Implementation of the normalized cross correlation of the rotated sample
class FixtureSymbolFinderHelper
{
public:
    FixtureSymbolFinderHelper() = delete;

    /// Grayscale image with the integral images of the values and of the squares
    /// of the values, so the ink and the variance of any window are computed
    /// in constant time
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<float> values;
        std::vector<double> sum;
        std::vector<double> sumOfSquares;

        double getSum(const std::vector<double>& table, int x, int y, int w, int h) const
        {
            const int stride = width + 1;
            return table[size_t(y + h) * stride + (x + w)] - table[size_t(y) * stride + (x + w)] - table[size_t(y + h) * stride + x] + table[size_t(y) * stride + x];
        }
    };

    /// Sample rotated by the angle (and scaled and mirrored). Pixels are stored as
    /// offsets from the center of the sample, only the pixels inside the transformed
    /// sample are used, so the neighbourhood of the symbol does not affect the correlation.
    struct RotatedTemplate
    {
        double angle = 0.0;
        double scale = 1.0;
        bool mirrored = false;
        int radius = 0;
        std::vector<qint16> offsetsX;
        std::vector<qint16> offsetsY;
        std::vector<float> values;
        double norm = 0.0;
        double mean = 0.0;

        bool isValid() const { return !values.empty() && norm > 1e-6; }
    };

    struct Candidate
    {
        /// Center in the image coordinates (pixel edges)
        QPointF center;
        double score = 0.0;
        double angle = 0.0;
        double scale = 1.0;
        bool mirrored = false;
        double edgeScore = 0.0;
    };

    /// Color of the ink of the sample, see \ref FixtureSymbolFinder::createInkImage
    struct InkColor
    {
        bool isValid = false;
        int red = 0;
        int green = 0;
        int blue = 0;
        int whiteDistance = 1;
    };

    /// Draws the area of the sheet (in the pixels of the image of the confirmation)
    /// as a grayscale ink image
    using AreaRenderer = std::function<QImage(const QRect&)>;

    /// Result of the coarse search
    struct CoarseResult
    {
        /// Candidates in the coordinates of the searched image
        std::vector<Candidate> candidates;

        /// Scale of the reduced image (relative to the searched image)
        double scale = 1.0;

        /// Step of the angles of the coarse search
        double angleStep = 360.0;

        /// Angle, by which the reduced sample is symmetric (only the angles
        /// below it were searched, a candidate can be rotated by its multiples)
        double symmetryAngle = 360.0;

        /// Mirror images were searched (the reduced sample is not symmetric)
        bool isMirrorSearched = false;

        /// Angles, by which the reduced sample rotated looks similar to the sample,
        /// although it is not symmetric (a small symbol with similar blobs on its
        /// opposite sides), the candidate can have a wrong orientation
        std::vector<double> ambiguousAngles;

        /// Angles of the mirror images, which look similar to the reduced sample,
        /// the candidate can be the mirror image rotated
        std::vector<double> ambiguousMirrorAngles;
    };

    /// Creates the image, the integral images are computed only when requested
    static Image createImage(const QImage& image, bool integralImages);
    static RotatedTemplate createTemplate(const QImage& sample, double angle, double scale, bool mirrored);

    /// Creates the templates of the sample rotated by the angles, for each scale
    /// and mirroring, index is ((mirror * scales.size()) + scale) * angles.size() + angle
    static std::vector<RotatedTemplate> createTemplates(const QImage& sample, const std::vector<double>& angles, const std::vector<double>& scales, const std::vector<bool>& mirrors);

    /// Returns the angles of the full circle with the step not larger than the given step
    static std::vector<double> getAngles(double step);

    /// Returns the angle step, by which the farthest pixel of the sample moves
    /// by the given number of pixels
    static double getAngleStep(const QSize& sampleSize, double pixels);

    /// Returns the scales from 1 - tolerance to 1 + tolerance, the step is chosen
    /// so the farthest pixel of the sample moves by the given number of pixels
    static std::vector<double> getScales(const QSize& sampleSize, double tolerance, double pixels);

    /// Returns the normalized cross correlation of the template placed with its
    /// center at the given pixel. The correlation does not depend on the contrast,
    /// so a window, whose contrast differs too much from the contrast of the sample
    /// (gray drawing of other layers, smaller or larger symbols, dense hatching),
    /// has zero correlation.
    /// \param image Image
    /// \param tmpl Template
    /// \param x Horizontal position of the center
    /// \param y Vertical position of the center
    /// \param contrastTolerance Maximal ratio of the contrasts of the window and of the sample
    static double correlate(const Image& image, const RotatedTemplate& tmpl, int x, int y, double contrastTolerance);

    /// Correlation of a symbol partially covered by other drawing
    struct OcclusionResult
    {
        double score = -1.0;
        double edgeScore = -1.0;
        double occludedFraction = 1.0;
        double visibleEdgeFraction = 0.0;
    };

    /// Returns the correlation of the ink and of the edges of the template placed
    /// with its center at the given pixel, the pixels covered by other drawing
    /// (a line or a text crossing the symbol, which was not removed) are not used.
    /// A covered pixel is darker than the template, and it is connected with the
    /// border of the template by covered pixels: a line or a text runs into the
    /// symbol from outside. Extra ink inside of the symbol (a cross in a circle,
    /// another symbol) is not ignored, nor is any missing ink. Edges of the
    /// covering drawing are ignored too.
    /// \param image Image
    /// \param edgeImage Edges of the image
    /// \param tmpl Template
    /// \param edgeTemplate Template of the edges (of the same transformation)
    /// \param x Horizontal position of the center
    /// \param y Vertical position of the center
    static OcclusionResult correlateOccluded(const Image& image, const Image& edgeImage, const RotatedTemplate& tmpl, const RotatedTemplate& edgeTemplate, int x, int y);

    /// Finds the local maxima of the correlation above the threshold
    /// \param image Image
    /// \param templates Templates (sample rotated by all angles)
    /// \param minimalInk Windows with less ink are skipped
    /// \param threshold Threshold
    static std::vector<Candidate> findCandidates(const Image& image, const std::vector<RotatedTemplate>& templates, double minimalInk, double threshold);

    /// Keeps only the best candidate from the candidates closer than the distance
    static std::vector<Candidate> suppressNonMaxima(std::vector<Candidate> candidates, double distance);

    /// Removes the weaker of the confirmed candidates, which cover the same symbol:
    /// the center of the weaker one lies in the central part of the stronger one
    /// (half of the size of the sample, which is rotated and scaled by the match),
    /// or nearer than the minimal distance
    static std::vector<Candidate> suppressOverlapping(std::vector<Candidate> candidates, const QSizeF& sampleSize, double minimalDistance);

    /// Blurs the grayscale image by a 3x3 box filter. Thin lines of the drawing
    /// are then less sensitive to the sub-pixel position and angle of the symbol.
    static QImage blur(const QImage& image);

    /// Returns the blurred magnitude of the gradient of the grayscale image. The
    /// correlation of the edges is more sensitive to the size of the symbol than
    /// the correlation of the ink (a filled rectangle correlates well with a little
    /// larger filled rectangle, but their edges do not overlap).
    static QImage getEdges(const QImage& image);

    /// Adds a white margin around the sample. The symbol is expected to be isolated,
    /// so parts of larger shapes (ends of thick lines, filled rectangles), which
    /// look like the symbol only inside its bounding rectangle, are not found.
    static QImage addMargin(const QImage& sample, int margin);

    /// Returns the copy of the area of the grayscale image, the pixels outside
    /// of the image are white
    static QImage copyArea(const QImage& image, const QRect& area);

    /// Sample placed in the middle of a white image
    struct SampleImage
    {
        Image image;
        int x = 0;
        int y = 0;
    };

    /// Places the sample in the middle of a white image, so every template
    /// of the sample (of the given maximal radius) fits into the image
    static SampleImage createSampleImage(const QImage& sample, int radius);

    /// Returns the best correlation of the template with the sample placed
    /// in the middle of a white image (the template can be shifted by one pixel)
    static double correlateWithSample(const SampleImage& sampleImage, const RotatedTemplate& tmpl);

    /// Returns the number of the first templates, which are enough for the search,
    /// because the sample is symmetric: a sample, which looks the same when it is
    /// rotated by some angle (a dot or a circle by any angle, a square by 90 degrees,
    /// a rectangle by 180 degrees), is searched only by the angles below that angle.
    /// \param sample Sample
    /// \param templates Templates of the sample rotated by the angles of the full circle
    static size_t getDistinctTemplateCount(const QImage& sample, const std::vector<RotatedTemplate>& templates);

    /// Returns true, if the mirror image of the sample differs from the sample
    /// rotated by any angle (only such samples are searched also mirrored)
    /// \param sample Sample
    /// \param mirroredTemplates Mirrored templates of the sample rotated by the angles of the full circle
    static bool isMirrorDistinct(const QImage& sample, const std::vector<RotatedTemplate>& mirroredTemplates);

    /// Angles of the templates (local maxima), which are similar to the sample
    /// by the ambiguity threshold, angles closer to zero than the minimal angle
    /// are skipped
    static std::vector<double> getAmbiguousAngles(const QImage& sample, const std::vector<RotatedTemplate>& templates, double minimalAngle);

    /// Returns the amount of the ink (sum of 255 - value) in the rectangle
    static double getInk(const QImage& image, const QRect& rect);

    /// Returns the color of the ink of the sample
    static InkColor getInkColor(const QImage& colorImage, const QRect& sampleRect);

    /// Creates the grayscale image, in which only the ink of the color is dark
    static QImage applyInkColor(const QImage& image, const InkColor& inkColor);

    /// Finds the candidates in the reduced image
    /// \param grayImage Grayscale image of the sheet
    /// \param inkRect Rectangle of the ink of the sample in the image
    /// \param settings Settings
    static CoarseResult searchCoarse(const QImage& grayImage, const QRect& inkRect, const FixtureSymbolFinder::Settings& settings);

    /// Confirms the candidates in the image of the higher resolution and refines
    /// their position, angle, size and mirroring. Areas of the image around the
    /// candidates are drawn by the renderer one after another.
    /// \param candidates Candidates in the coordinates of the searched image
    /// \param sample Grayscale image of the sample in the resolution of the confirmation
    /// \param fineScale Scale of the image of the confirmation relative to the searched image
    /// \param coarse Result of the coarse search
    /// \param renderer Renderer of the areas
    /// \param settings Settings
    static std::vector<Candidate> confirm(const QImage& sample,
                                          double fineScale,
                                          const CoarseResult& coarse,
                                          const AreaRenderer& renderer,
                                          const FixtureSymbolFinder::Settings& settings);

    /// Minimal standard deviation of the window, windows with lower deviation
    /// (blank paper) are skipped
    static constexpr double MIN_DEVIATION = 2.0;

    /// Step of the angles tried first in the coarse search, and how much lower
    /// than the threshold their correlation can be to try the remaining angles
    static constexpr size_t ANGLE_PREVIEW_STEP = 3;
    static constexpr double ANGLE_PREVIEW_TOLERANCE = 0.2;

    /// Maximal ratio of the contrast (standard deviation) of the window and of the sample
    static constexpr double COARSE_CONTRAST_TOLERANCE = 2.0;
    static constexpr double FINE_CONTRAST_TOLERANCE = 1.5;

    /// Minimal correlation of the sample and of the rotated sample of a symmetric symbol
    static constexpr double SYMMETRY_THRESHOLD = 0.92;

    /// The reduced sample rotated (or mirrored) by an angle is mistakable for the
    /// sample, if their correlation is at least this value
    static constexpr double AMBIGUITY_THRESHOLD = 0.75;

    /// The threshold of the coarse search is lower than the threshold of the search,
    /// it is derived from the threshold not higher than this limit
    static constexpr double COARSE_THRESHOLD_LIMIT = 0.8;

    /// The correlation of the edges of a confirmed symbol can be lower than
    /// the threshold by this tolerance (edges are thin and more sensitive
    /// to the drawing of the symbol than the ink)
    static constexpr double EDGE_THRESHOLD_TOLERANCE = 0.05;

    /// A pixel of the image darker than the template by this difference is
    /// extra ink (it can be covered by other drawing)
    static constexpr float EXTRA_INK_DIFFERENCE = 96.0f;

    /// Maximal part of the template covered by other drawing
    static constexpr double MAX_OCCLUDED_FRACTION = 0.25;

    /// Minimal part of the edges of the template, which are not covered
    static constexpr double MIN_VISIBLE_EDGE_FRACTION = 0.75;

    /// Size of the cells, by which the candidates are grouped into the areas drawn
    /// for the confirmation, in pixels of the image of the confirmation
    static constexpr int CONFIRMATION_CELL_SIZE = 1024;
};

FixtureSymbolFinderHelper::Image FixtureSymbolFinderHelper::createImage(const QImage& image, bool integralImages)
{
    Image result;
    result.width = image.width();
    result.height = image.height();
    result.values.resize(size_t(result.width) * result.height);

    if (!integralImages)
    {
        for (int y = 0; y < result.height; ++y)
        {
            const uchar* line = image.constScanLine(y);
            std::copy(line, line + result.width, result.values.begin() + size_t(y) * result.width);
        }
        return result;
    }

    result.sum.assign(size_t(result.width + 1) * (result.height + 1), 0.0);
    result.sumOfSquares.assign(size_t(result.width + 1) * (result.height + 1), 0.0);

    const size_t stride = size_t(result.width) + 1;
    for (int y = 0; y < result.height; ++y)
    {
        const uchar* line = image.constScanLine(y);
        double rowSum = 0.0;
        double rowSumOfSquares = 0.0;
        for (int x = 0; x < result.width; ++x)
        {
            const float value = line[x];
            result.values[size_t(y) * result.width + x] = value;
            rowSum += value;
            rowSumOfSquares += double(value) * value;
            result.sum[(y + 1) * stride + (x + 1)] = result.sum[y * stride + (x + 1)] + rowSum;
            result.sumOfSquares[(y + 1) * stride + (x + 1)] = result.sumOfSquares[y * stride + (x + 1)] + rowSumOfSquares;
        }
    }

    return result;
}

FixtureSymbolFinderHelper::RotatedTemplate FixtureSymbolFinderHelper::createTemplate(const QImage& sample, double angle, double scale, bool mirrored)
{
    RotatedTemplate result;
    result.angle = angle;
    result.scale = scale;
    result.mirrored = mirrored;

    const int width = sample.width();
    const int height = sample.height();
    if (width <= 0 || height <= 0 || scale <= 0.0)
    {
        return result;
    }

    const double centerX = (width - 1) * 0.5;
    const double centerY = (height - 1) * 0.5;
    const double radians = qDegreesToRadians(angle);
    const double cosAngle = std::cos(radians) / scale;
    const double sinAngle = std::sin(radians) / scale;
    result.radius = qCeil(std::hypot(width, height) * 0.5 * scale);

    auto getPixel = [&sample](int x, int y) { return double(sample.constScanLine(y)[x]); };

    double sum = 0.0;
    for (int dy = -result.radius; dy <= result.radius; ++dy)
    {
        for (int dx = -result.radius; dx <= result.radius; ++dx)
        {
            // Point of the sample, which is transformed onto this pixel. The mirror
            // image is mirrored horizontally first, then rotated.
            const double rx = cosAngle * dx + sinAngle * dy;
            const double sx = (mirrored ? -rx : rx) + centerX;
            const double sy = -sinAngle * dx + cosAngle * dy + centerY;
            if (sx < -0.5 || sy < -0.5 || sx > width - 0.5 || sy > height - 0.5)
            {
                continue;
            }

            const double x = qBound(0.0, sx, double(width - 1));
            const double y = qBound(0.0, sy, double(height - 1));
            const int x0 = qFloor(x);
            const int y0 = qFloor(y);
            const int x1 = qMin(x0 + 1, width - 1);
            const int y1 = qMin(y0 + 1, height - 1);
            const double fx = x - x0;
            const double fy = y - y0;
            const double value = (getPixel(x0, y0) * (1.0 - fx) + getPixel(x1, y0) * fx) * (1.0 - fy) +
                                 (getPixel(x0, y1) * (1.0 - fx) + getPixel(x1, y1) * fx) * fy;

            result.offsetsX.push_back(qint16(dx));
            result.offsetsY.push_back(qint16(dy));
            result.values.push_back(float(value));
            sum += value;
        }
    }

    const double mean = result.values.empty() ? 0.0 : sum / result.values.size();
    double sumOfSquares = 0.0;
    for (float& value : result.values)
    {
        value = float(value - mean);
        sumOfSquares += double(value) * value;
    }

    result.norm = std::sqrt(sumOfSquares);
    result.mean = mean;
    return result;
}

std::vector<FixtureSymbolFinderHelper::RotatedTemplate> FixtureSymbolFinderHelper::createTemplates(const QImage& sample,
                                                                                                   const std::vector<double>& angles,
                                                                                                   const std::vector<double>& scales,
                                                                                                   const std::vector<bool>& mirrors)
{
    std::vector<RotatedTemplate> templates(mirrors.size() * scales.size() * angles.size());
    std::vector<size_t> indices(templates.size());
    std::iota(indices.begin(), indices.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, indices.cbegin(), indices.cend(), [&](size_t index)
    {
        const size_t angleIndex = index % angles.size();
        const size_t scaleIndex = (index / angles.size()) % scales.size();
        const bool mirrored = mirrors[index / (angles.size() * scales.size())];
        templates[index] = createTemplate(sample, angles[angleIndex], scales[scaleIndex], mirrored);
    });
    return templates;
}

std::vector<double> FixtureSymbolFinderHelper::getAngles(double step)
{
    const int count = qMax(1, qCeil(360.0 / step - 1e-6));
    std::vector<double> angles(count, 0.0);
    for (int i = 0; i < count; ++i)
    {
        angles[i] = 360.0 * i / count;
    }
    return angles;
}

double FixtureSymbolFinderHelper::getAngleStep(const QSize& sampleSize, double pixels)
{
    const double radius = std::max(std::hypot(sampleSize.width(), sampleSize.height()) * 0.5, 1.0);
    return qRadiansToDegrees(std::atan(pixels / radius));
}

std::vector<double> FixtureSymbolFinderHelper::getScales(const QSize& sampleSize, double tolerance, double pixels)
{
    std::vector<double> scales;
    const double radius = std::max(std::hypot(sampleSize.width(), sampleSize.height()) * 0.5, 1.0);
    const int stepCount = tolerance > 0.0 ? qBound(1, qCeil(tolerance * radius / pixels), 2) : 0;
    for (int i = -stepCount; i <= stepCount; ++i)
    {
        scales.push_back(1.0 + tolerance * i / qMax(stepCount, 1));
    }

    // The scale 1 is the first one
    std::stable_sort(scales.begin(), scales.end(), [](double l, double r) { return std::abs(l - 1.0) < std::abs(r - 1.0); });
    return scales;
}

double FixtureSymbolFinderHelper::correlate(const Image& image, const RotatedTemplate& tmpl, int x, int y, double contrastTolerance)
{
    if (x < tmpl.radius || y < tmpl.radius || x + tmpl.radius >= image.width || y + tmpl.radius >= image.height)
    {
        return -1.0;
    }

    const size_t count = tmpl.values.size();
    const float* center = image.values.data() + size_t(y) * image.width + x;

    float sum = 0.0f;
    float sumOfSquares = 0.0f;
    float numerator = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const float value = center[ptrdiff_t(tmpl.offsetsY[i]) * image.width + tmpl.offsetsX[i]];
        sum += value;
        sumOfSquares += value * value;
        numerator += value * tmpl.values[i];
    }

    const double n = double(count);
    const double variance = std::max(double(sumOfSquares) - double(sum) * sum / n, 0.0);
    if (variance < MIN_DEVIATION * MIN_DEVIATION * n)
    {
        return 0.0;
    }

    // Sum of squared deviations of the template is the square of its norm
    const double templateVariance = tmpl.norm * tmpl.norm;
    const double toleranceSquared = contrastTolerance * contrastTolerance;
    if (variance * toleranceSquared < templateVariance || variance > templateVariance * toleranceSquared)
    {
        return 0.0;
    }

    return numerator / (tmpl.norm * std::sqrt(variance));
}

FixtureSymbolFinderHelper::OcclusionResult FixtureSymbolFinderHelper::correlateOccluded(const Image& image,
                                                                                         const Image& edgeImage,
                                                                                         const RotatedTemplate& tmpl,
                                                                                         const RotatedTemplate& edgeTemplate,
                                                                                         int x,
                                                                                         int y)
{
    OcclusionResult result;

    const int radius = tmpl.radius;
    const size_t count = tmpl.values.size();
    if (x < radius || y < radius || x + radius >= image.width || y + radius >= image.height || edgeTemplate.values.size() != count || count == 0)
    {
        return result;
    }

    // Pixels of the template in the square window around the center
    const int side = 2 * radius + 1;
    std::vector<int> windowIndices(size_t(side) * side, -1);
    std::vector<float> imageValues(count, 0.0f);
    std::vector<bool> isExtraInk(count, false);
    const float* center = image.values.data() + size_t(y) * image.width + x;
    for (size_t i = 0; i < count; ++i)
    {
        windowIndices[size_t(tmpl.offsetsY[i] + radius) * side + (tmpl.offsetsX[i] + radius)] = int(i);
        imageValues[i] = center[ptrdiff_t(tmpl.offsetsY[i]) * image.width + tmpl.offsetsX[i]];
        isExtraInk[i] = imageValues[i] < tmpl.values[i] + tmpl.mean - EXTRA_INK_DIFFERENCE;
    }

    auto getIndex = [&](int dx, int dy)
    {
        if (dx < -radius || dy < -radius || dx > radius || dy > radius)
        {
            return -1;
        }
        return windowIndices[size_t(dy + radius) * side + (dx + radius)];
    };

    // Covered pixels are the extra ink connected with the border of the template
    std::vector<bool> isOccluded(count, false);
    std::vector<size_t> stack;
    for (size_t i = 0; i < count; ++i)
    {
        const int dx = tmpl.offsetsX[i];
        const int dy = tmpl.offsetsY[i];
        const bool isBorder = getIndex(dx - 1, dy) < 0 || getIndex(dx + 1, dy) < 0 || getIndex(dx, dy - 1) < 0 || getIndex(dx, dy + 1) < 0;
        if (isBorder && isExtraInk[i])
        {
            isOccluded[i] = true;
            stack.push_back(i);
        }
    }

    while (!stack.empty())
    {
        const size_t i = stack.back();
        stack.pop_back();
        for (int ny = -1; ny <= 1; ++ny)
        {
            for (int nx = -1; nx <= 1; ++nx)
            {
                const int neighbour = getIndex(tmpl.offsetsX[i] + nx, tmpl.offsetsY[i] + ny);
                if (neighbour >= 0 && isExtraInk[neighbour] && !isOccluded[neighbour])
                {
                    isOccluded[neighbour] = true;
                    stack.push_back(size_t(neighbour));
                }
            }
        }
    }

    const size_t occludedCount = size_t(std::count(isOccluded.cbegin(), isOccluded.cend(), true));
    result.occludedFraction = double(occludedCount) / count;

    // Edges of the covering drawing lie around the covered pixels
    constexpr int edgeMargin = 2;
    std::vector<bool> isEdgeOccluded = isOccluded;
    for (size_t i = 0; i < count; ++i)
    {
        if (!isOccluded[i])
        {
            continue;
        }

        for (int ny = -edgeMargin; ny <= edgeMargin; ++ny)
        {
            for (int nx = -edgeMargin; nx <= edgeMargin; ++nx)
            {
                const int neighbour = getIndex(tmpl.offsetsX[i] + nx, tmpl.offsetsY[i] + ny);
                if (neighbour >= 0)
                {
                    isEdgeOccluded[neighbour] = true;
                }
            }
        }
    }

    // Normalized cross correlation of the pixels, which are not covered
    auto correlateMasked = [&](const std::vector<float>& values, const std::vector<float>& templateValues, const std::vector<bool>& isMasked)
    {
        double n = 0.0;
        double sumI = 0.0;
        double sumT = 0.0;
        double sumII = 0.0;
        double sumTT = 0.0;
        double sumIT = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            if (isMasked[i])
            {
                continue;
            }

            const double imageValue = values[i];
            const double templateValue = templateValues[i];
            n += 1.0;
            sumI += imageValue;
            sumT += templateValue;
            sumII += imageValue * imageValue;
            sumTT += templateValue * templateValue;
            sumIT += imageValue * templateValue;
        }

        if (n < 0.5 * count)
        {
            return -1.0;
        }

        const double varianceI = std::max(sumII - sumI * sumI / n, 0.0);
        const double varianceT = std::max(sumTT - sumT * sumT / n, 0.0);
        const double toleranceSquared = FINE_CONTRAST_TOLERANCE * FINE_CONTRAST_TOLERANCE;
        if (varianceI < MIN_DEVIATION * MIN_DEVIATION * n || varianceT < MIN_DEVIATION * MIN_DEVIATION * n ||
            varianceI * toleranceSquared < varianceT || varianceI > varianceT * toleranceSquared)
        {
            return 0.0;
        }

        return (sumIT - sumI * sumT / n) / std::sqrt(varianceI * varianceT);
    };

    std::vector<float> edgeValues(count, 0.0f);
    const float* edgeCenter = edgeImage.values.data() + size_t(y) * edgeImage.width + x;
    for (size_t i = 0; i < count; ++i)
    {
        edgeValues[i] = edgeCenter[ptrdiff_t(tmpl.offsetsY[i]) * edgeImage.width + tmpl.offsetsX[i]];
    }

    // Edges of the symbol must stay mostly visible, otherwise a larger symbol,
    // whose ink around the sample looks like a covering drawing, would be found
    double edges = 0.0;
    double visibleEdges = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        // Edge image is dark at the edges (255 - magnitude)
        const double edge = 255.0 - (edgeTemplate.values[i] + edgeTemplate.mean);
        edges += edge;
        if (!isEdgeOccluded[i])
        {
            visibleEdges += edge;
        }
    }
    result.visibleEdgeFraction = edges > 0.0 ? visibleEdges / edges : 0.0;

    result.score = correlateMasked(imageValues, tmpl.values, isOccluded);
    result.edgeScore = correlateMasked(edgeValues, edgeTemplate.values, isEdgeOccluded);
    return result;
}

std::vector<FixtureSymbolFinderHelper::Candidate> FixtureSymbolFinderHelper::findCandidates(const Image& image,
                                                                                             const std::vector<RotatedTemplate>& templates,
                                                                                             double minimalInk,
                                                                                             double threshold)
{
    std::vector<Candidate> candidates;
    if (templates.empty())
    {
        return candidates;
    }

    int radius = 0;
    size_t minimalCount = std::numeric_limits<size_t>::max();
    for (const RotatedTemplate& tmpl : templates)
    {
        radius = std::max(radius, tmpl.radius);
        minimalCount = std::min(minimalCount, tmpl.values.size());
    }

    const int width = image.width;
    const int height = image.height;
    if (width <= 2 * radius || height <= 2 * radius)
    {
        return candidates;
    }

    std::vector<float> scores(size_t(width) * height, -1.0f);
    std::vector<int> bestTemplates(size_t(width) * height, 0);
    std::vector<int> rows(height - 2 * radius);
    std::iota(rows.begin(), rows.end(), radius);

    const int side = 2 * radius + 1;
    const double windowArea = double(side) * side;
    const double minimalVariance = MIN_DEVIATION * MIN_DEVIATION * double(minimalCount);
    const float previewThreshold = templates.size() > ANGLE_PREVIEW_STEP ? float(threshold - ANGLE_PREVIEW_TOLERANCE) : -1.0f;

    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, rows.cbegin(), rows.cend(), [&](int y)
    {
        for (int x = radius; x < width - radius; ++x)
        {
            // The square around the pixel contains the rotated symbol for any angle,
            // so its ink and its variance are bounds independent of the angle (the sum
            // of squared deviations of a window is not lower than of its part).
            const double sum = image.getSum(image.sum, x - radius, y - radius, side, side);
            const double sumOfSquares = image.getSum(image.sumOfSquares, x - radius, y - radius, side, side);
            const double ink = 255.0 * windowArea - sum;
            if (ink < minimalInk || sumOfSquares - sum * sum / windowArea < minimalVariance)
            {
                continue;
            }

            // The correlation changes slowly with the angle (the image is blurred),
            // so every third angle is tried first and the remaining angles only
            // when the symbol is likely there
            float bestScore = -1.0f;
            int bestTemplate = 0;
            for (size_t pass = 0; pass < 2; ++pass)
            {
                for (size_t i = 0; i < templates.size(); ++i)
                {
                    if ((i % ANGLE_PREVIEW_STEP == 0) != (pass == 0))
                    {
                        continue;
                    }

                    const float score = float(correlate(image, templates[i], x, y, COARSE_CONTRAST_TOLERANCE));
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestTemplate = int(i);
                    }
                }

                if (bestScore < previewThreshold)
                {
                    break;
                }
            }

            scores[size_t(y) * width + x] = bestScore;
            bestTemplates[size_t(y) * width + x] = bestTemplate;
        }
    });

    for (int y = radius; y < height - radius; ++y)
    {
        for (int x = radius; x < width - radius; ++x)
        {
            const float score = scores[size_t(y) * width + x];
            if (score < threshold)
            {
                continue;
            }

            bool isMaximum = true;
            for (int j = -1; j <= 1 && isMaximum; ++j)
            {
                for (int i = -1; i <= 1; ++i)
                {
                    const float neighbourScore = scores[size_t(y + j) * width + (x + i)];

                    // Plateaus are resolved by the order of the pixels
                    if ((i != 0 || j != 0) && (neighbourScore > score || (neighbourScore == score && (j < 0 || (j == 0 && i < 0)))))
                    {
                        isMaximum = false;
                        break;
                    }
                }
            }

            if (isMaximum)
            {
                const RotatedTemplate& tmpl = templates[bestTemplates[size_t(y) * width + x]];
                candidates.push_back({ QPointF(x + 0.5, y + 0.5), score, tmpl.angle, tmpl.scale, tmpl.mirrored });
            }
        }
    }

    return candidates;
}

std::vector<FixtureSymbolFinderHelper::Candidate> FixtureSymbolFinderHelper::suppressNonMaxima(std::vector<Candidate> candidates, double distance)
{
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) { return l.score > r.score; });

    std::vector<Candidate> result;
    const double distanceSquared = distance * distance;
    for (const Candidate& candidate : candidates)
    {
        const bool isSuppressed = std::any_of(result.cbegin(), result.cend(), [&](const Candidate& kept)
        {
            const QPointF difference = kept.center - candidate.center;
            return QPointF::dotProduct(difference, difference) < distanceSquared;
        });

        if (!isSuppressed)
        {
            result.push_back(candidate);
        }
    }

    return result;
}

std::vector<FixtureSymbolFinderHelper::Candidate> FixtureSymbolFinderHelper::suppressOverlapping(std::vector<Candidate> candidates, const QSizeF& sampleSize, double minimalDistance)
{
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) { return l.score > r.score; });

    std::vector<Candidate> result;
    const double minimalDistanceSquared = minimalDistance * minimalDistance;
    for (const Candidate& candidate : candidates)
    {
        const bool isSuppressed = std::any_of(result.cbegin(), result.cend(), [&](const Candidate& kept)
        {
            const QPointF difference = candidate.center - kept.center;
            if (QPointF::dotProduct(difference, difference) < minimalDistanceSquared)
            {
                return true;
            }

            // Offset in the coordinates of the sample (the inverse of the rotation
            // of the template, the mirroring does not change the absolute values)
            const double radians = qDegreesToRadians(kept.angle);
            const double cosAngle = std::cos(radians);
            const double sinAngle = std::sin(radians);
            const double scale = kept.scale > 0.0 ? kept.scale : 1.0;
            const double x = (cosAngle * difference.x() + sinAngle * difference.y()) / scale;
            const double y = (-sinAngle * difference.x() + cosAngle * difference.y()) / scale;
            return std::abs(x) < 0.5 * sampleSize.width() && std::abs(y) < 0.5 * sampleSize.height();
        });

        if (!isSuppressed)
        {
            result.push_back(candidate);
        }
    }

    return result;
}

QImage FixtureSymbolFinderHelper::blur(const QImage& image)
{
    QImage result(image.size(), QImage::Format_Grayscale8);
    const int width = image.width();
    const int height = image.height();

    std::vector<int> rows(height);
    std::iota(rows.begin(), rows.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, rows.cbegin(), rows.cend(), [&](int y)
    {
        const uchar* lines[3] = { image.constScanLine(qMax(y - 1, 0)), image.constScanLine(y), image.constScanLine(qMin(y + 1, height - 1)) };
        uchar* target = result.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            const int x0 = qMax(x - 1, 0);
            const int x1 = qMin(x + 1, width - 1);
            int sum = 0;
            for (const uchar* line : lines)
            {
                sum += line[x0] + line[x] + line[x1];
            }
            target[x] = uchar((sum + 4) / 9);
        }
    });

    return result;
}

QImage FixtureSymbolFinderHelper::getEdges(const QImage& image)
{
    QImage edges(image.size(), QImage::Format_Grayscale8);
    const int width = image.width();
    const int height = image.height();
    for (int y = 0; y < height; ++y)
    {
        const uchar* top = image.constScanLine(qMax(y - 1, 0));
        const uchar* line = image.constScanLine(y);
        const uchar* bottom = image.constScanLine(qMin(y + 1, height - 1));
        uchar* target = edges.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            const int dx = int(line[qMin(x + 1, width - 1)]) - int(line[qMax(x - 1, 0)]);
            const int dy = int(bottom[x]) - int(top[x]);
            target[x] = uchar(255 - qMin(255, qRound(0.5 * std::sqrt(double(dx * dx + dy * dy)))));
        }
    }
    return blur(edges);
}

QImage FixtureSymbolFinderHelper::addMargin(const QImage& sample, int margin)
{
    QImage result(sample.width() + 2 * margin, sample.height() + 2 * margin, QImage::Format_Grayscale8);
    result.fill(Qt::white);
    for (int y = 0; y < sample.height(); ++y)
    {
        const uchar* source = sample.constScanLine(y);
        std::copy(source, source + sample.width(), result.scanLine(y + margin) + margin);
    }
    return result;
}

QImage FixtureSymbolFinderHelper::copyArea(const QImage& image, const QRect& area)
{
    QImage result(area.size(), QImage::Format_Grayscale8);
    result.fill(Qt::white);
    const QRect clippedArea = area.intersected(image.rect());
    for (int y = clippedArea.top(); y <= clippedArea.bottom(); ++y)
    {
        const uchar* source = image.constScanLine(y) + clippedArea.left();
        std::copy(source, source + clippedArea.width(), result.scanLine(y - area.top()) + (clippedArea.left() - area.left()));
    }
    return result;
}

FixtureSymbolFinderHelper::SampleImage FixtureSymbolFinderHelper::createSampleImage(const QImage& sample, int radius)
{
    SampleImage result;
    const int margin = radius + 2;
    result.image = createImage(addMargin(sample, margin), false);
    result.x = margin + (sample.width() - 1) / 2;
    result.y = margin + (sample.height() - 1) / 2;
    return result;
}

double FixtureSymbolFinderHelper::correlateWithSample(const SampleImage& sampleImage, const RotatedTemplate& tmpl)
{
    double score = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            score = std::max(score, correlate(sampleImage.image, tmpl, sampleImage.x + dx, sampleImage.y + dy, std::numeric_limits<double>::max()));
        }
    }
    return score;
}

size_t FixtureSymbolFinderHelper::getDistinctTemplateCount(const QImage& sample, const std::vector<RotatedTemplate>& templates)
{
    if (templates.size() < 2)
    {
        return templates.size();
    }

    int radius = 0;
    for (const RotatedTemplate& tmpl : templates)
    {
        radius = std::max(radius, tmpl.radius);
    }

    const SampleImage sampleImage = createSampleImage(sample, radius);
    std::vector<bool> isSame(templates.size(), true);
    for (size_t i = 1; i < templates.size(); ++i)
    {
        isSame[i] = correlateWithSample(sampleImage, templates[i]) >= SYMMETRY_THRESHOLD;
    }

    // The sample is symmetric by an angle, if the angle divides the full circle
    // and the sample looks the same rotated by all multiples of the angle
    for (size_t i = 1; i < templates.size(); ++i)
    {
        if (templates.size() % i != 0)
        {
            continue;
        }

        bool isSymmetric = true;
        for (size_t j = i; j < templates.size() && isSymmetric; j += i)
        {
            isSymmetric = isSame[j];
        }

        if (isSymmetric)
        {
            return i;
        }
    }

    return templates.size();
}

std::vector<double> FixtureSymbolFinderHelper::getAmbiguousAngles(const QImage& sample, const std::vector<RotatedTemplate>& templates, double minimalAngle)
{
    std::vector<double> result;
    if (templates.empty())
    {
        return result;
    }

    int radius = 0;
    for (const RotatedTemplate& tmpl : templates)
    {
        radius = std::max(radius, tmpl.radius);
    }

    const SampleImage sampleImage = createSampleImage(sample, radius);
    std::vector<double> correlations(templates.size(), 0.0);
    for (size_t i = 0; i < templates.size(); ++i)
    {
        correlations[i] = correlateWithSample(sampleImage, templates[i]);
    }

    const size_t count = templates.size();
    for (size_t i = 0; i < count; ++i)
    {
        const double angle = templates[i].angle;
        const double distanceFromZero = std::min(angle, 360.0 - angle);
        const double previous = correlations[(i + count - 1) % count];
        const double next = correlations[(i + 1) % count];
        if (distanceFromZero >= minimalAngle && correlations[i] >= AMBIGUITY_THRESHOLD && correlations[i] >= previous && correlations[i] >= next)
        {
            result.push_back(angle);
        }
    }

    return result;
}

bool FixtureSymbolFinderHelper::isMirrorDistinct(const QImage& sample, const std::vector<RotatedTemplate>& mirroredTemplates)
{
    int radius = 0;
    for (const RotatedTemplate& tmpl : mirroredTemplates)
    {
        radius = std::max(radius, tmpl.radius);
    }

    const SampleImage sampleImage = createSampleImage(sample, radius);
    return std::none_of(mirroredTemplates.cbegin(), mirroredTemplates.cend(), [&sampleImage](const RotatedTemplate& tmpl) { return correlateWithSample(sampleImage, tmpl) >= SYMMETRY_THRESHOLD; });
}

double FixtureSymbolFinderHelper::getInk(const QImage& image, const QRect& rect)
{
    const QRect clippedRect = rect.intersected(image.rect());
    double ink = 0.0;
    for (int y = clippedRect.top(); y <= clippedRect.bottom(); ++y)
    {
        const uchar* line = image.constScanLine(y);
        for (int x = clippedRect.left(); x <= clippedRect.right(); ++x)
        {
            ink += 255 - line[x];
        }
    }
    return ink;
}

FixtureSymbolFinderHelper::InkColor FixtureSymbolFinderHelper::getInkColor(const QImage& colorImage, const QRect& sampleRect)
{
    InkColor result;

    const QRect rect = sampleRect.normalized().intersected(colorImage.rect());
    if (rect.isEmpty())
    {
        return result;
    }

    // Color of the ink is the color of the darkest tenth of the sample
    std::vector<int> luminances;
    luminances.reserve(size_t(rect.width()) * rect.height());
    for (int y = rect.top(); y <= rect.bottom(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(colorImage.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); ++x)
        {
            luminances.push_back(qGray(line[x]));
        }
    }

    auto darkest = luminances.begin() + luminances.size() / 10;
    std::nth_element(luminances.begin(), darkest, luminances.end());
    const int luminanceThreshold = *darkest;

    // Median of each channel, so a few pixels of another dark color in the sample
    // (a colored line crossing the symbol) do not change the color of the ink
    std::vector<int> reds;
    std::vector<int> greens;
    std::vector<int> blues;
    for (int y = rect.top(); y <= rect.bottom(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(colorImage.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); ++x)
        {
            if (qGray(line[x]) <= luminanceThreshold)
            {
                reds.push_back(qRed(line[x]));
                greens.push_back(qGreen(line[x]));
                blues.push_back(qBlue(line[x]));
            }
        }
    }

    auto getMedian = [](std::vector<int>& values)
    {
        auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };

    result.red = getMedian(reds);
    result.green = getMedian(greens);
    result.blue = getMedian(blues);
    const int range = std::max({ 255 - result.red, 255 - result.green, 255 - result.blue });

    // Without ink in the sample, the image is only converted to grayscale
    result.isValid = range >= 32;
    result.whiteDistance = qMax(1, range * 7 / 10);
    return result;
}

QImage FixtureSymbolFinderHelper::applyInkColor(const QImage& image, const InkColor& inkColor)
{
    if (!inkColor.isValid)
    {
        return image.convertToFormat(QImage::Format_Grayscale8);
    }

    // Distance from the color of the ink (the largest difference of the channels),
    // white paper and the ink of other colors have the largest distance. Colors
    // far from the ink (also light colored backgrounds) are white.
    const QImage colorImage = image.convertToFormat(QImage::Format_RGB32);
    QImage result(colorImage.size(), QImage::Format_Grayscale8);
    std::vector<int> rows(colorImage.height());
    std::iota(rows.begin(), rows.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, rows.cbegin(), rows.cend(), [&](int y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(colorImage.constScanLine(y));
        uchar* target = result.scanLine(y);
        for (int x = 0; x < colorImage.width(); ++x)
        {
            const QRgb pixel = line[x];
            const int distance = std::max({ std::abs(qRed(pixel) - inkColor.red), std::abs(qGreen(pixel) - inkColor.green), std::abs(qBlue(pixel) - inkColor.blue) });
            target[x] = uchar(std::min(255, distance * 255 / inkColor.whiteDistance));
        }
    });

    return result;
}

FixtureSymbolFinderHelper::CoarseResult FixtureSymbolFinderHelper::searchCoarse(const QImage& grayImage, const QRect& inkRect, const FixtureSymbolFinder::Settings& settings)
{
    CoarseResult result;

    const int sampleSize = std::max(inkRect.width(), inkRect.height());
    const double scale = std::min(1.0, double(std::max(settings.coarseSampleSize, 6)) / sampleSize);
    result.scale = scale;

    // Coarse search on the reduced and blurred image, the blur makes the
    // correlation tolerant to the step of the angle and to the size
    const QImage coarseImage = blur(scale < 1.0 ? grayImage.scaled(qMax(1, qRound(grayImage.width() * scale)), qMax(1, qRound(grayImage.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                                : grayImage);
    const QRect coarseInkRect = QRect(qFloor(inkRect.left() * scale), qFloor(inkRect.top() * scale),
                                      qMax(3, qCeil(inkRect.width() * scale)), qMax(3, qCeil(inkRect.height() * scale))).intersected(coarseImage.rect());
    const QImage coarseSample = addMargin(coarseImage.copy(coarseInkRect), 1);

    const double angleStep = settings.rotations ? qBound(2.0, getAngleStep(coarseSample.size(), 1.5), 30.0) : 360.0;
    result.angleStep = angleStep;

    const std::vector<double> angles = getAngles(angleStep);
    std::vector<RotatedTemplate> templates = createTemplates(coarseSample, angles, { 1.0 }, settings.mirrors ? std::vector<bool>{ false, true } : std::vector<bool>{ false });
    if (std::any_of(templates.cbegin(), templates.cend(), [](const RotatedTemplate& tmpl) { return !tmpl.isValid(); }))
    {
        return result;
    }

    std::vector<RotatedTemplate> mirroredTemplates;
    if (settings.mirrors)
    {
        mirroredTemplates.assign(std::make_move_iterator(templates.begin() + angles.size()), std::make_move_iterator(templates.end()));
        templates.resize(angles.size());
    }

    // Symmetric samples are searched only by the distinct angles
    const size_t distinctCount = getDistinctTemplateCount(coarseSample, templates);
    if (distinctCount == angles.size())
    {
        result.ambiguousAngles = getAmbiguousAngles(coarseSample, templates, 1.5 * angleStep);
    }
    templates.resize(distinctCount);
    result.symmetryAngle = 360.0 * distinctCount / angles.size();
    if (settings.mirrors && isMirrorDistinct(coarseSample, mirroredTemplates))
    {
        result.isMirrorSearched = true;
        result.ambiguousMirrorAngles = getAmbiguousAngles(coarseSample, mirroredTemplates, -1.0);
        mirroredTemplates.resize(distinctCount);
        templates.insert(templates.end(), std::make_move_iterator(mirroredTemplates.begin()), std::make_move_iterator(mirroredTemplates.end()));
    }

    // Reduced image is blurred, so the threshold of the coarse search is lower,
    // the candidates are confirmed in the image of the higher resolution. Parts
    // of the symbol can be covered by other lines, so the ink is only roughly bounded.
    const double coarseThreshold = std::max(0.3, std::min(settings.threshold, COARSE_THRESHOLD_LIMIT) - 0.25);
    const double minimalInk = 0.5 * getInk(coarseImage, coarseInkRect);
    const Image coarseIntegralImage = createImage(coarseImage, true);
    std::vector<Candidate> candidates = findCandidates(coarseIntegralImage, templates, minimalInk, coarseThreshold);

    const double suppressDistance = 0.5 * std::min(coarseInkRect.width(), coarseInkRect.height());
    candidates = suppressNonMaxima(std::move(candidates), std::max(suppressDistance, 1.0));

    for (Candidate& candidate : candidates)
    {
        candidate.center /= scale;
    }

    result.candidates = std::move(candidates);
    return result;
}

std::vector<FixtureSymbolFinderHelper::Candidate> FixtureSymbolFinderHelper::confirm(const QImage& sample,
                                                                                      double fineScale,
                                                                                      const CoarseResult& coarse,
                                                                                      const AreaRenderer& renderer,
                                                                                      const FixtureSymbolFinder::Settings& settings)
{
    const std::vector<Candidate>& candidates = coarse.candidates;
    const double coarseAngleStep = coarse.angleStep;
    const double positionTolerance = 1.0 / coarse.scale;

    std::vector<Candidate> confirmed;
    if (candidates.empty() || sample.isNull())
    {
        return confirmed;
    }

    const int sampleSize = std::max(sample.width(), sample.height());
    const int margin = qMax(2, qRound(0.15 * sampleSize));
    const QImage sampleWithMargin = addMargin(sample, margin);
    const QImage edgeSample = getEdges(sampleWithMargin);

    // Templates for all angles, sizes and mirrors
    const double angleStep = settings.rotations ? qBound(0.5, getAngleStep(sampleWithMargin.size(), 1.25), coarseAngleStep) : 360.0;
    const std::vector<double> angles = getAngles(angleStep);
    const std::vector<double> scales = getScales(sample.size(), qBound(0.0, settings.scaleTolerance, 0.5), 1.5);
    std::vector<RotatedTemplate> templates = createTemplates(sampleWithMargin, angles, scales, { false });
    if (std::any_of(templates.cbegin(), templates.cend(), [](const RotatedTemplate& tmpl) { return !tmpl.isValid(); }))
    {
        return confirmed;
    }

    // Mirror image of a symmetric sample is the sample rotated, it is not searched
    size_t mirrorCount = 1;
    if (settings.mirrors && isMirrorDistinct(sampleWithMargin, createTemplates(sampleWithMargin, angles, { 1.0 }, { true })))
    {
        std::vector<RotatedTemplate> mirroredTemplates = createTemplates(sampleWithMargin, angles, scales, { true });
        templates.insert(templates.end(), std::make_move_iterator(mirroredTemplates.begin()), std::make_move_iterator(mirroredTemplates.end()));
        mirrorCount = 2;
    }

    // The reduced sample can be more symmetric than the sample (a small rectangle
    // looks like a square), then a candidate can be rotated by the multiples of
    // the angle of the symmetry of the reduced sample, up to the angle of the
    // symmetry of the sample. A mirror image can be found as the sample.
    const double fineAngleStep = 360.0 / angles.size();
    const double fineSymmetryAngle = fineAngleStep * getDistinctTemplateCount(sampleWithMargin, std::vector<RotatedTemplate>(templates.begin(), templates.begin() + angles.size()));
    std::vector<int> angleOffsets;
    for (double offset = 0.0; offset < fineSymmetryAngle - 0.5 * coarse.symmetryAngle; offset += coarse.symmetryAngle)
    {
        angleOffsets.push_back(qRound(offset / fineAngleStep));
    }
    if (angleOffsets.empty())
    {
        angleOffsets.push_back(0);
    }
    std::vector<int> ambiguousOffsets;
    if (settings.rotations)
    {
        for (double angle : coarse.ambiguousAngles)
        {
            ambiguousOffsets.push_back(qRound(angle / fineAngleStep));
        }
    }
    std::vector<int> ambiguousMirrorOffsets;
    for (double angle : coarse.ambiguousMirrorAngles)
    {
        if (settings.rotations || qFuzzyIsNull(angle))
        {
            ambiguousMirrorOffsets.push_back(qRound(angle / fineAngleStep));
        }
    }
    auto getTemplate = [&](size_t mirror, size_t scale, int angle) -> const RotatedTemplate&
    {
        const int angleCount = int(angles.size());
        const size_t angleIndex = size_t(((angle % angleCount) + angleCount) % angleCount);
        return templates[(mirror * scales.size() + scale) * angles.size() + angleIndex];
    };

    int maximalRadius = 0;
    for (const RotatedTemplate& tmpl : templates)
    {
        maximalRadius = std::max(maximalRadius, tmpl.radius);
    }

    const int angleRange = settings.rotations ? qCeil(coarseAngleStep / (360.0 / angles.size())) : 0;
    const int searchRadius = qCeil(positionTolerance * fineScale) + 1;
    const int gridStep = qMax(1, searchRadius / 2);
    const int angleStride = qMax(1, angleRange / 2);
    const double roughThreshold = std::max(0.3, std::min(settings.threshold, COARSE_THRESHOLD_LIMIT) - 0.25);

    // Candidates are grouped by cells, the area around the candidates of a cell
    // is drawn at once (areas are drawn one after another, the page is not drawn
    // by more threads at once)
    std::map<std::pair<int, int>, std::vector<size_t>> cells;
    std::vector<QPoint> fineCenters(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        fineCenters[i] = QPoint(qFloor(candidates[i].center.x() * fineScale), qFloor(candidates[i].center.y() * fineScale));
        cells[std::make_pair(qFloor(double(fineCenters[i].y()) / CONFIRMATION_CELL_SIZE), qFloor(double(fineCenters[i].x()) / CONFIRMATION_CELL_SIZE))].push_back(i);
    }

    std::vector<Candidate> refined(candidates.size());
    for (const auto& cell : cells)
    {
        const std::vector<size_t>& indices = cell.second;
        QRect area;
        for (size_t index : indices)
        {
            area = area.united(QRect(fineCenters[index], QSize(1, 1)));
        }

        const int areaMargin = maximalRadius + searchRadius + gridStep + 2;
        area.adjust(-areaMargin, -areaMargin, areaMargin, areaMargin);
        const QImage areaImage = renderer(area);
        if (areaImage.size() != area.size())
        {
            continue;
        }

        const QImage blurredAreaImage = blur(areaImage);
        const Image image = createImage(blurredAreaImage, false);
        const Image edgeImage = createImage(getEdges(blurredAreaImage), false);
        pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, indices.cbegin(), indices.cend(), [&](size_t index)
        {
            const Candidate& candidate = candidates[index];
            const int centerX = fineCenters[index].x() - area.left();
            const int centerY = fineCenters[index].y() - area.top();
            const int angleIndex = qRound(candidate.angle / fineAngleStep);
            const size_t candidateMirror = candidate.mirrored && mirrorCount > 1 ? 1 : 0;
            const bool tryBothMirrors = mirrorCount > 1 && !coarse.isMirrorSearched;

            Candidate best;
            best.score = -1.0;
            int bestX = centerX;
            int bestY = centerY;
            int bestAngle = angleIndex;
            size_t bestMirror = candidateMirror;
            size_t bestScale = 0;

            auto tryPosition = [&](size_t mirror, size_t scale, int angle, int x, int y)
            {
                const RotatedTemplate& tmpl = getTemplate(mirror, scale, angle);
                const double score = correlate(image, tmpl, x, y, FINE_CONTRAST_TOLERANCE);
                if (score > best.score)
                {
                    best.score = score;
                    best.angle = tmpl.angle;
                    best.scale = tmpl.scale;
                    best.mirrored = tmpl.mirrored;
                    bestX = x;
                    bestY = y;
                    bestAngle = angle;
                    bestMirror = mirror;
                    bestScale = scale;
                }
            };

            // Orientations (mirror and angle), which the candidate can have: the angle
            // of the coarse search, its multiples of the symmetry of the reduced sample,
            // and the orientations mistakable for it in the reduced image
            std::vector<std::pair<size_t, int>> orientations;
            for (size_t mirror = 0; mirror < mirrorCount; ++mirror)
            {
                if (mirror != candidateMirror && !tryBothMirrors)
                {
                    continue;
                }

                for (int angleOffset : angleOffsets)
                {
                    orientations.emplace_back(mirror, angleIndex + angleOffset);
                }

                for (int angleOffset : ambiguousOffsets)
                {
                    orientations.emplace_back(mirror, angleIndex + angleOffset);
                }
            }
            if (mirrorCount > 1)
            {
                // Sample S looks like its mirror image R(c) M S, so the candidate R(a) S
                // looks like R(a + c) M S and the candidate R(a) M S like R(a - c) S
                for (int angleOffset : ambiguousMirrorOffsets)
                {
                    orientations.emplace_back(1 - candidateMirror, candidateMirror == 0 ? angleIndex + angleOffset : angleIndex - angleOffset);
                }
            }

            // Positions on a grid and every other angle around each orientation, in the
            // original size. Orientations, which are not similar even roughly, are
            // rejected, the others are refined in the position and the angle (a wrong
            // orientation can be better on the grid). Then all sizes and mirrors are
            // tried near the best position and angle.
            struct GridResult
            {
                double score = -1.0;
                int x = 0;
                int y = 0;
                int angle = 0;
                size_t mirror = 0;
            };

            std::vector<GridResult> gridResults;
            for (const auto& orientation : orientations)
            {
                best.score = -1.0;
                for (int angle = orientation.second - angleRange; angle <= orientation.second + angleRange; angle += angleStride)
                {
                    for (int y = centerY - searchRadius; y <= centerY + searchRadius; y += gridStep)
                    {
                        for (int x = centerX - searchRadius; x <= centerX + searchRadius; x += gridStep)
                        {
                            tryPosition(orientation.first, 0, angle, x, y);
                        }
                    }
                }

                if (best.score >= roughThreshold)
                {
                    gridResults.push_back({ best.score, bestX, bestY, bestAngle, bestMirror });
                }
            }

            if (gridResults.empty())
            {
                return;
            }

            best.score = -1.0;
            for (const GridResult& gridResult : gridResults)
            {
                for (int angle = gridResult.angle - angleStride + 1; angle < gridResult.angle + angleStride; ++angle)
                {
                    for (int y = gridResult.y - gridStep + 1; y < gridResult.y + gridStep; ++y)
                    {
                        for (int x = gridResult.x - gridStep + 1; x < gridResult.x + gridStep; ++x)
                        {
                            tryPosition(gridResult.mirror, 0, angle, x, y);
                        }
                    }
                }
            }

            const int refinedX = bestX;
            const int refinedY = bestY;
            const int refinedAngle = bestAngle;
            const size_t refinedMirror = bestMirror;
            for (size_t mirror = 0; mirror < mirrorCount; ++mirror)
            {
                // A mirror image of a symbol, which is mirror symmetric by an axis,
                // is the symbol rotated, so the other mirror is tried by more angles
                const int mirrorAngleRange = mirror == refinedMirror ? 1 : angleRange;
                for (size_t scale = 0; scale < scales.size(); ++scale)
                {
                    for (int angle = refinedAngle - mirrorAngleRange; angle <= refinedAngle + mirrorAngleRange; ++angle)
                    {
                        for (int y = refinedY - 1; y <= refinedY + 1; ++y)
                        {
                            for (int x = refinedX - 1; x <= refinedX + 1; ++x)
                            {
                                tryPosition(mirror, scale, angle, x, y);
                            }
                        }
                    }
                }
            }

            // Edges of the symbol at the best position, angle, size and mirroring
            best.edgeScore = -1.0;
            for (int angle = bestAngle - 1; angle <= bestAngle + 1; ++angle)
            {
                const RotatedTemplate& tmpl = getTemplate(best.mirrored ? 1 : 0, 0, angle);
                const RotatedTemplate edgeTemplate = createTemplate(edgeSample, tmpl.angle, best.scale, best.mirrored);
                for (int y = bestY - 1; y <= bestY + 1; ++y)
                {
                    for (int x = bestX - 1; x <= bestX + 1; ++x)
                    {
                        best.edgeScore = std::max(best.edgeScore, correlate(edgeImage, edgeTemplate, x, y, FINE_CONTRAST_TOLERANCE));
                    }
                }
            }

            // A symbol partially covered by other drawing (a line or a text running
            // into the symbol, which was not removed) is compared without the covered
            // pixels, if most of the symbol is visible
            if (best.score >= roughThreshold && (best.score < settings.threshold || best.edgeScore < settings.threshold - EDGE_THRESHOLD_TOLERANCE))
            {
                const RotatedTemplate& tmpl = getTemplate(best.mirrored ? 1 : 0, bestScale, bestAngle);
                const RotatedTemplate edgeTemplate = createTemplate(edgeSample, tmpl.angle, tmpl.scale, tmpl.mirrored);
                for (int y = bestY - 1; y <= bestY + 1; ++y)
                {
                    for (int x = bestX - 1; x <= bestX + 1; ++x)
                    {
                        const OcclusionResult occlusion = correlateOccluded(image, edgeImage, tmpl, edgeTemplate, x, y);
                        if (occlusion.occludedFraction <= MAX_OCCLUDED_FRACTION && occlusion.visibleEdgeFraction >= MIN_VISIBLE_EDGE_FRACTION && std::min(occlusion.score, occlusion.edgeScore + EDGE_THRESHOLD_TOLERANCE) > std::min(best.score, best.edgeScore + EDGE_THRESHOLD_TOLERANCE))
                        {
                            best.score = occlusion.score;
                            best.edgeScore = occlusion.edgeScore;
                            bestX = x;
                            bestY = y;
                        }
                    }
                }
            }

            best.center = QPointF((bestX + area.left() + 0.5) / fineScale, (bestY + area.top() + 0.5) / fineScale);
            refined[index] = best;
        });
    }

    for (const Candidate& candidate : refined)
    {
        if (candidate.score >= settings.threshold && candidate.edgeScore >= settings.threshold - EDGE_THRESHOLD_TOLERANCE)
        {
            confirmed.push_back(candidate);
        }
    }

    return confirmed;
}

QRect FixtureSymbolFinder::getInkBoundingRect(const QImage& image, const QRect& rect)
{
    constexpr int inkThreshold = 224;

    const QRect clippedRect = rect.intersected(image.rect());
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (int y = clippedRect.top(); y <= clippedRect.bottom(); ++y)
    {
        const uchar* line = image.constScanLine(y);
        for (int x = clippedRect.left(); x <= clippedRect.right(); ++x)
        {
            if (line[x] < inkThreshold)
            {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (minX > maxX)
    {
        return QRect();
    }

    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QImage FixtureSymbolFinder::createInkImage(const QImage& image, const QRect& sampleRect)
{
    const QImage colorImage = image.convertToFormat(QImage::Format_RGB32);
    return FixtureSymbolFinderHelper::applyInkColor(colorImage, FixtureSymbolFinderHelper::getInkColor(colorImage, sampleRect));
}

std::vector<FixtureSymbolFinder::Match> FixtureSymbolFinder::find(const QImage& image, QRect sampleRect, const Settings& settings)
{
    std::vector<Match> matches;

    if (image.isNull())
    {
        return matches;
    }

    const QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);

    // Whitespace around the symbol, which the user included in the sample, would
    // make the sample depend on the neighbourhood of the symbol
    QRect inkRect = getInkBoundingRect(grayImage, sampleRect.normalized());
    if (inkRect.isEmpty() || inkRect.width() < 3 || inkRect.height() < 3)
    {
        return matches;
    }
    inkRect = inkRect.adjusted(-1, -1, 1, 1).intersected(grayImage.rect());

    const FixtureSymbolFinderHelper::CoarseResult coarse = FixtureSymbolFinderHelper::searchCoarse(grayImage, inkRect, settings);

    // Candidates are confirmed in the same image, a large sample in a reduced
    // image (the number of the angles and the size of the templates grow with
    // the size of the sample)
    const int sampleSize = std::max(inkRect.width(), inkRect.height());
    const int maximalSampleSize = 2 * qMax(settings.fineSampleSize, 8);
    const double fineScale = sampleSize > maximalSampleSize ? double(maximalSampleSize) / sampleSize : 1.0;
    const QImage fineImage = fineScale < 1.0 ? grayImage.scaled(qMax(1, qRound(grayImage.width() * fineScale)), qMax(1, qRound(grayImage.height() * fineScale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                             : grayImage;
    const QRect fineInkRect = fineScale < 1.0 ? QRectF(QPointF(inkRect.topLeft()) * fineScale, QSizeF(inkRect.size()) * fineScale).toAlignedRect().intersected(fineImage.rect()) : inkRect;
    const QImage sample = FixtureSymbolFinderHelper::blur(FixtureSymbolFinderHelper::copyArea(fineImage, fineInkRect.adjusted(-1, -1, 1, 1))).copy(1, 1, fineInkRect.width(), fineInkRect.height());
    auto renderer = [&fineImage](const QRect& area) { return FixtureSymbolFinderHelper::copyArea(fineImage, area); };
    std::vector<FixtureSymbolFinderHelper::Candidate> confirmed = FixtureSymbolFinderHelper::confirm(sample, fineScale, coarse, renderer, settings);

    const double suppressDistance = 0.5 * std::min(inkRect.width(), inkRect.height());
    confirmed = FixtureSymbolFinderHelper::suppressOverlapping(std::move(confirmed), inkRect.size(), std::max(suppressDistance, 1.0));

    for (const FixtureSymbolFinderHelper::Candidate& candidate : confirmed)
    {
        matches.push_back({ candidate.center, candidate.score, candidate.angle, candidate.scale, candidate.mirrored });
    }

    return matches;
}

std::vector<FixtureSymbolFinder::Match> FixtureSymbolFinder::findOnPage(const pdf::PDFPrecompiledPage& compiledPage,
                                                                       const pdf::PDFPage* page,
                                                                       const QRectF& sampleRect,
                                                                       pdf::PDFRenderer::Features features,
                                                                       const Settings& settings)
{
    std::vector<Match> result;

    const QSizeF pageSize = page ? page->getRotatedMediaBox().size() : QSizeF();
    const pdf::PDFReal sampleSize = qMax(sampleRect.width(), sampleRect.height());
    if (!page || sampleSize <= 0.0 || pageSize.isEmpty())
    {
        return result;
    }

    // Resolution of the search is chosen so the sample has about 40 pixels,
    // but the image of the whole sheet must not be too large
    constexpr pdf::PDFReal targetSamplePixels = 40.0;
    constexpr pdf::PDFReal maximalPixels = 24.0e6;
    pdf::PDFReal scale = qBound(0.5, targetSamplePixels / sampleSize, 6.0);
    const pdf::PDFReal pixels = pageSize.width() * pageSize.height() * scale * scale;
    if (pixels > maximalPixels)
    {
        scale *= std::sqrt(maximalPixels / pixels);
    }

    const QSize imageSize(qMax(1, qCeil(pageSize.width() * scale)), qMax(1, qCeil(pageSize.height() * scale)));
    const QTransform pagePointToImagePoint = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), imageSize));
    const QRect sampleImageRect = pagePointToImagePoint.mapRect(sampleRect).toAlignedRect();

    // Draws the area of the page in the image enlarged by the scale
    auto drawPage = [&](const pdf::PDFPrecompiledPage& pageToDraw, const QRect& area, double areaScale)
    {
        QImage image(area.size(), QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        const QTransform matrix = pagePointToImagePoint * QTransform::fromScale(areaScale, areaScale) * QTransform::fromTranslate(-area.left(), -area.top());
        pageToDraw.draw(&painter, page->getCropBox(), matrix, features, 1.0);
        return image;
    };

    // Amount of the ink (darkness) of the sample drawn from the page
    auto getSampleInk = [&](const pdf::PDFPrecompiledPage& candidatePage)
    {
        const QImage sampleImage = drawPage(candidatePage, sampleImageRect, 1.0).convertToFormat(QImage::Format_Grayscale8);
        return FixtureSymbolFinderHelper::getInk(sampleImage, sampleImage.rect());
    };

    // Photos (aerial images of site plans), scans and text (light levels, labels,
    // title block) are not drawn, so they do not disturb the search. The sample
    // symbol itself can be a part of an image (scanned plan) or of the text
    // (glyph of a symbol font): images are removed only if something of the sample
    // stays visible, text only if most of the sample stays visible.
    pdf::PDFPrecompiledPage vectorPage = compiledPage;
    vectorPage.removeImages();
    pdf::PDFPrecompiledPage reducedPage = vectorPage;
    reducedPage.removeText();

    constexpr double minimalInk = 255.0;
    constexpr double minimalInkWithoutText = 0.6;
    const double vectorInk = getSampleInk(vectorPage);

    const pdf::PDFPrecompiledPage* pageToSearch = &compiledPage;
    if (vectorInk >= minimalInk)
    {
        pageToSearch = getSampleInk(reducedPage) >= minimalInkWithoutText * vectorInk ? &reducedPage : &vectorPage;
    }

    // Lines and curves of the drawing larger than the symbol (isolines, curbs,
    // property lines, outlines of buildings) are not drawn, so a symbol crossed
    // or touched by such a line looks the same as an isolated symbol, and the
    // sample does not contain the lines around it. Some drawings consist of short
    // segments only, then nothing is removed. If the sample itself is a part of
    // a large path, the paths are kept.
    pdf::PDFPrecompiledPage smallPathsPage;
    if (settings.removeLargePaths && vectorInk >= minimalInk)
    {
        smallPathsPage = *pageToSearch;
        smallPathsPage.removeLargePaths(LARGE_PATH_FACTOR * sampleSize);
        if (getSampleInk(smallPathsPage) >= MINIMAL_INK_OF_SMALL_PATHS * getSampleInk(*pageToSearch))
        {
            pageToSearch = &smallPathsPage;
        }
    }

    // Drawing of other colors (calculation points and isolines of photometric
    // plans, gray background layers) is not drawn. Antialiased edges of black
    // ink are gray, so a gray drawing can't be separated from the black ink
    // in the image, only by the colors of the paths.
    FixtureSymbolFinderHelper::InkColor inkColor = FixtureSymbolFinderHelper::getInkColor(drawPage(*pageToSearch, sampleImageRect, 1.0), sampleImageRect.translated(-sampleImageRect.topLeft()));
    pdf::PDFPrecompiledPage inkColorPage;
    if (inkColor.isValid && vectorInk >= minimalInk)
    {
        inkColorPage = *pageToSearch;
        inkColorPage.removePathsOfOtherColors(QColor(inkColor.red, inkColor.green, inkColor.blue), INK_COLOR_TOLERANCE);
        if (getSampleInk(inkColorPage) >= MINIMAL_INK_OF_INK_COLOR * getSampleInk(*pageToSearch))
        {
            pageToSearch = &inkColorPage;
        }
    }

    const QImage image = drawPage(*pageToSearch, QRect(QPoint(0, 0), imageSize), 1.0);
    const QImage grayImage = FixtureSymbolFinderHelper::applyInkColor(image, inkColor);

    QRect inkRect = getInkBoundingRect(grayImage, sampleImageRect);
    if (inkRect.isEmpty() || inkRect.width() < 3 || inkRect.height() < 3)
    {
        return result;
    }
    inkRect = inkRect.adjusted(-1, -1, 1, 1).intersected(grayImage.rect());

    const FixtureSymbolFinderHelper::CoarseResult coarse = FixtureSymbolFinderHelper::searchCoarse(grayImage, inkRect, settings);

    // Candidates are confirmed in the areas of the page drawn at the resolution,
    // at which the sample has the requested size
    const double fineScale = qBound(0.125, double(qMax(settings.fineSampleSize, 8)) / qMax(inkRect.width(), inkRect.height()), 16.0);
    auto renderer = [&](const QRect& area) { return FixtureSymbolFinderHelper::applyInkColor(drawPage(*pageToSearch, area, fineScale), inkColor); };

    const QRect fineSampleArea = QRectF(QPointF(sampleImageRect.topLeft()) * fineScale, QPointF(sampleImageRect.bottomRight() + QPoint(1, 1)) * fineScale).toAlignedRect();
    const QImage fineSampleImage = renderer(fineSampleArea);
    QRect fineInkRect = getInkBoundingRect(fineSampleImage, fineSampleImage.rect());
    if (fineInkRect.isEmpty() || fineInkRect.width() < 3 || fineInkRect.height() < 3)
    {
        return result;
    }
    fineInkRect = fineInkRect.adjusted(-1, -1, 1, 1);
    const QImage fineSample = FixtureSymbolFinderHelper::blur(FixtureSymbolFinderHelper::copyArea(fineSampleImage, fineInkRect.adjusted(-1, -1, 1, 1))).copy(1, 1, fineInkRect.width(), fineInkRect.height());

    std::vector<FixtureSymbolFinderHelper::Candidate> confirmed = FixtureSymbolFinderHelper::confirm(fineSample, fineScale, coarse, renderer, settings);

    const double suppressDistance = 0.5 * std::min(inkRect.width(), inkRect.height());
    confirmed = FixtureSymbolFinderHelper::suppressOverlapping(std::move(confirmed), inkRect.size(), std::max(suppressDistance, 1.0));

    const QTransform imagePointToPagePoint = pagePointToImagePoint.inverted();
    for (const FixtureSymbolFinderHelper::Candidate& candidate : confirmed)
    {
        result.push_back({ imagePointToPagePoint.map(candidate.center), candidate.score, candidate.angle, candidate.scale, candidate.mirrored });
    }

    return result;
}

}   // namespace pdfplugin
