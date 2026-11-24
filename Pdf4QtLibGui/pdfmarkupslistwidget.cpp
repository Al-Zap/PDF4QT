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

#include "pdfmarkupslistwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentbuilder.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetutils.h"
#include "pdfutils.h"

#include <QMenu>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QStandardPaths>

namespace pdfviewer
{

/// Proxy model of the markups list, which filters the rows by a predicate
class PDFMarkupsListFilterModel : public QSortFilterProxyModel
{
public:
    explicit PDFMarkupsListFilterModel(QObject* parent) :
        QSortFilterProxyModel(parent)
    {

    }

    void setPredicate(std::function<bool(int)> predicate)
    {
        m_predicate = std::move(predicate);
        invalidate();
    }

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        Q_UNUSED(sourceParent);
        return !m_predicate || m_predicate(sourceRow);
    }

private:
    std::function<bool(int)> m_predicate;
};

/// Helper functions of the markups list widget
class PDFMarkupsListWidgetHelper
{
public:
    PDFMarkupsListWidgetHelper() = delete;

    /// Role holding the index of the item
    static constexpr int ItemIndexRole = Qt::UserRole;

    /// Role holding the sort key
    static constexpr int SortRole = Qt::UserRole + 1;

    /// Creates an icon with the color of the markup
    static QIcon createColorIcon(const QColor& color, int size);

    /// Background color of the status cell
    static QColor getStatusColor(pdf::PDFMarkupStatus status);
};

QIcon PDFMarkupsListWidgetHelper::createColorIcon(const QColor& color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    if (color.isValid())
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color.darker(140), 1));
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(1.5, 1.5, size - 3, size - 3), 2, 2);
    }

    return QIcon(pixmap);
}

QColor PDFMarkupsListWidgetHelper::getStatusColor(pdf::PDFMarkupStatus status)
{
    switch (status)
    {
        case pdf::PDFMarkupStatus::Accepted:
            return QColor(46, 125, 50);
        case pdf::PDFMarkupStatus::Rejected:
            return QColor(198, 40, 40);
        case pdf::PDFMarkupStatus::Cancelled:
            return QColor(117, 117, 117);
        case pdf::PDFMarkupStatus::Done:
            return QColor(21, 101, 192);
        default:
            break;
    }

    return QColor();
}

PDFMarkupsListWidget::PDFMarkupsListWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent) :
    BaseClass(parent),
    m_proxy(proxy),
    m_searchEdit(new QLineEdit(this)),
    m_sheetComboBox(new QComboBox(this)),
    m_kindComboBox(new QComboBox(this)),
    m_statusComboBox(new QComboBox(this)),
    m_statusButton(new QToolButton(this)),
    m_summaryButton(new QToolButton(this)),
    m_exportButton(new QToolButton(this)),
    m_countLabel(new QLabel(this)),
    m_stackedWidget(new QStackedWidget(this)),
    m_tableView(new QTableView(this)),
    m_summaryView(new QTableView(this)),
    m_model(new QStandardItemModel(this)),
    m_proxyModel(new PDFMarkupsListFilterModel(this)),
    m_summaryModel(new QStandardItemModel(this))
{
    setObjectName("markupsListWidget");

    m_searchEdit->setObjectName("markupsSearchEdit");
    m_searchEdit->setPlaceholderText(tr("Search markups..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_sheetComboBox->setObjectName("markupsSheetFilter");
    m_sheetComboBox->setToolTip(tr("Show the markups of a single sheet"));
    m_kindComboBox->setObjectName("markupsKindFilter");
    m_kindComboBox->setToolTip(tr("Show the markups of a single type"));
    m_statusComboBox->setObjectName("markupsStatusFilter");
    m_statusComboBox->setToolTip(tr("Show the markups with the status"));

    m_statusButton->setObjectName("markupsStatusButton");
    m_statusButton->setText(tr("Set Status"));
    m_statusButton->setToolTip(tr("Set the review status of the selected markups"));
    m_statusButton->setPopupMode(QToolButton::InstantPopup);
    m_statusButton->setMenu(createStatusMenu(m_statusButton));

    m_summaryButton->setObjectName("markupsSummaryButton");
    m_summaryButton->setText(tr("Takeoff Summary"));
    m_summaryButton->setToolTip(tr("Show the counts, total lengths and areas of the listed markups grouped by subject"));
    m_summaryButton->setCheckable(true);

    m_exportButton->setObjectName("markupsExportButton");
    m_exportButton->setText(tr("Export"));
    m_exportButton->setToolTip(tr("Export the listed markups"));
    m_exportButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* exportMenu = new QMenu(m_exportButton);
    exportMenu->addAction(tr("Markups to CSV..."), this, &PDFMarkupsListWidget::onExportCsvTriggered)->setObjectName("actionMarkupsExportCsv");
    exportMenu->addAction(tr("Takeoff Summary to CSV..."), this, &PDFMarkupsListWidget::onExportSummaryCsvTriggered)->setObjectName("actionMarkupsExportSummaryCsv");
    exportMenu->addAction(tr("Summary Report to PDF..."), this, &PDFMarkupsListWidget::onExportPdfTriggered)->setObjectName("actionMarkupsExportPdf");
    m_exportButton->setMenu(exportMenu);

    m_countLabel->setObjectName("markupsCountLabel");

    QHBoxLayout* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->addWidget(m_searchEdit, 2);
    toolbarLayout->addWidget(m_sheetComboBox, 1);
    toolbarLayout->addWidget(m_kindComboBox, 1);
    toolbarLayout->addWidget(m_statusComboBox, 1);
    toolbarLayout->addWidget(m_statusButton);
    toolbarLayout->addWidget(m_summaryButton);
    toolbarLayout->addWidget(m_exportButton);
    toolbarLayout->addWidget(m_countLabel);

    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setSortRole(PDFMarkupsListWidgetHelper::SortRole);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_tableView->setObjectName("markupsTableView");
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setWordWrap(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->verticalHeader()->hide();
    m_tableView->verticalHeader()->setDefaultSectionSize(pdf::PDFWidgetUtils::scaleDPI_y(this, 22));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->sortByColumn(-1, Qt::AscendingOrder);

    m_summaryView->setObjectName("markupsSummaryView");
    m_summaryView->setModel(m_summaryModel);
    m_summaryView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summaryView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_summaryView->setAlternatingRowColors(true);
    m_summaryView->verticalHeader()->hide();

    m_stackedWidget->addWidget(m_tableView);
    m_stackedWidget->addWidget(m_summaryView);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(toolbarLayout);
    layout->addWidget(m_stackedWidget);

    QStringList headers;
    for (int i = 0; i < pdf::PDFMarkupsList::ColumnCount; ++i)
    {
        headers << pdf::PDFMarkupsList::getColumnTitle(pdf::PDFMarkupsList::Column(i));
    }
    m_model->setHorizontalHeaderLabels(headers);

    const int characterWidth = m_tableView->fontMetrics().averageCharWidth();
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnSheet, characterWidth * 18);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnKind, characterWidth * 14);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnSubject, characterWidth * 18);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnContents, characterWidth * 40);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnMeasurement, characterWidth * 22);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnStatus, characterWidth * 13);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnAuthor, characterWidth * 14);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnDate, characterWidth * 19);
    m_tableView->setColumnWidth(pdf::PDFMarkupsList::ColumnReplies, characterWidth * 8);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &PDFMarkupsListWidget::applyFilter);
    connect(m_sheetComboBox, &QComboBox::currentIndexChanged, this, &PDFMarkupsListWidget::applyFilter);
    connect(m_kindComboBox, &QComboBox::currentIndexChanged, this, &PDFMarkupsListWidget::applyFilter);
    connect(m_statusComboBox, &QComboBox::currentIndexChanged, this, &PDFMarkupsListWidget::applyFilter);
    connect(m_summaryButton, &QToolButton::toggled, this, &PDFMarkupsListWidget::setSummaryVisible);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFMarkupsListWidget::onTableSelectionChanged);
    connect(m_tableView, &QTableView::doubleClicked, this, &PDFMarkupsListWidget::onTableDoubleClicked);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &PDFMarkupsListWidget::onTableContextMenuRequested);

    static_cast<PDFMarkupsListFilterModel*>(m_proxyModel)->setPredicate([this](int row) { return row >= 0 && row < int(m_items.size()) && isAccepted(m_items[row]); });

    updateFilterItems();
    updateCountLabel();
}

PDFMarkupsListWidget::~PDFMarkupsListWidget()
{

}

QMenu* PDFMarkupsListWidget::createStatusMenu(QWidget* parent)
{
    QMenu* menu = new QMenu(parent);
    for (int i = 0; i < int(pdf::PDFMarkupStatus::LastStatus); ++i)
    {
        const pdf::PDFMarkupStatus status = pdf::PDFMarkupStatus(i);
        QAction* action = menu->addAction(PDFMarkupsListWidgetHelper::createColorIcon(PDFMarkupsListWidgetHelper::getStatusColor(status), 16), pdf::PDFMarkupsList::getStatusName(status));
        action->setObjectName(QString("actionMarkupStatus%1").arg(QString::fromLatin1(pdf::PDFMarkupsList::getStateName(status))));
        connect(action, &QAction::triggered, this, [this, status]() { setSelectedStatus(status); });
    }
    return menu;
}

void PDFMarkupsListWidget::setDocument(const pdf::PDFModifiedDocument& document)
{
    const bool isChanged = m_document != document.getDocument();
    m_document = document.getDocument();

    if (isChanged || document.hasReset() || document.hasFlag(pdf::PDFModifiedDocument::Annotation))
    {
        rebuild();
    }

    // The annotation manager is created later, than this widget, so the connection is made here
    if (pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr)
    {
        connect(annotationManager, &pdf::PDFWidgetAnnotationManager::selectionChanged, this, &PDFMarkupsListWidget::onAnnotationSelectionChanged, Qt::UniqueConnection);
    }
}

void PDFMarkupsListWidget::rebuild()
{
    pdf::PDFTemporaryValueChange<bool> guard(&m_selectionChangeInProgress, true);

    // Selection is kept over the rebuild (the status of a markup was changed etc.)
    std::vector<pdf::PDFObjectReference> selectedAnnotations;
    for (const pdf::PDFMarkupsListItem& item : getSelectedItems())
    {
        selectedAnnotations.push_back(item.annotation);
    }

    m_items = pdf::PDFMarkupsList::collect(m_document);
    m_model->removeRows(0, m_model->rowCount());

    const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(this, 12);
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const pdf::PDFMarkupsListItem& item = m_items[i];

        QList<QStandardItem*> row;
        for (int column = 0; column < pdf::PDFMarkupsList::ColumnCount; ++column)
        {
            const QString text = pdf::PDFMarkupsList::getCellText(item, pdf::PDFMarkupsList::Column(column));
            QStandardItem* cell = new QStandardItem(text);
            cell->setData(int(i), PDFMarkupsListWidgetHelper::ItemIndexRole);
            cell->setData(text, PDFMarkupsListWidgetHelper::SortRole);

            switch (column)
            {
                case pdf::PDFMarkupsList::ColumnSheet:
                    cell->setData(QVariant::fromValue(qint64(item.pageIndex) * 100000 + qint64(i)), PDFMarkupsListWidgetHelper::SortRole);
                    break;

                case pdf::PDFMarkupsList::ColumnKind:
                    cell->setIcon(PDFMarkupsListWidgetHelper::createColorIcon(item.color, iconSize));
                    break;

                case pdf::PDFMarkupsList::ColumnContents:
                {
                    QString toolTip = item.contents;
                    if (!item.replies.isEmpty())
                    {
                        toolTip += QStringLiteral("\n\n") + item.replies.join(QChar('\n'));
                    }
                    cell->setToolTip(toolTip.trimmed());
                    cell->setText(QString(text).replace(QChar('\n'), QChar(' ')).replace(QChar('\r'), QChar(' ')));
                    break;
                }

                case pdf::PDFMarkupsList::ColumnMeasurement:
                    cell->setData(item.hasArea() ? item.area : (item.hasLength() ? item.length : (item.hasAngle() ? item.angle : -1.0)), PDFMarkupsListWidgetHelper::SortRole);
                    break;

                case pdf::PDFMarkupsList::ColumnStatus:
                {
                    const QColor color = PDFMarkupsListWidgetHelper::getStatusColor(item.status);
                    if (color.isValid())
                    {
                        cell->setForeground(color);
                        QFont font = cell->font();
                        font.setBold(true);
                        cell->setFont(font);
                    }
                    if (!item.statusAuthor.isEmpty())
                    {
                        cell->setToolTip(tr("%1 by %2, %3").arg(text, item.statusAuthor, QLocale().toString(item.statusDate.toLocalTime(), QLocale::ShortFormat)));
                    }
                    cell->setData(int(item.status), PDFMarkupsListWidgetHelper::SortRole);
                    break;
                }

                case pdf::PDFMarkupsList::ColumnDate:
                    cell->setData(item.modified, PDFMarkupsListWidgetHelper::SortRole);
                    break;

                case pdf::PDFMarkupsList::ColumnReplies:
                    cell->setData(item.replyCount, PDFMarkupsListWidgetHelper::SortRole);
                    break;

                default:
                    break;
            }

            row << cell;
        }

        m_model->appendRow(row);
    }

    updateFilterItems();
    applyFilter();

    // Restore the selection
    QItemSelection selection;
    for (int row = 0; row < m_proxyModel->rowCount(); ++row)
    {
        const QModelIndex index = m_proxyModel->index(row, 0);
        const int itemIndex = index.data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
        if (itemIndex >= 0 && itemIndex < int(m_items.size()) &&
            std::find(selectedAnnotations.cbegin(), selectedAnnotations.cend(), m_items[itemIndex].annotation) != selectedAnnotations.cend())
        {
            selection.select(index, m_proxyModel->index(row, pdf::PDFMarkupsList::ColumnCount - 1));
        }
    }
    m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}

void PDFMarkupsListWidget::updateFilterItems()
{
    // Current choices are kept, if they are still available
    const QVariant sheet = m_sheetComboBox->currentData();
    const QVariant kind = m_kindComboBox->currentData();
    const QVariant status = m_statusComboBox->currentData();

    QSignalBlocker sheetBlocker(m_sheetComboBox);
    QSignalBlocker kindBlocker(m_kindComboBox);
    QSignalBlocker statusBlocker(m_statusComboBox);

    m_sheetComboBox->clear();
    m_sheetComboBox->addItem(tr("All Sheets"), QVariant());
    std::vector<pdf::PDFInteger> pages;
    for (const pdf::PDFMarkupsListItem& item : m_items)
    {
        if (std::find(pages.cbegin(), pages.cend(), item.pageIndex) == pages.cend())
        {
            pages.push_back(item.pageIndex);
            m_sheetComboBox->addItem(item.sheet, QVariant::fromValue(item.pageIndex));
        }
    }

    m_kindComboBox->clear();
    m_kindComboBox->addItem(tr("All Types"), QVariant());
    std::vector<pdf::PDFMarkupKind> kinds;
    for (const pdf::PDFMarkupsListItem& item : m_items)
    {
        if (std::find(kinds.cbegin(), kinds.cend(), item.kind) == kinds.cend())
        {
            kinds.push_back(item.kind);
        }
    }
    std::sort(kinds.begin(), kinds.end());
    for (pdf::PDFMarkupKind itemKind : kinds)
    {
        m_kindComboBox->addItem(pdf::PDFMarkupsList::getKindName(itemKind), int(itemKind));
    }

    m_statusComboBox->clear();
    m_statusComboBox->addItem(tr("Any Status"), QVariant());
    for (int i = 0; i < int(pdf::PDFMarkupStatus::LastStatus); ++i)
    {
        m_statusComboBox->addItem(pdf::PDFMarkupsList::getStatusName(pdf::PDFMarkupStatus(i)), i);
    }

    auto restore = [](QComboBox* comboBox, const QVariant& data)
    {
        const int index = data.isValid() ? comboBox->findData(data) : 0;
        comboBox->setCurrentIndex(qMax(index, 0));
    };
    restore(m_sheetComboBox, sheet);
    restore(m_kindComboBox, kind);
    restore(m_statusComboBox, status);
}

bool PDFMarkupsListWidget::isAccepted(const pdf::PDFMarkupsListItem& item) const
{
    const QVariant sheet = m_sheetComboBox->currentData();
    if (sheet.isValid() && sheet.value<pdf::PDFInteger>() != item.pageIndex)
    {
        return false;
    }

    const QVariant kind = m_kindComboBox->currentData();
    if (kind.isValid() && kind.toInt() != int(item.kind))
    {
        return false;
    }

    const QVariant status = m_statusComboBox->currentData();
    if (status.isValid() && status.toInt() != int(item.status))
    {
        return false;
    }

    const QString text = m_searchEdit->text().trimmed();
    if (!text.isEmpty())
    {
        bool found = false;
        for (int column = 0; column < pdf::PDFMarkupsList::ColumnCount && !found; ++column)
        {
            found = pdf::PDFMarkupsList::getCellText(item, pdf::PDFMarkupsList::Column(column)).contains(text, Qt::CaseInsensitive);
        }
        found = found || item.replies.join(QChar('\n')).contains(text, Qt::CaseInsensitive);

        if (!found)
        {
            return false;
        }
    }

    return true;
}

void PDFMarkupsListWidget::applyFilter()
{
    static_cast<PDFMarkupsListFilterModel*>(m_proxyModel)->setPredicate([this](int row) { return row >= 0 && row < int(m_items.size()) && isAccepted(m_items[row]); });
    updateSummary();
    updateCountLabel();
}

pdf::PDFMarkupsListItems PDFMarkupsListWidget::getFilteredItems() const
{
    pdf::PDFMarkupsListItems items;
    for (int row = 0; row < m_proxyModel->rowCount(); ++row)
    {
        const int itemIndex = m_proxyModel->index(row, 0).data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
        if (itemIndex >= 0 && itemIndex < int(m_items.size()))
        {
            items.push_back(m_items[itemIndex]);
        }
    }
    return items;
}

std::vector<int> PDFMarkupsListWidget::getSelectedItemIndices() const
{
    std::vector<int> indices;
    for (const QModelIndex& index : m_tableView->selectionModel()->selectedRows())
    {
        const int itemIndex = index.data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
        if (itemIndex >= 0 && itemIndex < int(m_items.size()))
        {
            indices.push_back(itemIndex);
        }
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

pdf::PDFMarkupsListItems PDFMarkupsListWidget::getSelectedItems() const
{
    pdf::PDFMarkupsListItems items;
    for (int index : getSelectedItemIndices())
    {
        items.push_back(m_items[index]);
    }
    return items;
}

void PDFMarkupsListWidget::updateSummary()
{
    const pdf::PDFMarkupsSummaryItems summary = pdf::PDFMarkupsList::summarize(getFilteredItems());
    const std::vector<QStringList> table = pdf::PDFMarkupsList::createSummaryTable(summary);

    m_summaryModel->clear();
    if (table.empty())
    {
        return;
    }

    m_summaryModel->setHorizontalHeaderLabels(table.front());
    for (size_t i = 1; i < table.size(); ++i)
    {
        QList<QStandardItem*> row;
        for (const QString& text : table[i])
        {
            row << new QStandardItem(text);
        }
        m_summaryModel->appendRow(row);
    }

    m_summaryView->resizeColumnsToContents();
}

void PDFMarkupsListWidget::updateCountLabel()
{
    const int shown = m_proxyModel->rowCount();
    const int total = int(m_items.size());
    m_countLabel->setText(shown == total ? tr("%1 markups").arg(total) : tr("%1 of %2 markups").arg(shown).arg(total));
}

void PDFMarkupsListWidget::setStatus(const std::vector<pdf::PDFObjectReference>& annotations, pdf::PDFMarkupStatus status)
{
    if (!m_document || annotations.empty())
    {
        return;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    const QString author = pdf::PDFAuthorSettings::getAuthorName();
    bool isModified = false;

    for (const pdf::PDFObjectReference& annotation : annotations)
    {
        // Status is not changed, if it is the same
        auto it = std::find_if(m_items.cbegin(), m_items.cend(), [annotation](const pdf::PDFMarkupsListItem& item) { return item.annotation == annotation; });
        if (it != m_items.cend() && it->status == status)
        {
            continue;
        }

        isModified = pdf::PDFMarkupsList::setStatus(modifier.getBuilder(), annotation, status, author).isValid() || isModified;
    }

    if (!isModified)
    {
        return;
    }

    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        Q_EMIT documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void PDFMarkupsListWidget::setSelectedStatus(pdf::PDFMarkupStatus status)
{
    std::vector<pdf::PDFObjectReference> annotations;
    for (const pdf::PDFMarkupsListItem& item : getSelectedItems())
    {
        annotations.push_back(item.annotation);
    }
    setStatus(annotations, status);
}

void PDFMarkupsListWidget::setSummaryVisible(bool visible)
{
    QSignalBlocker blocker(m_summaryButton);
    m_summaryButton->setChecked(visible);
    m_stackedWidget->setCurrentWidget(visible ? static_cast<QWidget*>(m_summaryView) : static_cast<QWidget*>(m_tableView));
}

bool PDFMarkupsListWidget::isSummaryVisible() const
{
    return m_stackedWidget->currentWidget() == m_summaryView;
}

bool PDFMarkupsListWidget::exportCsv(const QString& fileName, bool summary, QString* errorMessage) const
{
    const pdf::PDFMarkupsListItems items = getFilteredItems();
    const std::vector<QStringList> table = summary ? pdf::PDFMarkupsList::createSummaryTable(pdf::PDFMarkupsList::summarize(items))
                                                   : pdf::PDFMarkupsList::createTable(items);

    // Spreadsheets in the locales with the decimal comma expect a semicolon
    const QChar separator = (QLocale().decimalPoint() == QStringLiteral(",")) ? QChar(';') : QChar(',');

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = tr("Can't write the file '%1': %2").arg(QDir::toNativeSeparators(fileName), file.errorString());
        }
        return false;
    }

    // UTF-8 with the byte order mark, so Excel reads the characters correctly
    file.write("\xEF\xBB\xBF");
    file.write(pdf::PDFMarkupsList::createCsv(table, separator).toUtf8());
    file.close();
    return true;
}

bool PDFMarkupsListWidget::exportPdf(const QString& fileName, QString* errorMessage) const
{
    QString title = m_document ? m_document->getInfo()->title : QString();
    if (title.trimmed().isEmpty())
    {
        title = QFileInfo(fileName).completeBaseName();
    }
    return pdf::PDFMarkupsList::writePdfReport(fileName, title, getFilteredItems(), errorMessage);
}

QString PDFMarkupsListWidget::getDefaultExportFileName(const QString& suffix) const
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(directory).filePath(tr("Markups") + suffix);
}

void PDFMarkupsListWidget::onExportCsvTriggered()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Markups"), getDefaultExportFileName(QStringLiteral(".csv")), tr("CSV files (*.csv)"));
    QString errorMessage;
    if (!fileName.isEmpty() && !exportCsv(fileName, false, &errorMessage))
    {
        QMessageBox::critical(this, tr("Export Markups"), errorMessage);
    }
}

void PDFMarkupsListWidget::onExportSummaryCsvTriggered()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Takeoff Summary"), getDefaultExportFileName(tr(" Summary") + QStringLiteral(".csv")), tr("CSV files (*.csv)"));
    QString errorMessage;
    if (!fileName.isEmpty() && !exportCsv(fileName, true, &errorMessage))
    {
        QMessageBox::critical(this, tr("Export Takeoff Summary"), errorMessage);
    }
}

void PDFMarkupsListWidget::onExportPdfTriggered()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Summary Report"), getDefaultExportFileName(tr(" Summary") + QStringLiteral(".pdf")), tr("PDF files (*.pdf)"));
    QString errorMessage;
    if (!fileName.isEmpty() && !exportPdf(fileName, &errorMessage))
    {
        QMessageBox::critical(this, tr("Export Summary Report"), errorMessage);
    }
}

void PDFMarkupsListWidget::showItem(const pdf::PDFMarkupsListItem& item)
{
    if (!m_proxy || item.pageIndex < 0)
    {
        return;
    }

    if (item.rectangle.isValid())
    {
        m_proxy->goToPageAndEnsureVisible(item.pageIndex, item.rectangle);
    }
    else
    {
        m_proxy->goToPage(item.pageIndex);
    }
}

void PDFMarkupsListWidget::onTableSelectionChanged()
{
    pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr;
    if (m_selectionChangeInProgress || !annotationManager)
    {
        return;
    }

    std::vector<pdf::PDFObjectReference> annotations;
    for (const pdf::PDFMarkupsListItem& item : getSelectedItems())
    {
        annotations.push_back(item.annotation);
    }

    pdf::PDFTemporaryValueChange<bool> guard(&m_selectionChangeInProgress, true);
    annotationManager->setSelectedAnnotations(annotations);
}

void PDFMarkupsListWidget::onAnnotationSelectionChanged()
{
    pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr;
    if (m_selectionChangeInProgress || !annotationManager)
    {
        return;
    }

    pdf::PDFTemporaryValueChange<bool> guard(&m_selectionChangeInProgress, true);
    const std::vector<pdf::PDFObjectReference> selectedAnnotations = annotationManager->getSelectedAnnotations();

    QItemSelection selection;
    QModelIndex firstIndex;
    for (int row = 0; row < m_proxyModel->rowCount(); ++row)
    {
        const QModelIndex index = m_proxyModel->index(row, 0);
        const int itemIndex = index.data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
        if (itemIndex >= 0 && itemIndex < int(m_items.size()) &&
            std::find(selectedAnnotations.cbegin(), selectedAnnotations.cend(), m_items[itemIndex].annotation) != selectedAnnotations.cend())
        {
            selection.select(index, m_proxyModel->index(row, pdf::PDFMarkupsList::ColumnCount - 1));
            if (!firstIndex.isValid())
            {
                firstIndex = index;
            }
        }
    }

    m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (firstIndex.isValid())
    {
        m_tableView->scrollTo(firstIndex);
    }
}

void PDFMarkupsListWidget::onTableDoubleClicked(const QModelIndex& index)
{
    const int itemIndex = index.data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
    if (index.isValid() && itemIndex >= 0 && itemIndex < int(m_items.size()))
    {
        showItem(m_items[itemIndex]);
    }
}

void PDFMarkupsListWidget::onTableContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid())
    {
        return;
    }

    const int itemIndex = index.data(PDFMarkupsListWidgetHelper::ItemIndexRole).toInt();
    if (itemIndex < 0 || itemIndex >= int(m_items.size()))
    {
        return;
    }

    QMenu menu(this);
    menu.addAction(tr("Go to Markup"), this, [this, itemIndex]() { showItem(m_items[itemIndex]); });
    QMenu* statusMenu = createStatusMenu(&menu);
    statusMenu->setTitle(tr("Set Status"));
    menu.addMenu(statusMenu);
    menu.addSeparator();
    menu.addAction(tr("Show Only This Type"), this, [this, itemIndex]()
    {
        m_kindComboBox->setCurrentIndex(qMax(0, m_kindComboBox->findData(int(m_items[itemIndex].kind))));
    });
    menu.addAction(tr("Show Only This Sheet"), this, [this, itemIndex]()
    {
        m_sheetComboBox->setCurrentIndex(qMax(0, m_sheetComboBox->findData(QVariant::fromValue(m_items[itemIndex].pageIndex))));
    });
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

}   // namespace pdfviewer
