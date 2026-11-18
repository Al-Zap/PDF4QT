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

#ifndef PDFRIBBON_H
#define PDFRIBBON_H

#include "pdfviewerglobal.h"

#include <QIcon>
#include <QList>
#include <QString>
#include <QWidget>
#include <QPointer>
#include <QToolButton>

#include <vector>

class QMenu;
class QLabel;
class QAction;
class QTabBar;
class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class QStackedWidget;

namespace pdfviewer
{

/// Button of the ribbon. The button is bound to an action (it shows its icon,
/// text and state and triggers it), and it can have a drop down list of other
/// actions. A large button has the icon above the text (wrapped to two lines),
/// a small button has the icon beside the text.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRibbonButton : public QToolButton
{
    Q_OBJECT

private:
    using BaseClass = QToolButton;

public:
    enum class Size
    {
        Large,      ///< Icon above the text
        Small,      ///< Icon beside the text
        IconOnly    ///< Small icon without the text
    };

    explicit PDFRibbonButton(Size size, QWidget* parent);

    /// Binds the button to the action
    /// \param action Action
    /// \param text Text of the button, the text of the action is used, if it is empty
    void setAction(QAction* action, const QString& text = QString());

    /// Adds a drop down list of actions. If the button is bound to an action, the
    /// arrow opens the list and the button triggers the action, otherwise the
    /// whole button opens the list. A nullptr is a separator.
    /// \param actions Actions of the list
    /// \param rememberLastUsed The button is bound to the action triggered last
    void setActionList(const QList<QAction*>& actions, bool rememberLastUsed);

    /// Uses an existing menu as the drop down list of the button
    void setDropDownMenu(QMenu* menu);

    /// Sets the text and the icon of a button, which is not bound to an action
    void setCaption(const QString& text, const QIcon& icon);

    QAction* getAction() const { return m_action; }
    Size getButtonSize() const { return m_size; }

    /// Returns the text of the action as it is displayed on a button
    /// (without the mnemonics and the ellipsis)
    static QString getCleanText(const QString& text);

    /// Wraps the text to two lines of similar length
    static QString getWrappedText(const QString& text);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

private:
    void updateFromAction();
    void onClicked();
    void setDisplayedText(const QString& text);

    Size m_size;
    QPointer<QAction> m_action;
    QString m_text;
    bool m_hasFixedText;
};

/// Group of buttons on a page of the ribbon, with a caption below the buttons.
/// The buttons are placed in columns from the left to the right: a large button
/// takes a whole column, small buttons are stacked by three.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRibbonGroup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFRibbonGroup(const QString& title, QWidget* parent);

    /// Adds a large button of the action
    PDFRibbonButton* addLargeAction(QAction* action, const QString& text = QString());

    /// Adds a small button of the action
    PDFRibbonButton* addSmallAction(QAction* action, const QString& text = QString());

    /// Adds a small button without the text
    PDFRibbonButton* addIconAction(QAction* action);

    /// Adds a button with a drop down list of the actions. The button triggers the
    /// first action (or the action used last), the arrow opens the list.
    /// \param text Text of the button, texts of the actions are used, if it is empty
    PDFRibbonButton* addActionList(PDFRibbonButton::Size size, const QList<QAction*>& actions, bool rememberLastUsed = true, const QString& text = QString());

    /// Adds a button, which opens the list of the actions
    PDFRibbonButton* addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, const QList<QAction*>& actions);

    /// Adds a button, which opens an existing menu
    PDFRibbonButton* addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, QMenu* menu);

    /// Adds a widget, which takes a whole column
    void addWidget(QWidget* widget);

    /// Adds a widget into the column of the small buttons
    void addSmallWidget(QWidget* widget);

    /// Next small button starts a new column
    void breakColumn();

    /// Adds a vertical line between the columns
    void addSeparator();

    /// Returns true, if the group has no buttons and widgets
    bool isEmpty() const { return m_itemCount == 0; }

    QString getTitle() const;

private:
    PDFRibbonButton* createButton(PDFRibbonButton::Size size);
    void placeButton(PDFRibbonButton* button);
    void placeSmallWidget(QWidget* widget);

    QHBoxLayout* m_contentLayout;
    QVBoxLayout* m_smallColumnLayout;
    QHBoxLayout* m_iconRowLayout;
    QLabel* m_titleLabel;
    int m_smallColumnCount;
    int m_itemCount;
};

/// Page (tab) of the ribbon, it consists of the groups
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRibbonPage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFRibbonPage(const QString& id, QWidget* parent);

    const QString& getId() const { return m_id; }

    /// Returns the group with a given identifier, the group is created
    /// at the end of the page, if it does not exist.
    PDFRibbonGroup* getGroup(const QString& id, const QString& title);

    /// Returns the group with a given identifier, or nullptr
    PDFRibbonGroup* findGroup(const QString& id) const;

    /// Hides the groups without buttons
    void updateGroups();

private:
    QString m_id;
    QHBoxLayout* m_layout;
    std::vector<std::pair<QString, PDFRibbonGroup*>> m_groups;
};

/// Ribbon: the application button (File), tabs with pages of grouped buttons
/// and an area for widgets on the right side of the tabs. It replaces the menu
/// bar and the toolbars of the main window.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFRibbon : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFRibbon(QWidget* parent);

    /// Sets the menu opened by the application button
    void setFileMenu(const QString& text, QMenu* menu);

    /// Adds a new page at the end of the ribbon
    PDFRibbonPage* addPage(const QString& id, const QString& title);

    /// Returns the page with a given identifier, or nullptr
    PDFRibbonPage* getPage(const QString& id) const;

    /// Returns the group of a page, the group is created, if it does not exist.
    /// Returns nullptr, if the page does not exist.
    PDFRibbonGroup* getGroup(const QString& pageId, const QString& groupId, const QString& title);

    /// Adds a widget to the right side of the tabs
    void addCornerWidget(QWidget* widget);

    /// Adds a small button of the action to the right side of the tabs
    PDFRibbonButton* addCornerAction(QAction* action);

    void setCurrentPage(const QString& id);
    QString getCurrentPageId() const;

    /// Pages are hidden in the collapsed ribbon, only the tabs are visible
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_isCollapsed; }

    /// Hides the groups without buttons, call it after the ribbon is filled
    void updateGroups();

    /// Returns the style sheet of the ribbon and of the main window
    /// \param isDarkTheme Colors for the dark theme
    static QString createStyleSheet(bool isDarkTheme);

    /// Accent color of the user interface
    static QColor getAccentColor(bool isDarkTheme);

signals:
    void collapsedChanged(bool collapsed);
    void currentPageChanged(const QString& id);

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onCurrentTabChanged(int index);
    void updateScrollButtons();
    void scrollPage(int direction);

    QToolButton* m_fileButton;
    QTabBar* m_tabBar;
    QHBoxLayout* m_cornerLayout;
    QScrollArea* m_scrollArea;
    QStackedWidget* m_stackedWidget;
    QToolButton* m_scrollLeftButton;
    QToolButton* m_scrollRightButton;
    std::vector<PDFRibbonPage*> m_pages;
    bool m_isCollapsed;
};

}   // namespace pdfviewer

#endif // PDFRIBBON_H
