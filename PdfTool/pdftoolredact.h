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

#ifndef PDFTOOLREDACT_H
#define PDFTOOLREDACT_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

/// Applies the redaction marks of the document and writes the redacted document.
/// Content can also be marked from the command line - words, phrases, regular
/// expressions, predefined patterns, areas and whole pages.
class PDFToolRedact : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    /// Adds the marking options of the command to the parser
    static void initializeCommandLineParser(QCommandLineParser* parser);

    /// Reads the marking options of the command. An invalid value is
    /// reported by PDFToolRedactOptions::invalidArgument.
    static void readOptions(QCommandLineParser* parser, PDFToolRedactOptions& options);

    /// Parses the area in the form "page:x,y,width,height" (page is one based,
    /// coordinates are in points of the page space, origin in the bottom left corner)
    static bool parseArea(const QString& text, pdf::PDFInteger* pageIndex, QRectF* rectangle);
};

}   // namespace pdftool

#endif // PDFTOOLREDACT_H
