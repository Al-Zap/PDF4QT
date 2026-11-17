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

#include <optional>
#include <vector>

namespace pdfviewer
{

/// Manages automatic recovery copies of edited documents. While a document
/// has unsaved changes, a copy of it is periodically written into the recovery
/// directory. The copy is removed, when the document is saved or closed normally,
/// so a recovery copy exists only, when the application has crashed (or was killed)
/// with unsaved changes. The recovery copy is offered, when the document is opened again.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRecoveryManager
{
public:
    /// Creates recovery manager
    /// \param recoveryDirectory Directory for recovery copies, if empty,
    ///        then default directory in the application data is used
    explicit PDFRecoveryManager(QString recoveryDirectory = QString());

    struct RecoveryItem
    {
        QString originalFileName;   ///< Absolute path of the original document
        QString recoveryFileName;   ///< Absolute path of the recovery copy
        QDateTime time;             ///< Time, when recovery copy was written
    };

    /// Returns default recovery directory (in the application data)
    static QString getDefaultRecoveryDirectory();

    /// Returns recovery directory
    const QString& getRecoveryDirectory() const { return m_recoveryDirectory; }

    /// Writes recovery copy of the document. Returns true, if copy was written.
    /// Function can be called from a worker thread.
    /// \param originalFileName File name of the original document
    /// \param document Document
    bool writeRecoveryCopy(const QString& originalFileName, const pdf::PDFDocument* document) const;

    /// Removes recovery copy of the document (if it exists)
    /// \param originalFileName File name of the original document
    void removeRecoveryCopy(const QString& originalFileName) const;

    /// Returns recovery copy of the document, if it exists
    /// \param originalFileName File name of the original document
    std::optional<RecoveryItem> findRecoveryItem(const QString& originalFileName) const;

    /// Returns all recovery copies (newest first)
    std::vector<RecoveryItem> getRecoveryItems() const;

private:
    QString getBaseName(const QString& originalFileName) const;
    static QString normalizeFileName(const QString& fileName);

    QString m_recoveryDirectory;
};

}   // namespace pdfviewer

#endif // PDFRECOVERYMANAGER_H
