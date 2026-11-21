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

#ifndef PDFDEFERREDIMAGE_H
#define PDFDEFERREDIMAGE_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfcolorspaces.h"
#include "pdfcolorconvertor.h"
#include "pdfstreamfilters.h"

#include <QImage>
#include <QTransform>

#include <atomic>
#include <memory>
#include <vector>

namespace pdf
{
class PDFCMS;
class PDFDocument;
class PDFRenderErrorReporter;

/// Color conversion of 8-bit samples of the device gray and device rgb color spaces,
/// sampled from the color management system, when a page is compiled. Images decoded
/// later (when a page is drawn at a larger zoom) are converted by this table, because
/// the color management system can already be destroyed at that time. Gray samples
/// are converted exactly. Rgb samples are converted exactly, if the color management
/// system does not change device rgb colors (generic color management system, or the
/// same input and output profiles), otherwise they are interpolated in a grid, whose
/// nodes are converted exactly. The object is immutable and thread safe.
class PDF4QTLIBCORESHARED_EXPORT PDFImageColorTable
{
public:
    /// Creates the table for the color space. Returns nullptr, if the color space
    /// is neither device gray nor device rgb.
    /// \param colorSpace Color space
    /// \param cms Color management system
    /// \param intent Rendering intent
    /// \param reporter Error reporter
    static std::shared_ptr<const PDFImageColorTable> create(const PDFAbstractColorSpace* colorSpace,
                                                            const PDFCMS* cms,
                                                            RenderingIntent intent,
                                                            PDFRenderErrorReporter* reporter);

    /// Returns the number of the color components of the input
    unsigned int getComponentCount() const { return m_componentCount; }

    /// Returns true, if the colors are not changed by the conversion
    bool isIdentity() const { return m_isIdentity; }

    /// Converts the colors to the 8-bit rgb buffer
    /// \param colors Input colors (getComponentCount() bytes per pixel)
    /// \param pixelCount Number of pixels
    /// \param outputBuffer Output buffer (3 bytes per pixel)
    void convert(const unsigned char* colors, size_t pixelCount, unsigned char* outputBuffer) const;

    /// Distance of the nodes of the grid of the rgb colors
    static constexpr int GRID_STEP = 5;

    /// Number of the nodes of the grid in a single dimension
    static constexpr int GRID_SIZE = 255 / GRID_STEP + 1;

private:
    PDFImageColorTable() = default;

    unsigned int m_componentCount = 0;
    bool m_isIdentity = false;

    /// Gray: rgb colors of 256 gray values. Rgb: rgb colors of the grid nodes.
    std::vector<unsigned char> m_table;
};

/// Large JPEG (DCT) image with an optional soft mask, which is decoded at the size, at
/// which it is drawn. A plan sheet plotted at 600 ppi can have tens of images of several
/// megapixels - decoding them at their full resolution takes seconds and the compiled page
/// would have hundreds of megabytes, while the sheet is displayed at a fraction of the
/// resolution. The JPEG decoder computes the scaled inverse DCT (1/2, 1/4 and 1/8 of the
/// size) of an image without a soft mask, which is much faster. An image with a soft mask
/// is averaged by blocks in the premultiplied space (colors weighted by the alpha), so thin
/// strokes on a transparent background keep their weight.
///
/// A precompiled page holds a preview of the image, decoded when the page is compiled at
/// the resolution \p PREVIEW_RESOLUTION (or at the full resolution, if the image is placed
/// so, that its resolution is lower). When the image is drawn at a larger size, it is
/// decoded again at a sufficient scale (memoized in \p PDFScaledImageCache).
///
/// The object holds the JPEG data and the stream of the soft mask, so it does not need
/// the document, nor the color management system, when the image is decoded. It is
/// immutable after the page is compiled and thread safe.
class PDF4QTLIBCORESHARED_EXPORT PDFDeferredImage
{
public:
    /// Creates the deferred image, if the image is suitable: a JPEG image of at least
    /// \p MINIMAL_PIXEL_COUNT pixels, 8 bits per component, device gray or device rgb
    /// color space, default decode array, no color key mask, no stencil mask, and an
    /// optional soft mask of the same size with 8 bits per component and no matte, whose
    /// filters can be applied without the document. Otherwise nullptr is returned and
    /// the image is decoded as usual. Exception is thrown, if the JPEG data are damaged.
    /// \param document Document
    /// \param stream Stream of the image
    /// \param colorSpace Color space of the image
    /// \param colorTable Color conversion table of the color space (can be nullptr - then nullptr is returned)
    /// \param imageMatrix Matrix mapping the unit square of the image to the page (in points)
    /// \param reporter Error reporter
    static std::shared_ptr<PDFDeferredImage> create(const PDFDocument* document,
                                                    const PDFStream* stream,
                                                    const PDFAbstractColorSpace* colorSpace,
                                                    std::shared_ptr<const PDFImageColorTable> colorTable,
                                                    const QTransform& imageMatrix,
                                                    PDFRenderErrorReporter* reporter);

    /// Returns the size of the image (at its full resolution)
    QSize getSize() const { return m_size; }

    /// Returns true, if the image has a soft mask
    bool hasSoftMask() const { return m_hasSoftMask; }

    /// Returns the scale denominator of the preview
    unsigned int getPreviewScaleDenominator() const { return m_previewScaleDenominator; }

    /// Returns the largest scale denominator (1, 2, 4 or 8), for which the decoded
    /// image is at least as large as the device size of the image
    /// \param deviceWidth Width of the image on the device (in pixels)
    /// \param deviceHeight Height of the image on the device (in pixels)
    unsigned int getScaleDenominator(qreal deviceWidth, qreal deviceHeight) const;

    /// Returns the size of the image decoded at the scale denominator
    QSize getScaledSize(unsigned int scaleDenominator) const;

    /// Decodes the image at the scale denominator. Exception is thrown, if the
    /// image can't be decoded.
    /// \param scaleDenominator Scale denominator (1, 2, 4 or 8)
    /// \param reporter Error reporter (can be nullptr)
    /// \param isMultithreaded Convert the rows in parallel (content scope of the execution policy).
    ///        Must be false, if the function is called from a task of the content scope.
    QImage decode(unsigned int scaleDenominator, PDFRenderErrorReporter* reporter, bool isMultithreaded) const;

    /// Decodes the image at the scale denominator. Returns null image, if the image
    /// can't be decoded (it has been decoded, when the page was compiled, so the
    /// errors have been reported already).
    /// \param scaleDenominator Scale denominator (1, 2, 4 or 8)
    QImage getImage(unsigned int scaleDenominator) const;

    /// Sets the color convertor, which is applied to the decoded images. Must be
    /// called before the page is drawn (the object is then immutable).
    void setColorConvertor(const PDFColorConvertor& colorConvertor) { m_colorConvertor = colorConvertor; }

    /// Returns the unique identifier of this image (used as a key of the caches)
    quint64 getId() const { return m_id; }

    /// Returns the estimate of the memory held by this object (the preview is
    /// held by the precompiled page and it is not included)
    qint64 getMemoryConsumptionEstimate() const;

    /// Images with less pixels are decoded as usual
    static constexpr qint64 MINIMAL_PIXEL_COUNT = 1024 * 1024;

    /// Resolution of the preview (pixels per inch)
    static constexpr qreal PREVIEW_RESOLUTION = 150.0;

    /// Tolerance of the comparison of the decoded size with the device size (pixels)
    static constexpr qreal SIZE_TOLERANCE = 0.01;

private:
    PDFDeferredImage();

    /// Returns the decoded data of the soft mask (8 bits per sample)
    QByteArray decodeSoftMask() const;

    quint64 m_id = 0;
    QSize m_size;
    unsigned int m_componentCount = 0;
    int m_colorTransform = -1;
    unsigned int m_previewScaleDenominator = 1;
    QByteArray m_jpegData;
    std::shared_ptr<const PDFImageColorTable> m_colorTable;
    PDFColorConvertor m_colorConvertor;

    bool m_hasSoftMask = false;
    PDFObject m_softMaskStream;
    PDFStreamFilterStorage::StreamFilters m_softMaskFilters;

    static std::atomic<quint64> s_lastId;
};

using PDFDeferredImagePointer = std::shared_ptr<PDFDeferredImage>;
using PDFDeferredImageConstPointer = std::shared_ptr<const PDFDeferredImage>;

}   // namespace pdf

#endif // PDFDEFERREDIMAGE_H
