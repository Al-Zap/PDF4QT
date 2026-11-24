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



#ifndef PDFTOOLBATCH_H
#define PDFTOOLBATCH_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

/// Batch processing: a sequence of actions (saved by the dialog Convert > Batch Process
/// of the editor as a JSON file) is applied to many PDF files. The original files are
/// never changed, see pdf::PDFBatchProcessor.
class PDFToolBatch : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    /// Adds the options of the command to the parser
    static void initializeCommandLineParser(QCommandLineParser* parser);

    /// Reads the options of the command. An invalid value is reported by
    /// PDFToolBatchOptions::invalidArgument.
    static void readOptions(QCommandLineParser* parser, const QStringList& positionalArguments, PDFToolBatchOptions& options);

    /// Creates the settings of the batch from the options (sequence file and
    /// the overrides of the command line). Returns an error message.
    static QString createSettings(const PDFToolBatchOptions& options, pdf::PDFBatchSettings& settings);

    /// Returns the PDF files of the inputs (files, and the PDF files of the folders)
    static QStringList getInputFiles(const PDFToolBatchOptions& options);
};

}   // namespace pdftool

#endif // PDFTOOLBATCH_H
