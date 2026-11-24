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


#include "pdftoolchestwidget.h"
#include "pdfcustomstampdialog.h"
#include "pdfplacemarkuptool.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfsecurityhandler.h"
#include "pdfwidgetutils.h"
#include "pdfcatalog.h"
#include "pdfutils.h"

#include <QMenu>
#include <QLabel>
#include <QAction>
#include <QPainter>
#include <QBoxLayout>
#include <QFileDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QToolButton>
#include <QInputDialog>

namespace pdfviewer
{

/// Size of the thumbnails of the items of the tool chest
static constexpr int THUMBNAIL_WIDTH = 72;
static constexpr int THUMBNAIL_HEIGHT = 48;

PDFToolChestWidget::PDFToolChestWidget(pdf::PDFDrawWidgetProxy* proxy,
                                       pdf::PDFToolManager* toolManager,
                                       QWidget* parent,
                                       QString toolChestFileName,
                                       QString stampsFileName) :
    BaseClass(parent),
    m_proxy(proxy),
    m_toolManager(toolManager),
    m_placeTool(nullptr),
    m_toolChestFileName(std::move(toolChestFileName)),
    m_stampsFileName(std::move(stampsFileName))
{
    m_placeTool = new pdf::PDFPlaceMarkupTool(proxy, toolManager, nullptr, this);
    if (m_toolManager)
    {
        m_toolManager->addTool(m_placeTool);
    }

    const QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT));

    QLabel* stampsLabel = new QLabel(tr("Custom Stamps"), this);
    QFont boldFont = stampsLabel->font();
    boldFont.setBold(true);
    stampsLabel->setFont(boldFont);

    m_stampsButton = new QToolButton(this);
    m_stampsButton->setObjectName("customStampsButton");
    m_stampsButton->setText(tr("Edit..."));
    m_stampsButton->setToolTip(tr("Create and edit the custom stamps (title, text with the date and the author, company, logo)"));
    m_stampsButton->setAutoRaise(true);

    m_stampsListWidget = new QListWidget(this);
    m_stampsListWidget->setObjectName("stampsListWidget");
    m_stampsListWidget->setIconSize(iconSize);

    QLabel* itemsLabel = new QLabel(tr("Saved Markups"), this);
    itemsLabel->setFont(boldFont);

    m_addButton = new QToolButton(this);
    m_addButton->setObjectName("addToToolChestButton");
    m_addButton->setText(tr("Add Selected"));
    m_addButton->setToolTip(tr("Save the selected markups (with their style, text and appearance) to the tool chest"));
    m_addButton->setAutoRaise(true);
    m_renameButton = new QToolButton(this);
    m_renameButton->setText(tr("Rename"));
    m_renameButton->setAutoRaise(true);
    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Delete"));
    m_removeButton->setAutoRaise(true);
    m_moreButton = new QToolButton(this);
    m_moreButton->setText(tr("More"));
    m_moreButton->setAutoRaise(true);
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* moreMenu = new QMenu(m_moreButton);
    moreMenu->addAction(tr("Move Up"), this, [this]() { moveItem(m_itemsListWidget->currentRow(), -1); });
    moreMenu->addAction(tr("Move Down"), this, [this]() { moveItem(m_itemsListWidget->currentRow(), +1); });
    moreMenu->addSeparator();
    moreMenu->addAction(tr("Import Tool Chest..."), this, &PDFToolChestWidget::onImportClicked);
    moreMenu->addAction(tr("Export Tool Chest..."), this, &PDFToolChestWidget::onExportClicked);
    m_moreButton->setMenu(moreMenu);

    m_itemsListWidget = new QListWidget(this);
    m_itemsListWidget->setObjectName("toolChestListWidget");
    m_itemsListWidget->setIconSize(iconSize);
    m_itemsListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_itemsListWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    m_hintLabel = new QLabel(tr("Select markups on the page and click Add Selected to keep them for later. Click an item, then click on the page to place it (hold Shift to place more copies)."), this);
    m_hintLabel->setWordWrap(true);
    QPalette hintPalette = m_hintLabel->palette();
    hintPalette.setColor(QPalette::WindowText, hintPalette.color(QPalette::Disabled, QPalette::WindowText));
    m_hintLabel->setPalette(hintPalette);

    QHBoxLayout* stampsHeaderLayout = new QHBoxLayout();
    stampsHeaderLayout->addWidget(stampsLabel);
    stampsHeaderLayout->addStretch();
    stampsHeaderLayout->addWidget(m_stampsButton);

    QHBoxLayout* itemsHeaderLayout = new QHBoxLayout();
    itemsHeaderLayout->addWidget(itemsLabel);
    itemsHeaderLayout->addStretch();

    QHBoxLayout* itemsButtonsLayout = new QHBoxLayout();
    itemsButtonsLayout->addWidget(m_addButton);
    itemsButtonsLayout->addWidget(m_renameButton);
    itemsButtonsLayout->addWidget(m_removeButton);
    itemsButtonsLayout->addStretch();
    itemsButtonsLayout->addWidget(m_moreButton);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(stampsHeaderLayout);
    layout->addWidget(m_stampsListWidget, 2);
    layout->addLayout(itemsHeaderLayout);
    layout->addLayout(itemsButtonsLayout);
    layout->addWidget(m_itemsListWidget, 3);
    layout->addWidget(m_hintLabel);

    connect(m_stampsButton, &QToolButton::clicked, this, &PDFToolChestWidget::editStamps);
    connect(m_addButton, &QToolButton::clicked, this, &PDFToolChestWidget::addSelectedMarkups);
    connect(m_renameButton, &QToolButton::clicked, this, &PDFToolChestWidget::onRenameClicked);
    connect(m_removeButton, &QToolButton::clicked, this, &PDFToolChestWidget::onRemoveClicked);
    connect(m_stampsListWidget, &QListWidget::itemClicked, this, &PDFToolChestWidget::onStampClicked);
    connect(m_itemsListWidget, &QListWidget::itemClicked, this, &PDFToolChestWidget::onItemClicked);
    connect(m_itemsListWidget, &QListWidget::currentRowChanged, this, &PDFToolChestWidget::updateButtons);
    connect(m_itemsListWidget, &QListWidget::customContextMenuRequested, this, &PDFToolChestWidget::onItemsContextMenuRequested);

    if (pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr)
    {
        connect(annotationManager, &pdf::PDFWidgetAnnotationManager::selectionChanged, this, &PDFToolChestWidget::updateButtons);
    }

    m_stamps = pdf::PDFCustomStampLibrary::load(m_stampsFileName);
    m_items = pdf::PDFToolChest::load(m_toolChestFileName);
    updateStampsList();
    updateItemsList();
}

PDFToolChestWidget::~PDFToolChestWidget() = default;

void PDFToolChestWidget::setDocument(const pdf::PDFModifiedDocument& document)
{
    m_document = document.getDocument();
    updateButtons();
}

void PDFToolChestWidget::setStamps(pdf::PDFCustomStamps stamps)
{
    m_stamps = std::move(stamps);
    pdf::PDFCustomStampLibrary::save(m_stamps, m_stampsFileName);
    updateStampsList();
    Q_EMIT stampsChanged();
}

bool PDFToolChestWidget::addMarkups(const std::vector<pdf::PDFObjectReference>& annotations, const QString& name)
{
    pdf::PDFToolChestItem item = pdf::PDFToolChest::createItem(m_document, annotations, name);
    if (!item.isValid())
    {
        return false;
    }

    m_items.push_back(std::move(item));
    saveItems();
    updateItemsList();
    m_itemsListWidget->setCurrentRow(int(m_items.size()) - 1);
    return true;
}

bool PDFToolChestWidget::addSelectedMarkups()
{
    pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr;
    const std::vector<pdf::PDFObjectReference> annotations = annotationManager ? annotationManager->getSelectedAnnotations() : std::vector<pdf::PDFObjectReference>();
    if (annotations.empty())
    {
        Q_EMIT m_placeTool->messageDisplayRequest(tr("Select the markups to save in the tool chest first."), 5000);
        return false;
    }

    if (!addMarkups(annotations))
    {
        Q_EMIT m_placeTool->messageDisplayRequest(tr("The selected markups can't be saved in the tool chest."), 5000);
        return false;
    }

    Q_EMIT m_placeTool->messageDisplayRequest(tr("'%1' has been saved in the tool chest.").arg(m_items.back().name), 5000);
    return true;
}

void PDFToolChestWidget::renameItem(int index, const QString& name)
{
    if (index < 0 || size_t(index) >= m_items.size() || name.trimmed().isEmpty())
    {
        return;
    }

    m_items[index].name = name.trimmed();
    saveItems();
    updateItemsList();
    m_itemsListWidget->setCurrentRow(index);
}

void PDFToolChestWidget::removeItem(int index)
{
    if (index < 0 || size_t(index) >= m_items.size())
    {
        return;
    }

    m_items.erase(std::next(m_items.begin(), index));
    saveItems();
    updateItemsList();
    m_itemsListWidget->setCurrentRow(qMin(index, int(m_items.size()) - 1));
}

void PDFToolChestWidget::moveItem(int index, int direction)
{
    const int newIndex = index + direction;
    if (index < 0 || newIndex < 0 || size_t(index) >= m_items.size() || size_t(newIndex) >= m_items.size())
    {
        return;
    }

    std::swap(m_items[index], m_items[newIndex]);
    saveItems();
    updateItemsList();
    m_itemsListWidget->setCurrentRow(newIndex);
}

int PDFToolChestWidget::importItems(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
    {
        return -1;
    }

    bool ok = false;
    pdf::PDFToolChestItems items = pdf::PDFToolChest::fromJson(file.readAll(), &ok);
    if (!ok)
    {
        return -1;
    }

    int count = 0;
    for (pdf::PDFToolChestItem& item : items)
    {
        // Items, which are already in the tool chest, are not added again
        auto it = std::find_if(m_items.begin(), m_items.end(), [&item](const pdf::PDFToolChestItem& existingItem) { return existingItem.id == item.id; });
        if (it == m_items.end())
        {
            m_items.push_back(std::move(item));
            ++count;
        }
    }

    saveItems();
    updateItemsList();
    return count;
}

bool PDFToolChestWidget::exportItems(const QString& fileName) const
{
    return pdf::PDFToolChest::save(m_items, fileName);
}

QImage PDFToolChestWidget::renderStampPreview(const pdf::PDFCustomStamp& stamp, pdf::PDFReal scale) const
{
    return pdf::PDFCustomStampLibrary::renderPreview(stamp, pdf::PDFAuthorSettings::getAuthorName(), QDateTime::currentDateTime(), scale);
}

bool PDFToolChestWidget::activateStamp(int index)
{
    if (index < 0 || size_t(index) >= m_stamps.size() || !canModifyDocument() || !m_toolManager)
    {
        return false;
    }

    const pdf::PDFCustomStamp stamp = m_stamps[index];
    const QSizeF size = pdf::PDFCustomStampLibrary::getSize(stamp, pdf::PDFAuthorSettings::getAuthorName(), QDateTime::currentDateTime());
    auto creator = [this, stamp](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page, pdf::PDFInteger pageIndex, QPointF center) -> std::vector<pdf::PDFObjectReference>
    {
        int rotation = 0;
        if (const pdf::PDFPage* pdfPage = m_document ? m_document->getCatalog()->getPage(pageIndex) : nullptr)
        {
            rotation = int(pdfPage->getPageRotation()) * 90;
        }

        const pdf::PDFObjectReference annotation = pdf::PDFCustomStampLibrary::createAnnotation(builder, page, center, stamp, pdf::PDFAuthorSettings::getAuthorName(), QDateTime::currentDateTime(), rotation);
        return annotation.isValid() ? std::vector<pdf::PDFObjectReference>{ annotation } : std::vector<pdf::PDFObjectReference>();
    };

    m_placeTool->setMarkup(stamp.name, creator, renderStampPreview(stamp, 2.0), size);
    m_toolManager->setActiveTool(nullptr);
    m_toolManager->setActiveTool(m_placeTool);
    return true;
}

bool PDFToolChestWidget::activateItem(int index)
{
    if (index < 0 || size_t(index) >= m_items.size() || !canModifyDocument() || !m_toolManager)
    {
        return false;
    }

    const pdf::PDFToolChestItem item = m_items[index];
    const QRectF boundingRectangle = pdf::PDFToolChest::getBoundingRectangle(item);
    if (!boundingRectangle.isValid())
    {
        return false;
    }

    auto creator = [item](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page, pdf::PDFInteger pageIndex, QPointF center) -> std::vector<pdf::PDFObjectReference>
    {
        Q_UNUSED(pageIndex);
        return pdf::PDFToolChest::placeItem(builder, page, center, item, pdf::PDFAuthorSettings::getAuthorName());
    };

    // Preview at 2 pixels per point, but not larger than 1500 pixels
    const pdf::PDFReal maximalSize = qMax(boundingRectangle.width(), boundingRectangle.height());
    const pdf::PDFReal scale = qMin(2.0, 1500.0 / qMax(maximalSize, 1.0));
    const QSize previewSize = (boundingRectangle.size() * scale).toSize().expandedTo(QSize(1, 1));

    // The preview has a margin around the annotations (see PDFToolChest::renderItem)
    const pdf::PDFReal margin = qMax(2.0, maximalSize * 0.04);
    const QSizeF size = boundingRectangle.size() + QSizeF(2.0 * margin, 2.0 * margin);

    m_placeTool->setMarkup(item.name, creator, pdf::PDFToolChest::renderItem(item, previewSize), size);
    m_toolManager->setActiveTool(nullptr);
    m_toolManager->setActiveTool(m_placeTool);
    return true;
}

bool PDFToolChestWidget::editStamps()
{
    PDFCustomStampDialog dialog(m_stamps, this);
    dialog.setCurrentIndex(m_stampsListWidget->currentRow() >= 0 ? m_stampsListWidget->currentRow() : (m_stamps.empty() ? -1 : 0));
    if (dialog.exec() != QDialog::Accepted || dialog.getStamps() == m_stamps)
    {
        return false;
    }

    setStamps(dialog.getStamps());
    return true;
}

bool PDFToolChestWidget::canModifyDocument() const
{
    return m_document && m_document->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems);
}

QList<QAction*> PDFToolChestWidget::createStampActions(QObject* parent)
{
    QList<QAction*> actions;
    const QSize iconSize(64, 32);
    for (int i = 0; i < int(m_stamps.size()); ++i)
    {
        const pdf::PDFCustomStamp& stamp = m_stamps[i];
        const QImage image = renderStampPreview(stamp, 1.0).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QAction* action = new QAction(QIcon(QPixmap::fromImage(image)), stamp.name, parent);
        action->setObjectName(QStringLiteral("actionCustomStamp_%1").arg(stamp.id));
        action->setEnabled(canModifyDocument());
        connect(action, &QAction::triggered, this, [this, i]() { activateStamp(i); });
        actions.push_back(action);
    }
    return actions;
}

void PDFToolChestWidget::updateStampsList()
{
    const QSize iconSize = m_stampsListWidget->iconSize() * devicePixelRatioF();

    m_stampsListWidget->clear();
    for (const pdf::PDFCustomStamp& stamp : m_stamps)
    {
        const QImage image = renderStampPreview(stamp, 2.0).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap pixmap(iconSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.drawImage((iconSize.width() - image.width()) / 2, (iconSize.height() - image.height()) / 2, image);
        painter.end();
        pixmap.setDevicePixelRatio(devicePixelRatioF());

        QListWidgetItem* item = new QListWidgetItem(QIcon(pixmap), stamp.name);
        item->setToolTip(tr("Click, then click on the page to place the stamp '%1'").arg(stamp.name));
        m_stampsListWidget->addItem(item);
    }

    updateButtons();
}

void PDFToolChestWidget::updateItemsList()
{
    const QSize iconSize = m_itemsListWidget->iconSize() * devicePixelRatioF();
    const int row = m_itemsListWidget->currentRow();

    QSignalBlocker blocker(m_itemsListWidget);
    m_itemsListWidget->clear();
    for (const pdf::PDFToolChestItem& toolChestItem : m_items)
    {
        const QImage image = pdf::PDFToolChest::renderItem(toolChestItem, iconSize);
        QPixmap pixmap(iconSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.drawImage((iconSize.width() - image.width()) / 2, (iconSize.height() - image.height()) / 2, image);
        painter.end();
        pixmap.setDevicePixelRatio(devicePixelRatioF());

        QListWidgetItem* item = new QListWidgetItem(QIcon(pixmap), toolChestItem.name);
        item->setToolTip(tr("Click, then click on the page to place '%1'").arg(toolChestItem.name));
        m_itemsListWidget->addItem(item);
    }
    m_itemsListWidget->setCurrentRow(qMin(row, m_itemsListWidget->count() - 1));

    updateButtons();
}

void PDFToolChestWidget::updateButtons()
{
    pdf::PDFWidgetAnnotationManager* annotationManager = m_proxy ? m_proxy->getAnnotationManager() : nullptr;
    const bool hasSelection = annotationManager && !annotationManager->getSelectedAnnotations().empty();
    const bool hasCurrentItem = m_itemsListWidget->currentRow() >= 0;

    m_addButton->setEnabled(m_document && hasSelection);
    m_renameButton->setEnabled(hasCurrentItem);
    m_removeButton->setEnabled(hasCurrentItem);
    m_hintLabel->setVisible(m_items.empty());
}

void PDFToolChestWidget::saveItems()
{
    if (!pdf::PDFToolChest::save(m_items, m_toolChestFileName))
    {
        Q_EMIT m_placeTool->messageDisplayRequest(tr("The tool chest can't be saved."), 5000);
    }
}

void PDFToolChestWidget::onStampClicked(QListWidgetItem* item)
{
    if (!activateStamp(m_stampsListWidget->row(item)) && !canModifyDocument())
    {
        Q_EMIT m_placeTool->messageDisplayRequest(tr("Open a document, into which markups can be added, to place the stamp."), 5000);
    }
}

void PDFToolChestWidget::onItemClicked(QListWidgetItem* item)
{
    if (!activateItem(m_itemsListWidget->row(item)) && !canModifyDocument())
    {
        Q_EMIT m_placeTool->messageDisplayRequest(tr("Open a document, into which markups can be added, to place the markup."), 5000);
    }
}

void PDFToolChestWidget::onItemsContextMenuRequested(const QPoint& pos)
{
    const int row = m_itemsListWidget->row(m_itemsListWidget->itemAt(pos));

    QMenu menu(this);
    if (row >= 0)
    {
        m_itemsListWidget->setCurrentRow(row);
        menu.addAction(tr("Place"), this, [this, row]() { activateItem(row); })->setEnabled(canModifyDocument());
        menu.addAction(tr("Rename..."), this, &PDFToolChestWidget::onRenameClicked);
        menu.addAction(tr("Move Up"), this, [this, row]() { moveItem(row, -1); })->setEnabled(row > 0);
        menu.addAction(tr("Move Down"), this, [this, row]() { moveItem(row, +1); })->setEnabled(row + 1 < int(m_items.size()));
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, &PDFToolChestWidget::onRemoveClicked);
        menu.addSeparator();
    }
    menu.addAction(tr("Add Selected Markups"), this, &PDFToolChestWidget::addSelectedMarkups)->setEnabled(m_addButton->isEnabled());
    menu.exec(m_itemsListWidget->viewport()->mapToGlobal(pos));
}

void PDFToolChestWidget::onRenameClicked()
{
    const int row = m_itemsListWidget->currentRow();
    if (row < 0 || size_t(row) >= m_items.size())
    {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name"), QLineEdit::Normal, m_items[row].name, &ok);
    if (ok)
    {
        renameItem(row, name);
    }
}

void PDFToolChestWidget::onRemoveClicked()
{
    const int row = m_itemsListWidget->currentRow();
    if (row < 0 || size_t(row) >= m_items.size())
    {
        return;
    }

    if (QMessageBox::question(this, tr("Delete"), tr("Delete '%1' from the tool chest?").arg(m_items[row].name)) == QMessageBox::Yes)
    {
        removeItem(row);
    }
}

void PDFToolChestWidget::onImportClicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Import Tool Chest"), QString(), tr("Tool chest (*.json)"));
    if (!fileName.isEmpty() && importItems(fileName) < 0)
    {
        QMessageBox::critical(this, tr("Error"), tr("The file '%1' is not a tool chest.").arg(fileName));
    }
}

void PDFToolChestWidget::onExportClicked()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Tool Chest"), QStringLiteral("toolchest.json"), tr("Tool chest (*.json)"));
    if (!fileName.isEmpty() && !exportItems(fileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("The file '%1' can't be written.").arg(fileName));
    }
}

}   // namespace pdfviewer
