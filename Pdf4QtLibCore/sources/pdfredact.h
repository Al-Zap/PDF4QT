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


#ifndef PDFREDACT_H
#define PDFREDACT_H

#include "pdfdocument.h"
#include "pdfrenderer.h"
#include "pdftextlayout.h"

#include <QColor>
#include <QPolygonF>
#include <QRegularExpression>

namespace pdf
{
class PDFDocumentBuilder;

/// Applies the redaction marks (redact annotations) of the document. The content
/// under the marks is removed from the page content - characters of the text
/// (also of an invisible text, for example of an OCR layer), the parts of the paths
/// and the pixels of the images - and the marked areas are filled by the color
/// of the marks. Annotations and form fields lying in the marked areas are removed,
/// together with the redaction marks. Content outside the marks is kept (text
/// remains text), and pages without marks are not changed at all. Objects, which
/// are no longer used (original content streams, forms and images), are removed
/// from the document, so the removed content can't be found in the written file.
class PDF4QTLIBCORESHARED_EXPORT PDFRedact
{
public:
    /// Creates the redaction processor
    /// \param document Document with redaction marks
    /// \param fontCache Font cache
    /// \param iccProfileCache ICC profile cache (can be nullptr)
    /// \param cms Color management system
    /// \param optionalContentActivity Optional content activity
    /// \param meshQualitySettings Mesh quality settings
    /// \param redactFillColor Color used to fill all marked areas. If it is
    ///        invalid, each area is filled by the interior color of its mark
    ///        (and the area is left unfilled, if the mark has no interior color).
    explicit PDFRedact(const PDFDocument* document,
                       const PDFFontCache* fontCache,
                       const PDFICCProfileCache* iccProfileCache,
                       const PDFCMS* cms,
                       const PDFOptionalContentActivity* optionalContentActivity,
                       const PDFMeshQualitySettings* meshQualitySettings,
                       QColor redactFillColor);

    enum Option
    {
        None            = 0x0000,
        CopyTitle       = 0x0001,   ///< Keep the document title
        CopyMetadata    = 0x0002,   ///< Keep the document information (other than title) and the XMP metadata
        CopyOutline     = 0x0004,   ///< Keep the outline (bookmarks)
        KeepDocumentData = CopyTitle | CopyMetadata | CopyOutline
    };
    Q_DECLARE_FLAGS(Options, Option)

    /// Statistics of the performed redaction
    struct Statistics
    {
        int markCount = 0;              ///< Number of applied redaction marks
        int pageCount = 0;              ///< Number of redacted pages
        int removedCharacterCount = 0;  ///< Number of removed characters
        int redactedImageCount = 0;     ///< Number of images, whose pixels were removed
        int removedImageCount = 0;      ///< Number of images removed completely
        int redactedPathCount = 0;      ///< Number of paths, which were cut by the marks
        int removedPathCount = 0;       ///< Number of paths removed completely
        int redactedShadingCount = 0;   ///< Number of shadings clipped by the marks
        int removedAnnotationCount = 0; ///< Number of removed annotations (redaction marks excluded)
    };

    /// Applies the redaction marks and returns the redacted document
    /// \param options Options, which document data are kept
    PDFDocument perform(Options options);

    /// Returns statistics of the last \p perform call
    const Statistics& getStatistics() const { return m_statistics; }

    /// Returns errors (with page numbers) reported during the last \p perform call
    const QStringList& getErrors() const { return m_errors; }

    /// Returns number of redaction marks (redact annotations) in the document
    /// \param document Document
    static int getRedactionMarkCount(const PDFDocument* document);

    /// Fraction of the area of a character, which must be covered by the redaction
    /// mark, so the character is removed. A character is removed also, if its center
    /// lies in the mark.
    static constexpr PDFReal CHARACTER_COVERAGE_LIMIT = 0.2;

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFMeshQualitySettings* m_meshQualitySettings;
    QColor m_redactFillColor;
    Statistics m_statistics;
    QStringList m_errors;
};

/// Finds text to be redacted (plain text, regular expressions and predefined
/// patterns, such as social security numbers or e-mail addresses) and creates
/// redaction marks for the found occurrences.
class PDF4QTLIBCORESHARED_EXPORT PDFRedactSearch
{
public:
    PDFRedactSearch() = delete;

    enum class Pattern
    {
        None,
        SocialSecurityNumber,
        PhoneNumber,
        EmailAddress,
        CreditCardNumber,
        Date
    };

    struct Settings
    {
        QString text;                       ///< Searched text (or regular expression)
        Pattern pattern = Pattern::None;    ///< Predefined pattern (used, if text is empty)
        bool caseSensitive = false;
        bool wholeWords = false;
        bool regularExpression = false;     ///< Text is a regular expression
    };

    /// Occurrence of the searched text on a single page
    struct Match
    {
        PDFInteger pageIndex = -1;
        QString matched;            ///< Matched text
        QString context;            ///< Text around the match
        QPolygonF quadrilaterals;   ///< Quadrilaterals of the match in the page space (four points each, in order of the QuadPoints entry)

        QRectF getBoundingRect() const { return quadrilaterals.boundingRect(); }
    };
    using Matches = std::vector<Match>;

    /// Returns all predefined patterns (excluding None)
    static std::vector<Pattern> getPatterns();

    /// Returns translated name of the pattern
    static QString getPatternName(Pattern pattern);

    /// Returns command line name of the pattern (for example "ssn")
    static QString getPatternKey(Pattern pattern);

    /// Returns pattern by its command line name (Pattern::None, if not found)
    static Pattern getPatternByKey(const QString& key);

    /// Returns the regular expression of the pattern
    static QString getPatternExpression(Pattern pattern);

    /// Creates regular expression for the settings. If the settings are invalid
    /// (empty text without a pattern, invalid regular expression), an empty
    /// expression is returned (see \p isValidExpression) and the error message is set.
    static QRegularExpression createExpression(const Settings& settings, QString* errorMessage);

    /// Returns true, if the expression is valid and not empty
    static bool isValidExpression(const QRegularExpression& expression);

    /// Finds all occurrences of the expression in the text layouts. Each occurrence
    /// is split into the pages, on which it lies.
    /// \param storage Text layouts of all pages
    /// \param expression Regular expression
    static Matches find(const PDFTextLayoutStorage& storage, const QRegularExpression& expression);

    /// Creates text layouts of all pages of the document (pages are processed in parallel)
    static PDFTextLayoutStorage createTextLayouts(const PDFDocument* document,
                                                  const PDFFontCache* fontCache,
                                                  const PDFICCProfileCache* iccProfileCache,
                                                  const PDFCMS* cms,
                                                  const PDFOptionalContentActivity* optionalContentActivity,
                                                  const PDFMeshQualitySettings& meshQualitySettings);

    /// Creates redaction marks for the matches. Returns number of created marks.
    /// \param builder Builder of the document, in which the matches were found
    /// \param document Document, in which the matches were found
    /// \param matches Matches
    /// \param color Fill and outline color of the marks
    static int createMarks(PDFDocumentBuilder* builder, const PDFDocument* document, const Matches& matches, QColor color);

    /// Creates redaction marks covering the whole media boxes
    /// of the pages. Returns number of created marks.
    /// \param builder Document builder
    /// \param document Document
    /// \param pageIndices Zero based page indices (invalid ones are ignored)
    /// \param color Fill and outline color of the marks
    static int createPageMarks(PDFDocumentBuilder* builder, const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, QColor color);
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRedact::Options)

#endif // PDFREDACT_H
