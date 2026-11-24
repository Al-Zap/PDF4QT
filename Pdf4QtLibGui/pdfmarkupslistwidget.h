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

#ifndef PDFMARKUPSLISTWIDGET_H
#define PDFMARKUPSLISTWIDGET_H

#include "pdfviewerglobal.h"
#include "pdfmarkupslist.h"
#include "pdfdocument.h"

#include <QWidget>

class QMenu;
class QLabel;
class QComboBox;
class QLineEdit;
class QTableView;
class QToolButton;
class QStackedWidget;
class QStandardItemModel;
class QSortFilterProxyModel;

namespace pdf
{
class PDFDrawWidgetProxy;
}

namespace pdfviewer
{

/// Markups list - a table of all comments, markups, counts and measurements
/// of the document (one row for each markup, all sheets), with the review status
/// of each of them, filters, a takeoff summary and exports to CSV and PDF.
/// Rows are synchronized with the selection of the annotations on the pages.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFMarkupsListWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFMarkupsListWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent);
    virtual ~PDFMarkupsListWidget() override;

    /// Sets the document. The list is rebuilt, when the annotations have changed.
    void setDocument(const pdf::PDFModifiedDocument& document);

    /// Returns the markups of the document (all of them, not only the filtered ones)
    const pdf::PDFMarkupsListItems& getItems() const { return m_items; }

    /// Returns the markups passing the filters, in the displayed order
    pdf::PDFMarkupsListItems getFilteredItems() const;

    /// Returns the markups of the selected rows
    pdf::PDFMarkupsListItems getSelectedItems() const;

    /// Sets the status of the markups (one modification of the document)
    void setStatus(const std::vector<pdf::PDFObjectReference>& annotations, pdf::PDFMarkupStatus status);

    /// Sets the status of the selected markups
    void setSelectedStatus(pdf::PDFMarkupStatus status);

    /// Shows the takeoff summary instead of the list (or the list)
    void setSummaryVisible(bool visible);
    bool isSummaryVisible() const;

    /// Writes the filtered markups (or their summary) to a CSV file
    bool exportCsv(const QString& fileName, bool summary, QString* errorMessage) const;

    /// Writes the PDF report of the filtered markups
    bool exportPdf(const QString& fileName, QString* errorMessage) const;

    QTableView* getTableView() const { return m_tableView; }
    QTableView* getSummaryView() const { return m_summaryView; }
    QLineEdit* getSearchEdit() const { return m_searchEdit; }
    QComboBox* getKindFilter() const { return m_kindComboBox; }
    QComboBox* getStatusFilter() const { return m_statusComboBox; }
    QComboBox* getSheetFilter() const { return m_sheetComboBox; }

signals:
    void documentModified(pdf::PDFModifiedDocument document);

private:
    void rebuild();
    void updateFilterItems();
    void updateSummary();
    void updateCountLabel();
    void applyFilter();
    void onTableSelectionChanged();
    void onAnnotationSelectionChanged();
    void onTableDoubleClicked(const QModelIndex& index);
    void onTableContextMenuRequested(const QPoint& pos);
    void onExportCsvTriggered();
    void onExportSummaryCsvTriggered();
    void onExportPdfTriggered();
    void showItem(const pdf::PDFMarkupsListItem& item);
    QString getDefaultExportFileName(const QString& suffix) const;
    bool isAccepted(const pdf::PDFMarkupsListItem& item) const;
    std::vector<int> getSelectedItemIndices() const;
    QMenu* createStatusMenu(QWidget* parent);

    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFDocument* m_document = nullptr;
    pdf::PDFMarkupsListItems m_items;

    QLineEdit* m_searchEdit;
    QComboBox* m_sheetComboBox;
    QComboBox* m_kindComboBox;
    QComboBox* m_statusComboBox;
    QToolButton* m_statusButton;
    QToolButton* m_summaryButton;
    QToolButton* m_exportButton;
    QLabel* m_countLabel;
    QStackedWidget* m_stackedWidget;
    QTableView* m_tableView;
    QTableView* m_summaryView;
    QStandardItemModel* m_model;
    QSortFilterProxyModel* m_proxyModel;
    QStandardItemModel* m_summaryModel;
    bool m_selectionChangeInProgress = false;
};

}   // namespace pdfviewer

#endif // PDFMARKUPSLISTWIDGET_H
