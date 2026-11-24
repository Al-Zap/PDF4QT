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


#ifndef PDFTOOLCHESTWIDGET_H
#define PDFTOOLCHESTWIDGET_H

#include "pdfviewerglobal.h"
#include "pdfcustomstamp.h"
#include "pdftoolchest.h"
#include "pdfdocument.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;
class QListWidgetItem;

namespace pdf
{
class PDFToolManager;
class PDFDrawWidgetProxy;
class PDFPlaceMarkupTool;
}

namespace pdfviewer
{

/// Tool chest: custom stamps and markups saved by the user. A click on an item
/// starts the placing of the item on the page.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFToolChestWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Creates the tool chest
    /// \param proxy Proxy of the draw widget
    /// \param toolManager Tool manager (the placing tool is added to it)
    /// \param parent Parent widget
    /// \param toolChestFileName File of the tool chest (empty means the default file of the user)
    /// \param stampsFileName File of the custom stamps (empty means the default file of the user)
    explicit PDFToolChestWidget(pdf::PDFDrawWidgetProxy* proxy,
                                pdf::PDFToolManager* toolManager,
                                QWidget* parent,
                                QString toolChestFileName = QString(),
                                QString stampsFileName = QString());
    virtual ~PDFToolChestWidget() override;

    void setDocument(const pdf::PDFModifiedDocument& document);

    const pdf::PDFCustomStamps& getStamps() const { return m_stamps; }
    const pdf::PDFToolChestItems& getItems() const { return m_items; }

    /// Replaces the custom stamps and saves them
    void setStamps(pdf::PDFCustomStamps stamps);

    /// Saves the annotations of the document as a new item of the tool chest
    /// \param annotations Annotations
    /// \param name Name (empty means a name created from the annotations)
    /// \returns true, if the item has been added
    bool addMarkups(const std::vector<pdf::PDFObjectReference>& annotations, const QString& name = QString());

    /// Saves the selected annotations as a new item of the tool chest
    bool addSelectedMarkups();

    /// Renames the item of the tool chest
    void renameItem(int index, const QString& name);

    /// Removes the item of the tool chest
    void removeItem(int index);

    /// Moves the item of the tool chest up (-1) or down (+1)
    void moveItem(int index, int direction);

    /// Adds the items of the file. Returns the number of the added items, or -1,
    /// if the file is not a tool chest.
    int importItems(const QString& fileName);

    /// Writes the items of the tool chest to the file
    bool exportItems(const QString& fileName) const;

    /// Starts the placing of the custom stamp. Returns false, if it can't be placed.
    bool activateStamp(int index);

    /// Starts the placing of the item of the tool chest. Returns false, if it can't be placed.
    bool activateItem(int index);

    /// Shows the editor of the custom stamps. Returns true, if the stamps were changed.
    bool editStamps();

    /// Returns true, if markups can be placed into the document (document is open
    /// and its security handler allows it)
    bool canModifyDocument() const;

    /// Returns the actions of the custom stamps (placing of the stamp), the
    /// actions are owned by the parent
    QList<QAction*> createStampActions(QObject* parent);

    pdf::PDFPlaceMarkupTool* getPlaceTool() const { return m_placeTool; }
    QListWidget* getStampsListWidget() const { return m_stampsListWidget; }
    QListWidget* getItemsListWidget() const { return m_itemsListWidget; }
    QToolButton* getAddButton() const { return m_addButton; }

signals:
    void stampsChanged();

private:
    void updateStampsList();
    void updateItemsList();
    void updateButtons();
    void saveItems();
    void onStampClicked(QListWidgetItem* item);
    void onItemClicked(QListWidgetItem* item);
    void onItemsContextMenuRequested(const QPoint& pos);
    void onRenameClicked();
    void onRemoveClicked();
    void onImportClicked();
    void onExportClicked();
    QImage renderStampPreview(const pdf::PDFCustomStamp& stamp, pdf::PDFReal scale) const;

    pdf::PDFDrawWidgetProxy* m_proxy;
    pdf::PDFToolManager* m_toolManager;
    pdf::PDFPlaceMarkupTool* m_placeTool;
    const pdf::PDFDocument* m_document = nullptr;
    QString m_toolChestFileName;
    QString m_stampsFileName;
    pdf::PDFCustomStamps m_stamps;
    pdf::PDFToolChestItems m_items;

    QListWidget* m_stampsListWidget = nullptr;
    QListWidget* m_itemsListWidget = nullptr;
    QToolButton* m_addButton = nullptr;
    QToolButton* m_renameButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QToolButton* m_moreButton = nullptr;
    QToolButton* m_stampsButton = nullptr;
    QLabel* m_hintLabel = nullptr;
};

}   // namespace pdfviewer

#endif // PDFTOOLCHESTWIDGET_H
