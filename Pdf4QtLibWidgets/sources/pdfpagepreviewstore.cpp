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


#include "pdfpagepreviewstore.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocument.h"
#include "pdffont.h"
#include "pdfpainter.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtMath>

#include "pdfdbgheap.h"

namespace pdf
{

PDFPagePreviewStore::PDFPagePreviewStore(PDFDrawWidgetProxy* proxy) :
    BaseClass(proxy),
    m_proxy(proxy),
    m_decodedPreviews(8)
{

}

PDFPagePreviewStore::~PDFPagePreviewStore()
{
    stop(true);
}

bool PDFPagePreviewStore::isEnabled()
{
    static const bool isEnabled = !qEnvironmentVariableIsSet("PDF4QT_DISABLE_PAGE_PREVIEWS");
    return isEnabled;
}

bool PDFPagePreviewStore::isOperationCancelled() const
{
    // This function is called by the worker thread only (often, while a page
    // is compiled), so the worker is paused also in the middle of a page.
    if (m_paused.load(std::memory_order_relaxed))
    {
        waitWhilePaused();
    }

    return m_stopRequested.load(std::memory_order_relaxed);
}

void PDFPagePreviewStore::waitWhilePaused() const
{
    QMutexLocker locker(&m_mutex);
    while (m_paused.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed))
    {
        m_waitCondition.wait(&m_mutex);
    }
}

void PDFPagePreviewStore::setPaused(bool paused)
{
    if (m_paused.load(std::memory_order_relaxed) != paused)
    {
        QMutexLocker locker(&m_mutex);
        m_paused.store(paused, std::memory_order_relaxed);
        m_waitCondition.wakeAll();
    }
}

void PDFPagePreviewStore::setDocumentPointer(PDFDocumentPointer document)
{
    Q_ASSERT(!m_thread);
    m_documentPointer = std::move(document);
}

void PDFPagePreviewStore::start(bool useDiskCache)
{
    if (!isEnabled() || m_thread)
    {
        return;
    }

    const PDFDocument* document = m_proxy->getDocument();
    if (!document || m_documentPointer.data() != document)
    {
        // No document, or the document is not kept alive by the shared pointer
        return;
    }

    const size_t pageCount = document->getCatalog()->getPageCount();

    bool isCleared = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_states.size() != pageCount)
        {
            isCleared = true;
            m_states.assign(pageCount, PageState::Waiting);
            m_previews.clear();
            m_memorySize = 0;
        }
    }

    // If the previews were not cleared, the page contents are the same as before,
    // so the previews on the disk are still used.
    const QString retainedDirectory = isCleared ? QString() : m_context.directory;

    m_context = Context();
    m_context.document = document;
    m_context.fontCache = m_proxy->getFontCache();
    m_context.iccProfileCache = m_proxy->getIccProfileCache();
    m_context.optionalContentActivity = m_proxy->getOptionalContentActivity();
    m_context.cms = m_proxy->getCMSManager()->getCurrentCMS();
    m_context.cmsSettings = m_proxy->getCMSManager()->getSettings();
    m_context.features = m_proxy->getFeatures();
    m_context.meshQualitySettings = m_proxy->getMeshQualitySettings();
    m_context.paperColor = m_proxy->getPaperColor();
    m_context.isDiskWritable = useDiskCache;

    if (isCleared)
    {
        static const bool isDiskCacheDisabled = qEnvironmentVariableIsSet("PDF4QT_DISABLE_PAGE_PREVIEW_DISK_CACHE");

        if (useDiskCache && !isDiskCacheDisabled && !QStandardPaths::isTestModeEnabled())
        {
            const QString root = getCacheRootDirectory();
            const QString name = getDocumentDirectoryName(m_context);

            if (!root.isEmpty() && !name.isEmpty())
            {
                m_context.directory = root + QLatin1Char('/') + name;
            }
        }
    }
    else
    {
        m_context.directory = retainedDirectory;
    }

    m_stopRequested.store(false, std::memory_order_relaxed);
    m_paused.store(false, std::memory_order_relaxed);
    m_thread = QThread::create([this]() { runWorker(); });
    m_thread->setObjectName(QStringLiteral("PDFPagePreviewStore"));
    m_thread->start(QThread::LowestPriority);
}

void PDFPagePreviewStore::stop(bool clear)
{
    if (m_thread)
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopRequested.store(true, std::memory_order_relaxed);
            m_waitCondition.wakeAll();
        }

        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    m_decodedPreviews.clear();
    m_pagesMissingOnDisk.clear();

    {
        QMutexLocker locker(&m_mutex);
        m_pageImages.clear();
    }

    // Document can be destroyed, when the store is stopped
    m_context.document = nullptr;
    m_context.fontCache = nullptr;
    m_context.iccProfileCache = nullptr;
    m_context.optionalContentActivity = nullptr;
    m_context.cms.reset();

    if (clear)
    {
        QMutexLocker locker(&m_mutex);
        m_states.clear();
        m_previews.clear();
        m_memorySize = 0;
        m_context.directory.clear();
    }
}

PDFPagePreviewStore::Preview PDFPagePreviewStore::getPreview(PDFInteger pageIndex)
{
    if (!m_context.document)
    {
        return Preview();
    }

    if (const Preview* preview = m_decodedPreviews.object(pageIndex))
    {
        return *preview;
    }

    QByteArray data;
    bool isReady = false;
    {
        QMutexLocker locker(&m_mutex);
        if (pageIndex < 0 || size_t(pageIndex) >= m_states.size())
        {
            return Preview();
        }

        isReady = m_states[pageIndex] == PageState::Ready;

        auto it = m_previews.find(pageIndex);
        if (it != m_previews.end())
        {
            data = it->second;
        }
    }

    // Preview from the disk is used also when the worker thread did not get
    // to the page yet (the document has just been opened again).
    const bool isSearchedOnDisk = !m_context.directory.isEmpty() && !m_pagesMissingOnDisk.count(pageIndex);
    if (!isReady && !isSearchedOnDisk)
    {
        return Preview();
    }

    const PDFPage* page = m_context.document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return Preview();
    }

    if (data.isEmpty() && !m_context.directory.isEmpty() && (isReady || isSearchedOnDisk))
    {
        QFile file(m_context.directory + QLatin1Char('/') + getPageFileName(m_context.document, page, pageIndex));
        if (file.open(QFile::ReadOnly))
        {
            data = file.readAll();
        }
    }

    if (data.isEmpty() && !isReady)
    {
        // Worker thread signals, when the preview is created
        m_pagesMissingOnDisk.insert(pageIndex);
        return Preview();
    }

    Preview preview;
    QSize imageSize;
    preview.pageToImage = createPageToImageMatrix(page, &imageSize);

    if (data.isEmpty() || !preview.image.loadFromData(data) || preview.image.size() != imageSize)
    {
        // Preview is damaged
        m_pagesMissingOnDisk.insert(pageIndex);

        QMutexLocker locker(&m_mutex);
        if (size_t(pageIndex) < m_states.size())
        {
            m_states[pageIndex] = PageState::Failed;
        }
        return Preview();
    }

    m_decodedPreviews.insert(pageIndex, new Preview(preview));
    return preview;
}

void PDFPagePreviewStore::addPageImage(PDFInteger pageIndex, const QImage& image, const QTransform& pageToImage)
{
    QMutexLocker locker(&m_mutex);

    if (!m_thread || pageIndex < 0 || size_t(pageIndex) >= m_states.size() || m_states[pageIndex] != PageState::Waiting ||
        m_pageImages.size() >= MAXIMAL_PAGE_IMAGE_COUNT)
    {
        return;
    }

    PageImage pageImage;
    pageImage.pageIndex = pageIndex;
    pageImage.image = image;
    pageImage.pageToImage = pageToImage;
    m_pageImages.push_back(std::move(pageImage));
}

void PDFPagePreviewStore::processPageImages()
{
    std::vector<PageImage> pageImages;
    {
        QMutexLocker locker(&m_mutex);
        pageImages = std::move(m_pageImages);
        m_pageImages.clear();
    }

    for (const PageImage& pageImage : pageImages)
    {
        const PDFInteger pageIndex = pageImage.pageIndex;
        const PDFPage* page = m_context.document->getCatalog()->getPage(pageIndex);

        {
            QMutexLocker locker(&m_mutex);
            if (!page || size_t(pageIndex) >= m_states.size() || m_states[pageIndex] != PageState::Waiting)
            {
                continue;
            }
        }

        const QString fileName = getPreviewFileName(page, pageIndex);
        PageState state = PageState::Ready;

        if (fileName.isEmpty() || !QFileInfo::exists(fileName))
        {
            bool isInvertible = false;
            const QTransform imageToPage = pageImage.pageToImage.inverted(&isInvertible);

            QSize imageSize;
            const QTransform pageToPreview = createPageToImageMatrix(page, &imageSize);
            const QTransform imageToPreview = imageToPage * pageToPreview;

            // Image of a much lower resolution is not used, the preview would be too blurry
            const qreal scale = qSqrt(qAbs(imageToPreview.determinant()));
            if (!isInvertible || scale > 2.0)
            {
                continue;
            }

            QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
            if (image.isNull())
            {
                continue;
            }
            image.fill(m_context.paperColor);

            {
                QPainter painter(&image);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.setWorldTransform(imageToPreview);
                painter.drawImage(QPointF(0.0, 0.0), pageImage.image);
            }

            state = storePreview(pageIndex, fileName, image);
        }

        {
            QMutexLocker locker(&m_mutex);
            if (size_t(pageIndex) < m_states.size())
            {
                m_states[pageIndex] = state;
            }
        }

        if (state == PageState::Ready)
        {
            Q_EMIT previewReady(pageIndex);
        }
    }
}

QString PDFPagePreviewStore::getPreviewFileName(const PDFPage* page, PDFInteger pageIndex) const
{
    if (m_context.directory.isEmpty())
    {
        return QString();
    }

    return m_context.directory + QLatin1Char('/') + getPageFileName(m_context.document, page, pageIndex);
}

void PDFPagePreviewStore::setCurrentPage(PDFInteger pageIndex)
{
    QMutexLocker locker(&m_mutex);
    m_currentPage = pageIndex;
}

void PDFPagePreviewStore::runWorker()
{
    if (!m_context.directory.isEmpty() && m_context.isDiskWritable)
    {
        if (QDir().mkpath(m_context.directory))
        {
            // Marker of the last use of the previews of this document
            QFile marker(m_context.directory + QStringLiteral("/used"));
            if (marker.open(QFile::WriteOnly | QFile::Truncate))
            {
                marker.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()));
                marker.close();
            }

            pruneDiskCache();
        }
    }

    while (!isOperationCancelled())
    {
        processPageImages();

        const PDFInteger pageIndex = pickPage();
        if (pageIndex < 0)
        {
            break;
        }

        PageState state = PageState::Failed;

        try
        {
            state = createPreview(pageIndex);
        }
        catch (const std::exception&)
        {
            state = PageState::Failed;
        }

        if (isOperationCancelled())
        {
            break;
        }

        {
            QMutexLocker locker(&m_mutex);
            if (size_t(pageIndex) < m_states.size())
            {
                m_states[pageIndex] = state;
            }
        }

        if (state == PageState::Ready)
        {
            Q_EMIT previewReady(pageIndex);
        }
    }
}

PDFInteger PDFPagePreviewStore::pickPage()
{
    QMutexLocker locker(&m_mutex);

    // Nearest page to the current page, which has no preview. Current page is
    // the last one - it is compiled and rasterized by the page view right now.
    const PDFInteger pageCount = PDFInteger(m_states.size());
    for (PDFInteger distance = 1; distance <= pageCount; ++distance)
    {
        for (const PDFInteger pageIndex : { m_currentPage + distance, m_currentPage - distance })
        {
            if (pageIndex >= 0 && pageIndex < pageCount && m_states[pageIndex] == PageState::Waiting)
            {
                return pageIndex;
            }
        }
    }

    if (m_currentPage >= 0 && m_currentPage < pageCount && m_states[m_currentPage] == PageState::Waiting)
    {
        return m_currentPage;
    }

    return -1;
}

PDFPagePreviewStore::PageState PDFPagePreviewStore::createPreview(PDFInteger pageIndex)
{
    const PDFPage* page = m_context.document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return PageState::Failed;
    }

    const QString fileName = getPreviewFileName(page, pageIndex);
    if (!fileName.isEmpty() && QFileInfo::exists(fileName))
    {
        // Preview has been created, when the document was opened last time
        return PageState::Ready;
    }

    const bool isWrittenToDisk = !fileName.isEmpty() && m_context.isDiskWritable;
    if (!isWrittenToDisk)
    {
        QMutexLocker locker(&m_mutex);
        if (m_memorySize >= MAXIMAL_MEMORY_SIZE)
        {
            return PageState::Failed;
        }
    }

    QSize imageSize;
    const QTransform pageToImage = createPageToImageMatrix(page, &imageSize);

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        return PageState::Failed;
    }
    image.fill(m_context.paperColor);

    {
        PDFFontCacheShrinkGuard guard(m_context.fontCache, this);

        PDFRenderer renderer(m_context.document, m_context.fontCache, m_context.iccProfileCache, m_context.cms.data(),
                             m_context.optionalContentActivity, m_context.features, m_context.meshQualitySettings);
        renderer.setOperationControl(this);

        PDFPrecompiledPage compiledPage;
        renderer.compile(&compiledPage, pageIndex);

        if (isOperationCancelled())
        {
            return PageState::Waiting;
        }

        if (compiledPage.isValid())
        {
            compiledPage.drawParallel(&image, page->getCropBox(), pageToImage, m_context.features, 1.0, 1, &m_stopRequested);
        }
    }

    if (isOperationCancelled())
    {
        return PageState::Waiting;
    }

    return storePreview(pageIndex, fileName, image);
}

PDFPagePreviewStore::PageState PDFPagePreviewStore::storePreview(PDFInteger pageIndex, const QString& fileName, const QImage& image)
{
    const bool isWrittenToDisk = !fileName.isEmpty() && m_context.isDiskWritable;

    // Preview is a rough image, so the lossy compression is used - it is smaller
    // and it is decoded faster (the preview is decoded by the main thread).
    const QImage opaqueImage = image.convertToFormat(QImage::Format_RGB32);
    QByteArray data;

    for (const char* format : { "JPG", "PNG" })
    {
        data.clear();
        QBuffer buffer(&data);
        buffer.open(QBuffer::WriteOnly);
        if (opaqueImage.save(&buffer, format, 85) && !data.isEmpty())
        {
            break;
        }
        data.clear();
    }

    if (data.isEmpty())
    {
        return PageState::Failed;
    }

    if (isWrittenToDisk)
    {
        QSaveFile file(fileName);
        if (file.open(QFile::WriteOnly) && file.write(data) == data.size() && file.commit())
        {
            return PageState::Ready;
        }
    }

    QMutexLocker locker(&m_mutex);
    m_memorySize += data.size();
    m_previews[pageIndex] = std::move(data);
    return PageState::Ready;
}

void PDFPagePreviewStore::pruneDiskCache() const
{
    struct DirectoryInfo
    {
        QString path;
        QDateTime lastUsed;
        qint64 size = 0;
    };

    const QString root = getCacheRootDirectory();
    std::vector<DirectoryInfo> directories;
    qint64 totalSize = 0;

    const QFileInfoList directoryInfos = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& directoryInfo : directoryInfos)
    {
        DirectoryInfo info;
        info.path = directoryInfo.absoluteFilePath();
        info.lastUsed = QFileInfo(info.path + QStringLiteral("/used")).lastModified();

        const QFileInfoList fileInfos = QDir(info.path).entryInfoList(QDir::Files);
        for (const QFileInfo& fileInfo : fileInfos)
        {
            info.size += fileInfo.size();
        }

        totalSize += info.size;
        directories.push_back(std::move(info));
    }

    std::sort(directories.begin(), directories.end(), [](const DirectoryInfo& left, const DirectoryInfo& right) { return left.lastUsed < right.lastUsed; });

    const QString currentDirectory = QFileInfo(m_context.directory).absoluteFilePath();
    for (const DirectoryInfo& info : directories)
    {
        if (totalSize <= MAXIMAL_DISK_SIZE || isOperationCancelled())
        {
            break;
        }

        if (info.path != currentDirectory && QDir(info.path).removeRecursively())
        {
            totalSize -= info.size;
        }
    }
}

QTransform PDFPagePreviewStore::createPageToImageMatrix(const PDFPage* page, QSize* imageSize)
{
    const QSizeF pageSize = page->getRotatedMediaBox().size();
    const PDFReal longerSide = qMax(pageSize.width(), pageSize.height());
    const PDFReal scale = longerSide > 0.0 ? qMin(PREVIEW_SIZE / longerSide, MAXIMAL_PREVIEW_SCALE) : 1.0;

    *imageSize = QSize(qMax(1, qCeil(pageSize.width() * scale)), qMax(1, qCeil(pageSize.height() * scale)));
    return PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0.0, 0.0), QSizeF(*imageSize)));
}

QString PDFPagePreviewStore::getCacheRootDirectory()
{
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cacheDirectory.isEmpty() ? QString() : cacheDirectory + QStringLiteral("/page-previews");
}

QString PDFPagePreviewStore::getDocumentDirectoryName(const Context& context)
{
    const PDFDocument* document = context.document;

    // Content of an encrypted document is never stored on the disk
    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (securityHandler && securityHandler->getMode() != EncryptionMode::None)
    {
        return QString();
    }

    // Document is identified by its identifier in the trailer dictionary
    PDFDocumentDataLoaderDecorator loader(document);
    const std::vector<QByteArray> identifiers = loader.readStringArrayFromDictionary(document->getTrailerDictionary(), "ID");
    if (identifiers.empty() || identifiers.front().isEmpty())
    {
        return QString();
    }

    // Everything, what changes the image of the page, is a part of the name
    QByteArray key;
    {
        QDataStream stream(&key, QDataStream::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << QByteArray("PDF4QT-PAGE-PREVIEW-1");

        for (const QByteArray& identifier : identifiers)
        {
            stream << identifier;
        }

        const PDFCMSSettings& cms = context.cmsSettings;
        const PDFMeshQualitySettings& mesh = context.meshQualitySettings;

        stream << qint64(document->getCatalog()->getPageCount());
        stream << qint32(PREVIEW_SIZE) << double(MAXIMAL_PREVIEW_SCALE);
        stream << quint32(context.features.toInt()) << context.paperColor;
        stream << qint32(cms.system) << qint32(cms.accuracy) << qint32(cms.intent) << qint32(cms.proofingIntent) << qint32(cms.colorAdaptationXYZ);
        stream << cms.isBlackPointCompensationActive << cms.isWhitePaperColorTransformed << cms.isGamutChecking << cms.isSoftProofing << cms.isConsiderOutputIntent;
        stream << cms.outOfGamutColor << cms.outputCS << cms.deviceGray << cms.deviceRGB << cms.deviceCMYK << cms.softProofingProfile;
        stream << cms.foregroundColor << cms.backgroundColor << qint32(cms.bitonalThreshold) << cms.sigmoidSlopeFactor;
        stream << mesh.preferredMeshResolutionRatio << mesh.minimalMeshResolutionRatio << mesh.tolerance;
    }

    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
}

QString PDFPagePreviewStore::getPageFileName(const PDFDocument* document, const PDFPage* page, PDFInteger pageIndex)
{
    // Name of the file contains a hash of the content of the page, so a preview
    // of a page, which has been changed and saved by an application, which
    // keeps the identifier of the document, is not used.
    QCryptographicHash hash(QCryptographicHash::Sha1);

    QByteArray geometry;
    {
        QDataStream stream(&geometry, QDataStream::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << page->getMediaBox() << page->getCropBox() << qint32(page->getPageRotation());
    }
    hash.addData(geometry);

    auto addStream = [document, &hash](const PDFObject& object)
    {
        const PDFObject& dereferencedObject = document->getObject(object);
        if (dereferencedObject.isStream())
        {
            hash.addData(*dereferencedObject.getStream()->getContent());
        }
    };

    const PDFObject& contents = document->getObject(page->getContents());
    if (contents.isArray())
    {
        const PDFArray* array = contents.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            addStream(array->getItem(i));
        }
    }
    else
    {
        addStream(contents);
    }

    return QStringLiteral("%1_%2.img").arg(pageIndex).arg(QString::fromLatin1(hash.result().toHex().left(16)));
}

}   // namespace pdf
