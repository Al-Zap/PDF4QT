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

#include "pdfrecoverymanager.h"
#include "pdfdocumentwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>

#include <algorithm>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFRecoveryManager::PDFRecoveryManager(QString recoveryDirectory) :
    m_recoveryDirectory(recoveryDirectory.isEmpty() ? getDefaultRecoveryDirectory() : std::move(recoveryDirectory))
{

}

QString PDFRecoveryManager::getDefaultRecoveryDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).absoluteFilePath("Recovery");
}

QString PDFRecoveryManager::normalizeFileName(const QString& fileName)
{
    QString normalized = QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());
#ifdef Q_OS_WIN
    // File names are case insensitive on Windows
    normalized = normalized.toLower();
#endif
    return normalized;
}

QString PDFRecoveryManager::getBaseName(const QString& originalFileName) const
{
    const QByteArray hash = QCryptographicHash::hash(normalizeFileName(originalFileName).toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QDir(m_recoveryDirectory).absoluteFilePath(QString::fromLatin1(hash));
}

bool PDFRecoveryManager::writeRecoveryCopy(const QString& originalFileName, const pdf::PDFDocument* document) const
{
    if (originalFileName.isEmpty() || !document || !QDir().mkpath(m_recoveryDirectory))
    {
        return false;
    }

    const QString baseName = getBaseName(originalFileName);

    pdf::PDFDocumentWriter writer(nullptr);
    if (!writer.write(baseName + ".pdf", document, true))
    {
        return false;
    }

    // Description of the recovery copy is written after the copy itself,
    // so it never describes a partially written copy.
    QSettings description(baseName + ".ini", QSettings::IniFormat);
    description.setValue("OriginalFileName", QFileInfo(originalFileName).absoluteFilePath());
    description.setValue("Time", QDateTime::currentDateTime());
    description.sync();

    return description.status() == QSettings::NoError;
}

void PDFRecoveryManager::removeRecoveryCopy(const QString& originalFileName) const
{
    if (originalFileName.isEmpty())
    {
        return;
    }

    const QString baseName = getBaseName(originalFileName);
    QFile::remove(baseName + ".ini");
    QFile::remove(baseName + ".pdf");
}

std::optional<PDFRecoveryManager::RecoveryItem> PDFRecoveryManager::findRecoveryItem(const QString& originalFileName) const
{
    if (originalFileName.isEmpty())
    {
        return std::nullopt;
    }

    const QString baseName = getBaseName(originalFileName);
    if (!QFileInfo::exists(baseName + ".ini") || !QFileInfo::exists(baseName + ".pdf"))
    {
        return std::nullopt;
    }

    QSettings description(baseName + ".ini", QSettings::IniFormat);
    RecoveryItem item;
    item.originalFileName = description.value("OriginalFileName").toString();
    item.recoveryFileName = baseName + ".pdf";
    item.time = description.value("Time").toDateTime();

    if (normalizeFileName(item.originalFileName) != normalizeFileName(originalFileName))
    {
        return std::nullopt;
    }

    return item;
}

std::vector<PDFRecoveryManager::RecoveryItem> PDFRecoveryManager::getRecoveryItems() const
{
    std::vector<RecoveryItem> items;

    QDir directory(m_recoveryDirectory);
    const QStringList descriptions = directory.entryList(QStringList() << "*.ini", QDir::Files);
    for (const QString& descriptionFileName : descriptions)
    {
        QSettings description(directory.absoluteFilePath(descriptionFileName), QSettings::IniFormat);
        const QString originalFileName = description.value("OriginalFileName").toString();

        if (std::optional<RecoveryItem> item = findRecoveryItem(originalFileName))
        {
            items.push_back(std::move(*item));
        }
    }

    std::sort(items.begin(), items.end(), [](const RecoveryItem& l, const RecoveryItem& r) { return l.time > r.time; });
    return items;
}

}   // namespace pdfviewer
