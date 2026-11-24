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


#include "pdftoolredact.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentbuilder.h"
#include "pdfconstants.h"
#include "pdfredact.h"
#include "pdffont.h"
#include "pdfutils.h"

#include <QCommandLineParser>

#include <array>

namespace pdftool
{

static PDFToolRedact s_redactApplication;

QString PDFToolRedact::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
    case Command:
        return "redact";

    case Name:
        return PDFToolTranslationContext::tr("Redact");

    case Description:
        return PDFToolTranslationContext::tr("Apply redaction marks and create a redacted document. Content can be marked also by text search, patterns, areas and pages.");

    default:
        Q_ASSERT(false);
        break;
    }

    return QString();
}

void PDFToolRedact::initializeCommandLineParser(QCommandLineParser* parser)
{
    QStringList patternKeys;
    for (pdf::PDFRedactSearch::Pattern pattern : pdf::PDFRedactSearch::getPatterns())
    {
        patternKeys << pdf::PDFRedactSearch::getPatternKey(pattern);
    }

    parser->addOption(QCommandLineOption("redact-search", "Mark all occurrences of the text for redaction (can be repeated).", "text"));
    parser->addOption(QCommandLineOption("redact-regex", "Mark all matches of the regular expression for redaction (can be repeated).", "expression"));
    parser->addOption(QCommandLineOption("redact-pattern", QString("Mark all occurrences of the predefined pattern for redaction (can be repeated): %1.").arg(patternKeys.join(", ")), "pattern"));
    parser->addOption(QCommandLineOption("redact-case-sensitive", "Text search and regular expressions are case sensitive."));
    parser->addOption(QCommandLineOption("redact-whole-words", "Text search and regular expressions match whole words only."));
    parser->addOption(QCommandLineOption("redact-area", "Mark an area for redaction (can be repeated). Format is page:x,y,width,height - page is one based, coordinates are in points, origin in the bottom left corner of the page.", "area"));
    parser->addOption(QCommandLineOption("redact-pages", "Mark whole pages for redaction (for example 1,3-5).", "pages"));
    parser->addOption(QCommandLineOption("redact-color", "Fill color of the redacted areas (default black). Value 'marks' uses the color of each redaction mark.", "color", "black"));
    parser->addOption(QCommandLineOption("redact-mark-only", "Only add the redaction marks (for review in the editor), do not apply them."));
}

bool PDFToolRedact::parseArea(const QString& text, pdf::PDFInteger* pageIndex, QRectF* rectangle)
{
    const QStringList pageAndRectangle = text.split(':');
    if (pageAndRectangle.size() != 2)
    {
        return false;
    }

    bool ok = false;
    const int page = pageAndRectangle[0].trimmed().toInt(&ok);
    if (!ok || page < 1)
    {
        return false;
    }

    const QStringList values = pageAndRectangle[1].split(',');
    if (values.size() != 4)
    {
        return false;
    }

    std::array<qreal, 4> numbers = { };
    for (int i = 0; i < 4; ++i)
    {
        numbers[i] = values[i].trimmed().toDouble(&ok);
        if (!ok || !qIsFinite(numbers[i]))
        {
            return false;
        }
    }

    if (numbers[2] <= 0.0 || numbers[3] <= 0.0)
    {
        return false;
    }

    *pageIndex = page - 1;
    *rectangle = QRectF(numbers[0], numbers[1], numbers[2], numbers[3]);
    return true;
}

void PDFToolRedact::readOptions(QCommandLineParser* parser, PDFToolRedactOptions& options)
{
    auto reportInvalidValue = [&options](const QString& option, const QString& value, const QString& validValues)
    {
        if (options.invalidArgument.isEmpty())
        {
            options.invalidArgument = PDFToolTranslationContext::tr("Invalid value '%1' of the option '--%2'. Valid values are %3.").arg(value, option, validValues);
        }
    };

    options.searchTexts = parser->values("redact-search");
    options.regularExpressions = parser->values("redact-regex");
    options.caseSensitive = parser->isSet("redact-case-sensitive");
    options.wholeWords = parser->isSet("redact-whole-words");
    options.pages = parser->value("redact-pages");
    options.markOnly = parser->isSet("redact-mark-only");

    QStringList patternKeys;
    for (pdf::PDFRedactSearch::Pattern pattern : pdf::PDFRedactSearch::getPatterns())
    {
        patternKeys << pdf::PDFRedactSearch::getPatternKey(pattern);
    }

    for (const QString& patternKey : parser->values("redact-pattern"))
    {
        pdf::PDFRedactSearch::Pattern pattern = pdf::PDFRedactSearch::getPatternByKey(patternKey);
        if (pattern == pdf::PDFRedactSearch::Pattern::None)
        {
            reportInvalidValue("redact-pattern", patternKey, patternKeys.join(", "));
            continue;
        }

        options.patterns.push_back(pattern);
    }

    for (const QString& areaText : parser->values("redact-area"))
    {
        pdf::PDFInteger pageIndex = -1;
        QRectF rectangle;
        if (!parseArea(areaText, &pageIndex, &rectangle))
        {
            reportInvalidValue("redact-area", areaText, PDFToolTranslationContext::tr("page:x,y,width,height (for example 1:72,72,200,50)"));
            continue;
        }

        options.areas.emplace_back(pageIndex, rectangle);
    }

    const QString colorText = parser->value("redact-color");
    if (colorText.compare("marks", Qt::CaseInsensitive) == 0)
    {
        options.fillColor = QColor();
    }
    else
    {
        options.fillColor = QColor::fromString(colorText);
        if (!options.fillColor.isValid())
        {
            reportInvalidValue("redact-color", colorText, PDFToolTranslationContext::tr("color names (black, white), #rrggbb or 'marks'"));
            options.fillColor = Qt::black;
        }
    }
}

int PDFToolRedact::execute(const PDFToolOptions& options)
{
    if (options.redactedDocument.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Redacted document file name is not set."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    const PDFToolRedactOptions& redact = options.redact;
    if (!redact.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(redact.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    iccProfileCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    const pdf::PDFInteger pageCount = pdf::PDFInteger(document.getCatalog()->getPageCount());
    const QColor markColor = redact.fillColor.isValid() ? redact.fillColor : QColor(Qt::black);

    // 1) Mark the content given by the command line options
    pdf::PDFDocumentBuilder builder(&document);
    int addedMarkCount = 0;

    std::vector<pdf::PDFRedactSearch::Settings> searches;
    for (const QString& text : redact.searchTexts)
    {
        pdf::PDFRedactSearch::Settings settings;
        settings.text = text;
        settings.caseSensitive = redact.caseSensitive;
        settings.wholeWords = redact.wholeWords;
        searches.push_back(settings);
    }
    for (const QString& expression : redact.regularExpressions)
    {
        pdf::PDFRedactSearch::Settings settings;
        settings.text = expression;
        settings.regularExpression = true;
        settings.caseSensitive = redact.caseSensitive;
        settings.wholeWords = redact.wholeWords;
        searches.push_back(settings);
    }
    for (pdf::PDFRedactSearch::Pattern pattern : redact.patterns)
    {
        pdf::PDFRedactSearch::Settings settings;
        settings.pattern = pattern;
        searches.push_back(settings);
    }

    if (!searches.empty())
    {
        pdf::PDFTextLayoutStorage textLayouts = pdf::PDFRedactSearch::createTextLayouts(&document, &fontCache, &iccProfileCache, cms.get(), &optionalContentActivity, meshQualitySettings);

        for (const pdf::PDFRedactSearch::Settings& settings : searches)
        {
            QString errorMessage;
            QRegularExpression expression = pdf::PDFRedactSearch::createExpression(settings, &errorMessage);
            if (!pdf::PDFRedactSearch::isValidExpression(expression))
            {
                PDFConsole::writeError(errorMessage, options.outputCodec);
                return ErrorInvalidArguments;
            }

            pdf::PDFRedactSearch::Matches matches = pdf::PDFRedactSearch::find(textLayouts, expression);
            const QString searched = settings.text.isEmpty() ? pdf::PDFRedactSearch::getPatternName(settings.pattern) : settings.text;
            PDFConsole::writeText(PDFToolTranslationContext::tr("'%1': %2 occurrence(s) found.\n").arg(searched).arg(matches.size()), options.outputCodec);
            addedMarkCount += pdf::PDFRedactSearch::createMarks(&builder, &document, matches, markColor);
        }
    }

    for (const auto& [pageIndex, rectangle] : redact.areas)
    {
        const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
        if (!page)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Page %1 of the redacted area doesn't exist (the document has %2 pages).").arg(pageIndex + 1).arg(pageCount), options.outputCodec);
            return ErrorInvalidArguments;
        }

        pdf::PDFObjectReference annotation = builder.createAnnotationRedact(page->getPageReference(), rectangle, markColor, markColor);
        builder.updateAnnotationAppearanceStreams(annotation);
        ++addedMarkCount;
    }

    if (!redact.pages.isEmpty())
    {
        QString errorMessage;
        pdf::PDFClosedIntervalSet pageSet = pdf::PDFClosedIntervalSet::parse(1, pageCount, redact.pages, &errorMessage);
        if (!errorMessage.isEmpty())
        {
            PDFConsole::writeError(errorMessage, options.outputCodec);
            return ErrorInvalidArguments;
        }

        std::vector<pdf::PDFInteger> pageIndices = pageSet.unfold();
        std::for_each(pageIndices.begin(), pageIndices.end(), [](auto& index) { --index; });
        addedMarkCount += pdf::PDFRedactSearch::createPageMarks(&builder, &document, pageIndices, markColor);
    }

    pdf::PDFDocument markedDocument = builder.build();

    pdf::PDFDocument redactedDocument;
    if (redact.markOnly)
    {
        redactedDocument = std::move(markedDocument);
        PDFConsole::writeText(PDFToolTranslationContext::tr("%1 redaction mark(s) added, the document has %2 mark(s).\n").arg(addedMarkCount).arg(pdf::PDFRedact::getRedactionMarkCount(&redactedDocument)), options.outputCodec);
    }
    else
    {
        // 2) Apply all marks (the ones of the document and the added ones)
        pdf::PDFModifiedDocument markedModifiedDocument(&markedDocument, &optionalContentActivity);
        optionalContentActivity.setDocument(&markedDocument);
        fontCache.setDocument(markedModifiedDocument);
        iccProfileCache.setDocument(markedModifiedDocument);

        pdf::PDFRedact redactor(&markedDocument, &fontCache, &iccProfileCache, cms.get(), &optionalContentActivity, &meshQualitySettings, redact.fillColor);
        redactedDocument = redactor.perform(options.redactOptions);

        const pdf::PDFRedact::Statistics& statistics = redactor.getStatistics();
        PDFConsole::writeText(PDFToolTranslationContext::tr("Applied %1 redaction mark(s) on %2 page(s): removed %3 character(s), %4 image(s), %5 drawing(s) and %6 annotation(s); cut %7 image(s) and %8 drawing(s).\n")
                              .arg(statistics.markCount)
                              .arg(statistics.pageCount)
                              .arg(statistics.removedCharacterCount)
                              .arg(statistics.removedImageCount)
                              .arg(statistics.removedPathCount)
                              .arg(statistics.removedAnnotationCount)
                              .arg(statistics.redactedImageCount)
                              .arg(statistics.redactedPathCount + statistics.redactedShadingCount), options.outputCodec);

        for (const QString& error : redactor.getErrors())
        {
            PDFConsole::writeError(error, options.outputCodec);
        }
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.redactedDocument, &redactedDocument, true);
    if (!result)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write redacted document. %1").arg(result.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolRedact::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | ColorManagementSystem | Redact;
}

}   // namespace pdftool
