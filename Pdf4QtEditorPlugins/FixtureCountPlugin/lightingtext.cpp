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


#include "lightingtext.h"

#include "pdftextlayout.h"

#include <QLineF>

#include <cmath>

namespace pdfplugin
{

LightingTextTokens LightingText::getTokens(const pdf::PDFTextLayout& layout, const QPolygonF& area, pdf::PDFReal gap, bool splitAtSpaces)
{
    LightingTextTokens tokens;

    auto finishToken = [&tokens, &area](LightingTextToken& token)
    {
        token.text = token.text.trimmed();
        if (!token.text.isEmpty() && area.containsPoint(token.getCenter(), Qt::OddEvenFill))
        {
            tokens.push_back(token);
        }
        token = LightingTextToken();
    };

    for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            LightingTextToken token;

            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                const bool isSpace = character.character.isSpace();
                if (isSpace && (splitAtSpaces || token.text.isEmpty()))
                {
                    finishToken(token);
                    continue;
                }

                if (!token.text.isEmpty())
                {
                    // Gap along the line and the shift across the line from the end of the previous character
                    const QPointF difference = character.position - token.end;
                    const pdf::PDFReal characterGap = difference.x() * token.direction.x() + difference.y() * token.direction.y();
                    const pdf::PDFReal shift = difference.x() * token.up.x() + difference.y() * token.up.y();
                    const pdf::PDFReal fontSize = qMax(token.fontSize, character.fontSize);

                    if (characterGap > gap * fontSize || characterGap < -0.5 * fontSize || std::abs(shift) > 0.5 * fontSize)
                    {
                        finishToken(token);
                    }
                    else if (!isSpace && characterGap > WORD_GAP * fontSize && !token.text.endsWith(QChar(' ')))
                    {
                        // Words are often placed without the space character (positioned
                        // by TJ or by a new text matrix), the gap is the space
                        if (splitAtSpaces)
                        {
                            finishToken(token);
                        }
                        else
                        {
                            token.text += QChar(' ');
                        }
                    }
                }

                if (isSpace && token.text.isEmpty())
                {
                    continue;
                }

                if (token.text.isEmpty())
                {
                    token.start = character.position;
                    token.direction = QLineF::fromPolar(1.0, character.angle).p2();
                    token.up = QPointF(-token.direction.y(), token.direction.x());
                }

                token.text += character.character;
                token.end = QLineF::fromPolar(qMax(character.advance, 0.0), character.angle).translated(character.position).p2();
                token.fontSize = qMax(token.fontSize, character.fontSize);
            }

            finishToken(token);
        }
    }

    return tokens;
}

}   // namespace pdfplugin
