#include "fixturesymbolfinder.h"

#include "pdfexecutionpolicy.h"

#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pdfplugin
{

/// Implementation of the normalized cross correlation used by the symbol finder
class FixtureSymbolFinderHelper
{
public:
    FixtureSymbolFinderHelper() = delete;

    /// Grayscale image with the integral images of the values and of the squares
    /// of the values, so the mean and the variance of any window are computed
    /// in constant time
    struct IntegralImage
    {
        int width = 0;
        int height = 0;
        std::vector<float> values;
        std::vector<double> sum;
        std::vector<double> sumOfSquares;

        double getSum(const std::vector<double>& table, int x, int y, int w, int h) const
        {
            const int stride = width + 1;
            return table[(y + h) * stride + (x + w)] - table[y * stride + (x + w)] - table[(y + h) * stride + x] + table[y * stride + x];
        }
    };

    /// Template prepared for the correlation (values minus mean)
    struct Template
    {
        int width = 0;
        int height = 0;
        std::vector<float> values;
        double norm = 0.0;

        bool isValid() const { return width > 0 && height > 0 && norm > 1e-6; }
    };

    struct Candidate
    {
        QPointF center;
        double score = 0.0;
    };

    static IntegralImage createIntegralImage(const QImage& image);
    static Template createTemplate(const QImage& image);

    /// Returns the normalized cross correlation of the template placed
    /// with its top left corner at the given position
    static double correlate(const IntegralImage& image, const Template& tmpl, int x, int y);

    /// Finds the local maxima of the correlation above the threshold
    static std::vector<Candidate> findCandidates(const IntegralImage& image, const Template& tmpl, double threshold);

    /// Keeps only the best candidate from the candidates closer than the distance
    static std::vector<Candidate> suppressNonMaxima(std::vector<Candidate> candidates, double distance);

    /// Blurs the grayscale image by a 3x3 box filter. Thin lines of the drawing
    /// are then less sensitive to the sub-pixel position of the symbol.
    static QImage blur(const QImage& image);

    /// Minimal standard deviation of the window, windows with lower deviation
    /// (blank paper) are skipped
    static constexpr double MIN_DEVIATION = 2.0;
};

FixtureSymbolFinderHelper::IntegralImage FixtureSymbolFinderHelper::createIntegralImage(const QImage& image)
{
    IntegralImage result;
    result.width = image.width();
    result.height = image.height();
    result.values.resize(size_t(result.width) * result.height);
    result.sum.assign(size_t(result.width + 1) * (result.height + 1), 0.0);
    result.sumOfSquares.assign(size_t(result.width + 1) * (result.height + 1), 0.0);

    const int stride = result.width + 1;
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

FixtureSymbolFinderHelper::Template FixtureSymbolFinderHelper::createTemplate(const QImage& image)
{
    Template result;
    result.width = image.width();
    result.height = image.height();
    result.values.resize(size_t(result.width) * result.height);

    double sum = 0.0;
    for (int y = 0; y < result.height; ++y)
    {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < result.width; ++x)
        {
            result.values[size_t(y) * result.width + x] = line[x];
            sum += line[x];
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

double FixtureSymbolFinderHelper::correlate(const IntegralImage& image, const Template& tmpl, int x, int y)
{
    if (x < 0 || y < 0 || x + tmpl.width > image.width || y + tmpl.height > image.height)
    {
        return -1.0;
    }

    const double n = double(tmpl.width) * tmpl.height;
    const double sum = image.getSum(image.sum, x, y, tmpl.width, tmpl.height);
    const double sumOfSquares = image.getSum(image.sumOfSquares, x, y, tmpl.width, tmpl.height);
    const double variance = std::max(sumOfSquares - sum * sum / n, 0.0);

    if (variance < MIN_DEVIATION * MIN_DEVIATION * n)
    {
        return 0.0;
    }

    double numerator = 0.0;
    for (int j = 0; j < tmpl.height; ++j)
    {
        const float* imageLine = image.values.data() + size_t(y + j) * image.width + x;
        const float* templateLine = tmpl.values.data() + size_t(j) * tmpl.width;
        float lineSum = 0.0f;
        for (int i = 0; i < tmpl.width; ++i)
        {
            lineSum += imageLine[i] * templateLine[i];
        }
        numerator += lineSum;
    }

    return numerator / (tmpl.norm * std::sqrt(variance));
}

std::vector<FixtureSymbolFinderHelper::Candidate> FixtureSymbolFinderHelper::findCandidates(const IntegralImage& image, const Template& tmpl, double threshold)
{
    std::vector<Candidate> candidates;

    const int width = image.width - tmpl.width + 1;
    const int height = image.height - tmpl.height + 1;
    if (width <= 0 || height <= 0)
    {
        return candidates;
    }

    std::vector<float> scores(size_t(width) * height, 0.0f);
    std::vector<int> rows(height);
    std::iota(rows.begin(), rows.end(), 0);

    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, rows.cbegin(), rows.cend(), [&](int y)
    {
        for (int x = 0; x < width; ++x)
        {
            scores[size_t(y) * width + x] = float(correlate(image, tmpl, x, y));
        }
    });

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
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
                    const int nx = x + i;
                    const int ny = y + j;
                    if ((i != 0 || j != 0) && nx >= 0 && ny >= 0 && nx < width && ny < height)
                    {
                        const float neighbourScore = scores[size_t(ny) * width + nx];

                        // Plateaus are resolved by the order of the pixels
                        if (neighbourScore > score || (neighbourScore == score && (j < 0 || (j == 0 && i < 0))))
                        {
                            isMaximum = false;
                            break;
                        }
                    }
                }
            }

            if (isMaximum)
            {
                candidates.push_back({ QPointF(x + tmpl.width * 0.5, y + tmpl.height * 0.5), score });
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

    for (int y = 0; y < height; ++y)
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
    }

    return result;
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
    const double scale = std::min(1.0, double(std::max(settings.coarseSampleSize, 4)) / sampleSize);

    std::vector<QTransform> rotations = { QTransform() };
    if (settings.rotations)
    {
        rotations.push_back(QTransform().rotate(90));
        rotations.push_back(QTransform().rotate(180));
        rotations.push_back(QTransform().rotate(270));
    }

    // Coarse search
    const QImage coarseImage = scale < 1.0 ? grayImage.scaled(qMax(1, qRound(grayImage.width() * scale)), qMax(1, qRound(grayImage.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                           : grayImage;
    const QRect coarseInkRect = QRect(qFloor(inkRect.left() * scale), qFloor(inkRect.top() * scale),
                                      qMax(2, qCeil(inkRect.width() * scale)), qMax(2, qCeil(inkRect.height() * scale))).intersected(coarseImage.rect());
    const QImage coarseSample = coarseImage.copy(coarseInkRect);
    const FixtureSymbolFinderHelper::IntegralImage coarseIntegralImage = FixtureSymbolFinderHelper::createIntegralImage(coarseImage);

    // Reduced image is blurred, so the threshold of the coarse search is lower,
    // the candidates are confirmed on the full resolution image
    const double coarseThreshold = std::max(0.3, settings.threshold - 0.25);

    std::vector<FixtureSymbolFinderHelper::Candidate> candidates;
    for (const QTransform& rotation : rotations)
    {
        const FixtureSymbolFinderHelper::Template coarseTemplate = FixtureSymbolFinderHelper::createTemplate(coarseSample.transformed(rotation));
        if (!coarseTemplate.isValid())
        {
            return matches;
        }

        std::vector<FixtureSymbolFinderHelper::Candidate> rotationCandidates = FixtureSymbolFinderHelper::findCandidates(coarseIntegralImage, coarseTemplate, coarseThreshold);
        candidates.insert(candidates.end(), rotationCandidates.begin(), rotationCandidates.end());
    }

    const double coarseSuppressDistance = 0.5 * std::min(coarseInkRect.width(), coarseInkRect.height());
    candidates = FixtureSymbolFinderHelper::suppressNonMaxima(std::move(candidates), std::max(coarseSuppressDistance, 1.0));

    // Fine search around the candidates in the full resolution image
    const QImage blurredImage = FixtureSymbolFinderHelper::blur(grayImage);
    const FixtureSymbolFinderHelper::IntegralImage integralImage = FixtureSymbolFinderHelper::createIntegralImage(blurredImage);
    const QImage sample = blurredImage.copy(inkRect);
    std::vector<FixtureSymbolFinderHelper::Template> templates;
    for (const QTransform& rotation : rotations)
    {
        templates.push_back(FixtureSymbolFinderHelper::createTemplate(sample.transformed(rotation)));
    }

    const int searchRadius = qCeil(1.0 / scale) + 1;
    std::vector<FixtureSymbolFinderHelper::Candidate> confirmed(candidates.size());

    std::vector<size_t> indices(candidates.size());
    std::iota(indices.begin(), indices.end(), 0);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Unknown, indices.cbegin(), indices.cend(), [&](size_t index)
    {
        const QPointF center = candidates[index].center / scale;
        FixtureSymbolFinderHelper::Candidate best;
        best.score = -1.0;

        for (const FixtureSymbolFinderHelper::Template& tmpl : templates)
        {
            const int x0 = qRound(center.x() - tmpl.width * 0.5);
            const int y0 = qRound(center.y() - tmpl.height * 0.5);
            for (int y = y0 - searchRadius; y <= y0 + searchRadius; ++y)
            {
                for (int x = x0 - searchRadius; x <= x0 + searchRadius; ++x)
                {
                    const double score = FixtureSymbolFinderHelper::correlate(integralImage, tmpl, x, y);
                    if (score > best.score)
                    {
                        best.score = score;
                        best.center = QPointF(x + tmpl.width * 0.5, y + tmpl.height * 0.5);
                    }
                }
            }
        }

        confirmed[index] = best;
    });

    confirmed.erase(std::remove_if(confirmed.begin(), confirmed.end(), [&settings](const auto& candidate) { return candidate.score < settings.threshold; }), confirmed.end());
    confirmed = FixtureSymbolFinderHelper::suppressNonMaxima(std::move(confirmed), 0.5 * std::min(inkRect.width(), inkRect.height()));

    for (const FixtureSymbolFinderHelper::Candidate& candidate : confirmed)
    {
        matches.push_back({ candidate.center, candidate.score });
    }

    return matches;
}

}   // namespace pdfplugin
