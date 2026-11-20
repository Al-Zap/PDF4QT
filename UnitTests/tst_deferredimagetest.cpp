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
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfconstants.h"
#include "pdfimage.h"
#include "pdfcms.h"
#include "pdfstreamfilters.h"
#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfexecutionpolicy.h"

#include <QtTest>
#include <QPainter>
#include <QPaintEngine>
#include <QRandomGenerator>

#include <cmath>

/// Vector paint device, which records the sizes of the drawn images
class ImageRecordingPaintEngine : public QPaintEngine
{
public:
    ImageRecordingPaintEngine() : QPaintEngine(QPaintEngine::AllFeatures) { }

    virtual bool begin(QPaintDevice*) override { return true; }
    virtual bool end() override { return true; }
    virtual void updateState(const QPaintEngineState&) override { }
    virtual void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override { }
    virtual void drawImage(const QRectF&, const QImage& image, const QRectF&, Qt::ImageConversionFlags) override { imageSizes.push_back(image.size()); }
    virtual void drawPath(const QPainterPath&) override { }
    virtual Type type() const override { return QPaintEngine::User; }

    std::vector<QSize> imageSizes;
};

class ImageRecordingPaintDevice : public QPaintDevice
{
public:
    virtual QPaintEngine* paintEngine() const override { return &m_engine; }

    const std::vector<QSize>& getImageSizes() const { return m_engine.imageSizes; }

protected:
    virtual int metric(PaintDeviceMetric metric) const override
    {
        switch (metric)
        {
            case PdmWidth:
                return 400;
            case PdmHeight:
                return 300;
            case PdmDpiX:
            case PdmDpiY:
            case PdmPhysicalDpiX:
            case PdmPhysicalDpiY:
                return 72;
            case PdmDepth:
                return 32;
            case PdmDevicePixelRatio:
                return 1;
            case PdmDevicePixelRatioScaled:
                return int(devicePixelRatioFScale());
            default:
                return 0;
        }
    }

private:
    mutable ImageRecordingPaintEngine m_engine;
};

class DeferredImageTest : public QObject
{
    Q_OBJECT

private slots:
    void test_ascii85_fast_path();
    void test_fill_rgb_buffer_8bit();
    void test_fast_image_path_identical();
    void test_color_table();
    void test_deferred_image_conditions();
    void test_deferred_image_full_resolution_identical();
    void test_deferred_image_scaled();
    void test_precompiled_page_preview();
    void test_precompiled_page_zoom();
    void test_precompiled_page_vector_device();
    void test_heavy_image_sheet();

private:
    struct ImageOptions
    {
        QSize size = QSize(1200, 900);
        bool softMask = true;
        bool softMaskAscii85 = true;
        bool softMaskPredictor = false;
        bool gray = false;
        bool decode = false;
        bool matte = false;
        bool intent = false;
        QSize softMaskSize;
        QRectF placement = QRectF(0, 0, 288, 216); ///< 1200 x 900 at 300 ppi
    };

    static QImage createColorImage(QSize size, bool gray);
    static QImage createMask(QSize size);
    static QByteArray encodeAscii85(const QByteArray& data);
    static pdf::PDFDocument createDocument(const ImageOptions& options);
    static const pdf::PDFStream* getImageStream(const pdf::PDFDocument& document);
    static QImage renderReference(const pdf::PDFDocument& document, QSize size, const pdf::PDFCMS* cms, pdf::PDFRenderer::Features features);
    static QImage renderCompiled(const pdf::PDFPrecompiledPage& page, const pdf::PDFDocument& document, QSize size, pdf::PDFRenderer::Features features, pdf::PDFScaledImageCache* cache);
    static void compile(const pdf::PDFDocument& document, const pdf::PDFCMS* cms, pdf::PDFRenderer::Features features, pdf::PDFPrecompiledPage* page);
    static double meanDifference(const QImage& left, const QImage& right, int* maximum = nullptr);
    static QImage referenceImage(const pdf::PDFDocument& document, const pdf::PDFCMS* cms);
};

QImage DeferredImageTest::createColorImage(QSize size, bool gray)
{
    QImage image(size, QImage::Format_RGB888);
    QRandomGenerator generator(42);

    for (int y = 0; y < size.height(); ++y)
    {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x)
        {
            int r = (x * 255) / size.width();
            int g = (y * 255) / size.height();
            int b = 128 + int(100 * std::sin(x / 40.0) * std::cos(y / 30.0));

            // Black strokes of a drawing
            if (x % 97 < 3 || y % 61 < 2)
            {
                r = g = b = 0;
            }

            if (gray)
            {
                r = g = b = (r + g + b) / 3;
            }

            const int noise = int(generator.bounded(5)) - 2;
            line[3 * x + 0] = uchar(qBound(0, r + noise, 255));
            line[3 * x + 1] = uchar(qBound(0, g + noise, 255));
            line[3 * x + 2] = uchar(qBound(0, b + noise, 255));
        }
    }

    return image;
}

QImage DeferredImageTest::createMask(QSize size)
{
    QImage mask(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y)
    {
        uchar* line = mask.scanLine(y);
        for (int x = 0; x < size.width(); ++x)
        {
            // Transparent background, opaque strokes and a soft gradient
            uchar value = (x % 97 < 3 || y % 61 < 2) ? 255 : 0;
            if (x > size.width() / 2)
            {
                value = uchar((y * 255) / size.height());
            }
            line[x] = value;
        }
    }
    return mask;
}

QByteArray DeferredImageTest::encodeAscii85(const QByteArray& data)
{
    QByteArray result;
    int column = 0;

    auto append = [&](char c)
    {
        result.append(c);
        if (++column == 75)
        {
            result.append("\r\n");
            column = 0;
        }
    };

    for (qsizetype i = 0; i < data.size(); i += 4)
    {
        const qsizetype count = qMin<qsizetype>(4, data.size() - i);
        uint32_t value = 0;
        for (qsizetype j = 0; j < 4; ++j)
        {
            value = (value << 8) | (j < count ? uchar(data[i + j]) : 0);
        }

        if (count == 4 && value == 0)
        {
            append('z');
            continue;
        }

        char encoded[5];
        for (int j = 4; j >= 0; --j)
        {
            encoded[j] = char(value % 85 + 33);
            value /= 85;
        }

        for (qsizetype j = 0; j < count + 1; ++j)
        {
            append(encoded[j]);
        }
    }

    result.append("~>");
    return result;
}

pdf::PDFDocument DeferredImageTest::createDocument(const ImageOptions& options)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 400, 300));

    pdf::PDFImage::ImageEncodeOptions encodeOptions;
    encodeOptions.compression = pdf::PDFImage::ImageCompression::JPEG;
    encodeOptions.colorMode = options.gray ? pdf::PDFImage::ImageColorMode::Grayscale : pdf::PDFImage::ImageColorMode::Preserve;
    encodeOptions.jpegQuality = 90;
    pdf::PDFStream imageStream = pdf::PDFImage::createStreamFromImage(createColorImage(options.size, options.gray), encodeOptions);

    pdf::PDFDictionaryBuilder imageDictionary(*imageStream.getDictionary());

    if (options.softMask)
    {
        const QSize maskSize = options.softMaskSize.isValid() ? options.softMaskSize : options.size;
        QImage mask = createMask(maskSize);
        QByteArray samples;
        for (int y = 0; y < mask.height(); ++y)
        {
            samples.append(reinterpret_cast<const char*>(mask.constScanLine(y)), mask.width());
        }

        pdf::PDFDictionaryBuilder maskDictionary;
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(maskSize.width()));
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(maskSize.height()));
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceGray"));
        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));

        if (options.matte)
        {
            pdf::PDFArrayBuilder matte;
            matte.appendItem(pdf::PDFObject::createReal(1.0));
            matte.appendItem(pdf::PDFObject::createReal(1.0));
            matte.appendItem(pdf::PDFObject::createReal(1.0));
            maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Matte"), pdf::PDFObject::createArray(std::move(matte)));
        }

        QByteArray data;
        if (options.softMaskPredictor)
        {
            // Rows with PNG predictor "None" (each row starts with zero byte)
            QByteArray predicted;
            for (int y = 0; y < maskSize.height(); ++y)
            {
                predicted.append(char(0));
                predicted.append(samples.mid(qsizetype(y) * maskSize.width(), maskSize.width()));
            }
            data = pdf::PDFFlateDecodeFilter::compress(predicted);
            maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createName("FlateDecode"));

            pdf::PDFDictionaryBuilder parameters;
            parameters.addEntry(pdf::PDFInplaceOrMemoryString("Predictor"), pdf::PDFObject::createInteger(15));
            parameters.addEntry(pdf::PDFInplaceOrMemoryString("Columns"), pdf::PDFObject::createInteger(maskSize.width()));
            maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_DECODE_PARMS), pdf::PDFObject::createDictionary(std::move(parameters)));
        }
        else if (options.softMaskAscii85)
        {
            // AutoCAD writes [/FlateDecode /ASCII85Decode], i.e. compressed ASCII85 text
            data = pdf::PDFFlateDecodeFilter::compress(encodeAscii85(samples));
            pdf::PDFArrayBuilder filters;
            filters.appendItem(pdf::PDFObject::createName("FlateDecode"));
            filters.appendItem(pdf::PDFObject::createName("ASCII85Decode"));
            maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createArray(std::move(filters)));
        }
        else
        {
            data = pdf::PDFFlateDecodeFilter::compress(samples);
            maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createName("FlateDecode"));
        }

        maskDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(data.size()));
        pdf::PDFObjectReference maskRef = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(maskDictionary), std::move(data))));
        imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(maskRef));
    }

    if (options.decode)
    {
        pdf::PDFArrayBuilder decode;
        for (int i = 0; i < (options.gray ? 1 : 3); ++i)
        {
            decode.appendItem(pdf::PDFObject::createReal(1.0));
            decode.appendItem(pdf::PDFObject::createReal(0.0));
        }
        imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Decode"), pdf::PDFObject::createArray(std::move(decode)));
    }

    if (options.intent)
    {
        imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Intent"), pdf::PDFObject::createName("Saturation"));
    }

    // Filters as written by AutoCAD: [/FlateDecode /DCTDecode]
    QByteArray jpegData = *imageStream.getContent();
    QByteArray imageData = pdf::PDFFlateDecodeFilter::compress(jpegData);
    pdf::PDFArrayBuilder imageFilters;
    imageFilters.appendItem(pdf::PDFObject::createName("FlateDecode"));
    imageFilters.appendItem(pdf::PDFObject::createName("DCTDecode"));
    imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createArray(std::move(imageFilters)));
    imageDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(imageData.size()));

    pdf::PDFObjectReference imageRef = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(imageDictionary), std::move(imageData))));

    const QRectF& r = options.placement;
    QByteArray content = QString("q %1 0 0 %2 %3 %4 cm /Im1 Do Q").arg(r.width()).arg(r.height()).arg(r.left()).arg(r.top()).toLatin1();
    pdf::PDFDictionaryBuilder contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));
    pdf::PDFObjectReference contentRef = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(contentDict), std::move(content))));

    pdf::PDFDictionaryBuilder xObject;
    xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

    pdf::PDFDictionaryBuilder resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xObject)));

    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));
    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::move(pageUpdate)));

    return builder.build();
}

const pdf::PDFStream* DeferredImageTest::getImageStream(const pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    const pdf::PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    const pdf::PDFDictionary* xObjects = document.getDictionaryFromObject(resources->get("XObject"));
    return document.getObject(xObjects->get("Im1")).getStream();
}

QImage DeferredImageTest::referenceImage(const pdf::PDFDocument& document, const pdf::PDFCMS* cms)
{
    const pdf::PDFStream* stream = getImageStream(document);
    pdf::PDFColorSpacePointer colorSpace = pdf::PDFAbstractColorSpace::createColorSpace(nullptr, &document, nullptr, document.getObject(stream->getDictionary()->get("ColorSpace")));
    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFImage image = pdf::PDFImage::createImage(&document, stream, colorSpace, false, pdf::RenderingIntent::Perceptual, &reporter);
    return image.getImage(cms, &reporter, nullptr);
}

void DeferredImageTest::compile(const pdf::PDFDocument& document, const pdf::PDFCMS* cms, pdf::PDFRenderer::Features features, pdf::PDFPrecompiledPage* page)
{
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(&document), nullptr));
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFRenderer renderer(&document, &fontCache, nullptr, cms, &optionalContentActivity, features, meshQualitySettings);
    renderer.compile(page, 0);
}

QImage DeferredImageTest::renderReference(const pdf::PDFDocument& document, QSize size, const pdf::PDFCMS* cms, pdf::PDFRenderer::Features features)
{
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(&document), nullptr));
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFRenderer renderer(&document, &fontCache, nullptr, cms, &optionalContentActivity, features, meshQualitySettings);

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    renderer.render(&painter, QRectF(QPointF(0, 0), size), 0);
    painter.end();
    return image;
}

QImage DeferredImageTest::renderCompiled(const pdf::PDFPrecompiledPage& page, const pdf::PDFDocument& document, QSize size, pdf::PDFRenderer::Features features, pdf::PDFScaledImageCache* cache)
{
    const pdf::PDFPage* pdfPage = document.getCatalog()->getPage(0);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QTransform matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(pdfPage, QRectF(QPointF(0, 0), size));
    page.draw(&painter, pdfPage->getCropBox(), matrix, features, 1.0, cache);
    painter.end();
    return image;
}

double DeferredImageTest::meanDifference(const QImage& left, const QImage& right, int* maximum)
{
    const QImage a = left.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage b = right.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    Q_ASSERT(a.size() == b.size());

    qint64 sum = 0;
    int maximumDifference = 0;
    for (int y = 0; y < a.height(); ++y)
    {
        const QRgb* lineA = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* lineB = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
        {
            const int differences[4] = { qAbs(qRed(lineA[x]) - qRed(lineB[x])), qAbs(qGreen(lineA[x]) - qGreen(lineB[x])),
                                         qAbs(qBlue(lineA[x]) - qBlue(lineB[x])), qAbs(qAlpha(lineA[x]) - qAlpha(lineB[x])) };
            for (int difference : differences)
            {
                sum += difference;
                maximumDifference = qMax(maximumDifference, difference);
            }
        }
    }

    if (maximum)
    {
        *maximum = maximumDifference;
    }

    return double(sum) / (4.0 * a.width() * a.height());
}

void DeferredImageTest::test_ascii85_fast_path()
{
    const pdf::PDFStreamFilter* filter = pdf::PDFStreamFilterStorage::getFilter("ASCII85Decode");
    QVERIFY(filter);

    auto decode = [filter](const QByteArray& data)
    {
        return filter->apply(data, [](const pdf::PDFObject& object) -> const pdf::PDFObject& { return object; }, pdf::PDFObject(), nullptr);
    };

    // Known vectors
    QCOMPARE(decode("87cURD]i,\"Ebo80~>"), QByteArray("Hello World!"));
    QCOMPARE(decode("87cU RD]i,\n\"Ebo\r\n80~>"), QByteArray("Hello World!"));
    QCOMPARE(decode("z~>"), QByteArray(4, char(0)));
    QCOMPARE(decode("zz87cURD]i,\"Ebo80"), QByteArray(8, char(0)) + QByteArray("Hello World!"));
    QCOMPARE(decode(""), QByteArray());
    QCOMPARE(decode("~>"), QByteArray());

    // Random data round trip with all the group lengths, whitespace and zero groups
    QRandomGenerator generator(7);
    for (int length = 0; length < 64; ++length)
    {
        QByteArray data(length, 0);
        for (int i = 0; i < length; ++i)
        {
            data[i] = (i % 9 < 4 && length > 20) ? char(0) : char(generator.bounded(256));
        }
        QCOMPARE(decode(encodeAscii85(data)), data);
    }

    QByteArray large(1 << 20, 0);
    for (qsizetype i = 0; i < large.size(); ++i)
    {
        large[i] = char(generator.bounded(256));
    }
    QCOMPARE(decode(encodeAscii85(large)), large);
}

void DeferredImageTest::test_fill_rgb_buffer_8bit()
{
    pdf::PDFCMSManager cmsManager(nullptr);
    pdf::PDFCMSPointer genericCms = cmsManager.getCurrentCMS();
    cmsManager.setSettings(cmsManager.getDefaultSettings());
    pdf::PDFCMSPointer littleCms = cmsManager.getCurrentCMS();
    pdf::PDFRenderErrorReporterDummy reporter;

    std::vector<unsigned char> colors;
    for (int r = 0; r < 256; r += 3)
    {
        for (int g = 0; g < 256; g += 5)
        {
            for (int b = 0; b < 256; b += 7)
            {
                colors.push_back(uchar(r));
                colors.push_back(uchar(g));
                colors.push_back(uchar(b));
            }
        }
    }
    const size_t pixelCount = colors.size() / 3;

    pdf::PDFDeviceRGBColorSpace rgb;
    pdf::PDFDeviceGrayColorSpace gray;

    for (const pdf::PDFCMSPointer& cms : { genericCms, littleCms })
    {
        std::vector<unsigned char> expected(pixelCount * 3, 0);
        std::vector<unsigned char> actual(pixelCount * 3, 0);

        rgb.fillRGBBuffer(pdf::PDFCMS::convert8BitColors(colors.data(), colors.size()), expected.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        rgb.fillRGBBuffer8(colors.data(), pixelCount, actual.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        QVERIFY(expected == actual);

        std::vector<unsigned char> grayValues(256, 0);
        for (int i = 0; i < 256; ++i)
        {
            grayValues[i] = uchar(i);
        }
        std::vector<unsigned char> expectedGray(256 * 3, 0);
        std::vector<unsigned char> actualGray(256 * 3, 0);
        gray.fillRGBBuffer(pdf::PDFCMS::convert8BitColors(grayValues.data(), grayValues.size()), expectedGray.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        gray.fillRGBBuffer8(grayValues.data(), grayValues.size(), actualGray.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        QVERIFY(expectedGray == actualGray);
    }
}

void DeferredImageTest::test_fast_image_path_identical()
{
    // 8-bit image with an identity decode array goes through the generic bit reader,
    // without it through the fast path - both must give the same pixels.
    pdf::PDFCMSManager cmsManager(nullptr);
    pdf::PDFCMSPointer genericCms = cmsManager.getCurrentCMS();
    cmsManager.setSettings(cmsManager.getDefaultSettings());
    pdf::PDFCMSPointer littleCms = cmsManager.getCurrentCMS();
    pdf::PDFRenderErrorReporterDummy reporter;

    const unsigned int width = 333;
    const unsigned int height = 77;
    QByteArray rgbData(width * height * 3, 0);
    QByteArray grayData(width * height, 0);
    QRandomGenerator generator(3);
    for (qsizetype i = 0; i < rgbData.size(); ++i)
    {
        rgbData[i] = char(generator.bounded(256));
    }
    for (qsizetype i = 0; i < grayData.size(); ++i)
    {
        grayData[i] = char(generator.bounded(256));
    }

    pdf::PDFDeviceRGBColorSpace rgb;
    for (const pdf::PDFCMSPointer& cms : { genericCms, littleCms })
    {
        for (bool softMask : { false, true })
        {
            const pdf::PDFImageData::MaskingType maskingType = softMask ? pdf::PDFImageData::MaskingType::SoftMask : pdf::PDFImageData::MaskingType::None;
            pdf::PDFImageData softMaskData(1, 8, width, height, width, pdf::PDFImageData::MaskingType::None, grayData, { }, { }, { });
            pdf::PDFImageData fastData(3, 8, width, height, width * 3, maskingType, rgbData, { }, { }, { });
            pdf::PDFImageData genericData(3, 8, width, height, width * 3, maskingType, rgbData, { }, { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 }, { });

            QImage fastImage = rgb.getImage(fastData, softMaskData, cms.data(), pdf::RenderingIntent::Perceptual, &reporter, nullptr);
            QImage genericImage = rgb.getImage(genericData, softMaskData, cms.data(), pdf::RenderingIntent::Perceptual, &reporter, nullptr);
            QCOMPARE(fastImage.format(), genericImage.format());
            QCOMPARE(fastImage, genericImage);
        }
    }

    // Truncated data are still reported as an error
    pdf::PDFImageData truncated(3, 8, width, height, width * 3, pdf::PDFImageData::MaskingType::None, rgbData.left(rgbData.size() - 1), { }, { }, { });
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, rgb.getImage(truncated, pdf::PDFImageData(), genericCms.data(), pdf::RenderingIntent::Perceptual, &reporter, nullptr));
}

void DeferredImageTest::test_color_table()
{
    pdf::PDFCMSManager cmsManager(nullptr);
    pdf::PDFCMSPointer genericCms = cmsManager.getCurrentCMS();
    cmsManager.setSettings(cmsManager.getDefaultSettings());
    pdf::PDFCMSPointer littleCms = cmsManager.getCurrentCMS();
    pdf::PDFRenderErrorReporterDummy reporter;

    pdf::PDFDeviceRGBColorSpace rgb;
    pdf::PDFDeviceGrayColorSpace gray;
    pdf::PDFDeviceCMYKColorSpace cmyk;

    QVERIFY(!pdf::PDFImageColorTable::create(&cmyk, genericCms.data(), pdf::RenderingIntent::Perceptual, &reporter));

    auto genericRgbTable = pdf::PDFImageColorTable::create(&rgb, genericCms.data(), pdf::RenderingIntent::Perceptual, &reporter);
    QVERIFY(genericRgbTable);
    QVERIFY(genericRgbTable->isIdentity());

    QRandomGenerator generator(11);
    std::vector<unsigned char> colors(3 * 100000, 0);
    for (unsigned char& color : colors)
    {
        color = uchar(generator.bounded(256));
    }
    // All the extremes
    for (int i = 0; i < 8; ++i)
    {
        colors[3 * i + 0] = (i & 1) ? 255 : 0;
        colors[3 * i + 1] = (i & 2) ? 255 : 0;
        colors[3 * i + 2] = (i & 4) ? 255 : 0;
    }
    const size_t pixelCount = colors.size() / 3;

    for (const pdf::PDFCMSPointer& cms : { genericCms, littleCms })
    {
        auto table = pdf::PDFImageColorTable::create(&rgb, cms.data(), pdf::RenderingIntent::Perceptual, &reporter);
        QVERIFY(table);

        std::vector<unsigned char> expected(pixelCount * 3, 0);
        std::vector<unsigned char> actual(pixelCount * 3, 0);
        rgb.fillRGBBuffer8(colors.data(), pixelCount, expected.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        table->convert(colors.data(), pixelCount, actual.data());

        int maximumDifference = 0;
        for (size_t i = 0; i < expected.size(); ++i)
        {
            maximumDifference = qMax(maximumDifference, qAbs(int(expected[i]) - int(actual[i])));
        }
        QVERIFY2(maximumDifference <= 2, qPrintable(QString("Maximal difference %1").arg(maximumDifference)));

        // Gray values are converted exactly
        auto grayTable = pdf::PDFImageColorTable::create(&gray, cms.data(), pdf::RenderingIntent::Perceptual, &reporter);
        QVERIFY(grayTable);
        std::vector<unsigned char> grayValues(256, 0);
        for (int i = 0; i < 256; ++i)
        {
            grayValues[i] = uchar(i);
        }
        std::vector<unsigned char> expectedGray(256 * 3, 0);
        std::vector<unsigned char> actualGray(256 * 3, 0);
        gray.fillRGBBuffer8(grayValues.data(), grayValues.size(), expectedGray.data(), pdf::RenderingIntent::Perceptual, cms.data(), &reporter);
        grayTable->convert(grayValues.data(), grayValues.size(), actualGray.data());
        QVERIFY(expectedGray == actualGray);
    }
}

void DeferredImageTest::test_deferred_image_conditions()
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFDeviceRGBColorSpace rgb;
    pdf::PDFDeviceGrayColorSpace gray;
    auto rgbTable = pdf::PDFImageColorTable::create(&rgb, &cms, pdf::RenderingIntent::Perceptual, &reporter);
    auto grayTable = pdf::PDFImageColorTable::create(&gray, &cms, pdf::RenderingIntent::Perceptual, &reporter);

    auto isDeferred = [&](const ImageOptions& options)
    {
        pdf::PDFDocument document = createDocument(options);
        const pdf::PDFAbstractColorSpace* colorSpace = options.gray ? static_cast<const pdf::PDFAbstractColorSpace*>(&gray) : &rgb;
        auto table = options.gray ? grayTable : rgbTable;
        return pdf::PDFDeferredImage::create(&document, getImageStream(document), colorSpace, table, QTransform(288, 0, 0, 216, 0, 0), &reporter) != nullptr;
    };

    ImageOptions options;
    QVERIFY(isDeferred(options));

    ImageOptions noMask;
    noMask.softMask = false;
    QVERIFY(isDeferred(noMask));

    ImageOptions grayImage;
    grayImage.gray = true;
    QVERIFY(isDeferred(grayImage));

    ImageOptions predictor;
    predictor.softMaskPredictor = true;
    QVERIFY(isDeferred(predictor));

    ImageOptions small;
    small.size = QSize(1000, 1000);
    QVERIFY(!isDeferred(small));

    ImageOptions decode;
    decode.decode = true;
    QVERIFY(!isDeferred(decode));

    ImageOptions matte;
    matte.matte = true;
    QVERIFY(!isDeferred(matte));

    ImageOptions maskSize;
    maskSize.softMaskSize = QSize(600, 450);
    QVERIFY(!isDeferred(maskSize));

    // Color space of other number of components than the JPEG
    {
        pdf::PDFDocument document = createDocument(options);
        QVERIFY(!pdf::PDFDeferredImage::create(&document, getImageStream(document), &gray, grayTable, QTransform(), &reporter));
    }

    // Preview resolution: 1200 px over 4 in is 300 ppi, so the preview is a half
    {
        pdf::PDFDocument document = createDocument(options);
        auto image = pdf::PDFDeferredImage::create(&document, getImageStream(document), &rgb, rgbTable, QTransform(288, 0, 0, 216, 0, 0), &reporter);
        QVERIFY(image);
        QCOMPARE(image->getSize(), QSize(1200, 900));
        QCOMPARE(image->getPreviewScaleDenominator(), 2u);
        QCOMPARE(image->getScaledSize(8), QSize(150, 113));
        QCOMPARE(image->getScaleDenominator(150, 112), 8u);
        QCOMPARE(image->getScaleDenominator(151, 112), 4u);
        QCOMPARE(image->getScaleDenominator(601, 100), 1u);
        QCOMPARE(image->getScaleDenominator(qInf(), 100), 1u);
        QVERIFY(image->hasSoftMask());
        QVERIFY(image->getMemoryConsumptionEstimate() < 1200 * 900);
    }
}

void DeferredImageTest::test_deferred_image_full_resolution_identical()
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFDeviceRGBColorSpace rgb;
    pdf::PDFDeviceGrayColorSpace gray;

    for (int variant = 0; variant < 4; ++variant)
    {
        ImageOptions options;
        options.softMask = variant != 1;
        options.gray = variant == 2;
        options.softMaskPredictor = variant == 3;

        pdf::PDFDocument document = createDocument(options);
        const pdf::PDFAbstractColorSpace* colorSpace = options.gray ? static_cast<const pdf::PDFAbstractColorSpace*>(&gray) : &rgb;
        auto table = pdf::PDFImageColorTable::create(colorSpace, &cms, pdf::RenderingIntent::Perceptual, &reporter);
        auto image = pdf::PDFDeferredImage::create(&document, getImageStream(document), colorSpace, table, QTransform(288, 0, 0, 216, 0, 0), &reporter);
        QVERIFY(image);

        const QImage expected = referenceImage(document, &cms);
        const QImage actual = image->decode(1, &reporter, true);
        QCOMPARE(actual.size(), expected.size());

        int maximum = 0;
        const double mean = meanDifference(expected, actual, &maximum);
        QVERIFY2(maximum == 0, qPrintable(QString("Variant %1: mean %2, maximum %3").arg(variant).arg(mean).arg(maximum)));
        QCOMPARE(image->getImage(1).size(), expected.size());
    }
}

void DeferredImageTest::test_deferred_image_scaled()
{
    pdf::PDFCMSGeneric cms;
    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFDeviceRGBColorSpace rgb;
    auto table = pdf::PDFImageColorTable::create(&rgb, &cms, pdf::RenderingIntent::Perceptual, &reporter);

    ImageOptions options;
    pdf::PDFDocument document = createDocument(options);
    auto image = pdf::PDFDeferredImage::create(&document, getImageStream(document), &rgb, table, QTransform(288, 0, 0, 216, 0, 0), &reporter);
    QVERIFY(image);

    const QImage full = referenceImage(document, &cms).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (unsigned int scaleDenominator : { 2u, 4u, 8u })
    {
        const QImage scaled = image->decode(scaleDenominator, &reporter, scaleDenominator != 4);
        QCOMPARE(scaled.size(), image->getScaledSize(scaleDenominator));

        // Compared with the full image averaged to the same size
        const QImage expected = full.scaled(scaled.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const double mean = meanDifference(expected, scaled);
        QVERIFY2(mean < 6.0, qPrintable(QString("Scale 1/%1: mean difference %2").arg(scaleDenominator).arg(mean)));
    }

    // Color convertor is applied to the decoded images
    pdf::PDFColorConvertor convertor;
    convertor.setMode(pdf::PDFColorConvertor::Mode::InvertedColors);
    image->setColorConvertor(convertor);
    const QImage inverted = image->decode(4, &reporter, true);
    image->setColorConvertor(pdf::PDFColorConvertor());
    QVERIFY(inverted != image->decode(4, &reporter, true));
}

void DeferredImageTest::test_precompiled_page_preview()
{
    pdf::PDFCMSGeneric cms;
    const pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures() | pdf::PDFRenderer::SmoothImages;

    ImageOptions options;
    pdf::PDFDocument document = createDocument(options);

    pdf::PDFPrecompiledPage page;
    compile(document, &cms, features, &page);
    QVERIFY(page.getErrors().isEmpty());

    // The page holds the preview (600 x 450, a half) and the compressed data
    QVERIFY(page.getMemoryConsumptionEstimate() < 600 * 450 * 4 + 400000);

    // Drawn page at the size of the page (72 dpi) is the same as the page drawn directly
    for (QSize size : { QSize(400, 300), QSize(800, 600) })
    {
        const QImage expected = renderReference(document, size, &cms, features);
        const QImage actual = renderCompiled(page, document, size, features, nullptr);
        int maximum = 0;
        const double mean = meanDifference(expected, actual, &maximum);
        QVERIFY2(mean < 3.0, qPrintable(QString("Size %1: mean difference %2, maximum %3").arg(size.width()).arg(mean).arg(maximum)));
    }

    // Snapped image is the full image
    QCOMPARE(page.getSnapInfo()->getSnapImages().size(), size_t(1));
    QCOMPARE(page.getSnapInfo()->getSnapImages().front().getImage().size(), QSize(1200, 900));
}

void DeferredImageTest::test_precompiled_page_zoom()
{
    pdf::PDFCMSGeneric cms;
    const pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures() | pdf::PDFRenderer::SmoothImages;

    ImageOptions options;
    pdf::PDFDocument document = createDocument(options);

    pdf::PDFPrecompiledPage page;
    compile(document, &cms, features, &page);

    // Page drawn at 400 % - the image (4 in) is 1600 px wide, so it is decoded at the full resolution
    pdf::PDFScaledImageCache cache;
    size_t imageCount = 0;
    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);
        const QSize size(1600, 1200);
        const QImage expected = renderReference(document, size, &cms, features);
        const QImage actual = renderCompiled(page, document, size, features, &cache);
        int maximum = 0;
        const double mean = meanDifference(expected, actual, &maximum);
        QVERIFY2(mean < 1.0, qPrintable(QString("Mean difference %1, maximum %2").arg(mean).arg(maximum)));
        imageCount = cache.getImageCount();
        QVERIFY(imageCount >= 1);
        QVERIFY(cache.getMemoryConsumptionEstimate() >= 1200 * 900 * 4);
    }

    // The decoded image is memoized, and discarded, when it is not used anymore
    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);
        renderCompiled(page, document, QSize(1600, 1200), features, &cache);
        QCOMPARE(cache.getImageCount(), imageCount);
    }
    for (int i = 0; i < 4; ++i)
    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);
        renderCompiled(page, document, QSize(400, 300), features, &cache);
    }

    // Only the downscaled preview is held now
    QCOMPARE(cache.getImageCount(), size_t(1));
    QVERIFY(cache.getMemoryConsumptionEstimate() < 400 * 300 * 4);
}

void DeferredImageTest::test_precompiled_page_vector_device()
{
    // A vector device (picture, printer, pdf writer) gets the full resolution
    pdf::PDFCMSGeneric cms;
    const pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures();

    ImageOptions options;
    pdf::PDFDocument document = createDocument(options);

    pdf::PDFPrecompiledPage page;
    compile(document, &cms, features, &page);

    ImageRecordingPaintDevice device;
    {
        QPainter painter(&device);
        const pdf::PDFPage* pdfPage = document.getCatalog()->getPage(0);
        page.draw(&painter, pdfPage->getCropBox(), pdf::PDFRenderer::createPagePointToDevicePointMatrix(pdfPage, QRectF(0, 0, 400, 300)), features, 1.0, nullptr);
    }

    QCOMPARE(device.getImageSizes().size(), size_t(1));
    QCOMPARE(device.getImageSizes().front(), QSize(1200, 900));
}

void DeferredImageTest::test_heavy_image_sheet()
{
    // Synthetic plan sheet with the traits of an AutoCAD plot (see UnitTests/data/generate_heavy_image_sheet.py)
    const QString fileName = QString(PDF4QT_UNIT_TESTS_DATA_DIRECTORY) + "/heavy-image-sheet.pdf";
    if (!QFile::exists(fileName))
    {
        QSKIP("Test file heavy-image-sheet.pdf not found");
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setSettings(cmsManager.getDefaultSettings());
    cmsManager.setDocument(&document);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();

    const pdf::PDFRenderer::Features features = pdf::PDFRenderer::getDefaultFeatures() | pdf::PDFRenderer::SmoothImages;

    QElapsedTimer timer;
    timer.start();
    pdf::PDFPrecompiledPage page;
    compile(document, cms.data(), features, &page);
    const qint64 compileTime = timer.elapsed();
    QVERIFY(page.getErrors().isEmpty());

    // All 49 images are deferred (the compiled page holds the previews at 150 ppi only)
    QVERIFY2(page.getMemoryConsumptionEstimate() < 128 * 1024 * 1024, qPrintable(QString("Memory %1 MB").arg(page.getMemoryConsumptionEstimate() / 1048576)));
    qInfo("Compile time %lld ms, compiled page %lld MB", compileTime, page.getMemoryConsumptionEstimate() / 1048576);

    // Page fit into a window and drawn directly agree
    const QSize size(1296, 864);
    timer.restart();
    const QImage actual = renderCompiled(page, document, size, features, nullptr);
    qInfo("First paint %lld ms", timer.elapsed());
    const QImage expected = renderReference(document, size, cms.data(), features);
    int maximum = 0;
    const double mean = meanDifference(expected, actual, &maximum);
    QVERIFY2(mean < 3.0, qPrintable(QString("Mean difference %1, maximum %2").arg(mean).arg(maximum)));
}

QTEST_MAIN(DeferredImageTest)

#include "tst_deferredimagetest.moc"
