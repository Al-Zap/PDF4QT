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



#include "pdftoolbatch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCommandLineParser>

namespace pdftool
{

static PDFToolBatch s_batchApplication;

QString PDFToolBatch::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "batch";

        case Name:
            return PDFToolTranslationContext::tr("Batch Process");

        case Description:
            return PDFToolTranslationContext::tr("Apply a sequence of actions (saved by Convert > Batch Process of the editor) to many PDF files.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolBatch::getOptionsFlags() const
{
    return ConsoleFormat | Batch;
}

void PDFToolBatch::initializeCommandLineParser(QCommandLineParser* parser)
{
    parser->addPositionalArgument("inputs", "PDF files and folders with PDF files to process.", "inputs...");
    parser->addOption(QCommandLineOption("sequence", "Sequence of the actions (JSON file saved by the batch dialog of the editor).", "file"));
    parser->addOption(QCommandLineOption("output-dir", "Folder of the processed files (overrides the folder of the sequence).", "folder"));
    parser->addOption(QCommandLineOption("next-to-original", "Write the processed files next to the original files (needs a suffix)."));
    parser->addOption(QCommandLineOption("suffix", "Text added to the names of the processed files (overrides the suffix of the sequence).", "text"));
    parser->addOption(QCommandLineOption("existing", "Existing output files. Valid values are rename|overwrite|skip (overrides the sequence).", "policy"));
    parser->addOption(QCommandLineOption("recursive", "Search the folders for PDF files also in the subfolders."));
    parser->addOption(QCommandLineOption("password", "Password of the protected original files.", "password"));
    parser->addOption(QCommandLineOption("set-user-password", "Password to open the files for the action 'password' of the sequence.", "password"));
    parser->addOption(QCommandLineOption("set-owner-password", "Password to change the files for the action 'password' of the sequence.", "password"));
}

void PDFToolBatch::readOptions(QCommandLineParser* parser, const QStringList& positionalArguments, PDFToolBatchOptions& options)
{
    options.inputs = positionalArguments;
    options.sequenceFile = parser->value("sequence");
    options.outputDirectory = parser->value("output-dir");
    options.nextToOriginal = parser->isSet("next-to-original");
    options.suffixSet = parser->isSet("suffix");
    options.suffix = parser->value("suffix");
    options.existing = parser->value("existing");
    options.recursive = parser->isSet("recursive");
    options.password = parser->value("password");
    options.userPassword = parser->value("set-user-password");
    options.ownerPassword = parser->value("set-owner-password");

    if (!options.existing.isEmpty() && options.existing != QLatin1String("rename") && options.existing != QLatin1String("overwrite") && options.existing != QLatin1String("skip"))
    {
        options.invalidArgument = PDFToolTranslationContext::tr("Invalid value '%1' of the option '--existing'. Valid values are rename|overwrite|skip.").arg(options.existing);
    }
    else if (options.nextToOriginal && !options.outputDirectory.isEmpty())
    {
        options.invalidArgument = PDFToolTranslationContext::tr("The options '--output-dir' and '--next-to-original' cannot be used together.");
    }
}

QString PDFToolBatch::createSettings(const PDFToolBatchOptions& options, pdf::PDFBatchSettings& settings)
{
    if (options.sequenceFile.isEmpty())
    {
        return PDFToolTranslationContext::tr("The sequence of the actions '--sequence' is missing.");
    }

    QFile file(options.sequenceFile);
    if (!file.open(QIODevice::ReadOnly))
    {
        return PDFToolTranslationContext::tr("The sequence '%1' cannot be read. %2").arg(options.sequenceFile, file.errorString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject())
    {
        return PDFToolTranslationContext::tr("The file '%1' is not a sequence of actions. %2").arg(options.sequenceFile, parseError.errorString());
    }

    QString errorMessage;
    settings = pdf::PDFBatchSettings::fromJson(document.object(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        return errorMessage;
    }

    if (!options.outputDirectory.isEmpty())
    {
        settings.outputLocation = pdf::PDFBatchSettings::OutputLocation::Folder;
        settings.outputDirectory = options.outputDirectory;
    }
    else if (options.nextToOriginal)
    {
        settings.outputLocation = pdf::PDFBatchSettings::OutputLocation::NextToOriginal;
    }

    if (options.suffixSet)
    {
        settings.suffix = options.suffix;
    }

    if (options.existing == QLatin1String("rename"))
    {
        settings.existingFilePolicy = pdf::PDFBatchSettings::ExistingFilePolicy::Rename;
    }
    else if (options.existing == QLatin1String("overwrite"))
    {
        settings.existingFilePolicy = pdf::PDFBatchSettings::ExistingFilePolicy::Overwrite;
    }
    else if (options.existing == QLatin1String("skip"))
    {
        settings.existingFilePolicy = pdf::PDFBatchSettings::ExistingFilePolicy::Skip;
    }

    settings.password = options.password;
    for (pdf::PDFBatchAction& action : settings.actions)
    {
        if (action.type == pdf::PDFBatchAction::Type::SetPassword)
        {
            action.userPassword = options.userPassword;
            action.ownerPassword = options.ownerPassword;
        }
    }

    return QString();
}

QStringList PDFToolBatch::getInputFiles(const PDFToolBatchOptions& options)
{
    QStringList files;
    for (const QString& input : options.inputs)
    {
        const QFileInfo info(input);
        const QStringList inputFiles = info.isDir() ? pdf::PDFBatchProcessor::findPdfFiles(info.absoluteFilePath(), options.recursive) : QStringList({ QDir::cleanPath(info.absoluteFilePath()) });
        for (const QString& file : inputFiles)
        {
            if (!files.contains(file, Qt::CaseInsensitive))
            {
                files << file;
            }
        }
    }
    return files;
}

int PDFToolBatch::execute(const PDFToolOptions& options)
{
    const PDFToolBatchOptions& batch = options.batch;
    if (!batch.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(batch.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFBatchSettings settings;
    const QString settingsError = createSettings(batch, settings);
    if (!settingsError.isEmpty())
    {
        PDFConsole::writeError(settingsError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    const QStringList files = getInputFiles(batch);
    if (files.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No PDF file to process."), options.outputCodec);
        return ErrorNoDocumentSpecified;
    }

    pdf::PDFBatchProcessor processor(settings);
    const QStringList errors = processor.validate();
    if (!errors.isEmpty())
    {
        PDFConsole::writeError(errors.join(QChar('\n')), options.outputCodec);
        return ErrorInvalidArguments;
    }

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("batch", PDFToolTranslationContext::tr("Batch processing of %1 files").arg(files.size()));
    formatter.endl();

    formatter.beginTable("files", PDFToolTranslationContext::tr("Files"));
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("file", PDFToolTranslationContext::tr("File"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("status", PDFToolTranslationContext::tr("Status"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("pages", PDFToolTranslationContext::tr("Pages"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("message", PDFToolTranslationContext::tr("Message"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("outputs", PDFToolTranslationContext::tr("Outputs"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    int failedCount = 0;
    int reference = 1;
    for (const QString& file : files)
    {
        const pdf::PDFBatchProcessor::FileResult result = processor.processFile(file, pdf::PDFBatchProcessor::ProgressCallback(), nullptr);

        QStringList outputs;
        for (const QString& output : result.outputFiles)
        {
            outputs << QDir::toNativeSeparators(output);
        }

        formatter.beginTableRow("file", reference++);
        formatter.writeTableColumn("file", QDir::toNativeSeparators(file));
        formatter.writeTableColumn("status", pdf::PDFBatchProcessor::getStatusName(result.status));
        formatter.writeTableColumn("pages", QString::number(result.pageCount), Qt::AlignRight);
        formatter.writeTableColumn("message", QStringList(QStringList(result.message) + result.details).join(QStringLiteral(" ")));
        formatter.writeTableColumn("outputs", outputs.join(QStringLiteral(", ")));
        formatter.endTableRow();

        if (result.status == pdf::PDFBatchProcessor::FileResult::Status::Failed || result.status == pdf::PDFBatchProcessor::FileResult::Status::Cancelled)
        {
            ++failedCount;
            PDFConsole::writeError(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(file), result.message), options.outputCodec);
        }
    }

    formatter.endTable();
    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return failedCount > 0 ? ExitFailure : ExitSuccess;
}

}   // namespace pdftool
