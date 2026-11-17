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

#ifndef PDFDOCXEXPORTER_H
#define PDFDOCXEXPORTER_H

#include "pdfglobal.h"
#include "pdfutils.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFOperationControl;

/// Settings of the export of a document into the Word format (DOCX)
struct PDFDocxExportSettings
{
    /// Graphics of the page (images, lines, fills, form field frames), without
    /// the text, are rendered into a picture placed behind the text of the page,
    /// so the Word document looks like the original page. Pages, where most
    /// of the text is invisible (scanned pages with a recognized text layer),
    /// are exported as the text only, because the picture already contains the text.
    bool includeGraphics = true;

    /// Resolution of the background picture (dots per inch)
    int graphicsResolution = 150;

    /// Text and graphics of the form fields and of the annotations
    /// (filled spec sheets have their values in the form fields)
    bool includeAnnotations = true;

    /// Lines of a body text are joined into paragraphs, which reflow in Word
    /// when the text is edited. When turned off, each line is a paragraph.
    bool reflowParagraphs = true;

    /// Parts of the package are compressed (turned off only by the tests)
    bool compress = true;
};

/// Exports pages of a PDF document into the Word format (Office Open XML, DOCX).
/// The conversion is aimed at text documents (spec sheets, letters, forms):
///  - each PDF page is a section of the Word document with the same page size,
///  - text lines are paragraphs positioned at the same place as in the PDF,
///    columns of a line are aligned by tab stops, lines of a body text are
///    joined into a paragraph, which reflows when it is edited,
///  - font family, size, bold, italic and color of the text are kept,
///  - graphics are kept as a picture behind the text (optional).
/// The DOCX package is a ZIP archive of XML files. It is written directly
/// by Qt XML writer and zlib (both are already a part of the build), no other
/// library or office suite is needed.
class PDF4QTLIBCORESHARED_EXPORT PDFDocxExporter
{
public:
    /// Progress callback, arguments are the count of processed pages and the
    /// total count of pages. It can be called from the worker threads.
    using ProgressCallback = std::function<void(size_t, size_t)>;

    explicit PDFDocxExporter(PDFDocxExportSettings settings = PDFDocxExportSettings());

    void setOperationControl(const PDFOperationControl* operationControl) { m_operationControl = operationControl; }
    void setProgressCallback(ProgressCallback progressCallback) { m_progressCallback = std::move(progressCallback); }

    /// Exports the pages into DOCX data
    /// \param document Document
    /// \param pageIndices Indices of the exported pages (zero based)
    /// \param[out] data DOCX data
    PDFOperationResult exportDocument(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, QByteArray& data) const;

    /// Exports the pages into a DOCX file
    /// \param document Document
    /// \param pageIndices Indices of the exported pages (zero based)
    /// \param fileName Name of the DOCX file
    PDFOperationResult exportDocument(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const QString& fileName) const;

    /// Creates a ZIP archive (DOCX package) from the files. Files are stored
    /// in the given order (the first file of a DOCX package must be [Content_Types].xml).
    /// \param files Pairs of the name of the file in the archive and of its data
    /// \param compress Compress the files (deflate), or store them
    static QByteArray createZipArchive(const std::vector<std::pair<QString, QByteArray>>& files, bool compress);

private:
    PDFDocxExportSettings m_settings;
    const PDFOperationControl* m_operationControl = nullptr;
    ProgressCallback m_progressCallback;
};

}   // namespace pdf

#endif // PDFDOCXEXPORTER_H
