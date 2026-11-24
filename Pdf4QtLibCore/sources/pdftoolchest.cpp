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

#include "pdftoolchest.h"
#include "pdfannotation.h"
#include "pdfannotationmanipulator.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfcolorspaces.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdftextlayout.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <QPainter>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace pdf
{

QJsonObject PDFToolChestItem::toJson() const
{
    QJsonObject object;
    object["id"] = id;
    object["name"] = name;
    object["data"] = QString::fromLatin1(data.toBase64());
    return object;
}

PDFToolChestItem PDFToolChestItem::fromJson(const QJsonObject& object)
{
    PDFToolChestItem item;
    item.id = object["id"].toString();
    item.name = object["name"].toString();
    item.data = QByteArray::fromBase64(object["data"].toString().toLatin1());
    return item;
}

PDFToolChestItem PDFToolChest::createItem(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations, const QString& name)
{
    PDFToolChestItem item;

    if (!document || annotations.empty())
    {
        return item;
    }

    // The whole comment threads are serialized - replies and review states
    // belong to the original markup, they are removed from the copy
    PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(PDFAnnotationManipulator::serializeAnnotations(document, annotations));
    if (!serialized.isValid())
    {
        return item;
    }

    PDFDocumentBuilder builder(&serialized.document);
    const PDFObjectStorage* storage = builder.getStorage();
    const PDFObjectReference page = serialized.document.getCatalog()->getPage(0)->getPageReference();
    for (const PDFObjectReference& annotationReference : serialized.document.getCatalog()->getPage(0)->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
        if (annotation && annotation->asMarkupAnnotation() && annotation->asMarkupAnnotation()->getInReplyTo().isValid())
        {
            builder.removeAnnotation(page, annotationReference);
        }
    }
    const PDFDocument cleanedDocument = builder.build();

    item.data = PDFAnnotationManipulator::serializeAnnotations(&cleanedDocument, serialized.annotations);
    if (item.data.isEmpty())
    {
        return item;
    }

    item.id = QUuid::createUuid().toString(QUuid::Id128);
    item.name = name.trimmed().isEmpty() ? getDefaultName(document, annotations) : name.trimmed();
    return item;
}

QString PDFToolChest::getDefaultName(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations)
{
    if (!document || annotations.empty())
    {
        return QString();
    }

    PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), annotations.front());
    if (!annotation)
    {
        return PDFTranslationContext::tr("Markup");
    }

    QString name;
    if (const PDFMarkupAnnotation* markupAnnotation = annotation->asMarkupAnnotation())
    {
        name = markupAnnotation->getSubject().simplified();
    }

    if (name.isEmpty())
    {
        name = annotation->getContents().section(QChar('\n'), 0, 0).simplified();
    }

    if (name.isEmpty())
    {
        name = annotation->getGUICaption().section(QChar('\n'), 0, 0).simplified();
    }

    if (name.size() > 40)
    {
        name = name.left(39) + QChar(0x2026);
    }

    if (annotations.size() > 1)
    {
        name = PDFTranslationContext::tr("%1 (+%2)").arg(name).arg(annotations.size() - 1);
    }

    return name;
}

QRectF PDFToolChest::getBoundingRectangle(const PDFToolChestItem& item)
{
    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(item.data);
    return serialized.isValid() ? serialized.boundingRectangle : QRectF();
}

std::vector<PDFObjectReference> PDFToolChest::placeItem(PDFDocumentBuilder* builder, PDFObjectReference page, QPointF center, const PDFToolChestItem& item, const QString& author)
{
    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(item.data);
    if (!serialized.isValid())
    {
        return { };
    }

    const QPointF offset = center - serialized.boundingRectangle.center();
    std::vector<PDFObjectReference> placedAnnotations = PDFAnnotationManipulator::insertAnnotations(builder, page, serialized, offset);

    for (const PDFObjectReference& annotation : placedAnnotations)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("T");
        factory << author;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("M");
        factory << WrapCurrentDateTime();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("CreationDate");
        factory << WrapCurrentDateTime();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
    }

    return placedAnnotations;
}

QImage PDFToolChest::renderItem(const PDFToolChestItem& item, QSize size)
{
    PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(item.data);
    if (!serialized.isValid() || size.isEmpty())
    {
        return QImage();
    }

    // Small margin around the annotations, so the strokes are not cut
    QRectF rectangle = serialized.boundingRectangle;
    const PDFReal margin = qMax(2.0, qMax(rectangle.width(), rectangle.height()) * 0.04);
    rectangle.adjust(-margin, -margin, margin, margin);
    if (rectangle.width() <= 0.0 || rectangle.height() <= 0.0)
    {
        return QImage();
    }

    const PDFReal scale = qMin(size.width() / rectangle.width(), size.height() / rectangle.height());
    const QSize imageSize = (rectangle.size() * scale).toSize().expandedTo(QSize(1, 1));

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    PDFDocument* document = &serialized.document;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::View, nullptr);
    PDFCMSManager cmsManager(nullptr);
    PDFModifiedDocument modifiedDocument(document, &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);

    PDFRenderer::Features features = PDFRenderer::getDefaultFeatures();
    features.setFlag(PDFRenderer::Antialiasing);
    features.setFlag(PDFRenderer::TextAntialiasing);
    PDFAnnotationManager annotationManager(&fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, PDFMeshQualitySettings(),
                                           features, PDFAnnotationManager::Target::View, nullptr);
    annotationManager.setDocument(modifiedDocument);

    // Page coordinates (y axis up) to the image
    const QTransform matrix(scale, 0, 0, -scale, -rectangle.left() * scale, rectangle.bottom() * scale);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    PDFPrecompiledPage compiledPage;
    PDFTextLayoutGetter layoutGetter(nullptr, 0);
    QList<PDFRenderError> errors;
    PDFColorConvertor convertor = cmsManager.getCurrentCMS()->getColorConvertor();
    annotationManager.drawPage(&painter, 0, &compiledPage, layoutGetter, matrix, convertor, errors);
    painter.end();

    return image;
}

QByteArray PDFToolChest::toJson(const PDFToolChestItems& items)
{
    QJsonArray array;
    for (const PDFToolChestItem& item : items)
    {
        array.append(item.toJson());
    }

    QJsonObject object;
    object["type"] = QStringLiteral("PDF4QT-ToolChest");
    object["version"] = 1;
    object["items"] = array;
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

PDFToolChestItems PDFToolChest::fromJson(const QByteArray& data, bool* ok)
{
    PDFToolChestItems items;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    const bool isValid = error.error == QJsonParseError::NoError && document.isObject() && document.object()["type"].toString() == QLatin1String("PDF4QT-ToolChest");
    if (ok)
    {
        *ok = isValid;
    }

    if (!isValid)
    {
        return items;
    }

    for (const QJsonValue& value : document.object()["items"].toArray())
    {
        PDFToolChestItem item = PDFToolChestItem::fromJson(value.toObject());

        // Data come from a file - they must be annotations
        if (item.isValid() && PDFAnnotationManipulator::deserializeAnnotations(item.data).isValid())
        {
            items.push_back(std::move(item));
        }
    }

    return items;
}

QString PDFToolChest::getDefaultFileName()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("toolchest.json"));
}

PDFToolChestItems PDFToolChest::load(const QString& fileName)
{
    QFile file(fileName.isEmpty() ? getDefaultFileName() : fileName);
    if (!file.open(QFile::ReadOnly))
    {
        return { };
    }

    return fromJson(file.readAll());
}

bool PDFToolChest::save(const PDFToolChestItems& items, const QString& fileName)
{
    const QString effectiveFileName = fileName.isEmpty() ? getDefaultFileName() : fileName;
    QDir().mkpath(QFileInfo(effectiveFileName).absolutePath());

    QSaveFile file(effectiveFileName);
    if (!file.open(QFile::WriteOnly))
    {
        return false;
    }

    file.write(toJson(items));
    return file.commit();
}

}   // namespace pdf
