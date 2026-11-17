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
#include <QLockFile>
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

bool PDFRecoveryManager::RecoveryItem::isOriginalModified() const
{
    QFileInfo fileInfo(originalFileName);
    return originalLastModified.isValid() && fileInfo.exists() && fileInfo.lastModified() != originalLastModified;
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

bool PDFRecoveryManager::isLockedByOther(const QString& baseName) const
{
    if (m_locks.count(baseName))
    {
        // Locked by us
        return false;
    }

    if (!QFileInfo::exists(baseName + ".lock"))
    {
        return false;
    }

    // Lock of a crashed window is stale and it is removed by tryLock.
    // Lock of a running window is never stale, regardless of its age.
    QLockFile lock(baseName + ".lock");
    lock.setStaleLockTime(0);
    if (lock.tryLock(0))
    {
        lock.unlock();
        return false;
    }

    return true;
}

bool PDFRecoveryManager::lockRecoveryCopy(const QString& originalFileName)
{
    if (originalFileName.isEmpty() || !QDir().mkpath(m_recoveryDirectory))
    {
        return false;
    }

    const QString baseName = getBaseName(originalFileName);
    if (m_locks.count(baseName))
    {
        return true;
    }

    auto lock = std::make_shared<QLockFile>(baseName + ".lock");
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0))
    {
        return false;
    }

    m_locks[baseName] = std::move(lock);
    return true;
}

bool PDFRecoveryManager::writeRecoveryCopy(const QString& originalFileName, const QDateTime& originalLastModified, const pdf::PDFDocument* document) const
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
    description.setValue("OriginalLastModified", originalLastModified);
    description.remove("Offered");
    description.sync();

    return description.status() == QSettings::NoError;
}

void PDFRecoveryManager::removeFiles(const QString& baseName)
{
    QFile::remove(baseName + ".ini");
    QFile::remove(baseName + ".pdf");

    auto it = m_locks.find(baseName);
    if (it != m_locks.end())
    {
        // Unlock removes the lock file
        it->second->unlock();
        m_locks.erase(it);
    }
}

void PDFRecoveryManager::removeRecoveryCopy(const QString& originalFileName)
{
    if (originalFileName.isEmpty())
    {
        return;
    }

    const QString baseName = getBaseName(originalFileName);
    if (!isLockedByOther(baseName))
    {
        removeFiles(baseName);
    }
}

void PDFRecoveryManager::releaseRecoveryCopy(const QString& originalFileName)
{
    if (originalFileName.isEmpty())
    {
        return;
    }

    const QString baseName = getBaseName(originalFileName);
    if (m_locks.count(baseName))
    {
        removeFiles(baseName);
    }
}

void PDFRecoveryManager::setOffered(const QString& originalFileName)
{
    if (findRecoveryItem(originalFileName))
    {
        QSettings description(getBaseName(originalFileName) + ".ini", QSettings::IniFormat);
        description.setValue("Offered", true);
    }
}

std::optional<PDFRecoveryManager::RecoveryItem> PDFRecoveryManager::findRecoveryItem(const QString& originalFileName) const
{
    if (originalFileName.isEmpty())
    {
        return std::nullopt;
    }

    const QString baseName = getBaseName(originalFileName);
    if (!QFileInfo::exists(baseName + ".ini") || !QFileInfo::exists(baseName + ".pdf") || isLockedByOther(baseName))
    {
        return std::nullopt;
    }

    QSettings description(baseName + ".ini", QSettings::IniFormat);
    RecoveryItem item;
    item.originalFileName = description.value("OriginalFileName").toString();
    item.recoveryFileName = baseName + ".pdf";
    item.time = description.value("Time").toDateTime();
    item.originalLastModified = description.value("OriginalLastModified").toDateTime();
    item.offered = description.value("Offered", false).toBool();

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

void PDFRecoveryManager::removeInvalidRecoveryCopies()
{
    QDir directory(m_recoveryDirectory);
    const QDateTime oldestTime = QDateTime::currentDateTime().addDays(-MAX_AGE_DAYS);

    QStringList baseNames;
    for (const QFileInfo& fileInfo : directory.entryInfoList(QStringList() << "*.ini" << "*.pdf" << "*.lock", QDir::Files))
    {
        const QString baseName = directory.absoluteFilePath(fileInfo.completeBaseName());
        if (!baseNames.contains(baseName))
        {
            baseNames << baseName;
        }
    }

    for (const QString& baseName : baseNames)
    {
        if (m_locks.count(baseName) || isLockedByOther(baseName))
        {
            // Recovery copy is being written by a running window
            continue;
        }

        bool isValid = QFileInfo::exists(baseName + ".ini") && QFileInfo::exists(baseName + ".pdf");
        if (isValid)
        {
            QSettings description(baseName + ".ini", QSettings::IniFormat);
            const QString originalFileName = description.value("OriginalFileName").toString();
            const QDateTime time = description.value("Time").toDateTime();
            isValid = !originalFileName.isEmpty() && getBaseName(originalFileName) == baseName && time.isValid() && time >= oldestTime;
        }

        if (!isValid)
        {
            removeFiles(baseName);
        }
    }
}

}   // namespace pdfviewer
