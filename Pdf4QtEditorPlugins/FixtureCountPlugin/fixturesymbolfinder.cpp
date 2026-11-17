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

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
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

    /// Sample rotated by the angle. Pixels are stored as offsets from the center
    /// of the sample, only the pixels inside the rotated sample are used, so
    /// the neighbourhood of the symbol does not affect the correlation.
    struct RotatedTemplate
    {
        double angle = 0.0;
        int radius = 0;
        std::vector<int> offsetsX;
        std::vector<int> offsetsY;
        std::vector<float> values;
        double norm = 0.0;

        bool isValid() const { return !values.empty() && norm > 1e-6; }
    };

    struct Candidate
    {
        /// Center in the image coordinates (pixel edges)
        QPointF center;
        double score = 0.0;
        double angle = 0.0;
    };

    /// Creates the image, the integral images are computed only when requested
    static Image createImage(const QImage& image, bool integralImages);
    static RotatedTemplate createTemplate(const QImage& sample, double angle);

    /// Returns the angles of the full circle with the step not larger than the given step
    static std::vector<double> getAngles(double step);

    /// Returns the angle step, by which the farthest pixel of the sample moves
    /// by the given number of pixels
    static double getAngleStep(const QSize& sampleSize, double pixels);

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

    /// Finds the local maxima of the correlation above the threshold
    /// \param image Image
    /// \param templates Templates (sample rotated by all angles)
    /// \param minimalInk Windows with less ink are skipped
    /// \param threshold Threshold
    static std::vector<Candidate> findCandidates(const Image& image, const std::vector<RotatedTemplate>& templates, double minimalInk, double threshold);

    /// Keeps only the best candidate from the candidates closer than the distance
    static std::vector<Candidate> suppressNonMaxima(std::vector<Candidate> candidates, double distance);

    /// Blurs the grayscale image by a 3x3 box filter. Thin lines of the drawing
    /// are then less sensitive to the sub-pixel position and angle of the symbol.
    static QImage blur(const QImage& image);

    /// Adds a white margin around the sample. The symbol is expected to be isolated,
    /// so parts of larger shapes (ends of thick lines, filled rectangles), which
    /// look like the symbol only inside its bounding rectangle, are not found.
    static QImage addMargin(const QImage& sample, int margin);

    /// Returns the number of the first templates, which are enough for the search,
    /// because the sample is symmetric: a sample, which looks the same when it is
    /// rotated by some angle (a dot or a circle by any angle, a square by 90 degrees,
    /// a rectangle by 180 degrees), is searched only by the angles below that angle.
    /// \param sample Sample
    /// \param templates Templates of the sample rotated by the angles of the full circle
    static size_t getDistinctTemplateCount(const QImage& sample, const std::vector<RotatedTemplate>& templates);

    /// Returns the amount of the ink (sum of 255 - value) in the rectangle
    static double getInk(const QImage& image, const QRect& rect);

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

FixtureSymbolFinderHelper::RotatedTemplate FixtureSymbolFinderHelper::createTemplate(const QImage& sample, double angle)
{
    RotatedTemplate result;
    result.angle = angle;

    const int width = sample.width();
    const int height = sample.height();
    if (width <= 0 || height <= 0)
    {
        return result;
    }

    const double centerX = (width - 1) * 0.5;
    const double centerY = (height - 1) * 0.5;
    const double radians = qDegreesToRadians(angle);
    const double cosAngle = std::cos(radians);
    const double sinAngle = std::sin(radians);
    result.radius = qCeil(std::hypot(width, height) * 0.5);

    auto getPixel = [&sample](int x, int y) { return double(sample.constScanLine(y)[x]); };

    double sum = 0.0;
    for (int dy = -result.radius; dy <= result.radius; ++dy)
    {
        for (int dx = -result.radius; dx <= result.radius; ++dx)
        {
            // Point of the sample, which is rotated onto this pixel
            const double sx = cosAngle * dx + sinAngle * dy + centerX;
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

            result.offsetsX.push_back(dx);
            result.offsetsY.push_back(dy);
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
    return result;
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
    std::vector<int> bestAngles(size_t(width) * height, 0);
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
            int bestAngle = 0;
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
                        bestAngle = int(i);
                    }
                }

                if (bestScore < previewThreshold)
                {
                    break;
                }
            }

            scores[size_t(y) * width + x] = bestScore;
            bestAngles[size_t(y) * width + x] = bestAngle;
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
                candidates.push_back({ QPointF(x + 0.5, y + 0.5), score, templates[bestAngles[size_t(y) * width + x]].angle });
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

size_t FixtureSymbolFinderHelper::getDistinctTemplateCount(const QImage& sample, const std::vector<RotatedTemplate>& templates)
{
    if (templates.size() < 2)
    {
        return templates.size();
    }

    // The sample in the middle of a white image, so every rotated template fits
    const int radius = templates.front().radius;
    const int margin = radius + 1;
    const Image image = createImage(addMargin(sample, margin), false);
    const int x = margin + (sample.width() - 1) / 2;
    const int y = margin + (sample.height() - 1) / 2;

    std::vector<bool> isSame(templates.size(), true);
    for (size_t i = 1; i < templates.size(); ++i)
    {
        double score = 0.0;
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                score = std::max(score, correlate(image, templates[i], x + dx, y + dy, std::numeric_limits<double>::max()));
            }
        }
        isSame[i] = score >= SYMMETRY_THRESHOLD;
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
    const QRect rect = sampleRect.normalized().intersected(colorImage.rect());
    if (rect.isEmpty())
    {
        return image.convertToFormat(QImage::Format_Grayscale8);
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

    const int inkRed = getMedian(reds);
    const int inkGreen = getMedian(greens);
    const int inkBlue = getMedian(blues);
    const int range = std::max({ 255 - inkRed, 255 - inkGreen, 255 - inkBlue });
    if (range < 32)
    {
        // There is no ink in the sample
        return image.convertToFormat(QImage::Format_Grayscale8);
    }

    // Distance from the color of the ink (the largest difference of the channels),
    // white paper and the ink of other colors have the largest distance. Colors
    // far from the ink (also light colored backgrounds) are white.
    const int whiteDistance = qMax(1, range * 7 / 10);
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
            const int distance = std::max({ std::abs(qRed(pixel) - inkRed), std::abs(qGreen(pixel) - inkGreen), std::abs(qBlue(pixel) - inkBlue) });
            target[x] = uchar(std::min(255, distance * 255 / whiteDistance));
        }
    });

    return result;
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

    const int sampleSize = std::max(inkRect.width(), inkRect.height());
    const double scale = std::min(1.0, double(std::max(settings.coarseSampleSize, 6)) / sampleSize);

    // Coarse search on the reduced and blurred image, the blur makes the
    // correlation tolerant to the step of the angle
    const QImage coarseImage = FixtureSymbolFinderHelper::blur(scale < 1.0 ? grayImage.scaled(qMax(1, qRound(grayImage.width() * scale)), qMax(1, qRound(grayImage.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                                                           : grayImage);
    const QRect coarseInkRect = QRect(qFloor(inkRect.left() * scale), qFloor(inkRect.top() * scale),
                                      qMax(3, qCeil(inkRect.width() * scale)), qMax(3, qCeil(inkRect.height() * scale))).intersected(coarseImage.rect());
    const QImage coarseSample = FixtureSymbolFinderHelper::addMargin(coarseImage.copy(coarseInkRect), 1);

    const double coarseAngleStep = settings.rotations ? qBound(2.0, FixtureSymbolFinderHelper::getAngleStep(coarseSample.size(), 1.5), 30.0) : 360.0;
    std::vector<FixtureSymbolFinderHelper::RotatedTemplate> coarseTemplates;
    for (double angle : FixtureSymbolFinderHelper::getAngles(coarseAngleStep))
    {
        coarseTemplates.push_back(FixtureSymbolFinderHelper::createTemplate(coarseSample, angle));
        if (!coarseTemplates.back().isValid())
        {
            return matches;
        }
    }

    coarseTemplates.resize(FixtureSymbolFinderHelper::getDistinctTemplateCount(coarseSample, coarseTemplates));

    // Reduced image is blurred, so the threshold of the coarse search is lower,
    // the candidates are confirmed on the full resolution image. Parts of the
    // symbol can be covered by other lines, so the ink is only roughly bounded.
    const double coarseThreshold = std::max(0.3, settings.threshold - 0.25);
    const double minimalInk = 0.5 * FixtureSymbolFinderHelper::getInk(coarseImage, coarseInkRect);
    const FixtureSymbolFinderHelper::Image coarseIntegralImage = FixtureSymbolFinderHelper::createImage(coarseImage, true);
    std::vector<FixtureSymbolFinderHelper::Candidate> candidates = FixtureSymbolFinderHelper::findCandidates(coarseIntegralImage, coarseTemplates, minimalInk, coarseThreshold);

    const double coarseSuppressDistance = 0.5 * std::min(coarseInkRect.width(), coarseInkRect.height());
    candidates = FixtureSymbolFinderHelper::suppressNonMaxima(std::move(candidates), std::max(coarseSuppressDistance, 1.0));

    // Fine search around the candidates in the full resolution image,
    // the position and the angle are refined
    const QImage blurredImage = FixtureSymbolFinderHelper::blur(grayImage);
    const FixtureSymbolFinderHelper::Image fineImage = FixtureSymbolFinderHelper::createImage(blurredImage, false);
    const int margin = qMax(2, qRound(0.15 * sampleSize));
    const QImage sample = FixtureSymbolFinderHelper::addMargin(blurredImage.copy(inkRect), margin);

    const double fineAngleStep = settings.rotations ? qBound(0.5, FixtureSymbolFinderHelper::getAngleStep(sample.size(), 1.0), coarseAngleStep) : 360.0;
    const std::vector<double> fineAngles = FixtureSymbolFinderHelper::getAngles(fineAngleStep);
    std::vector<FixtureSymbolFinderHelper::RotatedTemplate> templates(fineAngles.size());
    std::vector<size_t> angleIndices(fineAngles.size());
    std::iota(angleIndices.begin(), angleIndices.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, angleIndices.cbegin(), angleIndices.cend(), [&](size_t index)
    {
        templates[index] = FixtureSymbolFinderHelper::createTemplate(sample, fineAngles[index]);
    });

    const double realFineAngleStep = 360.0 / fineAngles.size();
    const int angleRange = settings.rotations ? qCeil(coarseAngleStep / realFineAngleStep) : 0;
    const int searchRadius = qCeil(1.0 / scale) + 1;
    std::vector<FixtureSymbolFinderHelper::Candidate> confirmed(candidates.size());

    std::vector<size_t> indices(candidates.size());
    std::iota(indices.begin(), indices.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, indices.cbegin(), indices.cend(), [&](size_t index)
    {
        const FixtureSymbolFinderHelper::Candidate& candidate = candidates[index];
        const QPointF center = candidate.center / scale;
        const int centerX = qFloor(center.x());
        const int centerY = qFloor(center.y());
        const int angleIndex = qRound(candidate.angle / realFineAngleStep);

        FixtureSymbolFinderHelper::Candidate best;
        best.score = -1.0;

        for (int a = angleIndex - angleRange; a <= angleIndex + angleRange; ++a)
        {
            const int count = int(templates.size());
            const FixtureSymbolFinderHelper::RotatedTemplate& tmpl = templates[((a % count) + count) % count];
            for (int y = centerY - searchRadius; y <= centerY + searchRadius; ++y)
            {
                for (int x = centerX - searchRadius; x <= centerX + searchRadius; ++x)
                {
                    const double score = FixtureSymbolFinderHelper::correlate(fineImage, tmpl, x, y, FixtureSymbolFinderHelper::FINE_CONTRAST_TOLERANCE);
                    if (score > best.score)
                    {
                        best.score = score;
                        best.center = QPointF(x + 0.5, y + 0.5);
                        best.angle = tmpl.angle;
                    }
                }
            }
        }

        confirmed[index] = best;
    });

    confirmed.erase(std::remove_if(confirmed.begin(), confirmed.end(), [&](const FixtureSymbolFinderHelper::Candidate& candidate) { return candidate.score < settings.threshold; }), confirmed.end());

    const double suppressDistance = 0.5 * std::min(inkRect.width(), inkRect.height());
    confirmed = FixtureSymbolFinderHelper::suppressNonMaxima(std::move(confirmed), std::max(suppressDistance, 1.0));

    for (const FixtureSymbolFinderHelper::Candidate& candidate : confirmed)
    {
        matches.push_back({ candidate.center, candidate.score, candidate.angle });
    }

    return matches;
}

}   // namespace pdfplugin
