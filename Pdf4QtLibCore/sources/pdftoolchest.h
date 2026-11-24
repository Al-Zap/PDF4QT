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

#ifndef PDFTOOLCHEST_H
#define PDFTOOLCHEST_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QImage>
#include <QRectF>
#include <QJsonObject>

#include <vector>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;

/// Markup saved in the tool chest. It holds copies of one or more annotations
/// (with their style, text and appearance), which can be placed again on any
/// page of any document.
struct PDF4QTLIBCORESHARED_EXPORT PDFToolChestItem
{
    QString id;
    QString name;
    QByteArray data;    ///< Annotations serialized by PDFAnnotationManipulator::serializeAnnotations

    bool isValid() const { return !id.isEmpty() && !data.isEmpty(); }

    QJsonObject toJson() const;
    static PDFToolChestItem fromJson(const QJsonObject& object);
};

using PDFToolChestItems = std::vector<PDFToolChestItem>;

/// Tool chest - markups saved by the user to be reused (styled clouds, callouts
/// with standard texts, arrows, ...)
class PDF4QTLIBCORESHARED_EXPORT PDFToolChest
{
public:
    PDFToolChest() = delete;

    /// Creates an item from the annotations of the document. Replies (also the
    /// review states) are not saved, the popup windows are.
    /// \param document Document
    /// \param annotations Annotations
    /// \param name Name of the item (if empty, the name is created from the annotations)
    /// \returns Item (invalid, if nothing can be saved)
    static PDFToolChestItem createItem(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations, const QString& name);

    /// Returns a name of the annotations - the subject, the text or the type
    static QString getDefaultName(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations);

    /// Returns the bounding rectangle of the annotations of the item (in the page coordinates
    /// of the page, from which they were saved), or an invalid rectangle
    static QRectF getBoundingRectangle(const PDFToolChestItem& item);

    /// Places the item on the page, centered at the point. Placed annotations get
    /// the author and the current date. Returns the placed annotations.
    /// \param builder Builder of the document
    /// \param page Page
    /// \param center Center in the page coordinates
    /// \param item Item
    /// \param author Author
    static std::vector<PDFObjectReference> placeItem(PDFDocumentBuilder* builder, PDFObjectReference page, QPointF center, const PDFToolChestItem& item, const QString& author);

    /// Renders the item (on a transparent background), the image fits the size
    /// \param item Item
    /// \param size Maximal size of the image
    static QImage renderItem(const PDFToolChestItem& item, QSize size);

    /// Converts the items to JSON
    static QByteArray toJson(const PDFToolChestItems& items);

    /// Reads the items from JSON. Invalid items are skipped.
    static PDFToolChestItems fromJson(const QByteArray& data, bool* ok = nullptr);

    /// Returns the path of the file, in which the tool chest of the user is stored
    static QString getDefaultFileName();

    /// Loads the tool chest of the user
    /// \param fileName File name (empty means the default file)
    static PDFToolChestItems load(const QString& fileName = QString());

    /// Saves the tool chest of the user
    /// \param items Items
    /// \param fileName File name (empty means the default file)
    static bool save(const PDFToolChestItems& items, const QString& fileName = QString());
};

}   // namespace pdf

#endif // PDFTOOLCHEST_H
