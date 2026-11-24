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

#include "pdftoolcompare.h"
#include "pdfvisualcompare.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QFileInfo>
#include <QCommandLineParser>

#include <numeric>
#include <optional>

namespace pdftool
{

static PDFToolCompare s_toolCompareApplication;

QString PDFToolCompare::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "compare";

        case Name:
            return PDFToolTranslationContext::tr("Compare revisions visually");

        case Description:
            return PDFToolTranslationContext::tr("Overlay two revisions of a document (drawing sheets), list the changed areas and optionally write a report with revision clouds.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolCompare::getOptionsFlags() const
{
    return ConsoleFormat | Diff | VisualCompare;
}

void PDFToolCompare::initializeCommandLineParser(QCommandLineParser* parser)
{
    parser->addOption(QCommandLineOption("compare-report", "Write a report document (summary, overlay of each page, revision clouds around the changes).", "file"));
    parser->addOption(QCommandLineOption("compare-resolution", "Resolution of the comparison in pixels per inch (10-600).", "ppi", "150"));
    parser->addOption(QCommandLineOption("compare-align", "Alignment of the old page to the new page. Valid values are auto|origin|center.", "alignment", "auto"));
    parser->addOption(QCommandLineOption("compare-max-shift", "Largest shift of the old page searched by the automatic alignment, in millimeters.", "mm", "25"));
    parser->addOption(QCommandLineOption("compare-scale-to-fit", "Scale the old page to the size of the new page (otherwise the pages are compared at the same scale)."));
    parser->addOption(QCommandLineOption("compare-tolerance", "Content moved at most by this number of pixels is not a change (0-16).", "pixels", "1"));
    parser->addOption(QCommandLineOption("compare-color-tolerance", "Content in both pages is changed, when its color differs by more than this value (0-255, 0 turns off the color comparison).", "value", "96"));
    parser->addOption(QCommandLineOption("compare-merge-distance", "Changes closer than this distance are reported as one change, in millimeters.", "mm", "4"));
    parser->addOption(QCommandLineOption("compare-min-size", "Changes smaller than this size are ignored, in millimeters.", "mm", "0.5"));
    parser->addOption(QCommandLineOption("compare-old-pages", "Pages of the old document to compare, for example 1-3,5 (all pages by default).", "pages"));
    parser->addOption(QCommandLineOption("compare-new-pages", "Pages of the new document to compare, for example 1-3,5 (all pages by default).", "pages"));
}

void PDFToolCompare::readOptions(QCommandLineParser* parser, PDFToolVisualCompareOptions& options)
{
    auto reportInvalid = [&options](const QString& option, const QString& value, const QString& validValues)
    {
        if (options.invalidArgument.isEmpty())
        {
            options.invalidArgument = PDFToolTranslationContext::tr("Invalid value '%1' of the option --%2. Valid values are %3.").arg(value, option, validValues);
        }
    };

    auto readReal = [&](const QString& option, pdf::PDFReal minimum, pdf::PDFReal maximum, pdf::PDFReal& value)
    {
        const QString text = parser->value(option).trimmed();
        bool ok = false;
        const pdf::PDFReal number = text.toDouble(&ok);
        if (!ok || number < minimum || number > maximum)
        {
            reportInvalid(option, text, QString("%1-%2").arg(minimum).arg(maximum));
            return;
        }
        value = number;
    };

    auto readInteger = [&](const QString& option, int minimum, int maximum, int& value)
    {
        const QString text = parser->value(option).trimmed();
        bool ok = false;
        const int number = text.toInt(&ok);
        if (!ok || number < minimum || number > maximum)
        {
            reportInvalid(option, text, QString("%1-%2").arg(minimum).arg(maximum));
            return;
        }
        value = number;
    };

    pdf::PDFVisualCompareSettings& settings = options.settings;
    options.reportFile = parser->value("compare-report");
    readReal("compare-resolution", 10.0, 600.0, settings.resolution);
    readReal("compare-max-shift", 0.0, 500.0, settings.maximumShiftMM);
    readInteger("compare-tolerance", 0, 16, settings.tolerancePixels);
    readInteger("compare-color-tolerance", 0, 255, settings.colorTolerance);
    readReal("compare-merge-distance", 0.0, 500.0, settings.mergeDistanceMM);
    readReal("compare-min-size", 0.0, 500.0, settings.minimumChangeSizeMM);
    settings.scaleToFit = parser->isSet("compare-scale-to-fit");

    const QString alignment = parser->value("compare-align").trimmed().toLower();
    if (alignment == "auto")
    {
        settings.alignment = pdf::PDFVisualCompareSettings::Alignment::Automatic;
    }
    else if (alignment == "origin")
    {
        settings.alignment = pdf::PDFVisualCompareSettings::Alignment::PageOrigin;
    }
    else if (alignment == "center")
    {
        settings.alignment = pdf::PDFVisualCompareSettings::Alignment::PageCenter;
    }
    else
    {
        reportInvalid("compare-align", alignment, "auto|origin|center");
    }

    options.oldPages = parser->value("compare-old-pages").trimmed();
    options.newPages = parser->value("compare-new-pages").trimmed();
}

int PDFToolCompare::execute(const PDFToolOptions& options)
{
    const PDFToolVisualCompareOptions& compareOptions = options.visualCompare;
    if (!compareOptions.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(compareOptions.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.diffFiles.size() != 2)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Exactly two documents must be specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (!compareOptions.reportFile.isEmpty())
    {
        const QFileInfo reportInfo(compareOptions.reportFile);
        for (const QString& fileName : options.diffFiles)
        {
            const QFileInfo info(fileName);
            if (reportInfo.exists() && info.exists() && reportInfo.canonicalFilePath() == info.canonicalFilePath())
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Report document must be different from the compared documents."), options.outputCodec);
                return ErrorInvalidArguments;
            }
        }
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, options.permissiveReading, false);

    pdf::PDFDocument oldDocument = reader.readFromFile(options.diffFiles.front());
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(options.diffFiles.front()), options.outputCodec);
        return ErrorDocumentReading;
    }

    pdf::PDFDocument newDocument = reader.readFromFile(options.diffFiles.back());
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(options.diffFiles.back()), options.outputCodec);
        return ErrorDocumentReading;
    }

    auto getPages = [&](const pdf::PDFDocument& document, const QString& text, std::vector<pdf::PDFInteger>& pages) -> bool
    {
        QString errorMessage;
        if (!pdf::PDFVisualCompare::parsePages(text, pdf::PDFInteger(document.getCatalog()->getPageCount()), pages, &errorMessage))
        {
            PDFConsole::writeError(errorMessage, options.outputCodec);
            return false;
        }
        return true;
    };

    std::vector<pdf::PDFInteger> oldPages;
    std::vector<pdf::PDFInteger> newPages;
    if (!getPages(oldDocument, compareOptions.oldPages, oldPages) || !getPages(newDocument, compareOptions.newPages, newPages))
    {
        return ErrorInvalidArguments;
    }

    pdf::PDFVisualCompare compare(&oldDocument, &newDocument, compareOptions.settings);
    const pdf::PDFVisualCompare::PagePairs pairs = pdf::PDFVisualCompare::createPagePairs(oldPages, newPages);

    pdf::PDFVisualCompareReport::Information information;
    information.oldDocumentName = QFileInfo(options.diffFiles.front()).fileName();
    information.newDocumentName = QFileInfo(options.diffFiles.back()).fileName();
    information.dateTime = QDateTime::currentDateTime();

    std::optional<pdf::PDFVisualCompareReport> report;
    if (!compareOptions.reportFile.isEmpty())
    {
        report.emplace(compareOptions.settings, information);
    }

    pdf::PDFVisualCompare::PageCallback callback;
    if (report)
    {
        callback = [&report](const pdf::PDFVisualComparePageResult& page) { report->addPage(page); };
    }

    const pdf::PDFVisualCompareResult result = compare.compare(pairs, callback);
    if (!result.isSuccess())
    {
        PDFConsole::writeError(result.errorMessage, options.outputCodec);
        return ErrorUnknown;
    }

    QLocale locale;
    auto formatMM = [&locale](pdf::PDFReal points) { return locale.toString(points / 72.0 * 25.4, 'f', 1); };

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("compare", PDFToolTranslationContext::tr("Visual comparison of '%1' and '%2'").arg(information.oldDocumentName, information.newDocumentName));
    formatter.endl();

    formatter.beginTable("pages", PDFToolTranslationContext::tr("Pages"));
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("old-page", PDFToolTranslationContext::tr("Old Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("new-page", PDFToolTranslationContext::tr("New Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("status", PDFToolTranslationContext::tr("Status"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("changes", PDFToolTranslationContext::tr("Changes"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("shift", PDFToolTranslationContext::tr("Shift of Old Page [mm]"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    int rowIndex = 0;
    for (const pdf::PDFVisualComparePageResult& page : result.pages)
    {
        QString status = PDFToolTranslationContext::tr("No changes");
        if (!page.errorMessage.isEmpty())
        {
            status = PDFToolTranslationContext::tr("Error: %1").arg(page.errorMessage);
        }
        else if (page.isPageAdded())
        {
            status = PDFToolTranslationContext::tr("Page added");
        }
        else if (page.isPageRemoved())
        {
            status = PDFToolTranslationContext::tr("Page removed");
        }
        else if (page.hasChanges())
        {
            status = PDFToolTranslationContext::tr("Changed");
        }

        const pdf::PDFReal pixelToPoint = 72.0 / compareOptions.settings.resolution;
        formatter.beginTableRow("page", rowIndex++);
        formatter.writeTableColumn("old-page", page.oldPageIndex >= 0 ? locale.toString(page.oldPageIndex + 1) : QString("-"), Qt::AlignRight);
        formatter.writeTableColumn("new-page", page.newPageIndex >= 0 ? locale.toString(page.newPageIndex + 1) : QString("-"), Qt::AlignRight);
        formatter.writeTableColumn("status", status, Qt::AlignLeft);
        formatter.writeTableColumn("changes", locale.toString(qulonglong(page.changes.size())), Qt::AlignRight);
        formatter.writeTableColumn("shift", QString("%1, %2").arg(formatMM(page.offset.x() * pixelToPoint), formatMM(page.offset.y() * pixelToPoint)), Qt::AlignRight);
        formatter.endTableRow();
    }
    formatter.endTable();
    formatter.endl();

    formatter.beginTable("changes", PDFToolTranslationContext::tr("Changes"));
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("old-page", PDFToolTranslationContext::tr("Old Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("new-page", PDFToolTranslationContext::tr("New Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("left", PDFToolTranslationContext::tr("Left [mm]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("top", PDFToolTranslationContext::tr("Top [mm]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("width", PDFToolTranslationContext::tr("Width [mm]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("height", PDFToolTranslationContext::tr("Height [mm]"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    int changeIndex = 0;
    for (const pdf::PDFVisualComparePageResult& page : result.pages)
    {
        for (const pdf::PDFVisualCompareChange& change : page.changes)
        {
            // Position is measured from the top left corner of the visible page
            const pdf::PDFReal top = page.pageSize.height() - change.pageRect.bottom();
            formatter.beginTableRow("change", changeIndex);
            formatter.writeTableColumn("no", locale.toString(++changeIndex), Qt::AlignRight);
            formatter.writeTableColumn("old-page", page.oldPageIndex >= 0 ? locale.toString(page.oldPageIndex + 1) : QString("-"), Qt::AlignRight);
            formatter.writeTableColumn("new-page", page.newPageIndex >= 0 ? locale.toString(page.newPageIndex + 1) : QString("-"), Qt::AlignRight);
            formatter.writeTableColumn("type", pdf::PDFVisualCompareChange::getTypeName(change.type), Qt::AlignLeft);
            formatter.writeTableColumn("left", formatMM(change.pageRect.left()), Qt::AlignRight);
            formatter.writeTableColumn("top", formatMM(top), Qt::AlignRight);
            formatter.writeTableColumn("width", formatMM(change.pageRect.width()), Qt::AlignRight);
            formatter.writeTableColumn("height", formatMM(change.pageRect.height()), Qt::AlignRight);
            formatter.endTableRow();
        }
    }
    formatter.endTable();
    formatter.endl();

    formatter.writeText("summary", PDFToolTranslationContext::tr("%1 page pairs compared, %2 with changes, %3 changes.").arg(result.pages.size()).arg(result.getChangedPageCount()).arg(result.getChangeCount()));
    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    if (report)
    {
        pdf::PDFDocument reportDocument = report->build(result);
        pdf::PDFDocumentWriter writer(nullptr);
        pdf::PDFOperationResult writeResult = writer.write(compareOptions.reportFile, &reportDocument, true);
        if (!writeResult)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write document. %1").arg(writeResult.getErrorMessage()), options.outputCodec);
            return ErrorFailedWriteToFile;
        }
    }

    return ExitSuccess;
}

}   // namespace pdftool
