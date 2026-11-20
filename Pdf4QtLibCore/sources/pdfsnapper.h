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

#ifndef PDFSNAPPER_H
#define PDFSNAPPER_H

#include "pdfglobal.h"

#include <QImage>
#include <QPainterPath>
#include <QTransform>

#include <array>
#include <map>
#include <memory>
#include <optional>

class QPainter;

namespace pdf
{
class PDFDeferredImage;
class PDFPrecompiledPage;
struct PDFWidgetSnapshot;

enum class SnapType
{
    Invalid,
    PageCorner,        ///< Corner of the page media box
    ImageCorner,       ///< Corner of image
    PageCenter,        ///< Center of page media box
    ImageCenter,       ///< Center of image
    LineCenter,        ///< Center of line
    GeneratedLineProjection,   ///< Generated point to line projections
    Annotation,        ///< Annotation geometry point
    Custom,  ///< Custom snap point
    GeometryEndpoint,       ///< End point of a line of the drawing (see PDFPageGeometrySnapIndex)
    GeometryIntersection,   ///< Intersection of two lines of the drawing
    GeometryMidpoint,       ///< Midpoint of a line of the drawing
    GeometryNearest         ///< Nearest point on a line of the drawing
};

/// Contain informations for snap points in the pdf page. Snap points
/// can be for example image centers, rectangle corners, line start/end
/// points, page boundary boxes etc. All coordinates are in page coordinates.
class PDF4QTLIBCORESHARED_EXPORT PDFSnapInfo
{
public:
    explicit inline PDFSnapInfo() = default;

    struct SnapPoint
    {
        explicit inline constexpr SnapPoint() = default;
        explicit inline constexpr SnapPoint(SnapType type, QPointF point) :
            type(type),
            point(point)
        {

        }

        SnapType type = SnapType::Invalid;
        QPointF point;
    };

    struct PDF4QTLIBCORESHARED_EXPORT SnapImage
    {
        QPainterPath imagePath;
        QImage image;

        /// Image decoded at the size, at which it is drawn (then \p image is null)
        std::shared_ptr<const PDFDeferredImage> deferredImage;

        /// Returns the image at its full resolution
        QImage getImage() const;
    };

    /// Adds page media box. Media box must be in page coordinates.
    /// \param mediaBox Media box
    void addPageMediaBox(const QRectF& mediaBox);

    /// Adds image box. Because it is not guaranteed, that it will be rectangle, five
    /// points are defined - four corners and center.
    /// \param points Four corner points in clockwise order, fifth point is center,
    ///        all in page coordinates.
    /// \param image Image
    /// \param deferredImage Image decoded at the size, at which it is drawn (then \p image is null)
    void addImage(const std::array<QPointF, 5>& points, const QImage& image, std::shared_ptr<const PDFDeferredImage> deferredImage = nullptr);

    /// Adds line and line center points
    /// \param start Start point of line, in page coordinates
    /// \param end End point of line, in page coordinates
    void addLine(const QPointF& start, const QPointF& end);

    /// Adds annotation point.
    /// \param point Annotation point in page coordinates
    void addAnnotationPoint(const QPointF& point);

    /// Adds annotation rectangle. Rectangle corners, center and edges are added.
    /// \param rectangle Annotation rectangle in page coordinates
    void addAnnotationRectangle(const QRectF& rectangle);

    /// Adds annotation line. Line endpoints and center are added.
    /// \param start Start point of line, in page coordinates
    /// \param end End point of line, in page coordinates
    void addAnnotationLine(const QPointF& start, const QPointF& end);

    /// Returns snap points
    const std::vector<SnapPoint>& getSnapPoints() const { return m_snapPoints; }

    /// Returns lines
    const std::vector<QLineF>& getLines() const { return m_snapLines; }

    /// Returns snap images (together with painter path in page coordinates,
    /// in which image is painted).
    const std::vector<SnapImage>& getSnapImages() const { return m_snapImages; }

private:
    std::vector<SnapPoint> m_snapPoints;
    std::vector<QLineF> m_snapLines;
    std::vector<SnapImage> m_snapImages;
};

/// Spatial index of the vector geometry of a single page - the segments of all
/// drawn paths, their end points, midpoints and mutual intersections. It allows
/// to snap to the drawing itself like in a CAD application (end of a line,
/// corner of a building, midpoint of a wall, point on a property line), which
/// is needed for an exact measurement on a plan. Coordinates are page coordinates.
/// The index is immutable after \p build, so it can be shared between threads.
class PDF4QTLIBCORESHARED_EXPORT PDFPageGeometrySnapIndex
{
public:
    explicit PDFPageGeometrySnapIndex() = default;

    /// Kind of the snapped point, ordered by priority - when more points
    /// lie within the tolerance, the kind with lower value wins
    enum class Kind
    {
        Endpoint,       ///< End point of a line or a curve, corner of a polyline or a rectangle
        Intersection,   ///< Intersection of two segments
        Midpoint,       ///< Midpoint of a straight segment
        Nearest,        ///< Nearest point lying on a segment
        None            ///< Nothing snapped
    };

    struct Result
    {
        Kind kind = Kind::None;
        QPointF point;          ///< Snapped point in page coordinates
        PDFReal distance = 0.0; ///< Distance from the queried point

        bool isValid() const { return kind != Kind::None; }
    };

    /// Creates the index from the vector graphics of a compiled page. Text is
    /// not included. If the page contains more segments, than the limit, then
    /// the remaining ones are ignored, so the memory consumption is bounded.
    /// \param compiledPage Compiled page
    /// \param maximalSegmentCount Maximal count of the indexed segments
    static PDFPageGeometrySnapIndex create(const PDFPrecompiledPage* compiledPage, size_t maximalSegmentCount = DEFAULT_MAXIMAL_SEGMENT_COUNT);

    /// Adds all segments of a path. Curves are approximated by line segments,
    /// but only their real end points are snapped as end points. Call \p build
    /// after all paths are added.
    /// \param path Path
    /// \param matrix Matrix mapping the path to the page coordinates
    void addPath(const QPainterPath& path, const QTransform& matrix);

    /// Adds a single segment. Call \p build after all segments are added.
    /// \param p1 Start point
    /// \param p2 End point
    /// \param isStraight Is it a real straight segment (otherwise it is a part of a curve, which has no midpoint)
    /// \param isP1Endpoint Is the start point a real end point (otherwise it is an inner point of a curve approximation)
    /// \param isP2Endpoint Is the end point a real end point
    void addSegment(QPointF p1, QPointF p2, bool isStraight, bool isP1Endpoint, bool isP2Endpoint);

    /// Builds the spatial index, must be called after the segments are added
    void build();

    /// Finds the point of the geometry, to which the given point snaps. The kind
    /// of the point with the highest priority wins, the nearest point is selected
    /// from the points of the same kind.
    /// \param point Point in page coordinates
    /// \param tolerance Maximal distance in page coordinates
    Result find(QPointF point, PDFReal tolerance) const;

    /// Returns count of the indexed segments
    size_t getSegmentCount() const { return m_segments.size(); }

    /// Returns true, if the segment limit was reached and some geometry was ignored
    bool isTruncated() const { return m_isTruncated; }

    bool isEmpty() const { return m_segments.empty(); }

    static constexpr size_t DEFAULT_MAXIMAL_SEGMENT_COUNT = 2'000'000;

private:
    enum SegmentFlag : uint8_t
    {
        Straight = 0x01,
        P1Endpoint = 0x02,
        P2Endpoint = 0x04
    };

    // Single precision is sufficient - it is a precision of about 0.01 pt
    // for coordinates of the largest drawings
    struct Segment
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        uint8_t flags = 0;
    };

    /// Calls the function for each cell, through which the segment passes
    template<typename Function>
    void forEachSegmentCell(const Segment& segment, Function function) const;

    int getCellColumn(PDFReal x) const;
    int getCellRow(PDFReal y) const;

    std::vector<Segment> m_segments;
    std::vector<uint32_t> m_cellStart;  ///< Index of the first item of each cell, one more for the end
    std::vector<uint32_t> m_cellItems;  ///< Indices of the segments in the cells
    QRectF m_bounds;
    PDFReal m_cellSize = 1.0;
    int m_columns = 0;
    int m_rows = 0;
    size_t m_maximalSegmentCount = DEFAULT_MAXIMAL_SEGMENT_COUNT;
    bool m_isTruncated = false;
};

/// Snap engine, which handles snapping of points on the page.
class PDF4QTLIBCORESHARED_EXPORT PDFSnapper
{
public:
    PDFSnapper();

    struct ViewportSnapPoint : public PDFSnapInfo::SnapPoint
    {
        QPointF viewportPoint;
        PDFInteger pageIndex;
    };

    struct ViewportSnapImage : public PDFSnapInfo::SnapImage
    {
        PDFInteger pageIndex;
        QPainterPath viewportPath;
    };

    /// Sets snap point pixel size
    /// \param snapPointPixelSize Snap point diameter in pixels
    void setSnapPointPixelSize(int snapPointPixelSize) { m_snapPointPixelSize = snapPointPixelSize; }

    /// Returns snap point pixel size
    int getSnapPointPixelSize() const { return m_snapPointPixelSize; }

    /// Draws snapping points onto the painter. This function needs valid snap points,
    /// so \p m_snapPoints must be valid before this function is called.
    /// \param painter Painter
    void drawSnapPoints(QPainter* painter) const;

    /// Returns true, if snapping is allowed at current page
    /// \param pageIndex Page index to be tested for allowing snapping
    bool isSnappingAllowed(PDFInteger pageIndex) const;

    /// Updates snapped point/image using given information. If current page is set, it means, we are
    /// using snapping info from current page, and if we are hovering at different page,
    /// then nothing happens. Otherwise, other page snap info is used to update snapped point.
    /// If mouse point distance from some snap point is lesser than tolerance, then new snap is set.
    /// \param mousePoint Mouse point in widget coordinates
    void updateSnappedPoint(const QPointF& mousePoint);

    /// Returns true, if some point of interest is snapped
    bool isSnapped() const { return m_snappedPoint.has_value(); }

    /// Builds snap points from the widget snapshot. Updates current value
    /// of snapped point (from mouse position).
    /// \param snapshot Widget snapshot
    void buildSnapPoints(const PDFWidgetSnapshot& snapshot);

    /// Builds snap images from the widget snapshot.
    /// \param snapshot Widget snapshot
    void buildSnapImages(const PDFWidgetSnapshot& snapshot);

    /// Adds snap points from external snap info, for example annotations.
    /// \param pageIndex Page index
    /// \param pageToDeviceMatrix Page-to-device matrix for the page
    /// \param info Snap info in page coordinates
    void addSnapInfo(PDFInteger pageIndex, const QTransform& pageToDeviceMatrix, const PDFSnapInfo& info);

    /// Returns current snap point tolerance (while aiming with the mouse cursor,
    /// when mouse cursor is at most tolerance distance from some snap point,
    /// it is snapped.
    int getSnapPointTolerance() const;

    /// Sets snap point tolerance
    void setSnapPointTolerance(int snapPointTolerance);

    /// Returns snapped position. If point at mouse cursor is not snapped, then
    /// mouse cursor position is returned.
    QPointF getSnappedPoint() const;

    /// Returns snapped image. If no image is present at mouse cursor, then
    /// nullptr is returned.
    const ViewportSnapImage* getSnappedImage() const;

    /// Sets current page index and reference point
    /// \param pageIndex Page index
    /// \param pagePoint Page point
    void setReferencePoint(PDFInteger pageIndex, QPointF pagePoint);

    /// Resets reference point (and current page)
    void clearReferencePoint();

    /// Clears all snapped data, including snap points, images and referenced data.
    void clear();

    /// Returns a vector of custom snap points
    const std::vector<QPointF>& getCustomSnapPoints() const;

    /// Sets custom set of snap points. Snap points are always referred to current
    /// page index. If page index changes, then custom snap points are cleared.
    /// \param customSnapPoints Custom snap points
    void setCustomSnapPoints(const std::vector<QPointF>& customSnapPoints);

    /// Enables snapping to the vector geometry of the pages - end points,
    /// intersections, midpoints of the lines and points on the lines. The
    /// geometry index of a page is built, when the mouse cursor enters the page
    /// for the first time, so scrolling over the pages builds nothing. The index
    /// is kept, until the page is compiled again or it is scrolled out of view.
    /// \param geometrySnapping Snap to the vector geometry
    void setGeometrySnapping(bool geometrySnapping);

    /// Returns true, if snapping to the vector geometry is enabled
    bool isGeometrySnapping() const { return m_geometrySnapping; }

    /// Temporarily suppresses snapping to the vector geometry, for example while
    /// a modifier key is held, so a point can be placed freely. Unlike disabling
    /// the snapping, the geometry indices of the pages are kept.
    /// \param suppressed Suppress snapping to the vector geometry
    void setGeometrySnappingSuppressed(bool suppressed) { m_geometrySnappingSuppressed = suppressed; }

    /// Returns the geometry index of a page, or nullptr, if it was not built
    /// \param pageIndex Page index
    const PDFPageGeometrySnapIndex* getGeometrySnapIndex(PDFInteger pageIndex) const;

    /// Returns the snapped point in the page coordinates. It is exact, unlike the
    /// snapped point in the widget coordinates, which is rounded to pixels, when
    /// it is mapped to the page. Returns false, if no point is snapped.
    /// \param pageIndex Page index of the snapped point
    /// \param pagePoint Snapped point in the page coordinates
    bool getSnappedPagePoint(PDFInteger& pageIndex, QPointF& pagePoint) const;

    /// Returns true, if the mouse point lies on a visible page, whose geometry
    /// index is not built yet. The snap data must be built again from a new
    /// snapshot of the widget then (the compiled pages are valid only, when
    /// the snapshot is taken).
    /// \param mousePoint Mouse point in the widget coordinates
    bool isGeometrySnapIndexMissing(const QPointF& mousePoint) const;

    /// Returns type of the snapped point, or SnapType::Invalid, if no point is snapped
    SnapType getSnappedPointType() const { return m_snappedPoint ? m_snappedPoint->type : SnapType::Invalid; }

private:
    struct GeometryPage
    {
        const PDFPrecompiledPage* compiledPage = nullptr;
        size_t pathCount = 0;
        QTransform pageToDeviceMatrix;
        QRectF rect;    ///< Rectangle of the page in the widget coordinates
        std::shared_ptr<const PDFPageGeometrySnapIndex> index;
    };

    /// Updates the geometry indices of the visible pages
    void buildGeometryPages(const PDFWidgetSnapshot& snapshot);

    /// Returns true, if the mouse point lies on the page (or near its edge)
    bool isGeometryPageUnderPoint(const GeometryPage& geometryPage, const QPointF& mousePoint) const;

    /// Snaps the mouse point to the vector geometry, returns true, if it was snapped
    bool updateSnappedGeometryPoint(const QPointF& mousePoint);

    /// Draws a marker of the snapped point of the geometry, the shape
    /// of the marker shows the kind of the point, as in CAD applications
    void drawGeometryMarker(QPainter* painter) const;

    std::map<PDFInteger, GeometryPage> m_geometryPages;
    bool m_geometrySnapping = false;
    bool m_geometrySnappingSuppressed = false;

    struct SnappedPoint
    {
        PDFInteger pageIndex = -1;
        QPointF snappedPoint;
    };

    std::vector<ViewportSnapPoint> m_snapPoints;
    std::vector<ViewportSnapImage> m_snapImages;
    std::vector<QPointF> m_customSnapPoints;
    std::optional<ViewportSnapPoint> m_snappedPoint;
    std::optional<ViewportSnapImage> m_snappedImage;
    QPointF m_mousePoint;
    std::optional<QPointF> m_referencePoint;
    PDFInteger m_currentPage = -1;
    int m_snapPointPixelSize = 0;
    int m_snapPointTolerance = 0;

    void addSnapInfoImpl(PDFInteger pageIndex, const QTransform& pageToDeviceMatrix, const PDFSnapInfo& info, bool includeCustomSnapPoints);
};

}   // namespace pdf

#endif // PDFSNAPPER_H
