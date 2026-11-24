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

#ifndef PDFMARKUPSLIST_H
#define PDFMARKUPSLIST_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QColor>
#include <QRectF>
#include <QDateTime>
#include <QStringList>

#include <array>
#include <cmath>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;
class PDFAnnotation;

/// Review status of a markup. The status is stored in the document the same way
/// other PDF applications store it - as a hidden reply (text annotation with
/// the entries IRT, StateModel /Review and State), so the status is visible
/// in other applications and it is kept together with the comment thread.
/// The most recent state reply defines the status.
enum class PDFMarkupStatus
{
    Open,       ///< No status set (state None of the review model)
    Accepted,   ///< State Accepted
    Rejected,   ///< State Rejected
    Cancelled,  ///< State Cancelled
    Done,       ///< State Completed
    LastStatus
};

/// Kind of a markup in the list. It is more specific than the annotation type -
/// a polygon can be a cloud or an area measurement, a circle can be a counted
/// fixture etc.
enum class PDFMarkupKind
{
    Note,           ///< Sticky note (text annotation)
    TextBox,        ///< Free text
    Callout,        ///< Free text with a callout line
    Typewriter,     ///< Free text with the typewriter intent
    Line,
    Arrow,
    Rectangle,
    Ellipse,
    Polygon,
    Polyline,
    Cloud,          ///< Cloudy polygon or rectangle (revision cloud)
    Ink,            ///< Freehand drawing
    TextMarkup,     ///< Highlight, underline, strikeout, squiggly
    Stamp,
    Count,          ///< Marker of the fixture count
    Length,         ///< Measured length (dimension line, polyline)
    Area,           ///< Measured area (and perimeter) of a polygon
    Angle,          ///< Measured angle
    Other
};

/// Single markup of the document (a row of the markups list)
struct PDF4QTLIBCORESHARED_EXPORT PDFMarkupsListItem
{
    PDFObjectReference annotation;
    PDFInteger pageIndex = -1;
    QString sheet;              ///< Name of the sheet (page label, bookmark), or "Page N"
    PDFMarkupKind kind = PDFMarkupKind::Other;
    QString subject;            ///< Subject of the annotation (Subj), fixture type of a count marker
    QString contents;           ///< Text of the comment (Contents)
    QString author;             ///< Author (T)
    QDateTime modified;         ///< Last modification (M), or the creation date
    QColor color;               ///< Color of the markup (invalid, if not defined)
    QRectF rectangle;           ///< Rectangle of the annotation on the page
    PDFMarkupStatus status = PDFMarkupStatus::Open;
    QString statusAuthor;       ///< Author of the most recent state reply
    QDateTime statusDate;       ///< Date of the most recent state reply
    int replyCount = 0;         ///< Count of the replies (state replies are not counted)
    QStringList replies;        ///< Texts of the replies ("Author: text"), oldest first

    /// Measured values. Length is the length of a line or polyline, or the
    /// perimeter of a polygon. NaN, if the markup does not measure the quantity.
    PDFReal length = qQNaN();
    QString lengthUnit;
    PDFReal area = qQNaN();
    QString areaUnit;
    PDFReal angle = qQNaN();

    bool hasLength() const { return !std::isnan(length); }
    bool hasArea() const { return !std::isnan(area); }
    bool hasAngle() const { return !std::isnan(angle); }

    /// Label of the markup in the takeoff summary - subject, or the kind
    QString getLabel() const;
};

using PDFMarkupsListItems = std::vector<PDFMarkupsListItem>;

/// Row of the takeoff summary. Markups with the same label, kind and units are
/// summed up (for example all counted fixtures of type A1, all measured lengths
/// with the subject "Conduit").
struct PDF4QTLIBCORESHARED_EXPORT PDFMarkupsSummaryItem
{
    QString label;
    PDFMarkupKind kind = PDFMarkupKind::Other;
    int count = 0;
    PDFReal length = 0.0;
    QString lengthUnit;
    PDFReal area = 0.0;
    QString areaUnit;
    bool hasLength = false;
    bool hasArea = false;
    std::array<int, size_t(PDFMarkupStatus::LastStatus)> statusCounts = { };
};

using PDFMarkupsSummaryItems = std::vector<PDFMarkupsSummaryItem>;

/// Markups list - one table of all comments, markups, counts and measurements
/// of all pages of the document, with a review status of each of them, a takeoff
/// summary and exports to CSV and to a PDF report.
class PDF4QTLIBCORESHARED_EXPORT PDFMarkupsList
{
public:
    PDFMarkupsList() = delete;

    /// Columns of the list (and of the exported table)
    enum Column
    {
        ColumnSheet,
        ColumnKind,
        ColumnSubject,
        ColumnContents,
        ColumnMeasurement,
        ColumnStatus,
        ColumnAuthor,
        ColumnDate,
        ColumnReplies,
        ColumnCount
    };

    /// Collects the markups of all pages of the document. Popup annotations,
    /// replies, links, form fields and other non-markup annotations are not
    /// listed. The items are ordered by the page and by the order of the
    /// annotations on the page.
    /// \param document Document (can be nullptr)
    static PDFMarkupsListItems collect(const PDFDocument* document);

    /// Creates the takeoff summary of the markups. Rows are ordered by the kind
    /// (counts first, then measurements, then the others) and by the label.
    /// \param items Markups
    static PDFMarkupsSummaryItems summarize(const PDFMarkupsListItems& items);

    /// Returns the name of each page. Sheet number is taken from the page label
    /// (plan sets are labelled by the sheet numbers, AutoCAD writes "[1] Name") or
    /// from the bookmark pointing to the page. Pages without a name get "Page N".
    static QStringList getSheetNames(const PDFDocument* document);

    /// Sets the status of the markup - a hidden state reply is added to the page
    /// of the markup. Returns reference of the reply, or an invalid reference, if
    /// the annotation can't be found.
    /// \param builder Builder of the document
    /// \param annotation Markup annotation
    /// \param status New status
    /// \param author Author of the change of the status
    static PDFObjectReference setStatus(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFMarkupStatus status, const QString& author);

    /// Returns the status defined by the state reply (state of the review model)
    static PDFMarkupStatus getStatusFromState(const QString& state);

    /// Returns the state name of the review model (Accepted, Completed, None, ...)
    static QByteArray getStateName(PDFMarkupStatus status);

    /// Returns the translated name of the status
    static QString getStatusName(PDFMarkupStatus status);

    /// Returns the translated name of the kind
    static QString getKindName(PDFMarkupKind kind);

    /// Returns the translated title of the column
    static QString getColumnTitle(Column column);

    /// Returns the text of the measurement ("12.5 ft", "A = 30 sq ft, P = 22 ft")
    static QString getMeasurementText(const PDFMarkupsListItem& item);

    /// Returns the text of the cell of the list
    static QString getCellText(const PDFMarkupsListItem& item, Column column);

    /// Formats a measured value with up to two decimals
    static QString formatValue(PDFReal value);

    /// Creates the table of the markups (header + one row for each markup).
    /// Measured values are in separate columns (length, unit, area, unit), so the
    /// table can be summed up in a spreadsheet.
    static std::vector<QStringList> createTable(const PDFMarkupsListItems& items);

    /// Creates the table of the takeoff summary (header + one row for each summary row)
    static std::vector<QStringList> createSummaryTable(const PDFMarkupsSummaryItems& items);

    /// Escapes the field of a CSV file. Fields starting with a character, which
    /// spreadsheets evaluate as a formula, are prefixed by an apostrophe.
    static QString escapeCsvField(const QString& fieldText);

    /// Creates CSV text of the table (fields separated by the separator, rows by CRLF)
    static QString createCsv(const std::vector<QStringList>& table, QChar separator = QChar(','));

    /// Writes a PDF report: title, takeoff summary and the list of the markups.
    /// \param fileName Name of the written file
    /// \param title Title of the report (name of the document)
    /// \param items Markups
    /// \param errorMessage Error message, if the report can't be written
    /// \returns true, if the report has been written
    static bool writePdfReport(const QString& fileName, const QString& title, const PDFMarkupsListItems& items, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFMARKUPSLIST_H
