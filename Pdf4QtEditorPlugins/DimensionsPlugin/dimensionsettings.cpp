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

#include "dimensionsettings.h"

#include "pdfdocument.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>

#include <algorithm>

namespace pdfplugin
{

namespace dimensionsettings
{

constexpr const char* SETTINGS_GROUP = "DimensionsPlugin";
constexpr const char* SETTINGS_PRESETS_ARRAY = "ScalePresets";
constexpr const char* SETTINGS_DOCUMENT_SCALES_ARRAY = "DocumentScales";

}   // namespace dimensionsettings

using namespace dimensionsettings;

DocumentIdentity DocumentIdentity::create(const pdf::PDFDocument* document, const QString& fileName)
{
    DocumentIdentity identity;

    if (!fileName.isEmpty())
    {
        // Canonical path resolves the symbolic links, so the same file opened
        // through different paths is recognized. It is empty for a file, which
        // does not exist anymore, the absolute path is used then.
        const QFileInfo fileInfo(fileName);
        identity.filePath = fileInfo.canonicalFilePath();

        if (identity.filePath.isEmpty())
        {
            identity.filePath = fileInfo.absoluteFilePath();
        }
    }

    if (document)
    {
        const QByteArray id = document->getIdPart(0);

        if (!id.isEmpty())
        {
            identity.permanentId = QString::fromLatin1(id.toHex());
        }
    }

    return identity;
}

/// Hashes the parts of the page, which identify the sheet
class DimensionsPageKeyHelper
{
public:
    DimensionsPageKeyHelper() = delete;

    /// Adds the raw data of the stream to the hash. Only the beginning and
    /// the end of a large stream are hashed, together with its length, so
    /// a huge drawing or a scanned image does not take a long time.
    static void addStream(QCryptographicHash& hash, const pdf::PDFObject& object)
    {
        if (!object.isStream())
        {
            return;
        }

        constexpr qsizetype HEAD_SIZE = 1024 * 1024;
        constexpr qsizetype TAIL_SIZE = 64 * 1024;

        const QByteArray* content = object.getStream()->getContent();
        const qsizetype size = content->size();
        hash.addData(QByteArray::number(size));

        if (size <= HEAD_SIZE + TAIL_SIZE)
        {
            hash.addData(*content);
        }
        else
        {
            hash.addData(QByteArrayView(content->constData(), HEAD_SIZE));
            hash.addData(QByteArrayView(content->constData() + size - TAIL_SIZE, TAIL_SIZE));
        }
    }
};

QString DocumentIdentity::createPageKey(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex)
{
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return QString();
    }

    const pdf::PDFPage* page = document->getCatalog()->getPage(size_t(pageIndex));
    const pdf::PDFObjectStorage& storage = document->getStorage();

    QCryptographicHash hash(QCryptographicHash::Sha1);

    // The media box is rounded, so a rounding error does not change the key
    const QRectF mediaBox = page->getMediaBox();
    hash.addData(QString("%1 %2 %3 %4").arg(qRound(mediaBox.left() * 100)).arg(qRound(mediaBox.top() * 100))
                                       .arg(qRound(mediaBox.width() * 100)).arg(qRound(mediaBox.height() * 100)).toLatin1());

    // Content streams
    const pdf::PDFObject& contents = storage.getObject(page->getContents());
    if (contents.isArray())
    {
        for (const pdf::PDFObject& item : *contents.getArray())
        {
            DimensionsPageKeyHelper::addStream(hash, storage.getObject(item));
        }
    }
    else
    {
        DimensionsPageKeyHelper::addStream(hash, contents);
    }

    // Scanned sheets have the same content streams (they just draw an image),
    // so the images and forms drawn by the page are a part of the key too
    if (const pdf::PDFDictionary* resources = storage.getDictionaryFromObject(page->getResources()))
    {
        if (const pdf::PDFDictionary* xobjects = storage.getDictionaryFromObject(resources->get("XObject")))
        {
            for (size_t i = 0; i < xobjects->getCount(); ++i)
            {
                hash.addData(xobjects->getKey(i).getString());
                DimensionsPageKeyHelper::addStream(hash, storage.getObject(xobjects->getValue(i)));
            }
        }
    }

    return QString::fromLatin1(hash.result().toHex());
}

DimensionsPluginSettings DimensionsPluginSettings::createDefault()
{
    DimensionsPluginSettings settings;

    // Plans are measured in feet in the countries using the imperial units,
    // in meters elsewhere - points are never a useful default
    const bool isImperial = QLocale().measurementSystem() != QLocale::MetricSystem;
    settings.lengthUnit = DimensionUnit::getLengthUnit(isImperial ? "ft" : "m");
    settings.areaUnit = DimensionUnit::getAreaUnit(isImperial ? "sqft" : "sqm");
    settings.angleUnit = DimensionUnit::getAngleUnits().front();
    settings.defaultScale = DimensionScale::createIdentity();

    // Measurements are saved with the document, as standard measurement
    // annotations, so they are not lost and other applications see them
    settings.storageMode = StorageMode::Annotations;
    settings.isScaleStoredPerDocument = true;
    settings.textColor = QColor(Qt::black);

    // The background color fills the area and the perimeter measurements and it
    // is drawn under the displayed value, so it must not hide the drawing
    settings.backgroundColor = QColor(0, 0, 0, 25);

    return settings;
}

DimensionsSettingsStorage::DimensionsSettingsStorage() :
    m_settings(DimensionsPluginSettings::createDefault()),
    m_presets(DimensionScale::getDefaultPresets())
{

}

void DimensionsSettingsStorage::load()
{
    m_settings = DimensionsPluginSettings::createDefault();
    m_presets = DimensionScale::getDefaultPresets();
    m_documentScales.clear();

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(SETTINGS_GROUP);

    m_settings.lengthUnit = DimensionUnit::getLengthUnit(settings.value("lengthUnit", m_settings.lengthUnit.id).toByteArray());
    m_settings.areaUnit = DimensionUnit::getAreaUnit(settings.value("areaUnit", m_settings.areaUnit.id).toByteArray());
    m_settings.angleUnit = DimensionUnit::getAngleUnit(settings.value("angleUnit", m_settings.angleUnit.id).toByteArray());

    DimensionScale defaultScale = DimensionScale::fromStringList(settings.value("defaultScale").toStringList());
    if (defaultScale.isValid())
    {
        m_settings.defaultScale = qMove(defaultScale);
    }

    m_settings.storageMode = settings.value("storeAsAnnotations", m_settings.storageMode == DimensionsPluginSettings::StorageMode::Annotations).toBool() ? DimensionsPluginSettings::StorageMode::Annotations
                                                                                  : DimensionsPluginSettings::StorageMode::Temporary;
    m_settings.isScaleStoredPerDocument = settings.value("scalePerDocument", m_settings.isScaleStoredPerDocument).toBool();
    m_settings.isScaleAppliedPerSheet = settings.value("scalePerSheet", m_settings.isScaleAppliedPerSheet).toBool();
    m_settings.isSnapToDrawing = settings.value("snapToDrawing", m_settings.isSnapToDrawing).toBool();

    const QString fontDescriptor = settings.value("font").toString();
    if (!fontDescriptor.isEmpty())
    {
        QFont font;
        if (font.fromString(fontDescriptor))
        {
            m_settings.font = qMove(font);
        }
    }

    const QColor textColor(settings.value("textColor", m_settings.textColor.name(QColor::HexArgb)).toString());
    if (textColor.isValid())
    {
        m_settings.textColor = textColor;
    }

    const QColor backgroundColor(settings.value("backgroundColor", m_settings.backgroundColor.name(QColor::HexArgb)).toString());
    if (backgroundColor.isValid())
    {
        m_settings.backgroundColor = backgroundColor;
    }

    // The presets were never stored yet, when the flag is missing. Without it,
    // an empty array could not be distinguished from an uninitialized one and
    // the user would not be able to delete all the presets permanently.
    const bool arePresetsStored = settings.value("presetsStored", false).toBool();

    const int presetCount = settings.beginReadArray(SETTINGS_PRESETS_ARRAY);
    if (arePresetsStored)
    {
        std::vector<DimensionScale> presets;
        presets.reserve(size_t(presetCount));

        for (int i = 0; i < presetCount; ++i)
        {
            settings.setArrayIndex(i);
            DimensionScale preset = DimensionScale::fromStringList(settings.value("scale").toStringList());
            if (preset.isValid())
            {
                presets.push_back(qMove(preset));
            }
        }

        m_presets = qMove(presets);
    }
    settings.endArray();

    const int documentScaleCount = settings.beginReadArray(SETTINGS_DOCUMENT_SCALES_ARRAY);
    m_documentScales.reserve(size_t(documentScaleCount));
    for (int i = 0; i < documentScaleCount; ++i)
    {
        settings.setArrayIndex(i);

        DocumentScale documentScale;
        documentScale.identity.filePath = settings.value("path").toString();
        documentScale.identity.permanentId = settings.value("id").toString();
        documentScale.scale.scale = DimensionScale::fromStringList(settings.value("scale").toStringList());
        documentScale.scale.drawingCorrection = qMax(settings.value("drawingCorrection", 0.0).toDouble(), 0.0);
        documentScale.lastUsed = settings.value("lastUsed").toDateTime();

        // Sheets are identified by the keys derived from their content, the
        // scales stored under the page indices by the older versions are not
        // loaded, because they may belong to different sheets now
        const QVariantMap pageScales = settings.value("sheetScales").toMap();
        for (auto it = pageScales.cbegin(); it != pageScales.cend(); ++it)
        {
            const QVariantMap item = it.value().toMap();

            SheetScale pageScale;
            pageScale.scale = DimensionScale::fromStringList(item.value("scale").toStringList());
            pageScale.drawingCorrection = qMax(item.value("drawingCorrection", 0.0).toDouble(), 0.0);

            if (!it.key().isEmpty() && pageScale.isValid())
            {
                documentScale.pageScales[it.key()] = qMove(pageScale);
            }
        }

        if (documentScale.isValid())
        {
            m_documentScales.push_back(qMove(documentScale));
        }
    }
    settings.endArray();

    settings.endGroup();
}

void DimensionsSettingsStorage::save() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(SETTINGS_GROUP);

    settings.setValue("lengthUnit", m_settings.lengthUnit.id);
    settings.setValue("areaUnit", m_settings.areaUnit.id);
    settings.setValue("angleUnit", m_settings.angleUnit.id);
    settings.setValue("defaultScale", m_settings.defaultScale.toStringList());
    settings.setValue("storeAsAnnotations", m_settings.storageMode == DimensionsPluginSettings::StorageMode::Annotations);
    settings.setValue("scalePerDocument", m_settings.isScaleStoredPerDocument);
    settings.setValue("scalePerSheet", m_settings.isScaleAppliedPerSheet);
    settings.setValue("snapToDrawing", m_settings.isSnapToDrawing);
    settings.setValue("font", m_settings.font.toString());
    settings.setValue("textColor", m_settings.textColor.name(QColor::HexArgb));
    settings.setValue("backgroundColor", m_settings.backgroundColor.name(QColor::HexArgb));
    settings.setValue("presetsStored", true);

    settings.beginWriteArray(SETTINGS_PRESETS_ARRAY, int(m_presets.size()));
    for (size_t i = 0; i < m_presets.size(); ++i)
    {
        settings.setArrayIndex(int(i));
        settings.setValue("scale", m_presets[i].toStringList());
    }
    settings.endArray();

    settings.beginWriteArray(SETTINGS_DOCUMENT_SCALES_ARRAY, int(m_documentScales.size()));
    for (size_t i = 0; i < m_documentScales.size(); ++i)
    {
        settings.setArrayIndex(int(i));
        settings.setValue("path", m_documentScales[i].identity.filePath);
        settings.setValue("id", m_documentScales[i].identity.permanentId);
        settings.setValue("scale", m_documentScales[i].scale.scale.toStringList());
        settings.setValue("drawingCorrection", m_documentScales[i].scale.drawingCorrection);
        settings.setValue("lastUsed", m_documentScales[i].lastUsed);

        QVariantMap pageScales;
        for (const auto& [pageKey, pageScale] : m_documentScales[i].pageScales)
        {
            QVariantMap item;
            item["scale"] = pageScale.scale.toStringList();
            item["drawingCorrection"] = pageScale.drawingCorrection;
            pageScales[pageKey] = item;
        }
        settings.setValue("sheetScales", pageScales);
        settings.remove("pageScales");
    }
    settings.endArray();

    settings.endGroup();
}

void DimensionsSettingsStorage::addPreset(const DimensionScale& scale)
{
    if (!scale.isValid() || scale.getName().isEmpty())
    {
        return;
    }

    auto it = std::find_if(m_presets.begin(), m_presets.end(), [&scale](const DimensionScale& preset) { return preset.getName() == scale.getName(); });

    if (it != m_presets.end())
    {
        *it = scale;
    }
    else
    {
        m_presets.push_back(scale);
    }
}

const DimensionsSettingsStorage::DocumentScale* DimensionsSettingsStorage::findDocumentScale(const DocumentIdentity& identity) const
{
    if (!identity.isValid())
    {
        return nullptr;
    }

    if (!identity.filePath.isEmpty())
    {
        // The file path identifies exactly one file, so it has a priority
        auto it = std::find_if(m_documentScales.cbegin(), m_documentScales.cend(),
                               [&identity](const DocumentScale& item) { return item.identity.filePath == identity.filePath; });

        if (it != m_documentScales.cend())
        {
            return &*it;
        }
    }

    if (identity.permanentId.isEmpty())
    {
        return nullptr;
    }

    // The file was not found by its path. The permanent identifier survives
    // renaming and moving of the file, but it is not unique - a copy of a document
    // keeps it as well. A record is therefore accepted only when it cannot belong
    // to a different, still existing file.
    for (const DocumentScale& item : m_documentScales)
    {
        if (item.identity.permanentId != identity.permanentId)
        {
            continue;
        }

        if (identity.filePath.isEmpty() || item.identity.filePath.isEmpty())
        {
            // One of the documents has no file at all, the identifier is
            // the only thing they can be matched by
            return &item;
        }

        if (!QFileInfo::exists(item.identity.filePath))
        {
            // The file of the record does not exist anymore, so the document
            // was renamed or moved
            return &item;
        }
    }

    return nullptr;
}

SheetScale DimensionsSettingsStorage::getDocumentScale(const DocumentIdentity& identity) const
{
    if (const DocumentScale* documentScale = findDocumentScale(identity))
    {
        return documentScale->scale;
    }

    return SheetScale();
}

DimensionsSettingsStorage::DocumentScale* DimensionsSettingsStorage::getOrCreateDocumentScale(const DocumentIdentity& identity)
{
    DocumentScale* documentScale = const_cast<DocumentScale*>(findDocumentScale(identity));

    if (!documentScale)
    {
        m_documentScales.emplace_back();
        documentScale = &m_documentScales.back();
    }

    // The identity is always rewritten, so a record found by the permanent
    // identifier is moved to the current path of the file
    documentScale->identity = identity;
    documentScale->lastUsed = QDateTime::currentDateTime();
    return documentScale;
}

void DimensionsSettingsStorage::discardOldDocumentScales()
{
    // Records without any scale are useless
    m_documentScales.erase(std::remove_if(m_documentScales.begin(), m_documentScales.end(), [](const DocumentScale& item) { return !item.isValid(); }), m_documentScales.end());

    if (m_documentScales.size() > MAXIMUM_DOCUMENT_SCALES)
    {
        // Discard the records, which were not used for the longest time
        std::sort(m_documentScales.begin(), m_documentScales.end(), [](const DocumentScale& left, const DocumentScale& right) { return left.lastUsed > right.lastUsed; });
        m_documentScales.resize(MAXIMUM_DOCUMENT_SCALES);
    }
}

void DimensionsSettingsStorage::setDocumentScale(const DocumentIdentity& identity, const SheetScale& scale, bool keepPageScales)
{
    if (!identity.isValid())
    {
        return;
    }

    if (!scale.isValid() && keepPageScales && !findDocumentScale(identity))
    {
        // Nothing to remove, no empty record is created
        return;
    }

    DocumentScale* documentScale = getOrCreateDocumentScale(identity);
    documentScale->scale = scale;

    if (!keepPageScales)
    {
        documentScale->pageScales.clear();
    }

    discardOldDocumentScales();
}

void DimensionsSettingsStorage::copyDocumentScales(const DocumentIdentity& source, const DocumentIdentity& target)
{
    if (!target.isValid() || source == target)
    {
        return;
    }

    const DocumentScale* sourceScale = findDocumentScale(source);
    if (!sourceScale || !sourceScale->isValid())
    {
        return;
    }

    // The record is copied first, the target record may be created
    // in the same vector, which would invalidate the pointer
    const DocumentScale copiedScale = *sourceScale;
    DocumentScale* targetScale = getOrCreateDocumentScale(target);
    targetScale->scale = copiedScale.scale;
    targetScale->pageScales = copiedScale.pageScales;

    discardOldDocumentScales();
}

std::map<QString, SheetScale> DimensionsSettingsStorage::getPageScales(const DocumentIdentity& identity) const
{
    if (const DocumentScale* documentScale = findDocumentScale(identity))
    {
        return documentScale->pageScales;
    }

    return {};
}

void DimensionsSettingsStorage::setPageScale(const DocumentIdentity& identity, const QString& pageKey, const SheetScale& scale)
{
    if (!identity.isValid() || pageKey.isEmpty())
    {
        return;
    }

    DocumentScale* documentScale = getOrCreateDocumentScale(identity);

    if (scale.isValid())
    {
        documentScale->pageScales[pageKey] = scale;
    }
    else
    {
        documentScale->pageScales.erase(pageKey);
    }

    discardOldDocumentScales();
}

}   // namespace pdfplugin
