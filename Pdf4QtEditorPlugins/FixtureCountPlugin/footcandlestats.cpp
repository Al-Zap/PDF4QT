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


#include "footcandlestats.h"

#include "lightingtext.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace pdfplugin
{

FootcandleStatistics FootcandleStatistics::compute(const FootcandleValues& values, bool ignoreZeros)
{
    FootcandleStatistics statistics;
    statistics.minimum = std::numeric_limits<pdf::PDFReal>::infinity();

    pdf::PDFReal sum = 0.0;
    int decimals = 0;
    for (const FootcandleValue& value : values)
    {
        if (ignoreZeros && value.value <= 0.0)
        {
            continue;
        }

        ++statistics.count;
        sum += value.value;
        statistics.maximum = qMax(statistics.maximum, value.value);
        statistics.minimum = qMin(statistics.minimum, value.value);
        decimals = qMax(decimals, value.decimals);
    }

    if (statistics.count == 0)
    {
        return FootcandleStatistics();
    }

    statistics.average = sum / statistics.count;
    statistics.decimals = qBound(1, decimals, 2);
    return statistics;
}

bool FootcandleGrid::parseValue(const QString& text, pdf::PDFReal* value, int* decimals)
{
    static const QRegularExpression expression(QStringLiteral("^\\+?([0-9]+(\\.([0-9]*))?|\\.([0-9]+))$"));
    const QRegularExpressionMatch match = expression.match(text.trimmed());
    if (!match.hasMatch())
    {
        return false;
    }

    QString number = match.captured(1);
    bool ok = false;
    const pdf::PDFReal parsedValue = number.toDouble(&ok);
    if (!ok || !std::isfinite(parsedValue))
    {
        return false;
    }

    if (value)
    {
        *value = parsedValue;
    }
    if (decimals)
    {
        *decimals = int(qMax(match.captured(3).size(), match.captured(4).size()));
    }
    return true;
}

FootcandleValues FootcandleGrid::collectValues(const pdf::PDFTextLayout& layout, const QPolygonF& area)
{
    FootcandleValues values;

    for (const LightingTextToken& token : LightingText::getTokens(layout, area, VALUE_GAP, true))
    {
        pdf::PDFReal value = 0.0;
        int decimals = 0;
        if (parseValue(token.text, &value, &decimals))
        {
            values.push_back(FootcandleValue{ value, token.getCenter(), decimals });
        }
    }

    return values;
}

QString FootcandleGrid::formatRatio(pdf::PDFReal ratio, bool isDefined)
{
    if (!isDefined || !std::isfinite(ratio))
    {
        return QCoreApplication::translate("pdfplugin::FootcandleGrid", "N/A");
    }

    return QString("%1:1").arg(ratio, 0, 'f', 1);
}

LightingTable FootcandleGrid::createTable(const FootcandleStatistics& statistics, const QString& label, const QString& unit)
{
    LightingTable table;
    table.title = QCoreApplication::translate("pdfplugin::FootcandleGrid", "CALCULATION SUMMARY");

    LightingTableRow header;
    header.bold = true;
    header.cells << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Area")
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Avg (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Max (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Min (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Avg:Min")
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Max:Min")
                 << QCoreApplication::translate("pdfplugin::FootcandleGrid", "Points");
    table.rows.push_back(header);

    LightingTableRow row;
    row.cells << (label.isEmpty() ? QCoreApplication::translate("pdfplugin::FootcandleGrid", "Selected Area") : label)
              << QString::number(statistics.average, 'f', statistics.decimals)
              << QString::number(statistics.maximum, 'f', statistics.decimals)
              << QString::number(statistics.minimum, 'f', statistics.decimals)
              << formatRatio(statistics.getAverageToMinimum(), statistics.hasRatios())
              << formatRatio(statistics.getMaximumToMinimum(), statistics.hasRatios())
              << QString::number(statistics.count);
    table.rows.push_back(row);

    return table;
}

}   // namespace pdfplugin
