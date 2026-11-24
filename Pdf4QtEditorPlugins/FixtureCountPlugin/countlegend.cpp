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


#include "countlegend.h"

#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"

#include <QCoreApplication>

namespace pdfplugin
{

LightingTable CountLegend::createTable(const FixtureTypes& types,
                                       const FixtureCountData& data,
                                       LightingLegendScope scope,
                                       pdf::PDFInteger pageIndex,
                                       const QString& sheetName)
{
    const pdf::PDFInteger countedPage = scope == LightingLegendScope::Document ? -1 : pageIndex;

    FixtureTypes allTypes = types;
    for (const FixtureType& documentType : data.getTypes())
    {
        if (std::none_of(allTypes.cbegin(), allTypes.cend(), [&documentType](const FixtureType& type) { return type.name == documentType.name; }))
        {
            allTypes.push_back(documentType);
        }
    }

    LightingTable table;
    table.hasSymbolColumn = true;

    if (scope == LightingLegendScope::Document)
    {
        table.title = QCoreApplication::translate("pdfplugin::CountLegend", "FIXTURE COUNT - ALL SHEETS");
    }
    else if (!sheetName.isEmpty())
    {
        table.title = QCoreApplication::translate("pdfplugin::CountLegend", "FIXTURE COUNT - %1").arg(sheetName.toUpper());
    }
    else
    {
        table.title = QCoreApplication::translate("pdfplugin::CountLegend", "FIXTURE COUNT");
    }

    LightingTableRow header;
    header.bold = true;
    header.cells << QCoreApplication::translate("pdfplugin::CountLegend", "Type")
                 << QCoreApplication::translate("pdfplugin::CountLegend", "Description")
                 << QCoreApplication::translate("pdfplugin::CountLegend", "Qty");
    table.rows.push_back(header);

    int total = 0;
    for (const FixtureType& type : allTypes)
    {
        const int count = data.getCount(type.name, countedPage);
        if (count <= 0)
        {
            continue;
        }

        // Markers of the document define the symbol of the type
        auto it = std::find_if(data.getTypes().cbegin(), data.getTypes().cend(), [&type](const FixtureType& item) { return item.name == type.name; });

        LightingTableRow row;
        row.symbol = it != data.getTypes().cend() ? *it : type;
        if (row.symbol.description.isEmpty())
        {
            row.symbol.description = type.description;
        }
        row.cells << type.name << row.symbol.description << QString::number(count);
        table.rows.push_back(row);
        total += count;
    }

    LightingTableRow totalRow;
    totalRow.bold = true;
    totalRow.cells << QCoreApplication::translate("pdfplugin::CountLegend", "Total") << QString() << QString::number(total);
    table.rows.push_back(totalRow);

    return table;
}

int CountLegend::updateLegends(pdf::PDFDocumentBuilder* builder, const FixtureTypes& types)
{
    // Snapshot of the modified document, the counts are taken from it
    const pdf::PDFDocument document = builder->build();
    const LightingTableAnnotations legends = LightingTableWriter::scan(&document, LightingTableKind::Legend);
    if (legends.empty())
    {
        return 0;
    }

    const FixtureCountData data = FixtureCountData::scan(&document);
    const QStringList sheetNames = FixtureCountReport::getSheetNames(&document);

    int count = 0;
    for (const LightingTableAnnotation& legend : legends)
    {
        const QString sheetName = legend.pageIndex < sheetNames.size() ? sheetNames[legend.pageIndex] : QString();
        const LightingTable table = createTable(types, data, legend.scope, legend.pageIndex, sheetName);
        if (LightingTableWriter::updateTable(builder, &document, legend, table))
        {
            ++count;
        }
    }

    return count;
}

}   // namespace pdfplugin
