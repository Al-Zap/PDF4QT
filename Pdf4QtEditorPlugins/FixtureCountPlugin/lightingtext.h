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


#ifndef LIGHTINGTEXT_H
#define LIGHTINGTEXT_H

#include "pdfglobal.h"

#include <QPointF>
#include <QPolygonF>
#include <QString>

#include <vector>

namespace pdf
{
class PDFTextLayout;
}

namespace pdfplugin
{

/// Piece of text of the sheet (a value or a word), separated from its
/// neighbours by a gap
struct LightingTextToken
{
    QString text;

    /// Start of the baseline (page coordinate system)
    QPointF start;

    /// End of the baseline (page coordinate system)
    QPointF end;

    /// Unit vector of the reading direction
    QPointF direction;

    /// Unit vector pointing up from the baseline
    QPointF up;

    pdf::PDFReal fontSize = 0.0;

    /// Returns the center of the text (the middle of the baseline lifted
    /// by a half of the cap height)
    QPointF getCenter() const { return (start + end) * 0.5 + up * (0.35 * fontSize); }
};

using LightingTextTokens = std::vector<LightingTextToken>;

/// Splits the text of the sheet into tokens
class LightingText
{
public:
    LightingText() = delete;

    /// Gap between the characters relative to the font size, which is a space
    /// between words, when the space character is not in the text (the space
    /// of Helvetica is 0.278 of the font size)
    static constexpr pdf::PDFReal WORD_GAP = 0.15;

    /// Returns the tokens of the text layout, whose centers lie in the area.
    /// A token ends at a gap larger than the given multiple of the font size,
    /// at a change of the baseline, and optionally at a space (a space character
    /// or a gap larger than \ref WORD_GAP, which is a space inside of the tokens otherwise).
    /// \param layout Text layout of the page (page coordinate system)
    /// \param area Area in the page coordinate system
    /// \param gap Gap relative to the font size, which separates the tokens
    /// \param splitAtSpaces Spaces separate the tokens (otherwise they are a part of them)
    static LightingTextTokens getTokens(const pdf::PDFTextLayout& layout, const QPolygonF& area, pdf::PDFReal gap, bool splitAtSpaces);
};

}   // namespace pdfplugin

#endif // LIGHTINGTEXT_H
