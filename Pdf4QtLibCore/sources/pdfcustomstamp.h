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

#ifndef PDFCUSTOMSTAMP_H
#define PDFCUSTOMSTAMP_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QColor>
#include <QImage>
#include <QDateTime>
#include <QJsonObject>

#include <vector>

class QPainter;

namespace pdf
{
class PDFDocumentBuilder;

/// Custom stamp (for example "REVIEWED - company - author - date" with a logo).
/// The stamp is created as a standard stamp annotation with its own appearance
/// stream, so it looks the same in all PDF applications. Texts can contain the
/// variables {date}, {time}, {author} and {company}, which are replaced, when
/// the stamp is placed on the page.
struct PDF4QTLIBCORESHARED_EXPORT PDFCustomStamp
{
    bool operator==(const PDFCustomStamp&) const = default;

    QString id;             ///< Unique identifier (name of the stamp in the document)
    QString name;           ///< Name displayed in the menus
    QString title;          ///< Large text of the stamp ("REVIEWED")
    QString text;           ///< Lines below the title, can contain variables
    QString company;        ///< Value of the variable {company}
    QColor color = QColor(21, 101, 192);
    QByteArray logo;        ///< Logo image (PNG or JPEG data), can be empty
    PDFReal width = 216.0;  ///< Width of the stamp in points

    bool isValid() const { return !id.isEmpty() && !name.isEmpty(); }

    /// Returns the logo image (null image, if there is no logo)
    QImage getLogoImage() const;

    QJsonObject toJson() const;
    static PDFCustomStamp fromJson(const QJsonObject& object);
};

using PDFCustomStamps = std::vector<PDFCustomStamp>;

/// Creates, draws and stores the custom stamps
class PDF4QTLIBCORESHARED_EXPORT PDFCustomStampLibrary
{
public:
    PDFCustomStampLibrary() = delete;

    /// Prefix of the name of a custom stamp annotation (entry Name)
    static constexpr const char* NAME_PREFIX = "PDF4QT_";

    /// Returns the stamps offered, when the user has not defined any stamps yet
    static PDFCustomStamps getDefaultStamps();

    /// Creates a new unique identifier of a stamp
    static QString createId();

    /// Replaces the variables of the text
    /// \param text Text with variables
    /// \param stamp Stamp (company)
    /// \param author Author
    /// \param dateTime Date and time
    static QString expandText(const QString& text, const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime);

    /// Returns the lines of the text below the title, with expanded variables. Empty
    /// lines (for example the line with the company, if no company is set) are removed.
    static QStringList getLines(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime);

    /// Returns the size of the stamp in points
    static QSizeF getSize(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime);

    /// Draws the stamp into the rectangle (0, 0, size) of the painter. Coordinate
    /// system of the painter has the y axis pointing down.
    static void draw(QPainter* painter, const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime);

    /// Renders the preview of the stamp
    /// \param stamp Stamp
    /// \param author Author
    /// \param dateTime Date and time
    /// \param scale Pixels per point
    static QImage renderPreview(const PDFCustomStamp& stamp, const QString& author, const QDateTime& dateTime, PDFReal scale);

    /// Creates the stamp annotation on the page, centered at the point
    /// \param builder Builder of the document
    /// \param page Page
    /// \param center Center of the stamp in the page coordinates
    /// \param stamp Stamp
    /// \param author Author (title of the annotation, variable {author})
    /// \param dateTime Date and time (variables {date} and {time})
    /// \param rotation Rotation of the page in degrees (the stamp is upright on the screen)
    /// \returns Reference of the annotation
    static PDFObjectReference createAnnotation(PDFDocumentBuilder* builder,
                                               PDFObjectReference page,
                                               QPointF center,
                                               const PDFCustomStamp& stamp,
                                               const QString& author,
                                               const QDateTime& dateTime,
                                               int rotation = 0);

    /// Returns true, if the stamp name (entry Name of a stamp annotation) is the
    /// name of a custom stamp of any application (not one of the 14 standard stamps)
    static bool isCustomStampName(const QByteArray& name);

    /// Converts the stamps to JSON (the file of the stamps, also used for import/export)
    static QByteArray toJson(const PDFCustomStamps& stamps);

    /// Reads the stamps from JSON. Invalid stamps are skipped.
    /// \param data JSON data
    /// \param ok Set to false, if the data is not a file of the stamps
    static PDFCustomStamps fromJson(const QByteArray& data, bool* ok = nullptr);

    /// Returns the path of the file, in which the stamps of the user are stored
    static QString getDefaultFileName();

    /// Loads the stamps of the user. Default stamps are returned, if the user
    /// has not saved any stamps yet.
    /// \param fileName File name (empty means the default file)
    static PDFCustomStamps load(const QString& fileName = QString());

    /// Saves the stamps of the user
    /// \param stamps Stamps
    /// \param fileName File name (empty means the default file)
    static bool save(const PDFCustomStamps& stamps, const QString& fileName = QString());
};

}   // namespace pdf

#endif // PDFCUSTOMSTAMP_H
