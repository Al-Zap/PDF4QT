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

#include "pdfsheetlinksdialog.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QDialogButtonBox>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFSheetLinksDialog::PDFSheetLinksDialog(const pdf::PDFDocument* document,
                                         const pdf::PDFTextLayoutStorage* storage,
                                         const pdf::PDFSheetLinks::Sheets& sheets,
                                         const QStringList& pageLabels,
                                         int existingLinks,
                                         QWidget* parent) :
    QDialog(parent),
    m_document(document),
    m_storage(storage),
    m_sheets(sheets),
    m_table(nullptr),
    m_summaryLabel(nullptr),
    m_buttonBox(nullptr)
{
    setWindowTitle(tr("Sheet Links"));
    setObjectName("sheetLinksDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* introLabel = new QLabel(tr("References to other sheets (for example \"SEE SHEET E-102\" or the sheet index of the cover) "
                                       "become links to those sheets. Sheet numbers are taken from the page labels or from the title blocks. "
                                       "Correct a wrong number, or clear it to leave the sheet out."), this);
    introLabel->setWordWrap(true);
    layout->addWidget(introLabel);

    m_table = new QTableWidget(int(m_sheets.size()), 3, this);
    m_table->setObjectName("sheetTable");
    m_table->setHorizontalHeaderLabels({ tr("Page"), tr("Sheet Number"), tr("Found In") });
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    for (int row = 0; row < int(m_sheets.size()); ++row)
    {
        const pdf::PDFSheetLinks::Sheet& sheet = m_sheets[row];
        const QString pageText = (row < pageLabels.size() && !pageLabels[row].isEmpty()) ? QString("%1 (%2)").arg(row + 1).arg(pageLabels[row]) : QString::number(row + 1);

        QTableWidgetItem* pageItem = new QTableWidgetItem(pageText);
        pageItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 0, pageItem);

        m_table->setItem(row, 1, new QTableWidgetItem(sheet.number));

        QString source = tr("Not found");
        if (!sheet.number.isEmpty())
        {
            source = sheet.fromPageLabel ? tr("Page label") : tr("Title block");
        }
        QTableWidgetItem* sourceItem = new QTableWidgetItem(source);
        sourceItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 2, sourceItem);
    }
    layout->addWidget(m_table, 1);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("summaryLabel");
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    if (existingLinks > 0)
    {
        QLabel* replaceLabel = new QLabel(tr("The %n sheet link(s) created before are replaced.", nullptr, existingLinks), this);
        replaceLabel->setObjectName("replaceLabel");
        layout->addWidget(replaceLabel);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Create Links"));
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttonBox);

    connect(m_table, &QTableWidget::itemChanged, this, &PDFSheetLinksDialog::updateLinks);
    updateLinks();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(560, 480));
}

pdf::PDFSheetLinks::Sheets PDFSheetLinksDialog::getSheets() const
{
    pdf::PDFSheetLinks::Sheets sheets = m_sheets;
    for (int row = 0; row < int(sheets.size()); ++row)
    {
        const QString number = m_table->item(row, 1)->text().trimmed();
        if (number != sheets[row].number)
        {
            sheets[row].number = number;
            sheets[row].fromPageLabel = false;
            sheets[row].rectangle = QRectF();
        }
    }
    return sheets;
}

void PDFSheetLinksDialog::updateLinks()
{
    m_links = pdf::PDFSheetLinks::findLinks(m_document, *m_storage, getSheets());

    int sheetCount = 0;
    for (const pdf::PDFSheetLinks::Sheet& sheet : getSheets())
    {
        sheetCount += sheet.number.isEmpty() ? 0 : 1;
    }

    const bool canCreate = !m_links.empty();
    m_summaryLabel->setText(tr("%1 of %2 pages have a sheet number, %3 reference(s) to other sheets will be linked.").arg(sheetCount).arg(m_sheets.size()).arg(m_links.size()));
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canCreate);
}

}   // namespace pdfviewer
