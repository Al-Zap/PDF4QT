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


#ifndef PDFPAGESTAMPER_H
#define PDFPAGESTAMPER_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QColor>
#include <QImage>
#include <QMarginsF>
#include <QDateTime>
#include <QTransform>

#include <array>
#include <optional>

namespace pdf
{
class PDFPage;

/// Fonts of the stamps (headers, footers, Bates numbers and watermarks). The stamps
/// use the standard fonts, which are not embedded: the text stays searchable and
/// extractable (Bates numbers are searched for), the stamps of thousands of pages
/// share a single font dictionary, and the metrics are known exactly. The text is
/// written in WinAnsiEncoding, characters outside of it are replaced by '?'.
enum class PDFStampFont
{
    Helvetica,
    HelveticaBold,
    TimesRoman,
    TimesBold,
    Courier,
    CourierBold
};

/// Style of the text of a stamp
struct PDF4QTLIBCORESHARED_EXPORT PDFStampTextStyle
{
    PDFStampFont font = PDFStampFont::Helvetica;

    /// Font size in points. For a text watermark, zero (or a negative value)
    /// means, that the text is fitted to the page (see \p PDFWatermarkSettings::relativeSize).
    PDFReal fontSize = 10.0;

    QColor color = Qt::black;

    /// Opacity of the stamp, in the range [0, 1]
    PDFReal opacity = 1.0;

    bool operator==(const PDFStampTextStyle&) const = default;
};

/// Values of the variables, which can be used in the texts of the stamps:
///
///  - {page}  number of the page: \p startPageNumber for the first stamped page,
///            increased by one for each further stamped page
///  - {pages} number of the last stamped page (with the default settings
///            and all pages selected, it is the page count of the document)
///  - {bates} Bates number: \p batesPrefix, the number \p batesStart (increased by
///            one for each stamped page) padded by zeros to \p batesDigits digits,
///            and \p batesSuffix
///  - {date}  date of \p dateTime in the format \p dateFormat
///  - {time}  time of \p dateTime in the format \p timeFormat
///  - {file}  \p fileName
///  - {title} title of the document (document information dictionary)
///
/// "{{" is written as "{". Unknown variables are kept as they are.
struct PDF4QTLIBCORESHARED_EXPORT PDFStampVariables
{
    PDFInteger startPageNumber = 1;

    QString batesPrefix;
    QString batesSuffix;
    PDFInteger batesStart = 1;
    int batesDigits = 6;

    QDateTime dateTime = QDateTime::currentDateTime();
    QString dateFormat = QStringLiteral("MM/dd/yyyy");
    QString timeFormat = QStringLiteral("h:mm AP");
    QString fileName;
};

/// Settings of headers and footers. Bates numbering is a header or a footer
/// with the variable {bates} in one of the positions.
struct PDF4QTLIBCORESHARED_EXPORT PDFHeaderFooterSettings
{
    enum Position
    {
        HeaderLeft,
        HeaderCenter,
        HeaderRight,
        FooterLeft,
        FooterCenter,
        FooterRight,
        PositionCount
    };

    /// Texts of the positions (empty = nothing is written). A text can have
    /// more lines (separated by '\n').
    std::array<QString, PositionCount> texts;

    PDFStampTextStyle style;

    /// Distances of the text from the edges of the visible page (crop box) in
    /// points, in the orientation, in which the page is displayed (page rotation
    /// is taken into account, so the header is always at the top of the displayed page).
    QMarginsF margins = QMarginsF(36.0, 36.0, 36.0, 36.0);

    PDFStampVariables variables;

    /// Returns true, if no position has a text
    bool isEmpty() const;

    /// Returns a name of the position, used in the command line ("header-left", ...)
    static QString getPositionName(Position position);

    /// Returns the position of the name (see \p getPositionName), or PositionCount
    static Position getPositionFromName(const QString& name);
};

/// Settings of a watermark. A watermark is a text, or an image (when the text is empty).
struct PDF4QTLIBCORESHARED_EXPORT PDFWatermarkSettings
{
    PDFWatermarkSettings();

    /// Text of the watermark (more lines are separated by '\n'), variables can be used
    QString text;

    /// Image of the watermark, it is used, when the text is empty
    QImage image;

    PDFStampTextStyle style;

    /// Rotation of the watermark in degrees, counterclockwise
    PDFReal rotation = 45.0;

    /// Size of the watermark relative to the visible page: the rotated watermark
    /// fits into a rectangle of this fraction of the width and of the height of the page.
    /// It is used for images, and for texts with zero font size.
    PDFReal relativeSize = 0.5;

    /// Alignment of the watermark on the page (combination of the horizontal and of the vertical flag)
    Qt::Alignment alignment = Qt::AlignCenter;

    /// Distances from the edges of the visible page, used by the alignment to an edge
    QMarginsF margins = QMarginsF(36.0, 36.0, 36.0, 36.0);

    /// Shift of the watermark in points (positive values move it to the right and up)
    QPointF offset;

    /// The watermark is drawn behind the page content (otherwise over it)
    bool behindContent = false;

    PDFStampVariables variables;
};

/// Adds headers, footers, Bates numbers and watermarks to the pages of a document,
/// and removes them. A stamp is a form XObject drawn in the coordinate system of the
/// displayed page (origin at the bottom left corner of the visible page, rotated by the
/// page rotation, in points), so it is placed at the same position of each displayed page
/// regardless of the rotation, of the boxes and of the user unit of the page.
///
/// The original page content is enclosed between the streams "q" and "Q" (balanced, when the
/// original content is not), so a transformation left by the original content does not move
/// the stamps. Each stamp is drawn by its own content stream, marked as a pagination artifact.
/// The stamps are recorded in the page piece dictionary (/PieceInfo /PDF4QT_Stamp), so they can
/// be removed (or replaced) later without touching the original content.
class PDF4QTLIBCORESHARED_EXPORT PDFPageStamper
{
public:
    PDFPageStamper() = delete;

    enum StampKind
    {
        None = 0x0000,
        HeaderFooter = 0x0001,      ///< Headers and footers (also Bates numbers)
        Watermark = 0x0002,         ///< Watermarks
        AllKinds = HeaderFooter | Watermark
    };
    Q_DECLARE_FLAGS(StampKinds, StampKind)

    struct Result
    {
        /// Modified document, or nullptr, when the document was not changed (or an error occurred)
        PDFDocumentPointer document;

        /// Modification flags of the modified document
        PDFModifiedDocument::ModificationFlags flags;

        /// Number of the pages, onto which the stamp was added (or from which it was removed)
        PDFInteger pageCount = 0;

        /// Number of the stamps removed (from the replaced, or removed stamps)
        PDFInteger removedStampCount = 0;

        /// Bates numbers of the first and of the last stamped page (empty, when the
        /// text does not contain the variable {bates})
        QString firstBatesNumber;
        QString lastBatesNumber;

        /// Bates number, which follows the last stamped page (to continue numbering
        /// in the next document)
        PDFInteger nextBatesNumber = 0;

        /// Error message (empty, when the operation succeeded)
        QString errorMessage;

        bool isSuccess() const { return errorMessage.isEmpty(); }
    };

    /// Adds headers and footers to the pages. Bates numbers increase over the stamped pages.
    /// \param document Document
    /// \param settings Settings of headers and footers
    /// \param pageIndices Indices of the pages (zero based), empty vector means all pages
    /// \param replaceExisting Removes the headers and footers previously added by this class
    static Result addHeaderFooter(const PDFDocument* document,
                                  const PDFHeaderFooterSettings& settings,
                                  std::vector<PDFInteger> pageIndices,
                                  bool replaceExisting);

    /// Adds a watermark to the pages
    /// \param document Document
    /// \param settings Settings of the watermark
    /// \param pageIndices Indices of the pages (zero based), empty vector means all pages
    /// \param replaceExisting Removes the watermarks previously added by this class
    static Result addWatermark(const PDFDocument* document,
                               const PDFWatermarkSettings& settings,
                               std::vector<PDFInteger> pageIndices,
                               bool replaceExisting);

    /// Removes the stamps of the given kinds (added by this class) from the pages
    /// \param document Document
    /// \param kinds Kinds of the stamps
    /// \param pageIndices Indices of the pages (zero based), empty vector means all pages
    static Result removeStamps(const PDFDocument* document, StampKinds kinds, std::vector<PDFInteger> pageIndices);

    /// Returns the kinds of the stamps present on the page
    static StampKinds getStampKinds(const PDFDocument* document, PDFInteger pageIndex);

    /// Returns the number of the pages, which have a stamp of one of the given kinds
    static PDFInteger getStampedPageCount(const PDFDocument* document, StampKinds kinds);

    /// Formats the Bates number (prefix, number padded by zeros, suffix)
    static QString formatBatesNumber(const QString& prefix, PDFInteger number, int digits, const QString& suffix);

    /// Replaces the variables of the text (see \p PDFStampVariables)
    /// \param text Text with variables
    /// \param variables Values of the variables
    /// \param index Index of the page in the stamped pages (zero for the first stamped page)
    /// \param count Number of the stamped pages
    /// \param title Title of the document
    static QString expandVariables(const QString& text, const PDFStampVariables& variables, PDFInteger index, PDFInteger count, const QString& title);

    /// Returns the text with the characters, which can't be written by the stamp
    /// fonts (outside of WinAnsiEncoding and control characters), replaced by '?'
    static QString getRepresentableText(const QString& text);

    /// Returns the width of a single line of text in points
    static PDFReal getTextWidth(PDFStampFont font, const QString& text, PDFReal fontSize);

    /// Returns the ascent of the font (positive, relative to the font size 1)
    static PDFReal getFontAscent(PDFStampFont font);

    /// Returns the descent of the font (negative, relative to the font size 1)
    static PDFReal getFontDescent(PDFStampFont font);

    /// Returns PostScript name of the standard font
    static QByteArray getFontName(PDFStampFont font);

    /// Returns the font of the name: PostScript name ("Helvetica-Bold") or a name
    /// used in the command line ("helvetica-bold", "times", "courier", ...)
    static std::optional<PDFStampFont> getFontFromName(const QString& name);

    /// Returns the size of the stamp coordinate system (the visible page,
    /// as it is displayed, in points)
    static QSizeF getStampSpaceSize(const PDFPage* page);

    /// Returns the matrix, which maps the stamp coordinate system (origin at the
    /// bottom left corner of the displayed visible page, y axis upwards, points)
    /// to the user space of the page
    static QTransform getStampSpaceToPageMatrix(const PDFPage* page);
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFPageStamper::StampKinds)

#endif // PDFPAGESTAMPER_H
