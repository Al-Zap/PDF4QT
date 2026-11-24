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


#ifndef PDFTOOLSTAMP_H
#define PDFTOOLSTAMP_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

/// Base class of the stamp commands (headers and footers, Bates numbering, watermarks
/// and removal of the stamps). The source document is never overwritten, the stamped
/// document is written into the target document.
class PDFToolStampApplication : public PDFToolAbstractApplication
{
public:
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    /// Adds the options of the command to the parser
    static void initializeCommandLineParser(QCommandLineParser* parser, const QString& command);

    /// Reads the options of the command. An invalid value is reported by
    /// PDFToolStampOptions::invalidArgument, never replaced by a default.
    static void readOptions(QCommandLineParser* parser, const QString& command, PDFToolStampOptions& options);

    /// Converts the escape sequences \\n (new line) and \\\\ (backslash) of the command line
    static QString unescapeText(const QString& text);

private:
    static void reportInvalidValue(PDFToolStampOptions& options, const QString& option, const QString& value, const QString& validValues);
    static void readTextStyle(QCommandLineParser* parser, PDFToolStampOptions& options, pdf::PDFStampTextStyle& style);
    static void readVariables(QCommandLineParser* parser, PDFToolStampOptions& options, pdf::PDFStampVariables& variables);
    static bool readMargins(const QString& text, QMarginsF* margins);
};

/// Adds headers and footers to the pages
class PDFToolHeaderFooter : public PDFToolStampApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
};

/// Adds Bates numbers to the pages
class PDFToolBates : public PDFToolStampApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
};

/// Adds a watermark to the pages
class PDFToolWatermark : public PDFToolStampApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
};

/// Removes headers, footers, Bates numbers and watermarks added by PDF4QT
class PDFToolRemoveStamps : public PDFToolStampApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
};

}   // namespace pdftool

#endif // PDFTOOLSTAMP_H
