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

#include "pdfdocxexporter.h"
#include "pdfdocument.h"
#include "pdfannotation.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdfpagecontentprocessor.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfexecutionpolicy.h"
#include "pdfconstants.h"
#include "pdfoperationcontrol.h"
#include "pdfexception.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QtMath>
#include <QXmlStreamWriter>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>

#include "pdfdbgheap.h"

namespace pdf
{

/// Text style of a character (without the font size)
struct PDFDocxTextStyle
{
    QString fontFamily;
    bool bold = false;
    bool italic = false;
    QRgb color = qRgb(0, 0, 0);

    bool operator==(const PDFDocxTextStyle& other) const
    {
        return fontFamily == other.fontFamily && bold == other.bold && italic == other.italic && color == other.color;
    }
};

/// Character of the page in the page coordinates (points, origin is the top left
/// corner of the visible page, y axis goes down, position is on the baseline)
struct PDFDocxCharacter
{
    QChar character;
    QPointF position;
    PDFReal advance = 0.0;
    PDFReal fontSize = 0.0;
    int style = 0;
    bool spaceBefore = false;
    bool invisible = false;
};

using PDFDocxCharacters = std::vector<PDFDocxCharacter>;

/// Run of the text with the same style
struct PDFDocxRun
{
    QString text;
    int style = 0;
    int halfPoints = 22;
    bool isTab = false;
};

/// Paragraph of the Word document. Positions are in points, horizontal
/// positions are measured from the left edge of the page.
struct PDFDocxParagraph
{
    PDFReal top = 0.0;          ///< Desired top of the first line
    PDFReal lineHeight = 0.0;   ///< Exact line height
    int lineCount = 1;          ///< Number of lines in the PDF
    PDFReal left = 0.0;         ///< Left edge of the text
    PDFReal right = 0.0;        ///< Right edge of the text (for reflowing paragraphs)
    bool reflow = false;        ///< Paragraph is joined from more lines
    std::vector<PDFReal> tabs;  ///< Positions of the tab stops
    std::vector<PDFDocxRun> runs;
};

/// Exported page
struct PDFDocxPage
{
    PDFReal width = 612.0;
    PDFReal height = 792.0;
    PDFReal marginLeft = 0.0;
    PDFReal marginTop = 0.0;
    PDFReal marginRight = 0.0;
    PDFReal marginBottom = 0.0;
    std::vector<PDFDocxTextStyle> styles;
    std::vector<PDFDocxParagraph> paragraphs;
    QByteArray backgroundPng;
};

/// Line of the text, characters are sorted by x coordinate
struct PDFDocxLine
{
    PDFReal baseline = 0.0;
    PDFReal fontSize = 0.0;
    PDFDocxCharacters characters;

    PDFReal left() const { return characters.empty() ? 0.0 : characters.front().position.x(); }
    PDFReal right() const
    {
        PDFReal result = 0.0;
        for (const PDFDocxCharacter& character : characters)
        {
            result = qMax(result, character.position.x() + character.advance);
        }
        return result;
    }
};

/// Collects the characters of the page content (and of the appearance
/// streams of the annotations) together with their style.
class PDFDocxTextCollector : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFDocxTextCollector(const PDFPage* page,
                                  const PDFDocument* document,
                                  const PDFFontCache* fontCache,
                                  const PDFICCProfileCache* iccProfileCache,
                                  const PDFCMS* cms,
                                  const PDFOptionalContentActivity* optionalContentActivity,
                                  QTransform pagePointToDevicePointMatrix,
                                  const PDFMeshQualitySettings& meshQualitySettings,
                                  PDFDocxCharacters* characters,
                                  std::vector<PDFDocxTextStyle>* styles) :
        BaseClass(page, document, fontCache, iccProfileCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_pageMatrix(pagePointToDevicePointMatrix),
        m_characters(characters),
        m_styles(styles)
    {

    }

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override;

private:
    int getCurrentStyle();

    QTransform m_pageMatrix;
    PDFDocxCharacters* m_characters;
    std::vector<PDFDocxTextStyle>* m_styles;
    bool m_spaceBefore = false;
    const PDFFont* m_lastFont = nullptr;
    QRgb m_lastColor = 0;
    TextRenderingMode m_lastRenderingMode = TextRenderingMode::Fill;
    int m_lastStyle = -1;
};

/// Paints the page without the text (graphics of the background picture)
class PDFDocxGraphicsPainter : public PDFPainter
{
    using BaseClass = PDFPainter;

public:
    using BaseClass::BaseClass;

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override { return kind == ContentKind::Text; }
    virtual bool performTextCharacterDrawing(const PDFRealTextDrawInfo& info) override { Q_UNUSED(info); return true; }
};

/// Helper functions of the export
class PDFDocxExporterHelper
{
public:
    PDFDocxExporterHelper() = delete;

    /// Font family, bold and italic of the font
    static void getFontStyle(const PDFFont* font, PDFDocxTextStyle& style);

    /// Converts the PostScript name of a font into a font family used by Word
    static QString getFontFamily(QByteArray fontName, bool* bold, bool* italic);

    /// Groups the characters into lines
    static std::vector<PDFDocxLine> createLines(PDFDocxCharacters characters);

    /// Creates paragraphs from the lines
    static std::vector<PDFDocxParagraph> createParagraphs(const std::vector<PDFDocxLine>& lines, bool reflow);

    /// Appends text of the characters as runs into the paragraph
    static void appendRuns(PDFDocxParagraph& paragraph, const PDFDocxCharacter* begin, const PDFDocxCharacter* end, bool spaceAtStart);

    /// Returns true, if the character can be written into the XML
    static bool isValidCharacter(QChar character);

    /// Processes appearance streams of the annotations of the page by the processor
    /// created by the factory (each annotation by its own processor)
    static void processAnnotations(const PDFDocument* document,
                                   const PDFPage* page,
                                   const std::function<std::unique_ptr<PDFPageContentProcessor>(void)>& factory);

    /// Returns true, if all pixels of the image are white
    static bool isWhiteImage(const QImage& image);

    static QByteArray createContentTypes(const std::vector<PDFDocxPage>& pages);
    static QByteArray createRootRelationships();
    static QByteArray createDocumentRelationships(const std::vector<PDFDocxPage>& pages);
    static QByteArray createStyles();
    static QByteArray createSettings();
    static QByteArray createCoreProperties(const PDFDocument* document);
    static QByteArray createAppProperties();
    static QByteArray createDocument(const std::vector<PDFDocxPage>& pages);

    static void writeParagraph(QXmlStreamWriter& writer, const PDFDocxPage& page, const PDFDocxParagraph& paragraph, PDFReal spacingBefore, PDFReal rightIndent, const PDFDocxPage* sectionPage, bool framed);
    static void writeBackground(QXmlStreamWriter& writer, const PDFDocxPage& page, size_t pageIndex);
    static void writeSectionProperties(QXmlStreamWriter& writer, const PDFDocxPage& page);

    /// Height of the empty paragraphs (background picture, section break)
    static constexpr PDFReal PARAGRAPH_MARK_HEIGHT = 1.0;

    static int toTwips(PDFReal points) { return qRound(points * 20.0); }
    static qint64 toEmu(PDFReal points) { return qRound64(points * 12700.0); }
    static QString number(qint64 value) { return QString::number(value); }

    static constexpr const char* NS_W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    static constexpr const char* NS_R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    static constexpr const char* NS_WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
    static constexpr const char* NS_A = "http://schemas.openxmlformats.org/drawingml/2006/main";
    static constexpr const char* NS_PIC = "http://schemas.openxmlformats.org/drawingml/2006/picture";
    static constexpr const char* NS_RELATIONSHIPS = "http://schemas.openxmlformats.org/package/2006/relationships";
};

bool PDFDocxTextCollector::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shapes:
        case ContentKind::Text:
        case ContentKind::Images:
        case ContentKind::Shading:
            return true;

        default:
            break;
    }

    // Tiling patterns and forms can contain a text
    return false;
}

int PDFDocxTextCollector::getCurrentStyle()
{
    const PDFPageContentProcessorState* state = getGraphicState();
    const PDFFont* font = state->getTextFont().get();
    const QRgb color = state->getFillColor().rgb();
    const TextRenderingMode renderingMode = state->getTextRenderingMode();

    if (m_lastStyle >= 0 && font == m_lastFont && color == m_lastColor && renderingMode == m_lastRenderingMode)
    {
        return m_lastStyle;
    }

    PDFDocxTextStyle style;
    PDFDocxExporterHelper::getFontStyle(font, style);
    style.color = color;

    if (renderingMode == TextRenderingMode::FillStroke || renderingMode == TextRenderingMode::FillStrokeClip)
    {
        // Fake bold (the glyphs are filled and stroked)
        style.bold = true;
    }

    auto it = std::find(m_styles->cbegin(), m_styles->cend(), style);
    if (it == m_styles->cend())
    {
        m_styles->push_back(style);
        it = std::prev(m_styles->cend());
    }

    m_lastFont = font;
    m_lastColor = color;
    m_lastRenderingMode = renderingMode;
    m_lastStyle = int(std::distance(m_styles->cbegin(), it));
    return m_lastStyle;
}

void PDFDocxTextCollector::performOutputCharacter(const PDFTextCharacterInfo& info)
{
    if (isContentSuppressed() || info.isVerticalWritingSystem)
    {
        return;
    }

    if (info.character.isSpace())
    {
        m_spaceBefore = true;
        return;
    }

    if (!PDFDocxExporterHelper::isValidCharacter(info.character))
    {
        return;
    }

    // Matrix of the character maps to the user space of the page, page matrix
    // then maps the user space to the visible page.
    const QTransform matrix = info.matrix * m_pageMatrix;

    // Only horizontal text (in the visible page) is exported, rotated
    // labels would be broken into single characters.
    const QLineF advanceLine = matrix.map(QLineF(QPointF(0.0, 0.0), QPointF(info.advance != 0.0 ? info.advance : 1.0, 0.0)));
    PDFReal angle = advanceLine.angle();
    if (angle > 180.0)
    {
        angle -= 360.0;
    }

    if (std::abs(angle) > 3.0)
    {
        m_spaceBefore = false;
        return;
    }

    const QLineF fontLine = matrix.map(QLineF(QPointF(0.0, 0.0), QPointF(0.0, info.fontSize)));

    PDFDocxCharacter character;
    character.character = info.character;
    character.position = matrix.map(QPointF(0.0, 0.0));
    character.advance = info.advance != 0.0 ? advanceLine.length() : 0.0;
    character.fontSize = fontLine.length();
    character.style = getCurrentStyle();
    character.spaceBefore = m_spaceBefore;

    const TextRenderingMode renderingMode = getGraphicState()->getTextRenderingMode();
    character.invisible = renderingMode == TextRenderingMode::Invisible || renderingMode == TextRenderingMode::Clip;

    m_spaceBefore = false;

    if (character.fontSize < 0.5)
    {
        // Text of zero size can't be read
        return;
    }

    m_characters->push_back(character);
}

bool PDFDocxExporterHelper::isValidCharacter(QChar character)
{
    if (character.isNull() || character.isSurrogate())
    {
        return false;
    }

    const char16_t unicode = character.unicode();
    if (unicode < 0x20 || unicode == 0xFFFE || unicode == 0xFFFF)
    {
        return false;
    }

    if (unicode == 0x00AD)
    {
        // Soft hyphen
        return false;
    }

    return true;
}

void PDFDocxExporterHelper::getFontStyle(const PDFFont* font, PDFDocxTextStyle& style)
{
    style.fontFamily = QStringLiteral("Arial");
    style.bold = false;
    style.italic = false;

    if (!font)
    {
        return;
    }

    const FontDescriptor* descriptor = font->getFontDescriptor();
    QByteArray fontName = descriptor->fontName;

    if (fontName.isEmpty())
    {
        if (const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font))
        {
            switch (simpleFont->getStandardFontType())
            {
                case StandardFontType::TimesRoman:
                    fontName = "Times-Roman";
                    break;
                case StandardFontType::TimesRomanBold:
                    fontName = "Times-Bold";
                    break;
                case StandardFontType::TimesRomanItalics:
                    fontName = "Times-Italic";
                    break;
                case StandardFontType::TimesRomanBoldItalics:
                    fontName = "Times-BoldItalic";
                    break;
                case StandardFontType::Helvetica:
                    fontName = "Helvetica";
                    break;
                case StandardFontType::HelveticaBold:
                    fontName = "Helvetica-Bold";
                    break;
                case StandardFontType::HelveticaOblique:
                    fontName = "Helvetica-Oblique";
                    break;
                case StandardFontType::HelveticaBoldOblique:
                    fontName = "Helvetica-BoldOblique";
                    break;
                case StandardFontType::Courier:
                    fontName = "Courier";
                    break;
                case StandardFontType::CourierBold:
                    fontName = "Courier-Bold";
                    break;
                case StandardFontType::CourierOblique:
                    fontName = "Courier-Oblique";
                    break;
                case StandardFontType::CourierBoldOblique:
                    fontName = "Courier-BoldOblique";
                    break;
                case StandardFontType::Symbol:
                    fontName = "Symbol";
                    break;
                case StandardFontType::ZapfDingbats:
                    fontName = "ZapfDingbats";
                    break;
                case StandardFontType::Invalid:
                    break;
            }
        }
    }

    if (fontName.isEmpty())
    {
        fontName = descriptor->fontFamily;
    }

    bool bold = false;
    bool italic = false;
    if (!fontName.isEmpty())
    {
        style.fontFamily = getFontFamily(fontName, &bold, &italic);
    }

    style.bold = bold || descriptor->fontWeight >= 600.0 || descriptor->isForceBold();
    style.italic = italic || descriptor->isItalic() || std::abs(descriptor->italicAngle) > 1.0;
}

QString PDFDocxExporterHelper::getFontFamily(QByteArray fontName, bool* bold, bool* italic)
{
    // Subset prefix (ABCDEF+FontName)
    const qsizetype plusIndex = fontName.indexOf('+');
    if (plusIndex == 6)
    {
        fontName = fontName.mid(plusIndex + 1);
    }

    QString name = QString::fromLatin1(fontName);
    const QString lowerName = name.toLower();

    *bold = lowerName.contains(QLatin1String("bold")) ||
            lowerName.contains(QLatin1String("black")) ||
            lowerName.contains(QLatin1String("heavy")) ||
            lowerName.contains(QLatin1String("semibold")) ||
            lowerName.contains(QLatin1String("demi"));
    *italic = lowerName.contains(QLatin1String("italic")) ||
              lowerName.contains(QLatin1String("oblique"));

    // Family name is the part before the style (Arial-BoldMT, Arial,Bold)
    qsizetype styleIndex = -1;
    for (QChar separator : { QChar('-'), QChar(',') })
    {
        const qsizetype index = name.indexOf(separator);
        if (index > 0 && (styleIndex == -1 || index < styleIndex))
        {
            styleIndex = index;
        }
    }

    QString family = styleIndex > 0 ? name.left(styleIndex) : name;

    for (const QLatin1String& suffix : { QLatin1String("PSMT"), QLatin1String("MT"), QLatin1String("PS") })
    {
        if (family.size() > suffix.size() && family.endsWith(suffix))
        {
            family.chop(suffix.size());
            break;
        }
    }

    const QString lowerFamily = family.toLower();
    if (lowerFamily.startsWith(QLatin1String("helvetica")) || lowerFamily == QLatin1String("arial"))
    {
        return QStringLiteral("Arial");
    }
    if (lowerFamily.startsWith(QLatin1String("times")))
    {
        return QStringLiteral("Times New Roman");
    }
    if (lowerFamily.startsWith(QLatin1String("courier")))
    {
        return QStringLiteral("Courier New");
    }
    if (lowerFamily == QLatin1String("zapfdingbats"))
    {
        return QStringLiteral("Wingdings");
    }

    // Font names without spaces (ArialNarrow, CenturyGothic) - a space is
    // inserted before each upper case letter, which follows a lower case letter.
    QString result;
    result.reserve(family.size() + 4);
    for (qsizetype i = 0; i < family.size(); ++i)
    {
        const QChar character = family.at(i);
        if (i > 0 && character.isUpper() && family.at(i - 1).isLower())
        {
            result.append(QChar(' '));
        }
        result.append(character);
    }

    // Style names, which are not separated by a hyphen (ArialBold)
    for (const QLatin1String& styleName : { QLatin1String(" Bold"), QLatin1String(" Italic"), QLatin1String(" Oblique") })
    {
        if (result.endsWith(styleName))
        {
            result.chop(styleName.size());
        }
    }

    return result.trimmed().isEmpty() ? QStringLiteral("Arial") : result.trimmed();
}

std::vector<PDFDocxLine> PDFDocxExporterHelper::createLines(PDFDocxCharacters characters)
{
    std::vector<PDFDocxLine> lines;

    std::stable_sort(characters.begin(), characters.end(), [](const PDFDocxCharacter& l, const PDFDocxCharacter& r) { return l.position.y() < r.position.y(); });

    // 1) Characters with nearly the same baseline form a line
    for (const PDFDocxCharacter& character : characters)
    {
        if (!lines.empty())
        {
            PDFDocxLine& line = lines.back();
            const PDFReal tolerance = 0.35 * qMax(line.fontSize, character.fontSize);
            if (std::abs(character.position.y() - line.baseline) <= tolerance)
            {
                line.characters.push_back(character);
                line.fontSize = qMax(line.fontSize, character.fontSize);
                continue;
            }
        }

        PDFDocxLine line;
        line.baseline = character.position.y();
        line.fontSize = character.fontSize;
        line.characters.push_back(character);
        lines.emplace_back(std::move(line));
    }

    for (PDFDocxLine& line : lines)
    {
        std::stable_sort(line.characters.begin(), line.characters.end(), [](const PDFDocxCharacter& l, const PDFDocxCharacter& r) { return l.position.x() < r.position.x(); });

        // Remove duplicated characters (some producers draw the text twice,
        // slightly shifted, to get a bold text)
        PDFDocxCharacters filtered;
        filtered.reserve(line.characters.size());
        for (const PDFDocxCharacter& character : line.characters)
        {
            bool isDuplicate = false;
            for (auto it = filtered.rbegin(); it != filtered.rend() && character.position.x() - it->position.x() < 0.2 * character.fontSize; ++it)
            {
                if (it->character == character.character &&
                    std::abs(it->position.x() - character.position.x()) < 0.2 * character.fontSize &&
                    std::abs(it->position.y() - character.position.y()) < 0.2 * character.fontSize)
                {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate)
            {
                filtered.push_back(character);
            }
        }
        line.characters = std::move(filtered);
    }

    // 2) Lines, which overlap vertically, but not horizontally (columns of a table
    //    with a slightly different baseline), are joined, so the columns are laid
    //    out by tab stops in a single paragraph and the vertical layout is kept.
    std::vector<PDFDocxLine> joinedLines;
    for (PDFDocxLine& line : lines)
    {
        if (!joinedLines.empty())
        {
            PDFDocxLine& previousLine = joinedLines.back();
            const PDFReal tolerance = 0.75 * qMax(previousLine.fontSize, line.fontSize);
            if (std::abs(line.baseline - previousLine.baseline) <= tolerance)
            {
                bool overlaps = false;
                for (const PDFDocxCharacter& character : line.characters)
                {
                    const PDFReal left = character.position.x();
                    const PDFReal right = left + qMax(character.advance, 0.1 * character.fontSize);
                    for (const PDFDocxCharacter& previousCharacter : previousLine.characters)
                    {
                        const PDFReal previousLeft = previousCharacter.position.x();
                        const PDFReal previousRight = previousLeft + qMax(previousCharacter.advance, 0.1 * previousCharacter.fontSize);
                        if (left < previousRight && previousLeft < right)
                        {
                            overlaps = true;
                            break;
                        }
                    }

                    if (overlaps)
                    {
                        break;
                    }
                }

                if (!overlaps)
                {
                    previousLine.characters.insert(previousLine.characters.end(), line.characters.begin(), line.characters.end());
                    std::stable_sort(previousLine.characters.begin(), previousLine.characters.end(), [](const PDFDocxCharacter& l, const PDFDocxCharacter& r) { return l.position.x() < r.position.x(); });
                    previousLine.fontSize = qMax(previousLine.fontSize, line.fontSize);
                    continue;
                }
            }
        }

        joinedLines.emplace_back(std::move(line));
    }

    return joinedLines;
}

void PDFDocxExporterHelper::appendRuns(PDFDocxParagraph& paragraph, const PDFDocxCharacter* begin, const PDFDocxCharacter* end, bool spaceAtStart)
{
    const PDFDocxCharacter* previous = nullptr;
    for (const PDFDocxCharacter* it = begin; it != end; ++it)
    {
        const PDFDocxCharacter& character = *it;

        bool space = (it == begin) ? spaceAtStart : false;
        if (previous)
        {
            const PDFReal gap = character.position.x() - (previous->position.x() + previous->advance);
            const PDFReal fontSize = qMax(character.fontSize, previous->fontSize);
            space = gap > 0.2 * fontSize || (character.spaceBefore && gap > -0.1 * fontSize);
        }

        const int halfPoints = qBound(2, qRound(character.fontSize * 2.0), 3276);

        if (paragraph.runs.empty() || paragraph.runs.back().isTab || paragraph.runs.back().style != character.style || paragraph.runs.back().halfPoints != halfPoints)
        {
            PDFDocxRun run;
            run.style = character.style;
            run.halfPoints = halfPoints;
            paragraph.runs.emplace_back(std::move(run));
        }

        PDFDocxRun& run = paragraph.runs.back();
        if (space)
        {
            run.text.append(QChar(' '));
        }
        run.text.append(character.character);

        previous = &character;
    }
}

std::vector<PDFDocxParagraph> PDFDocxExporterHelper::createParagraphs(const std::vector<PDFDocxLine>& lines, bool reflow)
{
    std::vector<PDFDocxParagraph> paragraphs;

    // Information about the last line of the last paragraph, needed to join lines
    bool canJoinLastParagraph = false;
    PDFReal lastBaseline = 0.0;
    PDFReal lastLineRight = 0.0;
    PDFReal lastFontSize = 0.0;
    PDFReal lastStep = 0.0;

    for (const PDFDocxLine& line : lines)
    {
        if (line.characters.empty())
        {
            continue;
        }

        // Split line into segments (columns) by large gaps
        std::vector<std::pair<size_t, size_t>> segments;
        size_t segmentStart = 0;
        for (size_t i = 1; i < line.characters.size(); ++i)
        {
            const PDFDocxCharacter& previous = line.characters[i - 1];
            const PDFDocxCharacter& current = line.characters[i];
            const PDFReal gap = current.position.x() - (previous.position.x() + previous.advance);
            const PDFReal fontSize = qMax(previous.fontSize, current.fontSize);

            // Bullet of a list item is separated from the text by a tab, so the text
            // of the item stays aligned
            const bool isBullet = i == 1 && QStringView(u"\u2022\u25AA\u25CF\u25E6\u2013\u00B7\u2023").contains(previous.character);

            if (gap > 1.5 * fontSize || (isBullet && gap > 0.4 * fontSize))
            {
                segments.emplace_back(segmentStart, i);
                segmentStart = i;
            }
        }
        segments.emplace_back(segmentStart, line.characters.size());

        const PDFReal lineLeft = line.left();
        const PDFReal lineRight = line.right();
        const PDFReal fontSize = line.fontSize;
        const bool isSingleSegment = segments.size() == 1;

        // Try to join the line to the previous paragraph (body text)
        if (reflow && canJoinLastParagraph && isSingleSegment && !paragraphs.empty())
        {
            PDFDocxParagraph& paragraph = paragraphs.back();
            const PDFReal step = line.baseline - lastBaseline;
            const bool sameFontSize = std::abs(fontSize - lastFontSize) <= 0.1 * fontSize;
            const bool sameLeft = std::abs(lineLeft - paragraph.left) <= qMax(1.5, 0.3 * fontSize);
            const bool normalStep = step >= 0.9 * fontSize && step <= 1.8 * fontSize && (lastStep == 0.0 || std::abs(step - lastStep) <= 0.15 * fontSize);
            const PDFReal paragraphRight = qMax(paragraph.right, lineRight);
            const bool previousLineIsFull = lastLineRight >= paragraphRight - 4.0 * fontSize && (paragraphRight - paragraph.left) > 20.0 * fontSize;

            if (sameFontSize && sameLeft && normalStep && previousLineIsFull)
            {
                // Hyphenated word at the end of the previous line is joined
                PDFDocxRun* lastTextRun = nullptr;
                for (auto it = paragraph.runs.rbegin(); it != paragraph.runs.rend(); ++it)
                {
                    if (!it->isTab)
                    {
                        lastTextRun = &*it;
                        break;
                    }
                }

                bool spaceAtStart = true;
                if (lastTextRun && lastTextRun->text.endsWith(QChar('-')) && line.characters.front().character.isLower())
                {
                    lastTextRun->text.chop(1);
                    spaceAtStart = false;
                }

                const PDFDocxCharacter* begin = line.characters.data();
                appendRuns(paragraph, begin, begin + line.characters.size(), spaceAtStart);

                paragraph.lineCount += 1;
                paragraph.reflow = true;
                paragraph.right = paragraphRight;
                paragraph.lineHeight = step;

                lastBaseline = line.baseline;
                lastLineRight = lineRight;
                lastStep = step;
                continue;
            }
        }

        // Merge segments, which would overlap in Word (text is wider, than the
        // gap to the next segment), and create the paragraph
        PDFDocxParagraph paragraph;
        paragraph.left = lineLeft;
        paragraph.right = lineRight;
        paragraph.lineHeight = 1.2 * fontSize;
        paragraph.top = line.baseline - 0.95 * fontSize;

        for (size_t i = 0; i < segments.size(); ++i)
        {
            const PDFDocxCharacter* begin = line.characters.data() + segments[i].first;
            const PDFDocxCharacter* end = line.characters.data() + segments[i].second;

            if (i > 0)
            {
                PDFDocxRun tab;
                tab.isTab = true;
                paragraph.runs.emplace_back(std::move(tab));
                paragraph.tabs.push_back(begin->position.x());
            }

            appendRuns(paragraph, begin, end, false);
        }

        paragraphs.emplace_back(std::move(paragraph));

        canJoinLastParagraph = isSingleSegment;
        lastBaseline = line.baseline;
        lastLineRight = lineRight;
        lastFontSize = fontSize;
        lastStep = 0.0;
    }

    return paragraphs;
}

void PDFDocxExporterHelper::processAnnotations(const PDFDocument* document,
                                               const PDFPage* page,
                                               const std::function<std::unique_ptr<PDFPageContentProcessor>(void)>& factory)
{
    PDFDocumentDataLoaderDecorator loader(document);

    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), annotationReference);
        if (!annotation)
        {
            continue;
        }

        switch (annotation->getType())
        {
            case AnnotationType::Popup:
            case AnnotationType::Link:
            case AnnotationType::Invalid:
                continue;

            default:
                break;
        }

        const PDFAnnotation::Flags flags = annotation->getEffectiveFlags();
        if (flags.testFlag(PDFAnnotation::Hidden) || (flags.testFlag(PDFAnnotation::NoView) && !flags.testFlag(PDFAnnotation::ToggleNoView)))
        {
            continue;
        }

        PDFObject appearanceStreamObject = document->getObject(annotation->getAppearanceStreams().getAppearance(PDFAppeareanceStreams::Appearance::Normal, annotation->getAppearanceState()));
        if (!appearanceStreamObject.isStream())
        {
            continue;
        }

        const PDFStream* formStream = appearanceStreamObject.getStream();
        const PDFDictionary* formDictionary = formStream->getDictionary();

        QRectF annotationRectangle = annotation->getRectangle();
        QRectF formBoundingBox = loader.readRectangle(formDictionary->get("BBox"), QRectF());
        QTransform formMatrix = loader.readMatrixFromDictionary(formDictionary, "Matrix", QTransform());
        QByteArray content = document->getDecodedStream(formStream);
        PDFObject resources = document->getObject(formDictionary->get("Resources"));
        PDFObject transparencyGroup = document->getObject(formDictionary->get("Group"));
        const PDFInteger formStructuralParentKey = loader.readIntegerFromDictionary(formDictionary, "StructParent", page->getStructureParentKey());

        if (formBoundingBox.isEmpty() || annotationRectangle.isEmpty())
        {
            continue;
        }

        // Algorithm 8.1 of PDF 1.7 reference (appearance streams), see PDFAnnotationManager
        QRectF transformedAppearanceBox = formMatrix.mapRect(formBoundingBox);
        if (transformedAppearanceBox.isEmpty())
        {
            continue;
        }

        const PDFReal scaleX = annotationRectangle.width() / transformedAppearanceBox.width();
        const PDFReal scaleY = annotationRectangle.height() / transformedAppearanceBox.height();
        const PDFReal translateX = annotationRectangle.left() - transformedAppearanceBox.left() * scaleX;
        const PDFReal translateY = annotationRectangle.bottom() - transformedAppearanceBox.bottom() * scaleY;
        QTransform A(scaleX, 0.0, 0.0, scaleY, translateX, translateY);
        QTransform AA = formMatrix * A;

        std::unique_ptr<PDFPageContentProcessor> processor = factory();
        processor->initializeProcessor();

        PDFObjectReference optionalContent = annotation->getOptionalContent();
        if (optionalContent.isValid() && processor->isContentSuppressedByOC(optionalContent))
        {
            continue;
        }

        processor->processForm(AA, formBoundingBox, resources, transparencyGroup, content, formStructuralParentKey);
    }
}

bool PDFDocxExporterHelper::isWhiteImage(const QImage& image)
{
    Q_ASSERT(image.format() == QImage::Format_RGB32);

    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if ((line[x] & 0x00FFFFFF) != 0x00FFFFFF)
            {
                return false;
            }
        }
    }

    return true;
}

QByteArray PDFDocxExporterHelper::createContentTypes(const std::vector<PDFDocxPage>& pages)
{
    const bool hasImages = std::any_of(pages.cbegin(), pages.cend(), [](const PDFDocxPage& page) { return !page.backgroundPng.isEmpty(); });

    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeStartElement(QStringLiteral("Types"));
    writer.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types"));

    auto writeDefault = [&writer](const QString& extension, const QString& contentType)
    {
        writer.writeEmptyElement(QStringLiteral("Default"));
        writer.writeAttribute(QStringLiteral("Extension"), extension);
        writer.writeAttribute(QStringLiteral("ContentType"), contentType);
    };

    auto writeOverride = [&writer](const QString& partName, const QString& contentType)
    {
        writer.writeEmptyElement(QStringLiteral("Override"));
        writer.writeAttribute(QStringLiteral("PartName"), partName);
        writer.writeAttribute(QStringLiteral("ContentType"), contentType);
    };

    writeDefault(QStringLiteral("rels"), QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
    writeDefault(QStringLiteral("xml"), QStringLiteral("application/xml"));
    if (hasImages)
    {
        writeDefault(QStringLiteral("png"), QStringLiteral("image/png"));
    }

    writeOverride(QStringLiteral("/word/document.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
    writeOverride(QStringLiteral("/word/styles.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"));
    writeOverride(QStringLiteral("/word/settings.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"));
    writeOverride(QStringLiteral("/docProps/core.xml"), QStringLiteral("application/vnd.openxmlformats-package.core-properties+xml"));
    writeOverride(QStringLiteral("/docProps/app.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"));

    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createRootRelationships()
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeStartElement(QStringLiteral("Relationships"));
    writer.writeDefaultNamespace(QLatin1String(NS_RELATIONSHIPS));

    auto writeRelationship = [&writer](const QString& id, const QString& type, const QString& target)
    {
        writer.writeEmptyElement(QStringLiteral("Relationship"));
        writer.writeAttribute(QStringLiteral("Id"), id);
        writer.writeAttribute(QStringLiteral("Type"), type);
        writer.writeAttribute(QStringLiteral("Target"), target);
    };

    writeRelationship(QStringLiteral("rId1"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"), QStringLiteral("word/document.xml"));
    writeRelationship(QStringLiteral("rId2"), QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"), QStringLiteral("docProps/core.xml"));
    writeRelationship(QStringLiteral("rId3"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"), QStringLiteral("docProps/app.xml"));

    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createDocumentRelationships(const std::vector<PDFDocxPage>& pages)
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeStartElement(QStringLiteral("Relationships"));
    writer.writeDefaultNamespace(QLatin1String(NS_RELATIONSHIPS));

    auto writeRelationship = [&writer](const QString& id, const QString& type, const QString& target)
    {
        writer.writeEmptyElement(QStringLiteral("Relationship"));
        writer.writeAttribute(QStringLiteral("Id"), id);
        writer.writeAttribute(QStringLiteral("Type"), type);
        writer.writeAttribute(QStringLiteral("Target"), target);
    };

    writeRelationship(QStringLiteral("rId1"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"), QStringLiteral("styles.xml"));
    writeRelationship(QStringLiteral("rId2"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings"), QStringLiteral("settings.xml"));

    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (!pages[i].backgroundPng.isEmpty())
        {
            writeRelationship(QStringLiteral("rIdPage%1").arg(i + 1),
                              QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"),
                              QStringLiteral("media/page%1.png").arg(i + 1));
        }
    }

    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createStyles()
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeNamespace(QLatin1String(NS_W), QStringLiteral("w"));
    const QString w = QLatin1String(NS_W);

    writer.writeStartElement(w, QStringLiteral("styles"));

    writer.writeStartElement(w, QStringLiteral("docDefaults"));
    writer.writeStartElement(w, QStringLiteral("rPrDefault"));
    writer.writeStartElement(w, QStringLiteral("rPr"));
    writer.writeEmptyElement(w, QStringLiteral("rFonts"));
    writer.writeAttribute(w, QStringLiteral("ascii"), QStringLiteral("Arial"));
    writer.writeAttribute(w, QStringLiteral("hAnsi"), QStringLiteral("Arial"));
    writer.writeAttribute(w, QStringLiteral("eastAsia"), QStringLiteral("Arial"));
    writer.writeAttribute(w, QStringLiteral("cs"), QStringLiteral("Arial"));
    writer.writeEmptyElement(w, QStringLiteral("sz"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("20"));
    writer.writeEmptyElement(w, QStringLiteral("szCs"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("20"));
    writer.writeEndElement(); // rPr
    writer.writeEndElement(); // rPrDefault
    writer.writeStartElement(w, QStringLiteral("pPrDefault"));
    writer.writeStartElement(w, QStringLiteral("pPr"));
    writer.writeEmptyElement(w, QStringLiteral("spacing"));
    writer.writeAttribute(w, QStringLiteral("before"), QStringLiteral("0"));
    writer.writeAttribute(w, QStringLiteral("after"), QStringLiteral("0"));
    writer.writeEndElement(); // pPr
    writer.writeEndElement(); // pPrDefault
    writer.writeEndElement(); // docDefaults

    writer.writeStartElement(w, QStringLiteral("style"));
    writer.writeAttribute(w, QStringLiteral("type"), QStringLiteral("paragraph"));
    writer.writeAttribute(w, QStringLiteral("default"), QStringLiteral("1"));
    writer.writeAttribute(w, QStringLiteral("styleId"), QStringLiteral("Normal"));
    writer.writeEmptyElement(w, QStringLiteral("name"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("Normal"));
    writer.writeEmptyElement(w, QStringLiteral("qFormat"));
    writer.writeStartElement(w, QStringLiteral("pPr"));
    writer.writeEmptyElement(w, QStringLiteral("widowControl"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("0"));
    writer.writeEndElement(); // pPr
    writer.writeEndElement(); // style

    writer.writeEndElement(); // styles
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createSettings()
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeNamespace(QLatin1String(NS_W), QStringLiteral("w"));
    const QString w = QLatin1String(NS_W);

    writer.writeStartElement(w, QStringLiteral("settings"));
    writer.writeEmptyElement(w, QStringLiteral("defaultTabStop"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("720"));
    writer.writeEmptyElement(w, QStringLiteral("characterSpacingControl"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("doNotCompress"));
    writer.writeStartElement(w, QStringLiteral("compat"));
    writer.writeEmptyElement(w, QStringLiteral("compatSetting"));
    writer.writeAttribute(w, QStringLiteral("name"), QStringLiteral("compatibilityMode"));
    writer.writeAttribute(w, QStringLiteral("uri"), QStringLiteral("http://schemas.microsoft.com/office/word"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("15"));
    writer.writeEndElement(); // compat
    writer.writeEndElement(); // settings
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createCoreProperties(const PDFDocument* document)
{
    const PDFDocumentInfo* info = document->getInfo();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);

    const QString cp = QStringLiteral("http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
    const QString dc = QStringLiteral("http://purl.org/dc/elements/1.1/");
    const QString dcterms = QStringLiteral("http://purl.org/dc/terms/");
    const QString xsi = QStringLiteral("http://www.w3.org/2001/XMLSchema-instance");

    writer.writeNamespace(cp, QStringLiteral("cp"));
    writer.writeNamespace(dc, QStringLiteral("dc"));
    writer.writeNamespace(dcterms, QStringLiteral("dcterms"));
    writer.writeNamespace(xsi, QStringLiteral("xsi"));

    writer.writeStartElement(cp, QStringLiteral("coreProperties"));
    if (!info->title.isEmpty())
    {
        writer.writeTextElement(dc, QStringLiteral("title"), info->title);
    }
    if (!info->subject.isEmpty())
    {
        writer.writeTextElement(dc, QStringLiteral("subject"), info->subject);
    }
    if (!info->author.isEmpty())
    {
        writer.writeTextElement(dc, QStringLiteral("creator"), info->author);
    }
    if (!info->keywords.isEmpty())
    {
        writer.writeTextElement(cp, QStringLiteral("keywords"), info->keywords);
    }

    writer.writeStartElement(dcterms, QStringLiteral("created"));
    writer.writeAttribute(xsi, QStringLiteral("type"), QStringLiteral("dcterms:W3CDTF"));
    writer.writeCharacters(now);
    writer.writeEndElement();

    writer.writeStartElement(dcterms, QStringLiteral("modified"));
    writer.writeAttribute(xsi, QStringLiteral("type"), QStringLiteral("dcterms:W3CDTF"));
    writer.writeCharacters(now);
    writer.writeEndElement();

    writer.writeEndElement(); // coreProperties
    writer.writeEndDocument();
    return data;
}

QByteArray PDFDocxExporterHelper::createAppProperties()
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeStartElement(QStringLiteral("Properties"));
    writer.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"));
    writer.writeTextElement(QStringLiteral("Application"), QStringLiteral("PDF4QT"));
    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

void PDFDocxExporterHelper::writeBackground(QXmlStreamWriter& writer, const PDFDocxPage& page, size_t pageIndex)
{
    const QString w = QLatin1String(NS_W);
    const QString wp = QLatin1String(NS_WP);
    const QString a = QLatin1String(NS_A);
    const QString pic = QLatin1String(NS_PIC);
    const QString r = QLatin1String(NS_R);

    const QString cx = number(toEmu(page.width));
    const QString cy = number(toEmu(page.height));
    const QString id = number(qint64(pageIndex) + 1);
    const QString name = QStringLiteral("Page %1").arg(pageIndex + 1);

    writer.writeStartElement(w, QStringLiteral("r"));
    writer.writeStartElement(w, QStringLiteral("drawing"));
    writer.writeStartElement(wp, QStringLiteral("anchor"));
    writer.writeAttribute(QStringLiteral("distT"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("distB"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("distL"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("distR"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("simplePos"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("relativeHeight"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("behindDoc"), QStringLiteral("1"));
    writer.writeAttribute(QStringLiteral("locked"), QStringLiteral("1"));
    writer.writeAttribute(QStringLiteral("layoutInCell"), QStringLiteral("1"));
    writer.writeAttribute(QStringLiteral("allowOverlap"), QStringLiteral("1"));

    writer.writeEmptyElement(wp, QStringLiteral("simplePos"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));

    writer.writeStartElement(wp, QStringLiteral("positionH"));
    writer.writeAttribute(QStringLiteral("relativeFrom"), QStringLiteral("page"));
    writer.writeTextElement(wp, QStringLiteral("posOffset"), QStringLiteral("0"));
    writer.writeEndElement();

    writer.writeStartElement(wp, QStringLiteral("positionV"));
    writer.writeAttribute(QStringLiteral("relativeFrom"), QStringLiteral("page"));
    writer.writeTextElement(wp, QStringLiteral("posOffset"), QStringLiteral("0"));
    writer.writeEndElement();

    writer.writeEmptyElement(wp, QStringLiteral("extent"));
    writer.writeAttribute(QStringLiteral("cx"), cx);
    writer.writeAttribute(QStringLiteral("cy"), cy);

    writer.writeEmptyElement(wp, QStringLiteral("effectExtent"));
    writer.writeAttribute(QStringLiteral("l"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("t"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("r"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("b"), QStringLiteral("0"));

    writer.writeEmptyElement(wp, QStringLiteral("wrapNone"));

    writer.writeEmptyElement(wp, QStringLiteral("docPr"));
    writer.writeAttribute(QStringLiteral("id"), id);
    writer.writeAttribute(QStringLiteral("name"), name);

    writer.writeEmptyElement(wp, QStringLiteral("cNvGraphicFramePr"));

    writer.writeStartElement(a, QStringLiteral("graphic"));
    writer.writeStartElement(a, QStringLiteral("graphicData"));
    writer.writeAttribute(QStringLiteral("uri"), QLatin1String(NS_PIC));
    writer.writeStartElement(pic, QStringLiteral("pic"));

    writer.writeStartElement(pic, QStringLiteral("nvPicPr"));
    writer.writeEmptyElement(pic, QStringLiteral("cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), id);
    writer.writeAttribute(QStringLiteral("name"), QStringLiteral("page%1.png").arg(pageIndex + 1));
    writer.writeEmptyElement(pic, QStringLiteral("cNvPicPr"));
    writer.writeEndElement(); // nvPicPr

    writer.writeStartElement(pic, QStringLiteral("blipFill"));
    writer.writeEmptyElement(a, QStringLiteral("blip"));
    writer.writeAttribute(r, QStringLiteral("embed"), QStringLiteral("rIdPage%1").arg(pageIndex + 1));
    writer.writeStartElement(a, QStringLiteral("stretch"));
    writer.writeEmptyElement(a, QStringLiteral("fillRect"));
    writer.writeEndElement(); // stretch
    writer.writeEndElement(); // blipFill

    writer.writeStartElement(pic, QStringLiteral("spPr"));
    writer.writeStartElement(a, QStringLiteral("xfrm"));
    writer.writeEmptyElement(a, QStringLiteral("off"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
    writer.writeEmptyElement(a, QStringLiteral("ext"));
    writer.writeAttribute(QStringLiteral("cx"), cx);
    writer.writeAttribute(QStringLiteral("cy"), cy);
    writer.writeEndElement(); // xfrm
    writer.writeStartElement(a, QStringLiteral("prstGeom"));
    writer.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
    writer.writeEmptyElement(a, QStringLiteral("avLst"));
    writer.writeEndElement(); // prstGeom
    writer.writeEndElement(); // spPr

    writer.writeEndElement(); // pic
    writer.writeEndElement(); // graphicData
    writer.writeEndElement(); // graphic
    writer.writeEndElement(); // anchor
    writer.writeEndElement(); // drawing
    writer.writeEndElement(); // r
}

void PDFDocxExporterHelper::writeParagraph(QXmlStreamWriter& writer, const PDFDocxPage& page, const PDFDocxParagraph& paragraph, PDFReal spacingBefore, PDFReal rightIndent, const PDFDocxPage* sectionPage, bool framed)
{
    const QString w = QLatin1String(NS_W);

    writer.writeStartElement(w, QStringLiteral("pPr"));

    // Horizontal positions are measured from the left margin, or from the left
    // edge of the frame for a paragraph in a frame
    const PDFReal origin = framed ? paragraph.left : page.marginLeft;

    if (framed)
    {
        // Frame at the exact position of the text, the width is a bit larger than
        // the text, because the fonts of Word can be wider than the fonts of the PDF
        const PDFReal height = paragraph.lineHeight * paragraph.lineCount;
        const PDFReal width = qMin(qMax((paragraph.right - paragraph.left) * 1.15 + paragraph.lineHeight, 6.0), page.width - paragraph.left);

        writer.writeEmptyElement(w, QStringLiteral("framePr"));
        writer.writeAttribute(w, QStringLiteral("w"), number(qMax(toTwips(width), 20)));
        writer.writeAttribute(w, QStringLiteral("h"), number(qMax(toTwips(height), 20)));
        writer.writeAttribute(w, QStringLiteral("hRule"), QStringLiteral("exact"));
        writer.writeAttribute(w, QStringLiteral("wrap"), QStringLiteral("through"));
        writer.writeAttribute(w, QStringLiteral("hAnchor"), QStringLiteral("page"));
        writer.writeAttribute(w, QStringLiteral("vAnchor"), QStringLiteral("page"));
        writer.writeAttribute(w, QStringLiteral("x"), number(toTwips(qMax(paragraph.left, 0.0))));
        writer.writeAttribute(w, QStringLiteral("y"), number(toTwips(qMax(paragraph.top, 0.0))));
    }

    if (!paragraph.tabs.empty())
    {
        writer.writeStartElement(w, QStringLiteral("tabs"));
        for (PDFReal tab : paragraph.tabs)
        {
            writer.writeEmptyElement(w, QStringLiteral("tab"));
            writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("left"));
            writer.writeAttribute(w, QStringLiteral("pos"), number(toTwips(tab - origin)));
        }
        writer.writeEndElement();
    }

    writer.writeEmptyElement(w, QStringLiteral("spacing"));
    writer.writeAttribute(w, QStringLiteral("before"), number(toTwips(qMax(spacingBefore, 0.0))));
    writer.writeAttribute(w, QStringLiteral("after"), QStringLiteral("0"));
    writer.writeAttribute(w, QStringLiteral("line"), number(qMax(toTwips(paragraph.lineHeight), 20)));
    writer.writeAttribute(w, QStringLiteral("lineRule"), QStringLiteral("exact"));

    writer.writeEmptyElement(w, QStringLiteral("ind"));
    writer.writeAttribute(w, QStringLiteral("left"), number(toTwips(paragraph.left - origin)));
    writer.writeAttribute(w, QStringLiteral("right"), number(toTwips(framed ? 0.0 : rightIndent)));

    if (sectionPage)
    {
        writeSectionProperties(writer, *sectionPage);
    }

    writer.writeEndElement(); // pPr
}

void PDFDocxExporterHelper::writeSectionProperties(QXmlStreamWriter& writer, const PDFDocxPage& page)
{
    const QString w = QLatin1String(NS_W);

    writer.writeStartElement(w, QStringLiteral("sectPr"));

    writer.writeEmptyElement(w, QStringLiteral("type"));
    writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("nextPage"));

    writer.writeEmptyElement(w, QStringLiteral("pgSz"));
    writer.writeAttribute(w, QStringLiteral("w"), number(toTwips(page.width)));
    writer.writeAttribute(w, QStringLiteral("h"), number(toTwips(page.height)));
    if (page.width > page.height)
    {
        writer.writeAttribute(w, QStringLiteral("orient"), QStringLiteral("landscape"));
    }

    writer.writeEmptyElement(w, QStringLiteral("pgMar"));
    writer.writeAttribute(w, QStringLiteral("top"), number(toTwips(page.marginTop)));
    writer.writeAttribute(w, QStringLiteral("right"), number(toTwips(page.marginRight)));
    writer.writeAttribute(w, QStringLiteral("bottom"), number(toTwips(page.marginBottom)));
    writer.writeAttribute(w, QStringLiteral("left"), number(toTwips(page.marginLeft)));
    writer.writeAttribute(w, QStringLiteral("header"), QStringLiteral("0"));
    writer.writeAttribute(w, QStringLiteral("footer"), QStringLiteral("0"));
    writer.writeAttribute(w, QStringLiteral("gutter"), QStringLiteral("0"));

    writer.writeEndElement(); // sectPr
}

QByteArray PDFDocxExporterHelper::createDocument(const std::vector<PDFDocxPage>& pages)
{
    const QString w = QLatin1String(NS_W);

    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeNamespace(w, QStringLiteral("w"));
    writer.writeNamespace(QLatin1String(NS_R), QStringLiteral("r"));
    writer.writeNamespace(QLatin1String(NS_WP), QStringLiteral("wp"));
    writer.writeNamespace(QLatin1String(NS_A), QStringLiteral("a"));
    writer.writeNamespace(QLatin1String(NS_PIC), QStringLiteral("pic"));

    writer.writeStartElement(w, QStringLiteral("document"));
    writer.writeStartElement(w, QStringLiteral("body"));

    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        const PDFDocxPage& page = pages[pageIndex];
        const bool isLastPage = pageIndex + 1 == pages.size();
        const PDFReal textAreaRight = page.width - page.marginRight;

        // Layout of the paragraphs: a paragraph flows (it is placed below the previous
        // paragraph by its spacing), unless it overlaps the previous paragraph (dense
        // labels of a drawing) or it would not fit on the page - then it is placed
        // in a frame at its exact position on the page, which takes no space in the flow.
        std::vector<bool> framed(page.paragraphs.size(), false);
        std::vector<PDFReal> spacings(page.paragraphs.size(), 0.0);
        PDFReal cursor = page.marginTop + PARAGRAPH_MARK_HEIGHT;
        const PDFReal bottom = page.height - page.marginBottom - PARAGRAPH_MARK_HEIGHT;
        for (size_t i = 0; i < page.paragraphs.size(); ++i)
        {
            const PDFDocxParagraph& paragraph = page.paragraphs[i];
            const PDFReal height = paragraph.lineHeight * paragraph.lineCount;
            const PDFReal spacing = paragraph.top - cursor;

            if (spacing < -0.25 * paragraph.lineHeight || cursor + qMax(spacing, 0.0) + height > bottom)
            {
                framed[i] = true;
                continue;
            }

            spacings[i] = qMax(spacing, 0.0);
            cursor += spacings[i] + height;
        }

        // Page, whose text doesn't flow (more than a few paragraphs would be in frames),
        // has all the paragraphs in frames, so the text of the page is not mixed
        // and it can't overflow to the next page
        if (std::count(framed.cbegin(), framed.cend(), true) > 3)
        {
            std::fill(framed.begin(), framed.end(), true);
        }

        // The first paragraph of the page is an empty paragraph, which holds the
        // background picture (and the section properties of a page without text)
        const bool isPageEmpty = page.paragraphs.empty();
        writer.writeStartElement(w, QStringLiteral("p"));
        writer.writeStartElement(w, QStringLiteral("pPr"));
        writer.writeEmptyElement(w, QStringLiteral("spacing"));
        writer.writeAttribute(w, QStringLiteral("before"), QStringLiteral("0"));
        writer.writeAttribute(w, QStringLiteral("after"), QStringLiteral("0"));
        writer.writeAttribute(w, QStringLiteral("line"), number(toTwips(PARAGRAPH_MARK_HEIGHT)));
        writer.writeAttribute(w, QStringLiteral("lineRule"), QStringLiteral("exact"));
        if (isPageEmpty && !isLastPage)
        {
            writeSectionProperties(writer, page);
        }
        writer.writeEndElement(); // pPr
        if (!page.backgroundPng.isEmpty())
        {
            writeBackground(writer, page, pageIndex);
        }
        writer.writeEndElement(); // p

        // Section properties are written into the last flowing paragraph, or into
        // an extra empty paragraph, if the last paragraph is in a frame
        const bool needsSectionParagraph = !isLastPage && !isPageEmpty && framed.back();

        for (size_t paragraphIndex = 0; paragraphIndex < page.paragraphs.size(); ++paragraphIndex)
        {
            const PDFDocxParagraph& paragraph = page.paragraphs[paragraphIndex];
            const bool isLastParagraph = paragraphIndex + 1 == page.paragraphs.size();

            // Reflowing paragraph keeps the width of the text, single line is
            // allowed to extend up to the edge of the page, because the fonts
            // of Word can be a little wider than the fonts of the PDF.
            PDFReal rightIndent = -page.marginRight;
            if (paragraph.reflow)
            {
                rightIndent = qMax(textAreaRight - paragraph.right - 0.25 * paragraph.lineHeight, -page.marginRight);
            }

            writer.writeStartElement(w, QStringLiteral("p"));
            const PDFDocxPage* sectionPage = (isLastParagraph && !isLastPage && !framed[paragraphIndex]) ? &page : nullptr;
            writeParagraph(writer, page, paragraph, spacings[paragraphIndex], rightIndent, sectionPage, framed[paragraphIndex]);

            for (const PDFDocxRun& run : paragraph.runs)
            {
                writer.writeStartElement(w, QStringLiteral("r"));

                if (!run.isTab)
                {
                    const PDFDocxTextStyle& style = page.styles.at(size_t(run.style));
                    writer.writeStartElement(w, QStringLiteral("rPr"));
                    writer.writeEmptyElement(w, QStringLiteral("rFonts"));
                    writer.writeAttribute(w, QStringLiteral("ascii"), style.fontFamily);
                    writer.writeAttribute(w, QStringLiteral("hAnsi"), style.fontFamily);
                    writer.writeAttribute(w, QStringLiteral("eastAsia"), style.fontFamily);
                    writer.writeAttribute(w, QStringLiteral("cs"), style.fontFamily);
                    if (style.bold)
                    {
                        writer.writeEmptyElement(w, QStringLiteral("b"));
                    }
                    if (style.italic)
                    {
                        writer.writeEmptyElement(w, QStringLiteral("i"));
                    }
                    if ((style.color & 0x00FFFFFF) != 0)
                    {
                        writer.writeEmptyElement(w, QStringLiteral("color"));
                        writer.writeAttribute(w, QStringLiteral("val"), QStringLiteral("%1").arg(style.color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper());
                    }
                    writer.writeEmptyElement(w, QStringLiteral("sz"));
                    writer.writeAttribute(w, QStringLiteral("val"), number(run.halfPoints));
                    writer.writeEmptyElement(w, QStringLiteral("szCs"));
                    writer.writeAttribute(w, QStringLiteral("val"), number(run.halfPoints));
                    writer.writeEndElement(); // rPr

                    writer.writeStartElement(w, QStringLiteral("t"));
                    writer.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                    writer.writeCharacters(run.text);
                    writer.writeEndElement(); // t
                }
                else
                {
                    writer.writeEmptyElement(w, QStringLiteral("tab"));
                }

                writer.writeEndElement(); // r
            }

            writer.writeEndElement(); // p
        }

        if (needsSectionParagraph)
        {
            writer.writeStartElement(w, QStringLiteral("p"));
            writer.writeStartElement(w, QStringLiteral("pPr"));
            writer.writeEmptyElement(w, QStringLiteral("spacing"));
            writer.writeAttribute(w, QStringLiteral("before"), QStringLiteral("0"));
            writer.writeAttribute(w, QStringLiteral("after"), QStringLiteral("0"));
            writer.writeAttribute(w, QStringLiteral("line"), number(toTwips(PARAGRAPH_MARK_HEIGHT)));
            writer.writeAttribute(w, QStringLiteral("lineRule"), QStringLiteral("exact"));
            writeSectionProperties(writer, page);
            writer.writeEndElement(); // pPr
            writer.writeEndElement(); // p
        }

        if (isLastPage)
        {
            // Section properties of the last page are the last element of the body,
            // the other pages have them in the properties of their last paragraph.
            writeSectionProperties(writer, page);
        }
    }

    writer.writeEndElement(); // body
    writer.writeEndElement(); // document
    writer.writeEndDocument();
    return data;
}

PDFDocxExporter::PDFDocxExporter(PDFDocxExportSettings settings) :
    m_settings(settings)
{

}

QByteArray PDFDocxExporter::createZipArchive(const std::vector<std::pair<QString, QByteArray>>& files, bool compress)
{
    QByteArray archive;
    QByteArray centralDirectory;

    auto appendUInt16 = [](QByteArray& data, quint32 value)
    {
        data.append(char(value & 0xFF));
        data.append(char((value >> 8) & 0xFF));
    };

    auto appendUInt32 = [](QByteArray& data, quint32 value)
    {
        data.append(char(value & 0xFF));
        data.append(char((value >> 8) & 0xFF));
        data.append(char((value >> 16) & 0xFF));
        data.append(char((value >> 24) & 0xFF));
    };

    // Time of the files (MS-DOS format)
    const QDateTime now = QDateTime::currentDateTime();
    const quint32 dosTime = quint32((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
    const quint32 dosDate = quint32((qMax(now.date().year() - 1980, 0) << 9) | (now.date().month() << 5) | now.date().day());

    for (const auto& file : files)
    {
        const QByteArray name = file.first.toUtf8();
        const QByteArray& data = file.second;

        const quint32 crc = quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size())));

        QByteArray storedData = data;
        quint32 method = 0;

        if (compress && !data.isEmpty())
        {
            // Raw deflate stream (negative window bits), as required by ZIP
            z_stream stream = { };
            if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK)
            {
                QByteArray compressed;
                compressed.resize(qsizetype(deflateBound(&stream, uLong(data.size()))));

                stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
                stream.avail_in = uInt(data.size());
                stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
                stream.avail_out = uInt(compressed.size());

                if (deflate(&stream, Z_FINISH) == Z_STREAM_END)
                {
                    compressed.resize(qsizetype(stream.total_out));
                    if (compressed.size() < data.size())
                    {
                        storedData = std::move(compressed);
                        method = 8;
                    }
                }

                deflateEnd(&stream);
            }
        }

        const quint32 offset = quint32(archive.size());

        // Local file header
        appendUInt32(archive, 0x04034b50);
        appendUInt16(archive, 20);                      // Version needed to extract
        appendUInt16(archive, 0x0800);                  // Flags (names are UTF-8)
        appendUInt16(archive, method);
        appendUInt16(archive, dosTime);
        appendUInt16(archive, dosDate);
        appendUInt32(archive, crc);
        appendUInt32(archive, quint32(storedData.size()));
        appendUInt32(archive, quint32(data.size()));
        appendUInt16(archive, quint32(name.size()));
        appendUInt16(archive, 0);                       // Extra field length
        archive.append(name);
        archive.append(storedData);

        // Central directory header
        appendUInt32(centralDirectory, 0x02014b50);
        appendUInt16(centralDirectory, 20);             // Version made by
        appendUInt16(centralDirectory, 20);             // Version needed to extract
        appendUInt16(centralDirectory, 0x0800);
        appendUInt16(centralDirectory, method);
        appendUInt16(centralDirectory, dosTime);
        appendUInt16(centralDirectory, dosDate);
        appendUInt32(centralDirectory, crc);
        appendUInt32(centralDirectory, quint32(storedData.size()));
        appendUInt32(centralDirectory, quint32(data.size()));
        appendUInt16(centralDirectory, quint32(name.size()));
        appendUInt16(centralDirectory, 0);              // Extra field length
        appendUInt16(centralDirectory, 0);              // Comment length
        appendUInt16(centralDirectory, 0);              // Disk number
        appendUInt16(centralDirectory, 0);              // Internal attributes
        appendUInt32(centralDirectory, 0);              // External attributes
        appendUInt32(centralDirectory, offset);
        centralDirectory.append(name);
    }

    const quint32 centralDirectoryOffset = quint32(archive.size());
    archive.append(centralDirectory);

    // End of central directory
    appendUInt32(archive, 0x06054b50);
    appendUInt16(archive, 0);
    appendUInt16(archive, 0);
    appendUInt16(archive, quint32(files.size()));
    appendUInt16(archive, quint32(files.size()));
    appendUInt32(archive, quint32(centralDirectory.size()));
    appendUInt32(archive, centralDirectoryOffset);
    appendUInt16(archive, 0);

    return archive;
}

PDFOperationResult PDFDocxExporter::exportDocument(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, QByteArray& data) const
{
    data.clear();

    if (!document)
    {
        return PDFTranslationContext::tr("No document to export.");
    }

    const PDFCatalog* catalog = document->getCatalog();
    for (PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount() || !catalog->getPage(size_t(pageIndex)))
        {
            return PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1);
        }
    }

    if (pageIndices.empty())
    {
        return PDFTranslationContext::tr("No pages to export.");
    }

    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::Export, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    std::vector<PDFDocxPage> pages(pageIndices.size());
    std::vector<size_t> indices(pageIndices.size());
    std::iota(indices.begin(), indices.end(), size_t(0));
    std::atomic<size_t> processedPages = 0;
    QMutex errorMutex;
    QString errorMessage;

    auto processPage = [&](size_t index)
    {
        if (PDFOperationControl::isOperationCancelled(m_operationControl))
        {
            return;
        }

        try
        {
            const PDFPage* page = catalog->getPage(size_t(pageIndices[index]));
            PDFDocxPage& docxPage = pages[index];

            const PageRotation rotation = page->getPageRotation();
            const QRectF cropBox = page->getCropBox().isValid() ? page->getCropBox() : page->getMediaBox();
            const QRectF rotatedBox = PDFPage::getRotatedBox(cropBox, rotation);

            // Word supports pages up to 22 inches, larger pages (plans) are scaled down
            // uniformly (text, positions and the picture together)
            const PDFReal maximalSize = 22.0 * 72.0;
            const PDFReal pageScale = qMin(1.0, qMin(maximalSize / qMax(rotatedBox.width(), 1.0), maximalSize / qMax(rotatedBox.height(), 1.0)));
            docxPage.width = qBound(36.0, rotatedBox.width() * pageScale, maximalSize);
            docxPage.height = qBound(36.0, rotatedBox.height() * pageScale, maximalSize);

            // Page coordinates: points, the origin is the top left corner of the visible page
            const QTransform pageMatrix = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, QRectF(0.0, 0.0, docxPage.width, docxPage.height), rotation);
            const QRectF pageRect(0.0, 0.0, docxPage.width, docxPage.height);

            // 1) Text
            PDFDocxCharacters characters;
            {
                PDFDocxTextCollector collector(page, document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, pageMatrix, meshQualitySettings, &characters, &docxPage.styles);
                collector.setOperationControl(m_operationControl);
                collector.processContents();
            }

            if (m_settings.includeAnnotations)
            {
                auto factory = [&]() -> std::unique_ptr<PDFPageContentProcessor>
                {
                    auto processor = std::make_unique<PDFDocxTextCollector>(page, document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, pageMatrix, meshQualitySettings, &characters, &docxPage.styles);
                    processor->setOperationControl(m_operationControl);
                    return processor;
                };
                PDFDocxExporterHelper::processAnnotations(document, page, factory);
            }

            // Characters outside of the visible page are not exported
            characters.erase(std::remove_if(characters.begin(), characters.end(), [&pageRect](const PDFDocxCharacter& character) { return !pageRect.contains(character.position); }), characters.end());

            const size_t invisibleCount = size_t(std::count_if(characters.cbegin(), characters.cend(), [](const PDFDocxCharacter& character) { return character.invisible; }));
            const bool isScannedPage = !characters.empty() && invisibleCount * 2 > characters.size();

            if (docxPage.styles.empty())
            {
                docxPage.styles.push_back(PDFDocxTextStyle());
            }

            std::vector<PDFDocxLine> lines = PDFDocxExporterHelper::createLines(std::move(characters));
            docxPage.paragraphs = PDFDocxExporterHelper::createParagraphs(lines, m_settings.reflowParagraphs);

            // Margins: at most half an inch, but the text must fit into the margins
            PDFReal minLeft = docxPage.width;
            PDFReal maxRight = 0.0;
            PDFReal minTop = docxPage.height;
            PDFReal maxBottom = 0.0;
            for (const PDFDocxParagraph& paragraph : docxPage.paragraphs)
            {
                minLeft = qMin(minLeft, paragraph.left);
                maxRight = qMax(maxRight, paragraph.right);
                minTop = qMin(minTop, paragraph.top);
                maxBottom = qMax(maxBottom, paragraph.top + paragraph.lineHeight * paragraph.lineCount);
            }

            docxPage.marginLeft = qBound(0.0, std::floor(minLeft) - 1.0, 36.0);
            docxPage.marginRight = qBound(0.0, std::floor(docxPage.width - maxRight) - 1.0, 36.0);
            docxPage.marginTop = qBound(0.0, std::floor(minTop) - 1.0 - PDFDocxExporterHelper::PARAGRAPH_MARK_HEIGHT, 36.0);
            docxPage.marginBottom = qBound(0.0, std::floor(docxPage.height - maxBottom) - 2.0, 36.0);

            if (docxPage.paragraphs.empty())
            {
                docxPage.marginLeft = 36.0;
                docxPage.marginRight = 36.0;
                docxPage.marginTop = 36.0;
                docxPage.marginBottom = 36.0;
            }

            // Text of the scanned pages is a part of the picture, so the picture is not
            // exported (the text would be doubled)
            if (m_settings.includeGraphics && !isScannedPage && !PDFOperationControl::isOperationCancelled(m_operationControl))
            {
                const PDFReal scale = qBound(36, m_settings.graphicsResolution, 600) / 72.0;
                const QSize imageSize(qMax(1, qCeil(docxPage.width * scale - 0.01)), qMax(1, qCeil(docxPage.height * scale - 0.01)));

                QImage image(imageSize, QImage::Format_RGB32);
                image.fill(Qt::white);

                const QTransform imageMatrix = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, QRectF(QPointF(0.0, 0.0), QSizeF(imageSize)), rotation);
                const PDFRenderer::Features features(PDFRenderer::Antialiasing | PDFRenderer::SmoothImages | PDFRenderer::ClipToCropBox);

                {
                    QPainter painter(&image);
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

                    {
                        PDFDocxGraphicsPainter graphicsPainter(&painter, features, imageMatrix, page, document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, meshQualitySettings);
                        graphicsPainter.setOperationControl(m_operationControl);
                        graphicsPainter.processContents();
                    }

                    if (m_settings.includeAnnotations)
                    {
                        auto factory = [&]() -> std::unique_ptr<PDFPageContentProcessor>
                        {
                            auto processor = std::make_unique<PDFDocxGraphicsPainter>(&painter, features, imageMatrix, page, document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, meshQualitySettings);
                            processor->setOperationControl(m_operationControl);
                            return processor;
                        };

                        painter.save();
                        PDFDocxExporterHelper::processAnnotations(document, page, factory);
                        painter.restore();
                    }
                }

                if (!PDFDocxExporterHelper::isWhiteImage(image))
                {
                    QBuffer buffer(&docxPage.backgroundPng);
                    buffer.open(QIODevice::WriteOnly);
                    image.save(&buffer, "PNG");
                }
            }
        }
        catch (const PDFException& exception)
        {
            QMutexLocker lock(&errorMutex);
            if (errorMessage.isEmpty())
            {
                errorMessage = PDFTranslationContext::tr("Page %1: %2").arg(pageIndices[index] + 1).arg(exception.getMessage());
            }
        }

        const size_t processed = ++processedPages;
        if (m_progressCallback)
        {
            m_progressCallback(processed, pageIndices.size());
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, indices.begin(), indices.end(), processPage);

    fontCache.setCacheShrinkEnabled(nullptr, true);

    if (PDFOperationControl::isOperationCancelled(m_operationControl))
    {
        return PDFTranslationContext::tr("Export was cancelled.");
    }

    if (!errorMessage.isEmpty())
    {
        return errorMessage;
    }

    std::vector<std::pair<QString, QByteArray>> files;
    files.emplace_back(QStringLiteral("[Content_Types].xml"), PDFDocxExporterHelper::createContentTypes(pages));
    files.emplace_back(QStringLiteral("_rels/.rels"), PDFDocxExporterHelper::createRootRelationships());
    files.emplace_back(QStringLiteral("docProps/core.xml"), PDFDocxExporterHelper::createCoreProperties(document));
    files.emplace_back(QStringLiteral("docProps/app.xml"), PDFDocxExporterHelper::createAppProperties());
    files.emplace_back(QStringLiteral("word/document.xml"), PDFDocxExporterHelper::createDocument(pages));
    files.emplace_back(QStringLiteral("word/styles.xml"), PDFDocxExporterHelper::createStyles());
    files.emplace_back(QStringLiteral("word/settings.xml"), PDFDocxExporterHelper::createSettings());
    files.emplace_back(QStringLiteral("word/_rels/document.xml.rels"), PDFDocxExporterHelper::createDocumentRelationships(pages));

    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (!pages[i].backgroundPng.isEmpty())
        {
            files.emplace_back(QStringLiteral("word/media/page%1.png").arg(i + 1), pages[i].backgroundPng);
        }
    }

    data = createZipArchive(files, m_settings.compress);
    return true;
}

PDFOperationResult PDFDocxExporter::exportDocument(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const QString& fileName) const
{
    QByteArray data;
    PDFOperationResult result = exportDocument(document, pageIndices, data);
    if (!result)
    {
        return result;
    }

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        return PDFTranslationContext::tr("Can't open file '%1' for writing: %2").arg(fileName, file.errorString());
    }

    if (file.write(data) != data.size())
    {
        return PDFTranslationContext::tr("Can't write file '%1': %2").arg(fileName, file.errorString());
    }

    file.close();
    return true;
}

}   // namespace pdf
