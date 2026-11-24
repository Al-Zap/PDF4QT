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


#include "pdftoolstamp.h"
#include "pdfdocumentwriter.h"

#include <QFileInfo>
#include <QImageReader>
#include <QCommandLineParser>

namespace pdftool
{

static PDFToolHeaderFooter s_headerFooterApplication;
static PDFToolBates s_batesApplication;
static PDFToolWatermark s_watermarkApplication;
static PDFToolRemoveStamps s_removeStampsApplication;

static constexpr const char* COMMAND_HEADER_FOOTER = "header-footer";
static constexpr const char* COMMAND_BATES = "bates";
static constexpr const char* COMMAND_WATERMARK = "watermark";
static constexpr const char* COMMAND_REMOVE_STAMPS = "remove-stamps";

void PDFToolStampApplication::initializeCommandLineParser(QCommandLineParser* parser, const QString& command)
{
    parser->addPositionalArgument("target", "Output document filename (the source document is not changed).");

    if (command == COMMAND_REMOVE_STAMPS)
    {
        parser->addOption(QCommandLineOption("remove-kind", "Kind of the removed stamps. Valid values are all|header-footer|watermark (Bates numbers are headers and footers).", "kind", "all"));
        return;
    }

    const bool isWatermark = command == COMMAND_WATERMARK;
    parser->addOption(QCommandLineOption("stamp-font", "Standard font of the text. Valid values are helvetica|helvetica-bold|times|times-bold|courier|courier-bold.", "font", isWatermark ? "helvetica-bold" : "helvetica"));
    parser->addOption(QCommandLineOption("stamp-font-size", isWatermark ? "Font size in points, 0 fits the text to the page (see --watermark-size)." : "Font size in points.", "size", isWatermark ? "0" : "10"));
    parser->addOption(QCommandLineOption("stamp-color", "Color of the text, for example black, red or #1F4E79.", "color", isWatermark ? "#FF0000" : "#000000"));
    parser->addOption(QCommandLineOption("stamp-opacity", "Opacity of the stamp, from 0 (invisible) to 1 (opaque).", "opacity", isWatermark ? "0.3" : "1"));
    parser->addOption(QCommandLineOption("stamp-margins", "Distances from the edges of the displayed page in points: a single value, or left,top,right,bottom.", "margins", "36"));
    parser->addOption(QCommandLineOption("stamp-replace", "Replace the stamps of the same kind, which were added before by PDF4QT."));
    parser->addOption(QCommandLineOption("start-page-number", "Number written by {page} on the first stamped page.", "number", "1"));
    parser->addOption(QCommandLineOption("date-format", "Format of {date} (Qt date format).", "format", "MM/dd/yyyy"));
    parser->addOption(QCommandLineOption("time-format", "Format of {time} (Qt time format).", "format", "h:mm AP"));
    parser->addOption(QCommandLineOption("file-name", "Text of {file} (the file name of the source document by default).", "text"));

    if (command == COMMAND_HEADER_FOOTER || command == COMMAND_BATES)
    {
        parser->addOption(QCommandLineOption("bates-prefix", "Text in front of the Bates number.", "prefix"));
        parser->addOption(QCommandLineOption("bates-suffix", "Text behind the Bates number.", "suffix"));
        parser->addOption(QCommandLineOption("bates-start", "Bates number of the first stamped page.", "number", "1"));
        parser->addOption(QCommandLineOption("bates-digits", "Minimal number of the digits of the Bates number (padded by zeros, 1-15).", "digits", "6"));
    }

    if (command == COMMAND_HEADER_FOOTER)
    {
        const QString description = QString("Text of the position %1. Variables {page}, {pages}, {bates}, {date}, {time}, {file} and {title} are replaced, \\n starts a new line.");
        for (int i = 0; i < pdf::PDFHeaderFooterSettings::PositionCount; ++i)
        {
            const QString name = pdf::PDFHeaderFooterSettings::getPositionName(static_cast<pdf::PDFHeaderFooterSettings::Position>(i));
            parser->addOption(QCommandLineOption(name, description.arg(name), "text"));
        }
    }

    if (command == COMMAND_BATES)
    {
        parser->addOption(QCommandLineOption("bates-position", "Position of the Bates number. Valid values are header-left|header-center|header-right|footer-left|footer-center|footer-right.", "position", "footer-right"));
        parser->addOption(QCommandLineOption("bates-text", "Text written at the position, {bates} is replaced by the Bates number (other variables can be used, see command header-footer).", "text", "{bates}"));
    }

    if (isWatermark)
    {
        parser->addOption(QCommandLineOption("watermark-text", "Text of the watermark (\\n starts a new line, variables of the command header-footer can be used).", "text"));
        parser->addOption(QCommandLineOption("watermark-image", "Image file of the watermark (used, when no text is given).", "file"));
        parser->addOption(QCommandLineOption("watermark-rotation", "Rotation in degrees, counterclockwise.", "degrees", "45"));
        parser->addOption(QCommandLineOption("watermark-size", "Size relative to the page (0.01-1), used for images and for texts with font size 0.", "fraction", "0.5"));
        parser->addOption(QCommandLineOption("watermark-alignment", "Position on the page. Valid values are center|top|bottom|left|right|top-left|top-right|bottom-left|bottom-right.", "alignment", "center"));
        parser->addOption(QCommandLineOption("watermark-offset", "Shift of the watermark in points (x,y; positive values move it to the right and up).", "offset", "0,0"));
        parser->addOption(QCommandLineOption("watermark-behind", "Draw the watermark behind the page content (it is hidden by opaque content, for example by scanned images)."));
    }
}

void PDFToolStampApplication::reportInvalidValue(PDFToolStampOptions& options, const QString& option, const QString& value, const QString& validValues)
{
    if (options.invalidArgument.isEmpty())
    {
        options.invalidArgument = PDFToolTranslationContext::tr("Invalid value '%1' of the option '--%2'. Valid values are %3.").arg(value, option, validValues);
    }
}

bool PDFToolStampApplication::readMargins(const QString& text, QMarginsF* margins)
{
    const QStringList parts = text.split(QChar(','), Qt::KeepEmptyParts);
    std::vector<double> values;
    for (const QString& part : parts)
    {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (!ok || value < 0.0)
        {
            return false;
        }
        values.push_back(value);
    }

    if (values.size() == 1)
    {
        *margins = QMarginsF(values[0], values[0], values[0], values[0]);
        return true;
    }

    if (values.size() == 4)
    {
        *margins = QMarginsF(values[0], values[1], values[2], values[3]);
        return true;
    }

    return false;
}

void PDFToolStampApplication::readTextStyle(QCommandLineParser* parser, PDFToolStampOptions& options, pdf::PDFStampTextStyle& style)
{
    const QString fontName = parser->value("stamp-font");
    if (std::optional<pdf::PDFStampFont> font = pdf::PDFPageStamper::getFontFromName(fontName))
    {
        style.font = *font;
    }
    else
    {
        reportInvalidValue(options, "stamp-font", fontName, "helvetica|helvetica-bold|times|times-bold|courier|courier-bold");
    }

    bool ok = false;
    const QString fontSize = parser->value("stamp-font-size");
    style.fontSize = fontSize.toDouble(&ok);
    if (!ok || style.fontSize < 0.0 || style.fontSize > 1000.0)
    {
        reportInvalidValue(options, "stamp-font-size", fontSize, "numbers from 0 to 1000");
    }

    const QString colorName = parser->value("stamp-color");
    style.color = QColor::fromString(colorName);
    if (!style.color.isValid())
    {
        reportInvalidValue(options, "stamp-color", colorName, "color names and #RRGGBB");
    }

    const QString opacity = parser->value("stamp-opacity");
    style.opacity = opacity.toDouble(&ok);
    if (!ok || style.opacity < 0.0 || style.opacity > 1.0)
    {
        reportInvalidValue(options, "stamp-opacity", opacity, "numbers from 0 to 1");
    }
}

void PDFToolStampApplication::readVariables(QCommandLineParser* parser, PDFToolStampOptions& options, pdf::PDFStampVariables& variables)
{
    bool ok = false;
    const QString startPageNumber = parser->value("start-page-number");
    variables.startPageNumber = startPageNumber.toLongLong(&ok);
    if (!ok)
    {
        reportInvalidValue(options, "start-page-number", startPageNumber, "whole numbers");
    }

    variables.dateFormat = parser->value("date-format");
    variables.timeFormat = parser->value("time-format");
    variables.fileName = parser->value("file-name");
}

QString PDFToolStampApplication::unescapeText(const QString& text)
{
    QString result;
    result.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (text[i] == QChar('\\') && i + 1 < text.size())
        {
            if (text[i + 1] == QChar('n'))
            {
                result.append(QChar('\n'));
                ++i;
                continue;
            }
            if (text[i + 1] == QChar('\\'))
            {
                result.append(QChar('\\'));
                ++i;
                continue;
            }
        }
        result.append(text[i]);
    }
    return result;
}

void PDFToolStampApplication::readOptions(QCommandLineParser* parser, const QString& command, PDFToolStampOptions& options)
{
    const QStringList positionalArguments = parser->positionalArguments();
    options.target = positionalArguments.size() >= 2 ? positionalArguments[1] : QString();

    if (command == COMMAND_REMOVE_STAMPS)
    {
        const QString kind = parser->value("remove-kind").trimmed().toLower();
        if (kind == "all")
        {
            options.removedKinds = pdf::PDFPageStamper::AllKinds;
        }
        else if (kind == "header-footer")
        {
            options.removedKinds = pdf::PDFPageStamper::HeaderFooter;
        }
        else if (kind == "watermark")
        {
            options.removedKinds = pdf::PDFPageStamper::Watermark;
        }
        else
        {
            reportInvalidValue(options, "remove-kind", kind, "all|header-footer|watermark");
        }
        return;
    }

    options.replace = parser->isSet("stamp-replace");

    QMarginsF margins;
    const QString marginsText = parser->value("stamp-margins");
    if (!readMargins(marginsText, &margins))
    {
        reportInvalidValue(options, "stamp-margins", marginsText, "non-negative numbers (one or four, separated by commas)");
    }

    if (command == COMMAND_HEADER_FOOTER || command == COMMAND_BATES)
    {
        pdf::PDFHeaderFooterSettings& settings = options.headerFooter;
        settings.margins = margins;
        readTextStyle(parser, options, settings.style);
        readVariables(parser, options, settings.variables);

        settings.variables.batesPrefix = parser->value("bates-prefix");
        settings.variables.batesSuffix = parser->value("bates-suffix");

        bool ok = false;
        const QString batesStart = parser->value("bates-start");
        settings.variables.batesStart = batesStart.toLongLong(&ok);
        if (!ok || settings.variables.batesStart < 0)
        {
            reportInvalidValue(options, "bates-start", batesStart, "non-negative whole numbers");
        }

        const QString batesDigits = parser->value("bates-digits");
        settings.variables.batesDigits = batesDigits.toInt(&ok);
        if (!ok || settings.variables.batesDigits < 1 || settings.variables.batesDigits > 15)
        {
            reportInvalidValue(options, "bates-digits", batesDigits, "whole numbers from 1 to 15");
        }

        if (command == COMMAND_HEADER_FOOTER)
        {
            for (int i = 0; i < pdf::PDFHeaderFooterSettings::PositionCount; ++i)
            {
                const QString name = pdf::PDFHeaderFooterSettings::getPositionName(static_cast<pdf::PDFHeaderFooterSettings::Position>(i));
                settings.texts[i] = unescapeText(parser->value(name));
            }
        }
        else
        {
            const QString positionName = parser->value("bates-position");
            const pdf::PDFHeaderFooterSettings::Position position = pdf::PDFHeaderFooterSettings::getPositionFromName(positionName);
            if (position == pdf::PDFHeaderFooterSettings::PositionCount)
            {
                reportInvalidValue(options, "bates-position", positionName, "header-left|header-center|header-right|footer-left|footer-center|footer-right");
            }
            else
            {
                settings.texts[position] = unescapeText(parser->value("bates-text"));
            }
        }
    }
    else if (command == COMMAND_WATERMARK)
    {
        pdf::PDFWatermarkSettings& settings = options.watermark;
        settings.margins = margins;
        readTextStyle(parser, options, settings.style);
        readVariables(parser, options, settings.variables);

        settings.text = unescapeText(parser->value("watermark-text"));
        options.watermarkImageFile = parser->value("watermark-image");
        settings.behindContent = parser->isSet("watermark-behind");

        bool ok = false;
        const QString rotation = parser->value("watermark-rotation");
        settings.rotation = rotation.toDouble(&ok);
        if (!ok)
        {
            reportInvalidValue(options, "watermark-rotation", rotation, "numbers");
        }

        const QString size = parser->value("watermark-size");
        settings.relativeSize = size.toDouble(&ok);
        if (!ok || settings.relativeSize < 0.01 || settings.relativeSize > 1.0)
        {
            reportInvalidValue(options, "watermark-size", size, "numbers from 0.01 to 1");
        }

        const QString alignment = parser->value("watermark-alignment").trimmed().toLower();
        static const std::map<QString, Qt::Alignment> alignments = {
            { "center", Qt::AlignCenter },
            { "top", Qt::AlignTop | Qt::AlignHCenter },
            { "bottom", Qt::AlignBottom | Qt::AlignHCenter },
            { "left", Qt::AlignLeft | Qt::AlignVCenter },
            { "right", Qt::AlignRight | Qt::AlignVCenter },
            { "top-left", Qt::AlignTop | Qt::AlignLeft },
            { "top-right", Qt::AlignTop | Qt::AlignRight },
            { "bottom-left", Qt::AlignBottom | Qt::AlignLeft },
            { "bottom-right", Qt::AlignBottom | Qt::AlignRight }
        };
        auto it = alignments.find(alignment);
        if (it != alignments.cend())
        {
            settings.alignment = it->second;
        }
        else
        {
            reportInvalidValue(options, "watermark-alignment", alignment, "center|top|bottom|left|right|top-left|top-right|bottom-left|bottom-right");
        }

        const QString offset = parser->value("watermark-offset");
        const QStringList offsetParts = offset.split(QChar(','));
        bool okX = false;
        bool okY = false;
        if (offsetParts.size() == 2)
        {
            settings.offset = QPointF(offsetParts[0].trimmed().toDouble(&okX), offsetParts[1].trimmed().toDouble(&okY));
        }
        if (!okX || !okY)
        {
            reportInvalidValue(options, "watermark-offset", offset, "two numbers x,y");
        }
    }
}

PDFToolAbstractApplication::Options PDFToolStampApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | PageStamp;
}

int PDFToolStampApplication::execute(const PDFToolOptions& options)
{
    const QString command = getStandardString(Command);
    PDFToolStampOptions stamp = options.stamp;

    if (!stamp.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(stamp.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (stamp.target.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Output document is not specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    const QFileInfo sourceInfo(options.document);
    const QFileInfo targetInfo(stamp.target);
    if (targetInfo.exists() && sourceInfo.exists() && sourceInfo.canonicalFilePath() == targetInfo.canonicalFilePath())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Output document must be different from the source document."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (command == COMMAND_HEADER_FOOTER && stamp.headerFooter.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No header or footer text is specified (use --header-left, --header-center, ..., --footer-right)."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (command == COMMAND_WATERMARK && stamp.watermark.text.trimmed().isEmpty())
    {
        if (stamp.watermarkImageFile.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("No watermark text or image is specified (use --watermark-text or --watermark-image)."), options.outputCodec);
            return ErrorInvalidArguments;
        }

        QImageReader reader(stamp.watermarkImageFile);
        reader.setAutoTransform(true);
        stamp.watermark.image = reader.read();
        if (stamp.watermark.image.isNull())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot read the image '%1'. %2").arg(stamp.watermarkImageFile, reader.errorString()), options.outputCodec);
            return ErrorInvalidArguments;
        }
    }

    pdf::PDFDocument document;
    if (!readDocument(options, document, nullptr, false))
    {
        return ErrorDocumentReading;
    }

    QString errorMessage;
    const pdf::PDFInteger pageCount = pdf::PDFInteger(document.getCatalog()->getPageCount());
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(pageCount, errorMessage, true);
    if (!errorMessage.isEmpty())
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    const QString fileName = sourceInfo.fileName();
    if (stamp.headerFooter.variables.fileName.isEmpty())
    {
        stamp.headerFooter.variables.fileName = fileName;
    }
    if (stamp.watermark.variables.fileName.isEmpty())
    {
        stamp.watermark.variables.fileName = fileName;
    }

    pdf::PDFPageStamper::Result result;
    if (command == COMMAND_REMOVE_STAMPS)
    {
        result = pdf::PDFPageStamper::removeStamps(&document, stamp.removedKinds, pageIndices);
    }
    else if (command == COMMAND_WATERMARK)
    {
        result = pdf::PDFPageStamper::addWatermark(&document, stamp.watermark, pageIndices, stamp.replace);
    }
    else
    {
        result = pdf::PDFPageStamper::addHeaderFooter(&document, stamp.headerFooter, pageIndices, stamp.replace);
    }

    if (!result.isSuccess())
    {
        PDFConsole::writeError(result.errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument(command, getStandardString(Name));

    if (command == COMMAND_REMOVE_STAMPS)
    {
        formatter.writeText("removed-count", PDFToolTranslationContext::tr("Stamps removed: %1 (pages: %2).").arg(result.removedStampCount).arg(result.pageCount));
    }
    else
    {
        formatter.writeText("page-count", PDFToolTranslationContext::tr("Pages stamped: %1.").arg(result.pageCount));
        if (result.removedStampCount > 0)
        {
            formatter.writeText("replaced-count", PDFToolTranslationContext::tr("Stamps replaced: %1.").arg(result.removedStampCount));
        }
        if (!result.firstBatesNumber.isEmpty())
        {
            formatter.writeText("first-bates", PDFToolTranslationContext::tr("First Bates number: %1").arg(result.firstBatesNumber));
            formatter.writeText("last-bates", PDFToolTranslationContext::tr("Last Bates number: %1").arg(result.lastBatesNumber));
            formatter.writeText("next-bates", PDFToolTranslationContext::tr("Next Bates number: %1").arg(result.nextBatesNumber));
        }
    }

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    // Nothing was removed: the target is a copy of the source document
    const pdf::PDFDocument* targetDocument = result.document ? result.document.data() : &document;

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult writeResult = writer.write(stamp.target, targetDocument, true);
    if (!writeResult)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write document. %1").arg(writeResult.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

QString PDFToolHeaderFooter::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return COMMAND_HEADER_FOOTER;

        case Name:
            return PDFToolTranslationContext::tr("Header and footer");

        case Description:
            return PDFToolTranslationContext::tr("Add headers and footers (page numbers, dates, file name, Bates numbers) to the pages.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PDFToolBates::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return COMMAND_BATES;

        case Name:
            return PDFToolTranslationContext::tr("Bates numbering");

        case Description:
            return PDFToolTranslationContext::tr("Add Bates numbers (prefix, sequential number, suffix) to the pages.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PDFToolWatermark::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return COMMAND_WATERMARK;

        case Name:
            return PDFToolTranslationContext::tr("Watermark");

        case Description:
            return PDFToolTranslationContext::tr("Add a text or image watermark to the pages.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PDFToolRemoveStamps::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return COMMAND_REMOVE_STAMPS;

        case Name:
            return PDFToolTranslationContext::tr("Remove stamps");

        case Description:
            return PDFToolTranslationContext::tr("Remove headers, footers, Bates numbers and watermarks added by PDF4QT.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

}   // namespace pdftool
