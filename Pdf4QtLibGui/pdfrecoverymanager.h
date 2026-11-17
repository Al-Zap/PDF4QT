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

#ifndef PDFRECOVERYMANAGER_H
#define PDFRECOVERYMANAGER_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"

#include <QString>
#include <QDateTime>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QLockFile;

namespace pdfviewer
{

/// Manages recovery copies of documents with unsaved changes. Recovery copy
/// is written periodically, while the document has unsaved changes, and it
/// is removed, when the document is saved or closed normally. If application
/// crashes, recovery copy remains, and it is offered to the user, when
/// application is started again (or the document is opened again).
///
/// Several application windows (processes) share the recovery directory.
/// Window, which writes a recovery copy, holds a lock file of the copy,
/// until the copy is removed. Copies locked by another running window are
/// not offered and can't be removed. If the window crashes, operating system
/// releases the lock, so the copy can be recovered.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRecoveryManager
{
public:
    /// Creates recovery manager
    /// \param recoveryDirectory Directory for recovery copies, if empty,
    ///        then default directory in the application data is used
    explicit PDFRecoveryManager(QString recoveryDirectory = QString());

    /// Recovery copies older than this are removed
    static constexpr int MAX_AGE_DAYS = 30;

    struct PDF4QTLIBGUILIBSHARED_EXPORT RecoveryItem
    {
        QString originalFileName;       ///< Absolute path of the original document
        QString recoveryFileName;       ///< Absolute path of the recovery copy
        QDateTime time;                 ///< Time, when recovery copy was written
        QDateTime originalLastModified; ///< Modification time of the original document, when it was opened (or saved)
        bool offered = false;           ///< Recovery was already offered at application start, and the user postponed it

        /// Returns true, if the original document was changed after it
        /// was opened, for example, saved by another application.
        bool isOriginalModified() const;
    };

    /// Returns default recovery directory (in the application data)
    static QString getDefaultRecoveryDirectory();

    /// Returns recovery directory
    const QString& getRecoveryDirectory() const { return m_recoveryDirectory; }

    /// Locks recovery copy of the document for this manager. Returns true, if
    /// the lock is held (it was already held, or it was acquired now). Returns
    /// false, if another window holds the lock (for example, the same document
    /// is opened in two windows), recovery copy must not be written then.
    /// \param originalFileName File name of the original document
    bool lockRecoveryCopy(const QString& originalFileName);

    /// Writes recovery copy of the document. Returns true, if copy was written.
    /// Recovery copy must be locked by \p lockRecoveryCopy before. Function
    /// can be called from a worker thread.
    /// \param originalFileName File name of the original document
    /// \param originalLastModified Modification time of the original document, when it was opened
    /// \param document Document
    bool writeRecoveryCopy(const QString& originalFileName, const QDateTime& originalLastModified, const pdf::PDFDocument* document) const;

    /// Removes recovery copy of the document (if it exists) and releases its
    /// lock. Recovery copy locked by another window is not removed.
    /// \param originalFileName File name of the original document
    void removeRecoveryCopy(const QString& originalFileName);

    /// Removes recovery copy of the document and releases its lock, but only
    /// if the copy is locked by this manager (it was written by this window).
    /// Recovery copy of a crashed window, which the user didn't decide about
    /// yet, is kept.
    /// \param originalFileName File name of the original document
    void releaseRecoveryCopy(const QString& originalFileName);

    /// Marks recovery copy as offered (the user postponed the recovery),
    /// so it isn't offered again at the application start.
    /// \param originalFileName File name of the original document
    void setOffered(const QString& originalFileName);

    /// Returns recovery copy of the document, if it exists, and it is not
    /// locked by another window.
    /// \param originalFileName File name of the original document
    std::optional<RecoveryItem> findRecoveryItem(const QString& originalFileName) const;

    /// Returns all recovery copies, which are not locked by another window (newest first)
    std::vector<RecoveryItem> getRecoveryItems() const;

    /// Removes files of incomplete or unreadable recovery copies and
    /// recovery copies older than \p MAX_AGE_DAYS days. Copies locked
    /// by another window are kept.
    void removeInvalidRecoveryCopies();

private:
    QString getBaseName(const QString& originalFileName) const;
    static QString normalizeFileName(const QString& fileName);

    /// Returns true, if the recovery copy is locked by another window
    /// \param baseName Base name of the recovery copy files
    bool isLockedByOther(const QString& baseName) const;

    /// Removes files of the recovery copy
    /// \param baseName Base name of the recovery copy files
    void removeFiles(const QString& baseName);

    QString m_recoveryDirectory;

    /// Locks held by this manager (base name -> lock). Copies of the manager
    /// (used in worker threads) share the locks.
    std::map<QString, std::shared_ptr<QLockFile>> m_locks;
};

}   // namespace pdfviewer

#endif // PDFRECOVERYMANAGER_H
