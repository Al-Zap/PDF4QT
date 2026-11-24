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

#ifndef PDFSHEETLINKSDIALOG_H
#define PDFSHEETLINKSDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfsheetlinks.h"

#include <QDialog>

class QLabel;
class QTableWidget;
class QDialogButtonBox;

namespace pdfviewer
{

/// Shows the sheet numbers found on the pages (they can be corrected, an empty number
/// excludes the sheet) and the number of the references, which will be linked.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFSheetLinksDialog : public QDialog
{
    Q_OBJECT

public:
    /// \param document Document
    /// \param storage Text layouts of all pages of the document
    /// \param sheets Sheet numbers found on the pages
    /// \param pageLabels Labels of the pages (shown in the table)
    /// \param existingLinks Number of the sheet links created before (they are replaced)
    explicit PDFSheetLinksDialog(const pdf::PDFDocument* document,
                                 const pdf::PDFTextLayoutStorage* storage,
                                 const pdf::PDFSheetLinks::Sheets& sheets,
                                 const QStringList& pageLabels,
                                 int existingLinks,
                                 QWidget* parent);

    /// Returns the sheet numbers as they are in the table
    pdf::PDFSheetLinks::Sheets getSheets() const;

    /// Returns the references to be linked (with the corrected sheet numbers)
    const pdf::PDFSheetLinks::Links& getLinks() const { return m_links; }

private:
    void updateLinks();

    const pdf::PDFDocument* m_document;
    const pdf::PDFTextLayoutStorage* m_storage;
    pdf::PDFSheetLinks::Sheets m_sheets;
    pdf::PDFSheetLinks::Links m_links;
    QTableWidget* m_table;
    QLabel* m_summaryLabel;
    QDialogButtonBox* m_buttonBox;
};

}   // namespace pdfviewer

#endif // PDFSHEETLINKSDIALOG_H
