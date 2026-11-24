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


#include "fixtureschedule.h"
#include "lightingtext.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace pdfplugin
{

/// Helper functions of the schedule reader
class FixtureScheduleHelper
{
public:
    FixtureScheduleHelper() = delete;

    /// Returns true, if the text is a header of the type column
    static bool isTypeHeader(const QString& text);

    /// Returns true, if the text is a header of the description column
    static bool isDescriptionHeader(const QString& text);

    /// Gap between the words of a cell relative to the font size (larger gaps separate the cells)
    static constexpr pdf::PDFReal CELL_GAP = 1.2;
};

bool FixtureScheduleHelper::isTypeHeader(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("^(fixture\\s+|luminaire\\s+)?(type|mark|tag|label|symbol|id|designation|callout)\\.?$"),
                                               QRegularExpression::CaseInsensitiveOption);
    return expression.match(text.trimmed()).hasMatch();
}

bool FixtureScheduleHelper::isDescriptionHeader(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("description|catalog|luminaire|fixture|product|model"),
                                               QRegularExpression::CaseInsensitiveOption);
    return !isTypeHeader(text) && expression.match(text).hasMatch();
}

FixtureScheduleTable FixtureSchedule::readTable(const pdf::PDFTextLayout& layout, const QPolygonF& area)
{
    FixtureScheduleTable table;

    const LightingTextTokens words = LightingText::getTokens(layout, area, FixtureScheduleHelper::CELL_GAP, false);
    if (words.empty())
    {
        return table;
    }

    // Reading direction is the direction of most of the text (schedules
    // on rotated sheets are rotated)
    std::map<std::pair<int, int>, int> directions;
    for (const LightingTextToken& word : words)
    {
        directions[{ qRound(word.direction.x()), qRound(word.direction.y()) }] += 1;
    }
    const auto dominant = std::max_element(directions.cbegin(), directions.cend(), [](const auto& l, const auto& r) { return l.second < r.second; });
    QPointF direction(dominant->first.first, dominant->first.second);
    if (direction.isNull())
    {
        direction = QPointF(1.0, 0.0);
    }
    const QPointF up(-direction.y(), direction.x());

    struct Word
    {
        QString text;
        pdf::PDFReal left = 0.0;
        pdf::PDFReal right = 0.0;
        pdf::PDFReal baseline = 0.0;
        pdf::PDFReal fontSize = 0.0;
    };

    std::vector<Word> readingWords;
    for (const LightingTextToken& word : words)
    {
        if (qRound(word.direction.x()) != qRound(direction.x()) || qRound(word.direction.y()) != qRound(direction.y()))
        {
            continue;
        }

        auto along = [&direction](const QPointF& point) { return point.x() * direction.x() + point.y() * direction.y(); };
        auto across = [&up](const QPointF& point) { return point.x() * up.x() + point.y() * up.y(); };
        readingWords.push_back(Word{ word.text, along(word.start), along(word.end), across(word.start), word.fontSize });
    }

    // Rows: words with the same baseline, from the top
    std::sort(readingWords.begin(), readingWords.end(), [](const Word& l, const Word& r) { return l.baseline > r.baseline; });

    std::vector<std::vector<Word>> rows;
    for (const Word& word : readingWords)
    {
        if (!rows.empty() && std::abs(rows.back().front().baseline - word.baseline) < 0.4 * qMax(word.fontSize, rows.back().front().fontSize))
        {
            rows.back().push_back(word);
        }
        else
        {
            rows.push_back({ word });
        }
    }

    for (std::vector<Word>& row : rows)
    {
        std::sort(row.begin(), row.end(), [](const Word& l, const Word& r) { return l.left < r.left; });
    }

    // Columns: strips covered by the words of the rows with more cells
    pdf::PDFReal minimum = std::numeric_limits<pdf::PDFReal>::max();
    pdf::PDFReal maximum = std::numeric_limits<pdf::PDFReal>::lowest();
    for (const Word& word : readingWords)
    {
        minimum = qMin(minimum, word.left);
        maximum = qMax(maximum, word.right);
    }

    std::vector<std::pair<pdf::PDFReal, pdf::PDFReal>> intervals;
    for (const std::vector<Word>& row : rows)
    {
        if (row.size() == 1 && row.front().right - row.front().left > 0.5 * (maximum - minimum) && rows.size() > 1)
        {
            // Title of the schedule, spanning many columns
            continue;
        }

        for (const Word& word : row)
        {
            intervals.emplace_back(word.left, word.right);
        }
    }

    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<pdf::PDFReal, pdf::PDFReal>> columns;
    for (const auto& interval : intervals)
    {
        if (!columns.empty() && interval.first <= columns.back().second)
        {
            columns.back().second = qMax(columns.back().second, interval.second);
        }
        else
        {
            columns.push_back(interval);
        }
    }

    if (columns.empty())
    {
        columns.emplace_back(minimum, maximum);
    }

    for (const std::vector<Word>& row : rows)
    {
        QStringList cells;
        for (size_t i = 0; i < columns.size(); ++i)
        {
            cells << QString();
        }

        for (const Word& word : row)
        {
            // Column with the largest overlap, or the nearest one. A word spanning
            // more columns (title of the schedule) is in the first of them.
            int bestColumn = 0;
            int overlappedColumns = 0;
            pdf::PDFReal bestScore = std::numeric_limits<pdf::PDFReal>::lowest();
            for (int i = 0; i < int(columns.size()); ++i)
            {
                const pdf::PDFReal overlap = qMin(word.right, columns[i].second) - qMax(word.left, columns[i].first);
                if (overlap > 0.0 && overlappedColumns++ == 0)
                {
                    bestColumn = i;
                    bestScore = std::numeric_limits<pdf::PDFReal>::max();
                }
                if (overlap > bestScore)
                {
                    bestScore = overlap;
                    bestColumn = i;
                }
            }

            QString& cell = cells[bestColumn];
            cell = cell.isEmpty() ? word.text : cell + QChar(' ') + word.text;
        }

        table.push_back(cells);
    }

    return table;
}

int FixtureSchedule::findHeaderRow(const FixtureScheduleTable& table)
{
    for (int i = 0; i < int(table.size()); ++i)
    {
        if (std::any_of(table[i].cbegin(), table[i].cend(), &FixtureScheduleHelper::isTypeHeader))
        {
            return i;
        }
    }

    return -1;
}

int FixtureSchedule::findTypeColumn(const FixtureScheduleTable& table, int headerRow)
{
    if (headerRow >= 0 && headerRow < int(table.size()))
    {
        const QStringList& header = table[headerRow];
        for (int i = 0; i < header.size(); ++i)
        {
            if (FixtureScheduleHelper::isTypeHeader(header[i]))
            {
                return i;
            }
        }
    }

    return 0;
}

int FixtureSchedule::findDescriptionColumn(const FixtureScheduleTable& table, int headerRow, int typeColumn)
{
    if (headerRow >= 0 && headerRow < int(table.size()))
    {
        const QStringList& header = table[headerRow];
        for (int i = 0; i < header.size(); ++i)
        {
            if (i != typeColumn && FixtureScheduleHelper::isDescriptionHeader(header[i]))
            {
                return i;
            }
        }
    }

    // Column with the longest text
    std::map<int, qsizetype> lengths;
    for (int row = headerRow + 1; row < int(table.size()); ++row)
    {
        for (int i = 0; i < table[row].size(); ++i)
        {
            if (i != typeColumn)
            {
                lengths[i] += table[row][i].size();
            }
        }
    }

    int bestColumn = -1;
    qsizetype bestLength = 0;
    for (const auto& item : lengths)
    {
        if (item.second > bestLength)
        {
            bestLength = item.second;
            bestColumn = item.first;
        }
    }

    return bestColumn;
}

FixtureScheduleTable FixtureSchedule::joinWrappedRows(const FixtureScheduleTable& table, int headerRow, int typeColumn)
{
    FixtureScheduleTable result;

    for (int row = 0; row < int(table.size()); ++row)
    {
        const QStringList& cells = table[row];
        const bool isWrapped = row > headerRow + 1 && typeColumn >= 0 && typeColumn < cells.size() && cells[typeColumn].trimmed().isEmpty() && !result.empty();
        if (!isWrapped)
        {
            result.push_back(cells);
            continue;
        }

        QStringList& previous = result.back();
        for (int i = 0; i < cells.size(); ++i)
        {
            if (cells[i].isEmpty())
            {
                continue;
            }

            while (previous.size() <= i)
            {
                previous << QString();
            }
            previous[i] = previous[i].isEmpty() ? cells[i] : previous[i] + QChar(' ') + cells[i];
        }
    }

    return result;
}

FixtureTypes FixtureSchedule::createTypes(const FixtureScheduleTable& table,
                                          int headerRow,
                                          int typeColumn,
                                          int descriptionColumn,
                                          const FixtureTypes& existingTypes)
{
    FixtureTypes types;

    auto isUsed = [&](const QString& name)
    {
        auto hasName = [&name](const FixtureType& type) { return type.name.compare(name, Qt::CaseInsensitive) == 0; };
        return std::any_of(existingTypes.cbegin(), existingTypes.cend(), hasName) || std::any_of(types.cbegin(), types.cend(), hasName);
    };

    for (int row = headerRow + 1; row < int(table.size()); ++row)
    {
        const QStringList& cells = table[row];
        if (typeColumn < 0 || typeColumn >= cells.size())
        {
            continue;
        }

        const QString name = cells[typeColumn].simplified();
        if (name.isEmpty() || name.size() > MAXIMAL_TYPE_LENGTH || FixtureScheduleHelper::isTypeHeader(name) || isUsed(name))
        {
            continue;
        }

        FixtureType type = FixtureType::createDefaultType(int(existingTypes.size() + types.size()));
        type.name = name;
        type.description = (descriptionColumn >= 0 && descriptionColumn < cells.size()) ? cells[descriptionColumn].simplified() : QString();
        types.push_back(type);
    }

    return types;
}

}   // namespace pdfplugin
