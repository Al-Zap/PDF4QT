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

#include "pdfdeferredimage.h"
#include "pdfdocument.h"
#include "pdfimage.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfutils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "pdfdbgheap.h"

namespace pdf
{

std::atomic<quint64> PDFDeferredImage::s_lastId = 0;

/// Helper functions of the deferred images
class PDFDeferredImageHelper
{
public:
    PDFDeferredImageHelper() = delete;

    /// Returns true, if the parameters of a stream filter can be used without the document
    /// (they contain no references, which would have to be resolved)
    static bool isDirectObject(const PDFObject& object, int depth = 0);

    /// Returns name of the last filter of the stream (empty, if there is no filter)
    static QByteArray getLastFilterName(const PDFDocument* document, const PDFDictionary* dictionary);

};

bool PDFDeferredImageHelper::isDirectObject(const PDFObject& object, int depth)
{
    if (depth > 8)
    {
        return false;
    }

    switch (object.getType())
    {
        case PDFObject::Type::Reference:
            return false;

        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            for (size_t i = 0, count = array->getCount(); i < count; ++i)
            {
                if (!isDirectObject(array->getItem(i), depth + 1))
                {
                    return false;
                }
            }
            return true;
        }

        case PDFObject::Type::Dictionary:
        {
            const PDFDictionary* dictionary = object.getDictionary();
            for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
            {
                if (!isDirectObject(dictionary->getValue(i), depth + 1))
                {
                    return false;
                }
            }
            return true;
        }

        case PDFObject::Type::Stream:
            return false;

        default:
            return true;
    }
}

QByteArray PDFDeferredImageHelper::getLastFilterName(const PDFDocument* document, const PDFDictionary* dictionary)
{
    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = document->getObject(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        return QByteArray("FILE");
    }

    if (filters.isName())
    {
        return filters.getString();
    }

    if (filters.isArray())
    {
        const PDFArray* filterArray = filters.getArray();
        if (const size_t filterCount = filterArray->getCount())
        {
            const PDFObject& object = document->getObject(filterArray->getItem(filterCount - 1));
            if (object.isName())
            {
                return object.getString();
            }
        }
    }

    return QByteArray();
}

std::shared_ptr<const PDFImageColorTable> PDFImageColorTable::create(const PDFAbstractColorSpace* colorSpace,
                                                                     const PDFCMS* cms,
                                                                     RenderingIntent intent,
                                                                     PDFRenderErrorReporter* reporter)
{
    if (!colorSpace || !cms)
    {
        return nullptr;
    }

    std::shared_ptr<PDFImageColorTable> table(new PDFImageColorTable());

    switch (colorSpace->getColorSpace())
    {
        case PDFAbstractColorSpace::ColorSpace::DeviceGray:
        {
            std::array<unsigned char, 256> values = { };
            for (size_t i = 0; i < values.size(); ++i)
            {
                values[i] = static_cast<unsigned char>(i);
            }

            table->m_componentCount = 1;
            table->m_table.resize(values.size() * 3);
            colorSpace->fillRGBBuffer8(values.data(), values.size(), table->m_table.data(), intent, cms, reporter);

            table->m_isIdentity = true;
            for (size_t i = 0; i < table->m_table.size(); ++i)
            {
                if (table->m_table[i] != values[i / 3])
                {
                    table->m_isIdentity = false;
                    break;
                }
            }
            break;
        }

        case PDFAbstractColorSpace::ColorSpace::DeviceRGB:
        {
            constexpr size_t nodeCount = size_t(GRID_SIZE) * size_t(GRID_SIZE) * size_t(GRID_SIZE);
            std::vector<unsigned char> nodes(nodeCount * 3, 0);

            auto it = nodes.begin();
            for (int r = 0; r < GRID_SIZE; ++r)
            {
                for (int g = 0; g < GRID_SIZE; ++g)
                {
                    for (int b = 0; b < GRID_SIZE; ++b)
                    {
                        *it++ = static_cast<unsigned char>(r * GRID_STEP);
                        *it++ = static_cast<unsigned char>(g * GRID_STEP);
                        *it++ = static_cast<unsigned char>(b * GRID_STEP);
                    }
                }
            }

            table->m_componentCount = 3;
            table->m_table.resize(nodes.size());
            colorSpace->fillRGBBuffer8(nodes.data(), nodeCount, table->m_table.data(), intent, cms, reporter);
            table->m_isIdentity = table->m_table == nodes;

            if (table->m_isIdentity)
            {
                // Table is not needed
                table->m_table.clear();
                table->m_table.shrink_to_fit();
            }
            break;
        }

        default:
            return nullptr;
    }

    return table;
}

void PDFImageColorTable::convert(const unsigned char* colors, size_t pixelCount, unsigned char* outputBuffer) const
{
    if (m_componentCount == 1)
    {
        const unsigned char* table = m_table.data();
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const unsigned char* rgb = table + size_t(colors[i]) * 3;
            *outputBuffer++ = rgb[0];
            *outputBuffer++ = rgb[1];
            *outputBuffer++ = rgb[2];
        }
        return;
    }

    Q_ASSERT(m_componentCount == 3);

    if (m_isIdentity)
    {
        std::memcpy(outputBuffer, colors, pixelCount * 3);
        return;
    }

    // Tetrahedral interpolation in the grid
    constexpr int strideB = 3;
    constexpr int strideG = GRID_SIZE * strideB;
    constexpr int strideR = GRID_SIZE * strideG;
    const unsigned char* table = m_table.data();

    for (size_t i = 0; i < pixelCount; ++i)
    {
        const int r = colors[0];
        const int g = colors[1];
        const int b = colors[2];
        colors += 3;

        const int ri = r / GRID_STEP;
        const int gi = g / GRID_STEP;
        const int bi = b / GRID_STEP;
        const int rf = r - ri * GRID_STEP;
        const int gf = g - gi * GRID_STEP;
        const int bf = b - bi * GRID_STEP;

        // Fractional part is zero for the last node, so the next node is not used then
        const int dr = rf ? strideR : 0;
        const int dg = gf ? strideG : 0;
        const int db = bf ? strideB : 0;

        const unsigned char* c000 = table + ri * strideR + gi * strideG + bi * strideB;
        const unsigned char* c111 = c000 + dr + dg + db;

        const unsigned char* c1 = nullptr;
        const unsigned char* c2 = nullptr;
        int f0 = 0;
        int f1 = 0;
        int f2 = 0;

        if (rf >= gf)
        {
            if (gf >= bf)
            {
                c1 = c000 + dr; c2 = c000 + dr + dg; f0 = rf; f1 = gf; f2 = bf;
            }
            else if (rf >= bf)
            {
                c1 = c000 + dr; c2 = c000 + dr + db; f0 = rf; f1 = bf; f2 = gf;
            }
            else
            {
                c1 = c000 + db; c2 = c000 + dr + db; f0 = bf; f1 = rf; f2 = gf;
            }
        }
        else
        {
            if (rf >= bf)
            {
                c1 = c000 + dg; c2 = c000 + dr + dg; f0 = gf; f1 = rf; f2 = bf;
            }
            else if (gf >= bf)
            {
                c1 = c000 + dg; c2 = c000 + dg + db; f0 = gf; f1 = bf; f2 = rf;
            }
            else
            {
                c1 = c000 + db; c2 = c000 + dg + db; f0 = bf; f1 = gf; f2 = rf;
            }
        }

        // Weights: c000 (STEP - f0), c1 (f0 - f1), c2 (f1 - f2), c111 f2
        const int w0 = GRID_STEP - f0;
        const int w1 = f0 - f1;
        const int w2 = f1 - f2;
        const int w3 = f2;

        for (int k = 0; k < 3; ++k)
        {
            const int value = c000[k] * w0 + c1[k] * w1 + c2[k] * w2 + c111[k] * w3;
            *outputBuffer++ = static_cast<unsigned char>((value + GRID_STEP / 2) / GRID_STEP);
        }
    }
}

PDFDeferredImage::PDFDeferredImage() :
    m_id(++s_lastId)
{

}

std::shared_ptr<PDFDeferredImage> PDFDeferredImage::create(const PDFDocument* document,
                                                           const PDFStream* stream,
                                                           const PDFAbstractColorSpace* colorSpace,
                                                           std::shared_ptr<const PDFImageColorTable> colorTable,
                                                           const QTransform& imageMatrix,
                                                           PDFRenderErrorReporter* reporter)
{
    if (!document || !stream || !colorSpace || !colorTable || colorTable->getComponentCount() != colorSpace->getColorComponentCount())
    {
        return nullptr;
    }

    const PDFDictionary* dictionary = stream->getDictionary();
    PDFDocumentDataLoaderDecorator loader(document);

    const QByteArray filterName = PDFDeferredImageHelper::getLastFilterName(document, dictionary);
    if (filterName != "DCTDecode" && filterName != "DCT")
    {
        return nullptr;
    }

    // Check the entries of the image, which need the generic decoding
    if (loader.readBooleanFromDictionary(dictionary, "ImageMask", false) ||
        dictionary->hasKey("Mask") ||
        !loader.readNumberArrayFromDictionary(dictionary, "Decode").empty() ||
        loader.readIntegerFromDictionary(dictionary, "BitsPerComponent", 8) != 8)
    {
        return nullptr;
    }

    const PDFInteger width = loader.readIntegerFromDictionary(dictionary, "Width", 0);
    const PDFInteger height = loader.readIntegerFromDictionary(dictionary, "Height", 0);
    if (width <= 0 || height <= 0 || width * height < MINIMAL_PIXEL_COUNT || width > 65500 || height > 65500)
    {
        return nullptr;
    }

    // Soft mask
    PDFObject softMaskObject = document->getObject(dictionary->get("SMask"));
    PDFStreamFilterStorage::StreamFilters softMaskFilters;
    if (!softMaskObject.isNull())
    {
        if (!softMaskObject.isStream())
        {
            return nullptr;
        }

        const PDFStream* softMaskStream = softMaskObject.getStream();
        const PDFDictionary* softMaskDictionary = softMaskStream->getDictionary();

        if (loader.readIntegerFromDictionary(softMaskDictionary, "Width", 0) != width ||
            loader.readIntegerFromDictionary(softMaskDictionary, "Height", 0) != height ||
            loader.readIntegerFromDictionary(softMaskDictionary, "BitsPerComponent", 0) != 8 ||
            loader.readBooleanFromDictionary(softMaskDictionary, "ImageMask", false) ||
            softMaskDictionary->hasKey("Mask") ||
            softMaskDictionary->hasKey("SMask") ||
            softMaskDictionary->hasKey("Matte") ||
            softMaskDictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER) ||
            !loader.readNumberArrayFromDictionary(softMaskDictionary, "Decode").empty())
        {
            return nullptr;
        }

        // Filters of the soft mask are applied later, without the document, so their
        // parameters can't contain references and the filters can't need the security handler.
        auto objectFetcher = [document](const PDFObject& object) -> const PDFObject& { return document->getObject(object); };
        softMaskFilters = PDFStreamFilterStorage::getStreamFilters(softMaskStream, objectFetcher);
        if (!softMaskFilters.valid || softMaskFilters.filterObjects.size() != softMaskFilters.filterParameterObjects.size())
        {
            return nullptr;
        }

        for (size_t i = 0; i < softMaskFilters.filterObjects.size(); ++i)
        {
            const PDFStreamFilter* filter = softMaskFilters.filterObjects[i];
            if (!filter || filter == PDFStreamFilterStorage::getFilter("Crypt") || !PDFDeferredImageHelper::isDirectObject(softMaskFilters.filterParameterObjects[i]))
            {
                return nullptr;
            }
        }
    }

    std::shared_ptr<PDFDeferredImage> image(new PDFDeferredImage());
    image->m_jpegData = document->getDecodedStream(stream);
    image->m_colorTransform = int(loader.readIntegerFromDictionary(dictionary, "ColorTransform", -1));

    if (image->m_jpegData.isEmpty())
    {
        return nullptr;
    }

    // The size of the image is given by the JPEG data
    std::optional<PDFImage::DCTImage> header = PDFImage::decodeDCT(image->m_jpegData, image->m_colorTransform, 1, true, reporter);
    if (!header || header->width != width || header->height != height || header->components != colorSpace->getColorComponentCount())
    {
        return nullptr;
    }

    image->m_size = QSize(int(width), int(height));
    image->m_componentCount = header->components;
    image->m_colorTable = qMove(colorTable);
    image->m_hasSoftMask = !softMaskObject.isNull();
    if (image->m_hasSoftMask)
    {
        image->m_softMaskStream = softMaskObject;
        image->m_softMaskFilters = qMove(softMaskFilters);
    }

    // Preview resolution
    const qreal widthInPoints = std::hypot(imageMatrix.m11(), imageMatrix.m12());
    const qreal heightInPoints = std::hypot(imageMatrix.m21(), imageMatrix.m22());
    const qreal pixelsPerPoint = PREVIEW_RESOLUTION / 72.0;
    image->m_previewScaleDenominator = image->getScaleDenominator(widthInPoints * pixelsPerPoint, heightInPoints * pixelsPerPoint);

    return image;
}

unsigned int PDFDeferredImage::getScaleDenominator(qreal deviceWidth, qreal deviceHeight) const
{
    if (!std::isfinite(deviceWidth) || !std::isfinite(deviceHeight))
    {
        return 1;
    }

    for (unsigned int scaleDenominator : { 8u, 4u, 2u })
    {
        // Small tolerance for the rounding errors of the matrices
        const QSize size = getScaledSize(scaleDenominator);
        if (size.width() + SIZE_TOLERANCE >= deviceWidth && size.height() + SIZE_TOLERANCE >= deviceHeight)
        {
            return scaleDenominator;
        }
    }

    return 1;
}

QSize PDFDeferredImage::getScaledSize(unsigned int scaleDenominator) const
{
    Q_ASSERT(scaleDenominator > 0);
    return QSize(int((unsigned int)(m_size.width()) + scaleDenominator - 1) / int(scaleDenominator),
                 int((unsigned int)(m_size.height()) + scaleDenominator - 1) / int(scaleDenominator));
}

QByteArray PDFDeferredImage::decodeSoftMask() const
{
    Q_ASSERT(m_hasSoftMask);

    const PDFStream* stream = m_softMaskStream.getStream();
    QByteArray data = *stream->getContent();

    auto objectFetcher = [](const PDFObject& object) -> const PDFObject& { return object; };
    for (size_t i = 0; i < m_softMaskFilters.filterObjects.size(); ++i)
    {
        data = m_softMaskFilters.filterObjects[i]->apply(data, objectFetcher, m_softMaskFilters.filterParameterObjects[i], nullptr);
    }

    // Missing samples are opaque
    const qsizetype requiredSize = qsizetype(m_size.width()) * qsizetype(m_size.height());
    if (data.size() < requiredSize)
    {
        data.append(requiredSize - data.size(), '\xFF');
    }

    return data;
}

QImage PDFDeferredImage::decode(unsigned int scaleDenominator, PDFRenderErrorReporter* reporter, bool isMultithreaded) const
{
    // Jakub Melka: an image with a soft mask must be averaged in the premultiplied space,
    // i.e. the colors weighted by the alpha - colors under the transparent pixels are
    // arbitrary (usually white paper around black strokes), and the plain average would
    // wash out thin strokes. So the JPEG is decoded at its full resolution then (the
    // entropy decoding, which is the same for all scales, is the major part of the
    // work anyway) and the blocks of pixels are averaged here. Images without
    // a soft mask are scaled by the JPEG decoder.
    const bool isPremultipliedReduction = m_hasSoftMask && scaleDenominator > 1;
    const unsigned int decoderScaleDenominator = isPremultipliedReduction ? 1 : scaleDenominator;
    const QSize size = getScaledSize(scaleDenominator);
    const QSize decodedSize = getScaledSize(decoderScaleDenominator);

    std::optional<PDFImage::DCTImage> dctImage = PDFImage::decodeDCT(m_jpegData, m_colorTransform, decoderScaleDenominator, false, reporter);
    if (!dctImage || dctImage->components != m_componentCount || int(dctImage->width) != decodedSize.width() || int(dctImage->height) != decodedSize.height())
    {
        throw PDFException(PDFTranslationContext::tr("Can't decode the image."));
    }

    QByteArray softMask;
    if (m_hasSoftMask)
    {
        softMask = decodeSoftMask();
    }

    QImage image(size, m_hasSoftMask ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB888);
    if (image.isNull())
    {
        throw PDFException(PDFTranslationContext::tr("Can't decode the image."));
    }

    const int width = size.width();
    const int decodedWidth = decodedSize.width();
    const int decodedHeight = decodedSize.height();
    const int componentCount = int(m_componentCount);
    const int denominator = int(scaleDenominator);
    const qsizetype sourceStride = qsizetype(decodedWidth) * componentCount;
    const unsigned char* source = reinterpret_cast<const unsigned char*>(dctImage->data.constData());
    const unsigned char* alpha = reinterpret_cast<const unsigned char*>(softMask.constData());

    auto convertRow = [&](int row)
    {
        if (!m_hasSoftMask)
        {
            m_colorTable->convert(source + qsizetype(row) * sourceStride, width, image.scanLine(row));
            return;
        }

        std::vector<unsigned char> colors;
        std::vector<unsigned char> alphas;

        if (!isPremultipliedReduction)
        {
            const unsigned char* sourceRow = source + qsizetype(row) * sourceStride;
            const unsigned char* alphaRow = alpha + qsizetype(row) * decodedWidth;
            colors.assign(sourceRow, sourceRow + qsizetype(width) * componentCount);
            alphas.assign(alphaRow, alphaRow + width);
        }
        else
        {
            // Sums of the alpha and of the colors multiplied by the alpha
            std::vector<quint32> alphaSums(width, 0);
            std::vector<quint32> colorSums(size_t(width) * componentCount, 0);
            std::vector<quint32> counts(width, 0);

            const int rowBegin = row * denominator;
            const int rowEnd = qMin(rowBegin + denominator, decodedHeight);
            for (int sourceRowIndex = rowBegin; sourceRowIndex < rowEnd; ++sourceRowIndex)
            {
                const unsigned char* sourceRow = source + qsizetype(sourceRowIndex) * sourceStride;
                const unsigned char* alphaRow = alpha + qsizetype(sourceRowIndex) * decodedWidth;

                for (int column = 0; column < decodedWidth; ++column)
                {
                    const int targetColumn = column / denominator;
                    const quint32 alphaValue = alphaRow[column];
                    alphaSums[targetColumn] += alphaValue;
                    ++counts[targetColumn];

                    if (alphaValue)
                    {
                        const unsigned char* sourcePixel = sourceRow + column * componentCount;
                        quint32* colorSum = colorSums.data() + targetColumn * componentCount;
                        for (int component = 0; component < componentCount; ++component)
                        {
                            colorSum[component] += sourcePixel[component] * alphaValue;
                        }
                    }
                }
            }

            colors.resize(size_t(width) * componentCount);
            alphas.resize(width);
            for (int column = 0; column < width; ++column)
            {
                const quint32 alphaSum = alphaSums[column];
                const quint32 count = counts[column];
                alphas[column] = count ? static_cast<unsigned char>((alphaSum + count / 2) / count) : 0;

                for (int component = 0; component < componentCount; ++component)
                {
                    const quint32 colorSum = colorSums[size_t(column) * componentCount + component];
                    colors[size_t(column) * componentCount + component] = alphaSum ? static_cast<unsigned char>((colorSum + alphaSum / 2) / alphaSum) : 0;
                }
            }
        }

        std::vector<unsigned char> rgb(size_t(width) * 3, 0);
        m_colorTable->convert(colors.data(), width, rgb.data());

        QRgb* targetRow = reinterpret_cast<QRgb*>(image.scanLine(row));
        const unsigned char* rgbData = rgb.data();
        for (int column = 0; column < width; ++column)
        {
            targetRow[column] = qPremultiply(qRgba(rgbData[0], rgbData[1], rgbData[2], alphas[column]));
            rgbData += 3;
        }
    };

    PDFIntegerRange<int> range(0, size.height());
    if (isMultithreaded)
    {
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Content, range.begin(), range.end(), convertRow);
    }
    else
    {
        std::for_each(range.begin(), range.end(), convertRow);
    }

    if (m_colorConvertor.isActive())
    {
        image = m_colorConvertor.convert(qMove(image));
    }

    return image;
}

QImage PDFDeferredImage::getImage(unsigned int scaleDenominator) const
{
    try
    {
        return decode(scaleDenominator, nullptr, true);
    }
    catch (const PDFException&)
    {
        return QImage();
    }
}

qint64 PDFDeferredImage::getMemoryConsumptionEstimate() const
{
    qint64 memory = sizeof(*this) + m_jpegData.size();

    if (m_hasSoftMask)
    {
        memory += m_softMaskStream.getStream()->getContent()->size();
    }

    return memory;
}

}   // namespace pdf
