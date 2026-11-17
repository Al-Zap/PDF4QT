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

#include "pdfsnapper.h"
#include "pdfutils.h"
#include "pdfpainter.h"
#include "pdfwidgetsnapshot.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

void PDFSnapInfo::addPageMediaBox(const QRectF& mediaBox)
{
    QPointF tl = mediaBox.topLeft();
    QPointF tr = mediaBox.topRight();
    QPointF bl = mediaBox.bottomLeft();
    QPointF br = mediaBox.bottomRight();
    QPointF center = mediaBox.center();

    m_snapPoints.insert(m_snapPoints.cend(), {
                            SnapPoint(SnapType::PageCorner, tl ),
                            SnapPoint(SnapType::PageCorner, tr ),
                            SnapPoint(SnapType::PageCorner, bl ),
                            SnapPoint(SnapType::PageCorner, br ),
                            SnapPoint(SnapType::PageCenter, center)
                        });

    addLine(tl, tr);
    addLine(tr, br);
    addLine(br, bl);
    addLine(tl, bl);
}

void PDFSnapInfo::addImage(const std::array<QPointF, 5>& points, const QImage& image)
{
    m_snapPoints.insert(m_snapPoints.cend(), {
                            SnapPoint(SnapType::ImageCorner, points[0]),
                            SnapPoint(SnapType::ImageCorner, points[1]),
                            SnapPoint(SnapType::ImageCorner, points[2]),
                            SnapPoint(SnapType::ImageCorner, points[3]),
                            SnapPoint(SnapType::ImageCenter, points[4])
                        });

    for (size_t i = 0; i < 4; ++i)
    {
        addLine(points[i], points[(i + 1) % 4]);
    }

    SnapImage snapImage;
    snapImage.imagePath.moveTo(points[0]);
    snapImage.imagePath.lineTo(points[1]);
    snapImage.imagePath.lineTo(points[2]);
    snapImage.imagePath.lineTo(points[3]);
    snapImage.imagePath.lineTo(points[0]);
    snapImage.image = image;
    m_snapImages.emplace_back(qMove(snapImage));
}

void PDFSnapInfo::addLine(const QPointF& start, const QPointF& end)
{
    QLineF line(start, end);
    m_snapPoints.emplace_back(SnapType::LineCenter, line.center());
    m_snapLines.emplace_back(line);
}

void PDFSnapInfo::addAnnotationPoint(const QPointF& point)
{
    m_snapPoints.emplace_back(SnapType::Annotation, point);
}

void PDFSnapInfo::addAnnotationRectangle(const QRectF& rectangle)
{
    if (!rectangle.isValid())
    {
        return;
    }

    const QPointF topLeft = rectangle.topLeft();
    const QPointF topRight = rectangle.topRight();
    const QPointF bottomRight = rectangle.bottomRight();
    const QPointF bottomLeft = rectangle.bottomLeft();

    addAnnotationPoint(rectangle.center());

    addAnnotationLine(topLeft, topRight);
    addAnnotationLine(topRight, bottomRight);
    addAnnotationLine(bottomRight, bottomLeft);
    addAnnotationLine(bottomLeft, topLeft);
}

void PDFSnapInfo::addAnnotationLine(const QPointF& start, const QPointF& end)
{
    m_snapPoints.emplace_back(SnapType::Annotation, start);
    m_snapPoints.emplace_back(SnapType::Annotation, end);
    m_snapPoints.emplace_back(SnapType::Annotation, QLineF(start, end).center());
    m_snapLines.emplace_back(start, end);
}

PDFSnapper::PDFSnapper()
{

}

void PDFSnapper::drawSnapPoints(QPainter* painter) const
{
    Q_ASSERT(painter);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen pen = painter->pen();
    pen.setCapStyle(Qt::RoundCap);
    pen.setWidth(m_snapPointPixelSize);

    for (const ViewportSnapPoint& snapPoint : m_snapPoints)
    {
        if (!isSnappingAllowed(snapPoint.pageIndex))
        {
            // We are drawing only snap points, which are on current page
            continue;
        }

        QColor color = pen.color();
        QColor newColor = color;
        switch (snapPoint.type)
        {
            case SnapType::PageCorner:
                newColor = Qt::blue;
                break;

            case SnapType::GeneratedLineProjection:
                newColor = Qt::green;
                break;

            case SnapType::Annotation:
                newColor = Qt::magenta;
                break;

            case SnapType::Custom:
                newColor = Qt::black;
                break;

            default:
                newColor = Qt::red;
                break;
        }

        if (color != newColor)
        {
            pen.setColor(newColor);
            painter->setPen(pen);
        }

        QPoint point = snapPoint.viewportPoint.toPoint();
        painter->drawPoint(point);
    }

    if (isSnapped() && getSnappedPointType() >= SnapType::GeometryEndpoint)
    {
        drawGeometryMarker(painter);
    }
    else if (isSnapped())
    {
        pen.setColor(Qt::yellow);
        painter->setPen(pen);
        QPoint point = m_snappedPoint->viewportPoint.toPoint();
        painter->drawPoint(point);
    }

    painter->restore();
}

bool PDFSnapper::isSnappingAllowed(PDFInteger pageIndex) const
{
    return (m_currentPage == -1) || (m_currentPage == pageIndex);
}

void PDFSnapper::updateSnappedPoint(const QPointF& mousePoint)
{
    m_snappedPoint = std::nullopt;
    m_snappedImage = std::nullopt;
    m_mousePoint = mousePoint;

    // Iterate trough all points, check, if some satisfies condition
    const PDFReal toleranceSquared = m_snapPointTolerance * m_snapPointTolerance;
    for (const ViewportSnapPoint& snapPoint : m_snapPoints)
    {
        QPointF difference = mousePoint - snapPoint.viewportPoint;
        PDFReal distanceSquared = QPointF::dotProduct(difference, difference);
        if (distanceSquared < toleranceSquared && isSnappingAllowed(snapPoint.pageIndex))
        {
            m_snappedPoint = snapPoint;
            break;
        }
    }

    // The points of interest have a priority over the geometry of the drawing,
    // so for example the first point of a polygon can always be picked to close it
    if (!m_snappedPoint && m_geometrySnapping && !m_geometrySnappingSuppressed)
    {
        updateSnappedGeometryPoint(mousePoint);
    }

    // Iterate trough all images, check, if some is under mouse cursor
    for (const ViewportSnapImage& snapImage : m_snapImages)
    {
        if (snapImage.viewportPath.contains(mousePoint))
        {
            m_snappedImage = snapImage;
            break;
        }
    }
}

void PDFSnapper::buildSnapPoints(const PDFWidgetSnapshot& snapshot)
{
    // First, clear all snap points
    m_snapPoints.clear();

    // Second, create snapping points from snapshot
    for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
    {
        if (!item.compiledPage)
        {
            continue;
        }

        addSnapInfoImpl(item.pageIndex, item.pageToDeviceMatrix, *item.compiledPage->getSnapInfo(), true);
    }

    buildGeometryPages(snapshot);

    // Third, update snap shot position
    updateSnappedPoint(m_mousePoint);
}

void PDFSnapper::buildSnapImages(const PDFWidgetSnapshot& snapshot)
{
    // First, clear all snap images
    m_snapImages.clear();

    // Second, create snapping points from snapshot
    for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
    {
        if (!item.compiledPage)
        {
            continue;
        }

        const PDFSnapInfo* info = item.compiledPage->getSnapInfo();
        for (const PDFSnapInfo::SnapImage& snapImage : info->getSnapImages())
        {
            ViewportSnapImage viewportSnapImage;
            viewportSnapImage.image = snapImage.image;
            viewportSnapImage.imagePath = snapImage.imagePath;
            viewportSnapImage.pageIndex = item.pageIndex;
            viewportSnapImage.viewportPath = item.pageToDeviceMatrix.map(snapImage.imagePath);
            m_snapImages.emplace_back(qMove(viewportSnapImage));
        }
    }
}

void PDFSnapper::addSnapInfo(PDFInteger pageIndex, const QTransform& pageToDeviceMatrix, const PDFSnapInfo& info)
{
    addSnapInfoImpl(pageIndex, pageToDeviceMatrix, info, false);
    updateSnappedPoint(m_mousePoint);
}

void PDFSnapper::addSnapInfoImpl(PDFInteger pageIndex, const QTransform& pageToDeviceMatrix, const PDFSnapInfo& info, bool includeCustomSnapPoints)
{
    for (const PDFSnapInfo::SnapPoint& snapPoint : info.getSnapPoints())
    {
        ViewportSnapPoint viewportSnapPoint;
        viewportSnapPoint.type = snapPoint.type;
        viewportSnapPoint.point = snapPoint.point;
        viewportSnapPoint.pageIndex = pageIndex;
        viewportSnapPoint.viewportPoint = pageToDeviceMatrix.map(snapPoint.point);
        m_snapPoints.push_back(qMove(viewportSnapPoint));
    }

    // Add custom snap points
    if (includeCustomSnapPoints && m_currentPage == pageIndex)
    {
        for (const QPointF& customSnapPoint : m_customSnapPoints)
        {
            ViewportSnapPoint viewportSnapPoint;
            viewportSnapPoint.type = SnapType::Custom;
            viewportSnapPoint.point = customSnapPoint;
            viewportSnapPoint.pageIndex = pageIndex;
            viewportSnapPoint.viewportPoint = pageToDeviceMatrix.map(customSnapPoint);
            m_snapPoints.push_back(qMove(viewportSnapPoint));
        }
    }

    // Fill line projections snap points
    if (m_currentPage == pageIndex && m_referencePoint.has_value())
    {
        QPointF referencePoint = *m_referencePoint;
        for (const QLineF& line : info.getLines())
        {
            // Project point onto line.
            const qreal lineLength = line.length();
            if (qFuzzyIsNull(lineLength))
            {
                continue;
            }

            QPointF vector = referencePoint - line.p1();
            QPointF tangentVector = (line.p2() - line.p1()) / lineLength;
            const qreal absoluteParameter = QPointF::dotProduct(vector, tangentVector);
            if (absoluteParameter >= 0 && absoluteParameter <= lineLength)
            {
                QPointF projectedSnapPoint = line.pointAt(absoluteParameter / lineLength);
                const PDFReal tolerance = lineLength * 0.01;
                const PDFReal squaredTolerance = tolerance * tolerance;

                // Test, if projected snap point is not already present in snap points
                auto testSamePoints = [projectedSnapPoint, squaredTolerance](const ViewportSnapPoint& testedSnapPoint)
                {
                    return isFuzzyComparedPointsSame(projectedSnapPoint, testedSnapPoint.point, squaredTolerance);
                };
                if (m_snapPoints.cend() == std::find_if(m_snapPoints.cbegin(), m_snapPoints.cend(), testSamePoints))
                {
                    ViewportSnapPoint viewportSnapPoint;
                    viewportSnapPoint.type = SnapType::GeneratedLineProjection;
                    viewportSnapPoint.point = projectedSnapPoint;
                    viewportSnapPoint.pageIndex = pageIndex;
                    viewportSnapPoint.viewportPoint = pageToDeviceMatrix.map(projectedSnapPoint);
                    m_snapPoints.push_back(qMove(viewportSnapPoint));
                }
            }
        }
    }
}

int PDFSnapper::getSnapPointTolerance() const
{
    return m_snapPointTolerance;
}

void PDFSnapper::setSnapPointTolerance(int snapPointTolerance)
{
    m_snapPointTolerance = snapPointTolerance;
}

QPointF PDFSnapper::getSnappedPoint() const
{
    if (isSnapped())
    {
        return m_snappedPoint->viewportPoint;
    }

    return m_mousePoint;
}

bool PDFSnapper::getSnappedPagePoint(PDFInteger& pageIndex, QPointF& pagePoint) const
{
    if (!m_snappedPoint)
    {
        return false;
    }

    pageIndex = m_snappedPoint->pageIndex;
    pagePoint = m_snappedPoint->point;
    return true;
}

const PDFSnapper::ViewportSnapImage* PDFSnapper::getSnappedImage() const
{
    if (m_snappedImage.has_value())
    {
        return &*m_snappedImage;
    }

    return nullptr;
}

void PDFSnapper::setReferencePoint(PDFInteger pageIndex, QPointF pagePoint)
{
    // Clear custom snap points, if page changes
    if (m_currentPage != pageIndex)
    {
        m_customSnapPoints.clear();
    }

    m_currentPage = pageIndex;
    m_referencePoint = pagePoint;
}

void PDFSnapper::clearReferencePoint()
{
    m_customSnapPoints.clear();
    m_currentPage = -1;
    m_referencePoint = std::nullopt;
}

void PDFSnapper::clear()
{
    clearReferencePoint();

    m_customSnapPoints.clear();
    m_snapPoints.clear();
    m_snapImages.clear();
    m_geometryPages.clear();
    m_snappedPoint = std::nullopt;
    m_snappedImage = std::nullopt;
    m_mousePoint = QPointF();
}

const std::vector<QPointF>& PDFSnapper::getCustomSnapPoints() const
{
    return m_customSnapPoints;
}

void PDFSnapper::setCustomSnapPoints(const std::vector<QPointF>& customSnapPoints)
{
    m_customSnapPoints = customSnapPoints;
}

PDFPageGeometrySnapIndex PDFPageGeometrySnapIndex::create(const PDFPrecompiledPage* compiledPage, size_t maximalSegmentCount)
{
    PDFPageGeometrySnapIndex index;
    index.m_maximalSegmentCount = maximalSegmentCount;

    if (compiledPage)
    {
        compiledPage->enumerateVectorPaths([&index](const QPainterPath& path, const QTransform& matrix) { index.addPath(path, matrix); });
    }

    index.build();
    return index;
}

void PDFPageGeometrySnapIndex::addPath(const QPainterPath& path, const QTransform& matrix)
{
    // Count of the line segments approximating a single cubic curve. Points
    // on a curve are snapped only as the nearest points, so a rough
    // approximation is sufficient.
    constexpr int CURVE_SEGMENTS = 8;

    QPointF currentPoint;
    const int elementCount = path.elementCount();

    for (int i = 0; i < elementCount; ++i)
    {
        const QPainterPath::Element& element = path.elementAt(i);

        switch (element.type)
        {
            case QPainterPath::MoveToElement:
                currentPoint = matrix.map(QPointF(element.x, element.y));
                break;

            case QPainterPath::LineToElement:
            {
                const QPointF point = matrix.map(QPointF(element.x, element.y));
                addSegment(currentPoint, point, true, true, true);
                currentPoint = point;
                break;
            }

            case QPainterPath::CurveToElement:
            {
                if (i + 2 >= elementCount)
                {
                    // Malformed path, curve must have two data elements
                    i = elementCount;
                    break;
                }

                const QPointF p0 = currentPoint;
                const QPointF p1 = matrix.map(QPointF(element.x, element.y));
                const QPointF p2 = matrix.map(QPointF(path.elementAt(i + 1).x, path.elementAt(i + 1).y));
                const QPointF p3 = matrix.map(QPointF(path.elementAt(i + 2).x, path.elementAt(i + 2).y));
                i += 2;

                QPointF previousPoint = p0;
                for (int j = 1; j <= CURVE_SEGMENTS; ++j)
                {
                    const PDFReal t = PDFReal(j) / CURVE_SEGMENTS;
                    const PDFReal u = 1.0 - t;
                    const QPointF point = (j == CURVE_SEGMENTS) ? p3 : u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
                    addSegment(previousPoint, point, false, j == 1, j == CURVE_SEGMENTS);
                    previousPoint = point;
                }

                currentPoint = p3;
                break;
            }

            default:
                break;
        }
    }
}

void PDFPageGeometrySnapIndex::addSegment(QPointF p1, QPointF p2, bool isStraight, bool isP1Endpoint, bool isP2Endpoint)
{
    if (m_segments.size() >= m_maximalSegmentCount)
    {
        m_isTruncated = true;
        return;
    }

    if (!qIsFinite(p1.x()) || !qIsFinite(p1.y()) || !qIsFinite(p2.x()) || !qIsFinite(p2.y()))
    {
        return;
    }

    Segment segment;
    segment.x1 = float(p1.x());
    segment.y1 = float(p1.y());
    segment.x2 = float(p2.x());
    segment.y2 = float(p2.y());
    segment.flags = (isStraight ? Straight : 0) | (isP1Endpoint ? P1Endpoint : 0) | (isP2Endpoint ? P2Endpoint : 0);
    m_segments.push_back(segment);
}

int PDFPageGeometrySnapIndex::getCellColumn(PDFReal x) const
{
    return qBound(0, int((x - m_bounds.left()) / m_cellSize), m_columns - 1);
}

int PDFPageGeometrySnapIndex::getCellRow(PDFReal y) const
{
    return qBound(0, int((y - m_bounds.top()) / m_cellSize), m_rows - 1);
}

template<typename Function>
void PDFPageGeometrySnapIndex::forEachSegmentCell(const Segment& segment, Function function) const
{
    // The segment is walked in steps of a half of the cell, which visits each
    // cell the segment passes through, except for cells, of which only a tiny
    // corner is crossed. The query compensates it by searching one more cell
    // around the queried area.
    const PDFReal dx = segment.x2 - segment.x1;
    const PDFReal dy = segment.y2 - segment.y1;
    const PDFReal length = qSqrt(dx * dx + dy * dy);
    const int steps = qMax(1, int(std::ceil(2.0 * length / m_cellSize)));

    int lastCell = -1;
    for (int i = 0; i <= steps; ++i)
    {
        const PDFReal t = PDFReal(i) / steps;
        const int cell = getCellRow(segment.y1 + t * dy) * m_columns + getCellColumn(segment.x1 + t * dx);

        if (cell != lastCell)
        {
            function(cell);
            lastCell = cell;
        }
    }
}

void PDFPageGeometrySnapIndex::build()
{
    m_cellStart.clear();
    m_cellItems.clear();
    m_columns = 0;
    m_rows = 0;

    if (m_segments.empty())
    {
        return;
    }

    m_segments.shrink_to_fit();

    PDFReal minX = std::numeric_limits<PDFReal>::max();
    PDFReal minY = std::numeric_limits<PDFReal>::max();
    PDFReal maxX = std::numeric_limits<PDFReal>::lowest();
    PDFReal maxY = std::numeric_limits<PDFReal>::lowest();

    for (const Segment& segment : m_segments)
    {
        minX = qMin(minX, PDFReal(qMin(segment.x1, segment.x2)));
        minY = qMin(minY, PDFReal(qMin(segment.y1, segment.y2)));
        maxX = qMax(maxX, PDFReal(qMax(segment.x1, segment.x2)));
        maxY = qMax(maxY, PDFReal(qMax(segment.y1, segment.y2)));
    }

    m_bounds = QRectF(minX, minY, qMax(maxX - minX, 1.0), qMax(maxY - minY, 1.0));

    // A cell holds roughly a few segments on average, the grid is limited,
    // so the memory of the grid itself stays small even for huge drawings
    constexpr int MAXIMAL_GRID_SIZE = 2048;
    const PDFReal targetCellCount = qBound(1.0, PDFReal(m_segments.size()) * 0.5, PDFReal(MAXIMAL_GRID_SIZE) * MAXIMAL_GRID_SIZE);
    m_cellSize = qSqrt(m_bounds.width() * m_bounds.height() / targetCellCount);
    m_cellSize = qMax(m_cellSize, qMax(m_bounds.width(), m_bounds.height()) / MAXIMAL_GRID_SIZE);
    m_columns = qBound(1, int(std::ceil(m_bounds.width() / m_cellSize)), MAXIMAL_GRID_SIZE);
    m_rows = qBound(1, int(std::ceil(m_bounds.height() / m_cellSize)), MAXIMAL_GRID_SIZE);

    // Counting sort of the segments into the cells (compressed sparse rows)
    const size_t cellCount = size_t(m_columns) * size_t(m_rows);
    m_cellStart.assign(cellCount + 1, 0);

    for (const Segment& segment : m_segments)
    {
        forEachSegmentCell(segment, [this](int cell) { ++m_cellStart[cell + 1]; });
    }

    for (size_t i = 1; i <= cellCount; ++i)
    {
        m_cellStart[i] += m_cellStart[i - 1];
    }

    m_cellItems.resize(m_cellStart.back());
    std::vector<uint32_t> fillPosition(m_cellStart.begin(), m_cellStart.end() - 1);

    for (uint32_t segmentIndex = 0; segmentIndex < uint32_t(m_segments.size()); ++segmentIndex)
    {
        forEachSegmentCell(m_segments[segmentIndex], [&, segmentIndex](int cell) { m_cellItems[fillPosition[cell]++] = segmentIndex; });
    }
}

PDFPageGeometrySnapIndex::Result PDFPageGeometrySnapIndex::find(QPointF point, PDFReal tolerance) const
{
    Result result;

    if (m_segments.empty() || m_cellStart.empty() || tolerance <= 0.0)
    {
        return result;
    }

    const QRectF queryRect(point.x() - tolerance, point.y() - tolerance, 2.0 * tolerance, 2.0 * tolerance);
    if (!queryRect.intersects(m_bounds.adjusted(-m_cellSize, -m_cellSize, m_cellSize, m_cellSize)))
    {
        return result;
    }

    // One more cell around, see forEachSegmentCell
    const int column1 = qMax(0, getCellColumn(queryRect.left()) - 1);
    const int column2 = qMin(m_columns - 1, getCellColumn(queryRect.right()) + 1);
    const int row1 = qMax(0, getCellRow(queryRect.top()) - 1);
    const int row2 = qMin(m_rows - 1, getCellRow(queryRect.bottom()) + 1);

    std::vector<uint32_t> candidates;
    for (int row = row1; row <= row2; ++row)
    {
        for (int column = column1; column <= column2; ++column)
        {
            const size_t cell = size_t(row) * size_t(m_columns) + size_t(column);
            candidates.insert(candidates.end(), m_cellItems.begin() + m_cellStart[cell], m_cellItems.begin() + m_cellStart[cell + 1]);
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // Points of the same kind are compared by a score - the distance from the
    // queried point, increased by the distance of the segment, to which the point
    // belongs. On a dense drawing (hatching, contours) the end of the line under
    // the cursor is then preferred to a slightly nearer end of a neighbouring line.
    PDFReal bestScore = std::numeric_limits<PDFReal>::max();
    auto updateResult = [&result, &bestScore, tolerance](Kind kind, QPointF snappedPoint, PDFReal distance, PDFReal score)
    {
        if (distance > tolerance)
        {
            return;
        }

        if (kind < result.kind || (kind == result.kind && score < bestScore))
        {
            result.kind = kind;
            result.point = snappedPoint;
            result.distance = distance;
            bestScore = score;
        }
    };

    auto getDistance = [point](QPointF other)
    {
        const QPointF difference = other - point;
        return qSqrt(QPointF::dotProduct(difference, difference));
    };

    // Segments, which pass near the point, are candidates for the intersections
    std::vector<QLineF> nearLines;

    for (uint32_t segmentIndex : candidates)
    {
        const Segment& segment = m_segments[segmentIndex];
        const QPointF p1(segment.x1, segment.y1);
        const QPointF p2(segment.x2, segment.y2);

        // Projection of the point onto the segment
        const QPointF direction = p2 - p1;
        const PDFReal squaredLength = QPointF::dotProduct(direction, direction);
        QPointF nearestPoint = p1;
        if (squaredLength > 0.0)
        {
            const PDFReal t = qBound(0.0, QPointF::dotProduct(point - p1, direction) / squaredLength, 1.0);
            nearestPoint = p1 + t * direction;
        }

        const PDFReal nearestDistance = getDistance(nearestPoint);
        updateResult(Kind::Nearest, nearestPoint, nearestDistance, nearestDistance);

        constexpr PDFReal SEGMENT_DISTANCE_WEIGHT = 2.0;
        PDFReal segmentPenalty = SEGMENT_DISTANCE_WEIGHT * nearestDistance;

        // Segments shorter than twice the tolerance are mostly hatching, symbols or
        // details too small to be measured at the current zoom, so their points
        // are preferred less than the points of the long lines of the drawing
        if (squaredLength < 4.0 * tolerance * tolerance)
        {
            segmentPenalty += tolerance;
        }

        if (segment.flags & P1Endpoint)
        {
            const PDFReal distance = getDistance(p1);
            updateResult(Kind::Endpoint, p1, distance, distance + segmentPenalty);
        }

        if (segment.flags & P2Endpoint)
        {
            const PDFReal distance = getDistance(p2);
            updateResult(Kind::Endpoint, p2, distance, distance + segmentPenalty);
        }

        if (segment.flags & Straight)
        {
            const QPointF midpoint = (p1 + p2) * 0.5;
            const PDFReal distance = getDistance(midpoint);
            updateResult(Kind::Midpoint, midpoint, distance, distance + segmentPenalty);
        }

        if (nearestDistance <= tolerance && squaredLength > 0.0)
        {
            nearLines.emplace_back(p1, p2);
        }
    }

    // Intersections are searched only, if no end point was found, because an end
    // point has a higher priority. The count of the tested pairs is limited, so
    // a dense hatching cannot slow down the movement of the mouse.
    constexpr size_t MAXIMAL_INTERSECTION_LINES = 256;
    if (result.kind > Kind::Intersection && nearLines.size() >= 2 && nearLines.size() <= MAXIMAL_INTERSECTION_LINES)
    {
        const PDFReal epsilon = tolerance * 1e-3;
        auto isEndOfLine = [epsilon](const QLineF& line, QPointF testedPoint)
        {
            return QLineF(line.p1(), testedPoint).length() <= epsilon || QLineF(line.p2(), testedPoint).length() <= epsilon;
        };

        for (size_t i = 0; i < nearLines.size(); ++i)
        {
            for (size_t j = i + 1; j < nearLines.size(); ++j)
            {
                QPointF intersectionPoint;
                if (nearLines[i].intersects(nearLines[j], &intersectionPoint) == QLineF::BoundedIntersection &&
                    !(isEndOfLine(nearLines[i], intersectionPoint) && isEndOfLine(nearLines[j], intersectionPoint)))
                {
                    // Consecutive segments of a polyline or of a curve touch each other
                    // at their common vertex, which is not an intersection
                    const PDFReal distance = getDistance(intersectionPoint);
                    updateResult(Kind::Intersection, intersectionPoint, distance, distance);
                }
            }
        }
    }

    return result;
}

void PDFSnapper::setGeometrySnapping(bool geometrySnapping)
{
    m_geometrySnapping = geometrySnapping;

    if (!m_geometrySnapping)
    {
        m_geometryPages.clear();
    }
}

const PDFPageGeometrySnapIndex* PDFSnapper::getGeometrySnapIndex(PDFInteger pageIndex) const
{
    auto it = m_geometryPages.find(pageIndex);
    return (it != m_geometryPages.cend() && it->second.index) ? it->second.index.get() : nullptr;
}

void PDFSnapper::buildGeometryPages(const PDFWidgetSnapshot& snapshot)
{
    if (!m_geometrySnapping)
    {
        return;
    }

    std::map<PDFInteger, GeometryPage> geometryPages;

    for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
    {
        if (!item.compiledPage || !item.compiledPage->isValid())
        {
            continue;
        }

        GeometryPage geometryPage;
        geometryPage.compiledPage = item.compiledPage;
        geometryPage.pathCount = item.compiledPage->getPathCount();
        geometryPage.pageToDeviceMatrix = item.pageToDeviceMatrix;
        geometryPage.rect = item.rect;

        // The index depends only on the compiled page, not on the zoom, so the
        // index of a page, which was not compiled again, is reused
        auto it = m_geometryPages.find(item.pageIndex);
        if (it != m_geometryPages.end() &&
            it->second.compiledPage == geometryPage.compiledPage &&
            it->second.pathCount == geometryPage.pathCount)
        {
            geometryPage.index = it->second.index;
        }
        else if (isGeometryPageUnderPoint(geometryPage, m_mousePoint))
        {
            // Building the index of a large drawing takes a noticeable time, so
            // it is built only for the page, on which the user really works
            geometryPage.index = std::make_shared<const PDFPageGeometrySnapIndex>(PDFPageGeometrySnapIndex::create(item.compiledPage));
        }

        geometryPages[item.pageIndex] = qMove(geometryPage);
    }

    // Pages, which are not visible anymore, are released
    m_geometryPages = qMove(geometryPages);
}

bool PDFSnapper::isGeometryPageUnderPoint(const GeometryPage& geometryPage, const QPointF& mousePoint) const
{
    // The tolerance is added, so the points at the edge of the page snap too
    return geometryPage.rect.adjusted(-m_snapPointTolerance, -m_snapPointTolerance, m_snapPointTolerance, m_snapPointTolerance).contains(mousePoint);
}

bool PDFSnapper::isGeometrySnapIndexMissing(const QPointF& mousePoint) const
{
    if (!m_geometrySnapping)
    {
        return false;
    }

    for (const auto& [pageIndex, geometryPage] : m_geometryPages)
    {
        if (!geometryPage.index && isSnappingAllowed(pageIndex) && isGeometryPageUnderPoint(geometryPage, mousePoint))
        {
            return true;
        }
    }

    return false;
}

bool PDFSnapper::updateSnappedGeometryPoint(const QPointF& mousePoint)
{
    for (const auto& [pageIndex, geometryPage] : m_geometryPages)
    {
        if (!geometryPage.index || !isSnappingAllowed(pageIndex) || !geometryPage.pageToDeviceMatrix.isInvertible())
        {
            continue;
        }

        const QTransform deviceToPageMatrix = geometryPage.pageToDeviceMatrix.inverted();
        const PDFReal pixelsPerPageUnit = qSqrt(qAbs(geometryPage.pageToDeviceMatrix.determinant()));

        if (qFuzzyIsNull(pixelsPerPageUnit))
        {
            continue;
        }

        const PDFReal tolerance = m_snapPointTolerance / pixelsPerPageUnit;
        const PDFPageGeometrySnapIndex::Result result = geometryPage.index->find(deviceToPageMatrix.map(mousePoint), tolerance);

        if (!result.isValid())
        {
            continue;
        }

        ViewportSnapPoint snapPoint;
        snapPoint.point = result.point;
        snapPoint.pageIndex = pageIndex;
        snapPoint.viewportPoint = geometryPage.pageToDeviceMatrix.map(result.point);

        switch (result.kind)
        {
            case PDFPageGeometrySnapIndex::Kind::Endpoint:
                snapPoint.type = SnapType::GeometryEndpoint;
                break;

            case PDFPageGeometrySnapIndex::Kind::Intersection:
                snapPoint.type = SnapType::GeometryIntersection;
                break;

            case PDFPageGeometrySnapIndex::Kind::Midpoint:
                snapPoint.type = SnapType::GeometryMidpoint;
                break;

            default:
                snapPoint.type = SnapType::GeometryNearest;
                break;
        }

        m_snappedPoint = snapPoint;
        return true;
    }

    return false;
}

void PDFSnapper::drawGeometryMarker(QPainter* painter) const
{
    if (!m_snappedPoint)
    {
        return;
    }

    const SnapType type = m_snappedPoint->type;
    if (type != SnapType::GeometryEndpoint &&
        type != SnapType::GeometryIntersection &&
        type != SnapType::GeometryMidpoint &&
        type != SnapType::GeometryNearest)
    {
        return;
    }

    const QPointF center = m_snappedPoint->viewportPoint;
    const PDFReal size = qMax(4.0, m_snapPointPixelSize * 0.6);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    // Dark outline under the colored marker keeps it visible on any drawing
    for (int pass = 0; pass < 2; ++pass)
    {
        QPen pen(pass == 0 ? QColor(Qt::black) : QColor(0, 200, 0));
        pen.setWidthF(pass == 0 ? 4.0 : 2.0);
        pen.setCosmetic(true);
        painter->setPen(pen);

        switch (type)
        {
            case SnapType::GeometryEndpoint:
                painter->drawRect(QRectF(center.x() - size, center.y() - size, 2.0 * size, 2.0 * size));
                break;

            case SnapType::GeometryIntersection:
                painter->drawLine(center + QPointF(-size, -size), center + QPointF(size, size));
                painter->drawLine(center + QPointF(-size, size), center + QPointF(size, -size));
                break;

            case SnapType::GeometryMidpoint:
            {
                const QPointF triangle[] = { center + QPointF(0, -size), center + QPointF(size, size), center + QPointF(-size, size) };
                painter->drawPolygon(triangle, 3);
                break;
            }

            default:
            {
                // Hourglass marks the nearest point on a line
                const QPointF hourglass[] = { center + QPointF(-size, -size), center + QPointF(size, -size), center + QPointF(-size, size), center + QPointF(size, size) };
                painter->drawPolygon(hourglass, 4);
                break;
            }
        }
    }

    painter->restore();
}

}   // namespace pdf
