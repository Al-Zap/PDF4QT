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

#ifndef PDFVISUALCOMPARE_H
#define PDFVISUALCOMPARE_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfrenderer.h"

#include <QColor>
#include <QImage>
#include <QDateTime>

#include <functional>
#include <vector>

namespace pdf
{
class PDFOperationControl;
class PDFProgress;

/// Settings of the visual comparison of two documents (revisions of drawing sheets).
/// Pages are rasterized, aligned to each other and compared pixel by pixel. Content,
/// which is only in the old page, is removed, content only in the new page is added,
/// content in both pages with a different color is changed.
struct PDF4QTLIBCORESHARED_EXPORT PDFVisualCompareSettings
{
    enum class Alignment
    {
        PageOrigin,     ///< Top left corners of the visible pages are aligned
        PageCenter,     ///< Centers of the visible pages are aligned
        Automatic       ///< The old page is shifted to the position, where it matches the new page best
    };

    /// Resolution of the comparison in pixels per inch
    PDFReal resolution = 150.0;

    /// Alignment of the old page to the new page
    Alignment alignment = Alignment::Automatic;

    /// Largest shift of the old page searched by the automatic alignment, in millimeters
    PDFReal maximumShiftMM = 25.0;

    /// The old page is scaled to the size of the new page (otherwise both pages
    /// are compared at the same physical scale)
    bool scaleToFit = false;

    /// A pixel lighter than this gray level (0-255) is the paper, darker pixel is ink
    int inkThreshold = 245;

    /// Ink, which moved at most by this number of pixels, is not a change (removes
    /// the differences of the antialiasing and of the rounding of the positions)
    int tolerancePixels = 1;

    /// Ink in both pages is changed, when its color differs by more than this value
    /// in some channel (0-255). Zero turns off the detection of the changed colors.
    /// Ink, which is much darker than the ink of the other page at the same place (a new
    /// line drawn over a hatched area or an aerial photo), is always added or removed.
    int colorTolerance = 96;

    /// Changes closer to each other than this distance are reported as a single change
    PDFReal mergeDistanceMM = 4.0;

    /// Changes smaller than this size (in both directions) are ignored
    PDFReal minimumChangeSizeMM = 0.5;

    /// Colors of the overlay
    QColor removedColor = QColor(230, 20, 20);
    QColor addedColor = QColor(0, 150, 30);
    QColor changedColor = QColor(255, 140, 0);
    QColor unchangedColor = QColor(150, 150, 150);

    /// Renderer features used to rasterize the pages
    PDFRenderer::Features features = PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::SmoothImages | PDFRenderer::ClipToCropBox);

    /// Returns the number of pixels of a distance in millimeters at the resolution
    int toPixels(PDFReal mm) const;

    bool operator==(const PDFVisualCompareSettings&) const = default;
};

/// A single change found by the visual comparison
struct PDF4QTLIBCORESHARED_EXPORT PDFVisualCompareChange
{
    enum class Type
    {
        Added,      ///< Content only in the new page
        Removed,    ///< Content only in the old page
        Changed     ///< Both added and removed content, or content with a changed color
    };

    Type type = Type::Changed;

    /// Bounding box of the change in pixels of the overlay image
    QRect pixelRect;

    /// Bounding box of the change in the coordinates of the report page (points,
    /// the origin is the bottom left corner of the visible new page)
    QRectF pageRect;

    int addedPixels = 0;
    int removedPixels = 0;
    int changedPixels = 0;

    /// Returns translated description of the type
    static QString getTypeName(Type type);
};

/// Result of the comparison of a pair of pages
struct PDF4QTLIBCORESHARED_EXPORT PDFVisualComparePageResult
{
    /// Page index in the old document (-1 = the page was added)
    PDFInteger oldPageIndex = -1;

    /// Page index in the new document (-1 = the page was removed)
    PDFInteger newPageIndex = -1;

    /// Size of the report page in points (visible size of the new page,
    /// or of the old page, when the page was removed)
    QSizeF pageSize;

    /// Overlay image: unchanged content faded, removed, added and changed
    /// content in the colors of the settings
    QImage overlay;

    /// Shift of the old page in pixels of the overlay (old pixel (x, y) is
    /// compared with the new pixel (x + dx, y + dy))
    QPoint offset;

    /// Scale of the old page applied by the setting \p scaleToFit
    PDFReal oldPageScale = 1.0;

    std::vector<PDFVisualCompareChange> changes;

    qint64 addedPixels = 0;
    qint64 removedPixels = 0;
    qint64 changedPixels = 0;

    /// Error of the rendering of the pages (empty = no error)
    QString errorMessage;

    bool isPageAdded() const { return oldPageIndex == -1 && newPageIndex != -1; }
    bool isPageRemoved() const { return oldPageIndex != -1 && newPageIndex == -1; }
    bool hasChanges() const { return !changes.empty(); }

    /// Returns the number of changes of the given type
    size_t getChangeCount(PDFVisualCompareChange::Type type) const;
};

/// Result of the comparison of the documents
struct PDF4QTLIBCORESHARED_EXPORT PDFVisualCompareResult
{
    std::vector<PDFVisualComparePageResult> pages;

    /// Error, which stopped the comparison
    QString errorMessage;
    bool cancelled = false;

    bool isSuccess() const { return errorMessage.isEmpty() && !cancelled; }

    /// Returns total count of the changes
    size_t getChangeCount() const;

    /// Returns count of the page pairs with a change
    size_t getChangedPageCount() const;
};

/// Visual comparison of two revisions of a document. It is intended for drawings
/// (site plans, lighting plans, ...), where the content-level comparison (\sa PDFDiff)
/// reports many differences of the drawing operators, which do not change the
/// drawing, and where the user wants to see the old and new revision overlaid.
class PDF4QTLIBCORESHARED_EXPORT PDFVisualCompare
{
public:
    using PagePair = std::pair<PDFInteger, PDFInteger>;
    using PagePairs = std::vector<PagePair>;
    using PageCallback = std::function<void(const PDFVisualComparePageResult&)>;

    /// Creates comparison of the documents. Documents must exist, while this object exists.
    explicit PDFVisualCompare(const PDFDocument* oldDocument,
                              const PDFDocument* newDocument,
                              const PDFVisualCompareSettings& settings);
    ~PDFVisualCompare();

    void setOperationControl(const PDFOperationControl* operationControl) { m_operationControl = operationControl; }
    void setProgress(PDFProgress* progress) { m_progress = progress; }

    /// Pairs the pages in the order of the page lists. The longer list gets pairs
    /// with -1 (added or removed pages).
    static PagePairs createPagePairs(const std::vector<PDFInteger>& oldPages, const std::vector<PDFInteger>& newPages);

    /// Parses page ranges (for example "1-3,5"), pages are numbered from one, the
    /// returned indices from zero. Empty text means all pages.
    /// \param text Page ranges
    /// \param pageCount Page count of the document
    /// \param[out] pages Page indices in ascending order
    /// \param[out] errorMessage Error message
    static bool parsePages(const QString& text, PDFInteger pageCount, std::vector<PDFInteger>& pages, QString* errorMessage);

    /// Returns pairs of all pages of the documents
    PagePairs createPagePairs() const;

    /// Compares a single pair of pages. One of the indices can be -1 (added or removed page).
    PDFVisualComparePageResult comparePages(PDFInteger oldPageIndex, PDFInteger newPageIndex) const;

    /// Compares the pairs of pages. Pages are compared in parallel. When a callback
    /// is given, it is called for each page in the order of the pairs (in the calling
    /// thread) and the overlay images are not kept in the result (the images of large
    /// sheets would need a lot of memory). Without a callback, the result keeps them.
    PDFVisualCompareResult compare(const PagePairs& pairs, const PageCallback& callback = PageCallback()) const;

    /// Renders the visible area of the page at the resolution of the settings
    /// \param document Document
    /// \param pageIndex Page index
    /// \param scale Additional scale of the page
    /// \param[out] errorMessage Error message
    QImage renderPage(const PDFDocument* document, PDFInteger pageIndex, PDFReal scale, QString* errorMessage) const;

    /// Returns the visible size of the page in points (rotated crop box)
    static QSizeF getPageSize(const PDFDocument* document, PDFInteger pageIndex);

    /// Compares rendered pages. The old image is aligned to the new image according
    /// to the settings. Images must have white background.
    /// \param oldImage Old page (null image = the page was added)
    /// \param newImage New page (null image = the page was removed)
    /// \param settings Settings
    /// \param pageSize Size of the report page in points
    static PDFVisualComparePageResult compareImages(const QImage& oldImage,
                                                    const QImage& newImage,
                                                    const PDFVisualCompareSettings& settings,
                                                    QSizeF pageSize);

    /// Finds the shift of the old image, which matches the new image best. Pixel
    /// (x, y) of the old image corresponds to the pixel (x + dx, y + dy) of the new image.
    static QPoint findOffset(const QImage& oldImage, const QImage& newImage, const PDFVisualCompareSettings& settings);

private:
    struct Context;

    const PDFDocument* m_oldDocument;
    const PDFDocument* m_newDocument;
    PDFVisualCompareSettings m_settings;
    const PDFOperationControl* m_operationControl = nullptr;
    PDFProgress* m_progress = nullptr;
    std::unique_ptr<Context> m_oldContext;
    std::unique_ptr<Context> m_newContext;
};

/// Writes the result of the visual comparison into a new document: a summary page
/// with the legend and the list of the compared pages, followed by a page with the
/// overlay for each pair of pages. Each change is marked by a revision cloud
/// annotation (so the changes are listed in the comments of a viewer), and the
/// outline contains an item for each page and each change.
class PDF4QTLIBCORESHARED_EXPORT PDFVisualCompareReport
{
public:
    struct Information
    {
        QString oldDocumentName;
        QString newDocumentName;
        QDateTime dateTime;
    };

    explicit PDFVisualCompareReport(const PDFVisualCompareSettings& settings, const Information& information);
    ~PDFVisualCompareReport();

    /// Adds a page with the overlay of the compared pair of pages
    void addPage(const PDFVisualComparePageResult& page);

    /// Creates the document (the summary is created from the result, the overlay
    /// pages must have been added by \p addPage in the order of the result)
    PDFDocument build(const PDFVisualCompareResult& result);

    /// Returns translated title of the page pair (for example, "Page 3 / 4")
    static QString getPageTitle(const PDFVisualComparePageResult& page);

    /// Name of the annotations of the changes (subject of the markup annotations)
    static QString getChangeSubject();

private:
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

}   // namespace pdf

#endif // PDFVISUALCOMPARE_H
