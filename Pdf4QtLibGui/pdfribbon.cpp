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

#include "pdfribbon.h"
#include "pdfwidgetutils.h"

#include <QMenu>
#include <QLabel>
#include <QAction>
#include <QTabBar>
#include <QScrollBar>
#include <QBoxLayout>
#include <QScrollArea>
#include <QStyle>
#include <QStatusBar>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QStackedWidget>
#include <QTimer>

#include "pdfdbgheap.h"

namespace pdfviewer
{

/// Sizes and colors of the ribbon
class PDFRibbonHelper
{
public:
    PDFRibbonHelper() = delete;

    static constexpr int LARGE_BUTTON_HEIGHT = 76;
    static constexpr int LARGE_BUTTON_MIN_WIDTH = 52;
    static constexpr int LARGE_ICON_SIZE = 32;
    static constexpr int SMALL_BUTTON_HEIGHT = 24;
    static constexpr int SMALL_ICON_SIZE = 18;
    static constexpr int SMALL_BUTTONS_IN_COLUMN = 3;
    static constexpr int ICON_BUTTONS_IN_ROW = 4;
    static constexpr int WRAP_TEXT_LENGTH = 9;

    struct Colors
    {
        QString bar;            ///< Background of the tabs and of the status bar
        QString page;           ///< Background of the ribbon pages
        QString border;
        QString text;
        QString mutedText;
        QString disabledText;
        QString accent;
        QString accentHover;
        QString hover;
        QString hoverBorder;
        QString checked;
        QString checkedBorder;
    };

    static Colors getColors(bool isDarkTheme)
    {
        Colors colors;

        if (isDarkTheme)
        {
            colors.bar = QStringLiteral("#1f2227");
            colors.page = QStringLiteral("#2a2e34");
            colors.border = QStringLiteral("#3d434c");
            colors.text = QStringLiteral("#e6e9ed");
            colors.mutedText = QStringLiteral("#9aa3ae");
            colors.disabledText = QStringLiteral("#6b727c");
            colors.accent = QStringLiteral("#4c9bf5");
            colors.accentHover = QStringLiteral("#6aaef8");
            colors.hover = QStringLiteral("#373d46");
            colors.hoverBorder = QStringLiteral("#4a525d");
            colors.checked = QStringLiteral("#2f4666");
            colors.checkedBorder = QStringLiteral("#4c7fc0");
        }
        else
        {
            colors.bar = QStringLiteral("#eef1f5");
            colors.page = QStringLiteral("#fbfcfd");
            colors.border = QStringLiteral("#d3d9e0");
            colors.text = QStringLiteral("#1f2933");
            colors.mutedText = QStringLiteral("#66717e");
            colors.disabledText = QStringLiteral("#a3abb5");
            colors.accent = QStringLiteral("#1a5fb4");
            colors.accentHover = QStringLiteral("#2470cc");
            colors.hover = QStringLiteral("#e4edf9");
            colors.hoverBorder = QStringLiteral("#c3d6f0");
            colors.checked = QStringLiteral("#cfe0f7");
            colors.checkedBorder = QStringLiteral("#8db4ea");
        }

        return colors;
    }
};

PDFRibbonButton::PDFRibbonButton(Size size, QWidget* parent) :
    BaseClass(parent),
    m_size(size),
    m_hasFixedText(false)
{
    setAutoRaise(true);

    // The focus stays in the document, so the keyboard works after a click on the ribbon
    setFocusPolicy(Qt::NoFocus);
    setProperty("ribbonButton", true);
    setProperty("ribbonMenu", QStringLiteral("none"));

    applyButtonSize();

    connect(this, &QToolButton::clicked, this, &PDFRibbonButton::onClicked);
}

void PDFRibbonButton::setButtonSize(Size size)
{
    if (m_size == size)
    {
        return;
    }

    m_size = size;
    applyButtonSize();

    // The style sheet of the button depends on its size
    style()->unpolish(this);
    style()->polish(this);

    // A large button wraps its text, the other buttons do not
    if (m_action)
    {
        updateFromAction();
    }
    else if (m_hasFixedText)
    {
        setDisplayedText(getCleanText(m_text));
    }

    if (m_size == Size::IconOnly && !m_action && m_hasFixedText)
    {
        setToolTip(getCleanText(m_text));
    }

    updateGeometry();
}

void PDFRibbonButton::applyButtonSize()
{
    switch (m_size)
    {
        case Size::Large:
        {
            const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(this, PDFRibbonHelper::LARGE_ICON_SIZE);
            setProperty("ribbonSize", QStringLiteral("large"));
            setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            setIconSize(QSize(iconSize, iconSize));
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            break;
        }

        case Size::Small:
        {
            const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(this, PDFRibbonHelper::SMALL_ICON_SIZE);
            setProperty("ribbonSize", QStringLiteral("small"));
            setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            setIconSize(QSize(iconSize, iconSize));
            setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            break;
        }

        case Size::IconOnly:
        {
            const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(this, PDFRibbonHelper::SMALL_ICON_SIZE);
            setProperty("ribbonSize", QStringLiteral("icon"));
            setToolButtonStyle(Qt::ToolButtonIconOnly);
            setIconSize(QSize(iconSize, iconSize));
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            break;
        }
    }
}

void PDFRibbonButton::setAction(QAction* action, const QString& text)
{
    if (m_action)
    {
        disconnect(m_action, nullptr, this, nullptr);
    }

    m_action = action;
    m_hasFixedText = !text.isEmpty();
    m_text = text;

    if (m_action)
    {
        connect(m_action, &QAction::changed, this, &PDFRibbonButton::updateFromAction);
        updateFromAction();
    }
}

void PDFRibbonButton::setActionList(const QList<QAction*>& actions, bool rememberLastUsed)
{
    QMenu* dropDownMenu = new QMenu(this);
    dropDownMenu->setToolTipsVisible(true);

    for (QAction* action : actions)
    {
        if (!action)
        {
            dropDownMenu->addSeparator();
            continue;
        }

        dropDownMenu->addAction(action);

        if (rememberLastUsed)
        {
            connect(action, &QAction::triggered, this, [this, action]()
            {
                if (m_action != action)
                {
                    setAction(action, m_hasFixedText ? m_text : QString());
                }
            });
        }
    }

    setDropDownMenu(dropDownMenu);
}

void PDFRibbonButton::setDropDownMenu(QMenu* menu)
{
    setMenu(menu);

    if (m_action)
    {
        setPopupMode(QToolButton::MenuButtonPopup);
        setProperty("ribbonMenu", QStringLiteral("split"));
    }
    else
    {
        setPopupMode(QToolButton::InstantPopup);
        setProperty("ribbonMenu", QStringLiteral("popup"));
    }

    // The arrow of the menu takes the space under the text, the text of a large button is wrapped only without it
    if (m_action)
    {
        updateFromAction();
    }
    else if (m_hasFixedText)
    {
        setDisplayedText(getCleanText(m_text));
    }

    updateGeometry();
}

void PDFRibbonButton::setCaption(const QString& text, const QIcon& icon)
{
    m_hasFixedText = true;
    m_text = text;
    setIcon(icon);
    setDisplayedText(getCleanText(text));

    if (m_size == Size::IconOnly)
    {
        setToolTip(getCleanText(text));
    }
}

QString PDFRibbonButton::getCleanText(const QString& text)
{
    QString result;
    result.reserve(text.size());

    // Mnemonics are removed, a doubled ampersand is the ampersand itself
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];
        if (character == QChar('&'))
        {
            if (i + 1 < text.size() && text[i + 1] == QChar('&'))
            {
                result += character;
                ++i;
            }
            continue;
        }

        result += character;
    }

    result = result.trimmed();
    while (result.endsWith(QChar('.')) || result.endsWith(QChar(0x2026)))
    {
        result.chop(1);
    }

    return result.trimmed();
}

QString PDFRibbonButton::getWrappedText(const QString& text)
{
    if (text.size() <= PDFRibbonHelper::WRAP_TEXT_LENGTH || text.contains(QChar('\n')))
    {
        return text;
    }

    // The text is broken at the space, which is the nearest to its middle
    const qsizetype middle = text.size() / 2;
    qsizetype bestIndex = -1;

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (text[i] == QChar(' ') && (bestIndex == -1 || qAbs(i - middle) < qAbs(bestIndex - middle)))
        {
            bestIndex = i;
        }
    }

    if (bestIndex == -1)
    {
        return text;
    }

    QString result = text;
    result[bestIndex] = QChar('\n');
    return result;
}

QSize PDFRibbonButton::sizeHint() const
{
    QSize size = BaseClass::sizeHint();

    switch (m_size)
    {
        case Size::Large:
            size.setWidth(qMax(size.width(), pdf::PDFWidgetUtils::scaleDPI_x(this, PDFRibbonHelper::LARGE_BUTTON_MIN_WIDTH)));
            size.setHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, PDFRibbonHelper::LARGE_BUTTON_HEIGHT));
            break;

        case Size::Small:
            size.setHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, PDFRibbonHelper::SMALL_BUTTON_HEIGHT));
            break;

        case Size::IconOnly:
            size.setHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, PDFRibbonHelper::SMALL_BUTTON_HEIGHT));
            size.setWidth(qMax(size.width(), size.height()));
            break;
    }

    return size;
}

QSize PDFRibbonButton::minimumSizeHint() const
{
    return sizeHint();
}

void PDFRibbonButton::updateFromAction()
{
    if (!m_action)
    {
        return;
    }

    setIcon(m_action->icon());
    setDisplayedText(getCleanText(m_hasFixedText ? m_text : m_action->text()));
    setEnabled(m_action->isEnabled());
    setCheckable(m_action->isCheckable());
    setChecked(m_action->isChecked());

    if (isHidden() == m_action->isVisible())
    {
        setVisible(m_action->isVisible());
        Q_EMIT sizeHintChanged();
    }

    QString toolTip = getCleanText(m_action->toolTip());
    if (toolTip.isEmpty())
    {
        toolTip = getCleanText(m_action->text());
    }

    const QString shortcut = m_action->shortcut().toString(QKeySequence::NativeText);
    if (!shortcut.isEmpty() && !toolTip.contains(QString("(%1)").arg(shortcut)))
    {
        toolTip = QString("%1 (%2)").arg(toolTip, shortcut);
    }
    setToolTip(toolTip);
}

void PDFRibbonButton::onClicked()
{
    if (!m_action)
    {
        return;
    }

    m_action->trigger();

    // The action can refuse the change of its state (for example an active tool),
    // and no signal is emitted in this case.
    updateFromAction();
}

void PDFRibbonButton::setDisplayedText(const QString& text)
{
    QString displayedText = text;

    // A large button with a menu has the arrow of the menu under the text, there is no space for the second line
    if (m_size == Size::Large && !menu())
    {
        displayedText = getWrappedText(displayedText);
    }

    // The ampersand is not a mnemonic on the button
    displayedText.replace(QChar('&'), QStringLiteral("&&"));

    if (displayedText != this->text())
    {
        setText(displayedText);
        updateGeometry();
        Q_EMIT sizeHintChanged();
    }
}

PDFRibbonGroup::PDFRibbonGroup(const QString& title, QWidget* parent) :
    BaseClass(parent),
    m_groupLayout(nullptr),
    m_contentWidget(nullptr),
    m_contentLayout(nullptr),
    m_titleLabel(nullptr),
    m_collapsedButton(nullptr),
    m_collapsedMenu(nullptr),
    m_collapsedMenuContainer(nullptr),
    m_reduction(Reduction::None),
    m_appliedReduction(Reduction::None),
    m_contentReduction(Reduction::None),
    m_itemCount(0),
    m_isUpdatingLayout(false),
    m_isCollapsedMenuOpen(false)
{
    m_reducedWidths.fill(-1);

    setProperty("ribbonGroup", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_groupLayout = new QVBoxLayout(this);
    m_groupLayout->setContentsMargins(6, 3, 7, 1);
    m_groupLayout->setSpacing(0);

    // The buttons lie in a widget of their own, which is moved into the drop down
    // window of the collapsed group
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("ribbonGroupContent");
    m_contentLayout = new QHBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    m_contentLayout->addStrut(pdf::PDFWidgetUtils::scaleDPI_y(this, PDFRibbonHelper::LARGE_BUTTON_HEIGHT));
    m_groupLayout->addWidget(m_contentWidget, 0, Qt::AlignHCenter | Qt::AlignTop);

    m_titleLabel = new QLabel(PDFRibbonButton::getCleanText(title), this);
    m_titleLabel->setProperty("ribbonGroupTitle", true);
    m_titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_groupLayout->addWidget(m_titleLabel);
}

PDFRibbonButton* PDFRibbonGroup::addLargeAction(QAction* action, const QString& text)
{
    if (!action)
    {
        return nullptr;
    }

    PDFRibbonButton* button = createButton(PDFRibbonButton::Size::Large);
    button->setAction(action, text);
    addItem(ItemType::Button, button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addSmallAction(QAction* action, const QString& text)
{
    if (!action)
    {
        return nullptr;
    }

    PDFRibbonButton* button = createButton(PDFRibbonButton::Size::Small);
    button->setAction(action, text);
    addItem(ItemType::Button, button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addIconAction(QAction* action)
{
    if (!action)
    {
        return nullptr;
    }

    PDFRibbonButton* button = createButton(PDFRibbonButton::Size::IconOnly);
    button->setAction(action);
    addItem(ItemType::Button, button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addActionList(PDFRibbonButton::Size size, const QList<QAction*>& actions, bool rememberLastUsed, const QString& text)
{
    QAction* defaultAction = nullptr;
    for (QAction* action : actions)
    {
        if (action)
        {
            defaultAction = action;
            break;
        }
    }

    if (!defaultAction)
    {
        return nullptr;
    }

    PDFRibbonButton* button = createButton(size);
    button->setAction(defaultAction, text);
    button->setActionList(actions, rememberLastUsed);
    addItem(ItemType::Button, button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, const QList<QAction*>& actions)
{
    PDFRibbonButton* button = createButton(size);
    button->setCaption(text, icon);
    button->setActionList(actions, false);
    addItem(ItemType::Button, button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, QMenu* menu)
{
    PDFRibbonButton* button = createButton(size);
    button->setCaption(text, icon);
    button->setDropDownMenu(menu);
    addItem(ItemType::Button, button);
    return button;
}

void PDFRibbonGroup::addWidget(QWidget* widget)
{
    widget->setParent(m_contentWidget);
    addItem(ItemType::Widget, widget);
}

void PDFRibbonGroup::addSmallWidget(QWidget* widget)
{
    widget->setParent(m_contentWidget);
    addItem(ItemType::SmallWidget, widget);
}

void PDFRibbonGroup::breakColumn()
{
    addItem(ItemType::ColumnBreak, nullptr);
}

void PDFRibbonGroup::addSeparator()
{
    QFrame* line = new QFrame(m_contentWidget);
    line->setProperty("ribbonSeparator", true);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedWidth(1);
    addItem(ItemType::Separator, line);
}

QString PDFRibbonGroup::getTitle() const
{
    return m_titleLabel->text();
}

int PDFRibbonGroup::getReducedWidth(Reduction reduction)
{
    int& width = m_reducedWidths[size_t(reduction)];

    if (width < 0)
    {
        // The content lies in the drop down window, the width is not known now
        if (m_isCollapsedMenuOpen)
        {
            return sizeHint().width();
        }

        applyReduction(reduction);
        width = sizeHint().width();
    }

    return width;
}

void PDFRibbonGroup::setReduction(Reduction reduction)
{
    m_reduction = reduction;

    // The content is laid out again, when the drop down window is closed
    if (!m_isCollapsedMenuOpen)
    {
        applyReduction(reduction);
    }
}

void PDFRibbonGroup::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
    {
        invalidateReduction();
    }
}

PDFRibbonButton* PDFRibbonGroup::createButton(PDFRibbonButton::Size size)
{
    PDFRibbonButton* button = new PDFRibbonButton(size, m_contentWidget);
    connect(button, &PDFRibbonButton::sizeHintChanged, this, &PDFRibbonGroup::invalidateReduction);

    // The drop down window of the collapsed group is closed, when its button is used
    connect(button, &QToolButton::clicked, this, &PDFRibbonGroup::closeCollapsedMenu);
    connect(button, &QToolButton::triggered, this, &PDFRibbonGroup::closeCollapsedMenu);
    return button;
}

void PDFRibbonGroup::addItem(ItemType type, QWidget* widget)
{
    Item item;
    item.type = type;
    item.widget = widget;

    if (PDFRibbonButton* button = qobject_cast<PDFRibbonButton*>(widget))
    {
        item.size = button->getButtonSize();
    }

    m_items.push_back(item);

    if (type != ItemType::ColumnBreak && type != ItemType::Separator)
    {
        ++m_itemCount;
    }

    // New items are added to the full group, the group is reduced again by the ribbon
    m_reduction = Reduction::None;
    m_contentReduction = Reduction::Count;
    applyReduction(Reduction::None);
    invalidateReduction();
}

void PDFRibbonGroup::layoutContent(Reduction reduction)
{
    if (m_contentReduction == reduction)
    {
        return;
    }

    m_contentReduction = reduction;

    // All items are removed from the layout and placed again, the widgets stay
    while (QLayoutItem* layoutItem = m_contentLayout->takeAt(0))
    {
        delete layoutItem;
    }

    QVBoxLayout* smallColumnLayout = nullptr;
    QHBoxLayout* iconRowLayout = nullptr;
    int smallColumnCount = 0;

    auto startNewColumn = [&]()
    {
        smallColumnLayout = nullptr;
        iconRowLayout = nullptr;
    };

    // Widgets and rows of icons are stacked in columns by three
    auto placeSmallItem = [&](QWidget* widget, QLayout* layout)
    {
        if (!smallColumnLayout || smallColumnCount >= PDFRibbonHelper::SMALL_BUTTONS_IN_COLUMN)
        {
            smallColumnLayout = new QVBoxLayout();
            smallColumnLayout->setContentsMargins(0, 0, 0, 0);
            smallColumnLayout->setSpacing(2);
            smallColumnLayout->addStretch(1);
            m_contentLayout->addLayout(smallColumnLayout);
            smallColumnCount = 0;
        }

        // The stretch stays the last item of the column
        if (widget)
        {
            smallColumnLayout->insertWidget(smallColumnLayout->count() - 1, widget);
        }
        else
        {
            smallColumnLayout->insertLayout(smallColumnLayout->count() - 1, layout);
        }
        ++smallColumnCount;
    };

    for (const Item& item : m_items)
    {
        switch (item.type)
        {
            case ItemType::Button:
            {
                PDFRibbonButton* button = static_cast<PDFRibbonButton*>(item.widget);
                PDFRibbonButton::Size size = item.size;

                switch (reduction)
                {
                    case Reduction::SmallButtons:
                        if (size == PDFRibbonButton::Size::Large)
                        {
                            size = PDFRibbonButton::Size::Small;
                        }
                        break;

                    case Reduction::IconButtons:
                        // A button without an icon keeps its text
                        if (size != PDFRibbonButton::Size::IconOnly)
                        {
                            size = button->icon().isNull() ? PDFRibbonButton::Size::Small : PDFRibbonButton::Size::IconOnly;
                        }
                        break;

                    default:
                        break;
                }

                button->setButtonSize(size);

                if (size == PDFRibbonButton::Size::Large)
                {
                    m_contentLayout->addWidget(button, 0, Qt::AlignTop);
                    startNewColumn();
                }
                else if (size == PDFRibbonButton::Size::IconOnly && item.size == PDFRibbonButton::Size::IconOnly)
                {
                    // Buttons created without a text are placed in rows
                    if (!iconRowLayout || iconRowLayout->count() > PDFRibbonHelper::ICON_BUTTONS_IN_ROW)
                    {
                        iconRowLayout = new QHBoxLayout();
                        iconRowLayout->setContentsMargins(0, 0, 0, 0);
                        iconRowLayout->setSpacing(0);
                        iconRowLayout->addStretch(1);
                        QHBoxLayout* rowLayout = iconRowLayout;
                        placeSmallItem(nullptr, rowLayout);
                        iconRowLayout = rowLayout;
                    }

                    // The stretch stays the last item of the row
                    iconRowLayout->insertWidget(iconRowLayout->count() - 1, button);
                }
                else
                {
                    placeSmallItem(button, nullptr);
                    iconRowLayout = nullptr;
                }
                break;
            }

            case ItemType::Widget:
                m_contentLayout->addWidget(item.widget, 0, Qt::AlignVCenter);
                startNewColumn();
                break;

            case ItemType::SmallWidget:
                placeSmallItem(item.widget, nullptr);
                iconRowLayout = nullptr;
                break;

            case ItemType::ColumnBreak:
                startNewColumn();
                break;

            case ItemType::Separator:
                m_contentLayout->addSpacing(2);
                m_contentLayout->addWidget(item.widget);
                m_contentLayout->addSpacing(2);
                startNewColumn();
                break;
        }
    }

    m_contentLayout->invalidate();
    m_contentWidget->updateGeometry();
}

void PDFRibbonGroup::applyReduction(Reduction reduction)
{
    const bool isUpdatingLayout = m_isUpdatingLayout;
    m_isUpdatingLayout = true;

    const bool isCollapsed = reduction == Reduction::Collapsed;

    // The collapsed group opens the full group in its drop down window
    layoutContent(isCollapsed ? Reduction::None : reduction);

    if (isCollapsed)
    {
        createCollapsedButton();
    }

    if (m_collapsedButton)
    {
        m_collapsedButton->setVisible(isCollapsed);
    }
    m_contentWidget->setVisible(!isCollapsed);
    m_titleLabel->setVisible(!isCollapsed);

    m_appliedReduction = reduction;
    m_groupLayout->invalidate();
    updateGeometry();

    m_isUpdatingLayout = isUpdatingLayout;
}

void PDFRibbonGroup::invalidateReduction()
{
    if (m_isUpdatingLayout)
    {
        return;
    }

    m_reducedWidths.fill(-1);
    Q_EMIT reductionInvalidated();
}

void PDFRibbonGroup::createCollapsedButton()
{
    if (m_collapsedButton)
    {
        return;
    }

    // Icon of the collapsed group is the icon of its first button
    QIcon icon;
    for (const Item& item : m_items)
    {
        if (item.type == ItemType::Button && !static_cast<PDFRibbonButton*>(item.widget)->icon().isNull())
        {
            icon = static_cast<PDFRibbonButton*>(item.widget)->icon();
            break;
        }
    }

    m_collapsedMenuContainer = new QWidget();
    m_collapsedMenuContainer->setObjectName("ribbonCollapsedGroup");
    m_collapsedMenuContainer->setProperty("ribbonPage", true);
    m_collapsedMenuContainer->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* containerLayout = new QVBoxLayout(m_collapsedMenuContainer);
    containerLayout->setContentsMargins(4, 3, 4, 3);
    containerLayout->setSpacing(0);

    m_collapsedMenu = new QMenu(this);
    m_collapsedMenu->setObjectName("ribbonCollapsedGroupMenu");
    QWidgetAction* widgetAction = new QWidgetAction(m_collapsedMenu);
    widgetAction->setDefaultWidget(m_collapsedMenuContainer);
    m_collapsedMenu->addAction(widgetAction);
    connect(m_collapsedMenu, &QMenu::aboutToShow, this, &PDFRibbonGroup::onCollapsedMenuAboutToShow);
    connect(m_collapsedMenu, &QMenu::aboutToHide, this, &PDFRibbonGroup::onCollapsedMenuAboutToHide);

    m_collapsedButton = new PDFRibbonButton(PDFRibbonButton::Size::Large, this);
    m_collapsedButton->setObjectName("ribbonCollapsedGroupButton");
    m_collapsedButton->setCaption(getTitle(), icon);
    m_collapsedButton->setToolTip(getTitle());
    m_collapsedButton->setDropDownMenu(m_collapsedMenu);
    m_collapsedButton->hide();
    m_groupLayout->insertWidget(0, m_collapsedButton, 0, Qt::AlignHCenter | Qt::AlignTop);
}

void PDFRibbonGroup::closeCollapsedMenu()
{
    if (m_collapsedMenu && m_collapsedMenu->isVisible())
    {
        m_collapsedMenu->close();
    }
}

void PDFRibbonGroup::onCollapsedMenuAboutToShow()
{
    if (m_isCollapsedMenuOpen)
    {
        return;
    }

    m_isUpdatingLayout = true;
    layoutContent(Reduction::None);
    m_groupLayout->removeWidget(m_contentWidget);
    m_collapsedMenuContainer->layout()->addWidget(m_contentWidget);
    m_contentWidget->show();
    m_isCollapsedMenuOpen = true;
    m_isUpdatingLayout = false;
}

void PDFRibbonGroup::onCollapsedMenuAboutToHide()
{
    if (!m_isCollapsedMenuOpen)
    {
        return;
    }

    m_isUpdatingLayout = true;
    m_collapsedMenuContainer->layout()->removeWidget(m_contentWidget);
    m_groupLayout->insertWidget(m_collapsedButton ? 1 : 0, m_contentWidget, 0, Qt::AlignHCenter | Qt::AlignTop);
    m_contentWidget->hide();
    m_isCollapsedMenuOpen = false;
    m_isUpdatingLayout = false;

    // The ribbon could have changed the reduction, while the window was open
    applyReduction(m_reduction);
}

PDFRibbonPage::PDFRibbonPage(const QString& id, QWidget* parent) :
    BaseClass(parent),
    m_id(id),
    m_layout(nullptr)
{
    setProperty("ribbonPage", true);
    setAttribute(Qt::WA_StyledBackground, true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(0);
    m_layout->addStretch(1);
}

PDFRibbonGroup* PDFRibbonPage::getGroup(const QString& id, const QString& title)
{
    if (PDFRibbonGroup* group = findGroup(id))
    {
        return group;
    }

    PDFRibbonGroup* group = new PDFRibbonGroup(title, this);
    group->setObjectName(QString("ribbonGroup_%1_%2").arg(m_id, id));
    m_layout->insertWidget(m_layout->count() - 1, group);
    m_groups.emplace_back(id, group);
    connect(group, &PDFRibbonGroup::reductionInvalidated, this, &PDFRibbonPage::reductionInvalidated);
    return group;
}

PDFRibbonGroup* PDFRibbonPage::findGroup(const QString& id) const
{
    for (const auto& group : m_groups)
    {
        if (group.first == id)
        {
            return group.second;
        }
    }

    return nullptr;
}

void PDFRibbonPage::updateGroups()
{
    for (const auto& group : m_groups)
    {
        group.second->setVisible(!group.second->isEmpty());
    }
}

bool PDFRibbonPage::fitToWidth(int width)
{
    using Reduction = PDFRibbonGroup::Reduction;

    std::vector<PDFRibbonGroup*> groups;
    for (const auto& group : m_groups)
    {
        if (!group.second->isEmpty() && !group.second->isHidden())
        {
            groups.push_back(group.second);
        }
    }

    const QMargins margins = m_layout->contentsMargins();
    std::vector<Reduction> reductions(groups.size(), Reduction::None);
    std::vector<int> widths(groups.size(), 0);

    int totalWidth = margins.left() + margins.right();
    for (size_t i = 0; i < groups.size(); ++i)
    {
        widths[i] = groups[i]->getReducedWidth(Reduction::None);
        totalWidth += widths[i];
    }

    // Each step of the reduction is done for all groups (from the right to the left),
    // before the next step starts. A step, which does not make a group narrower, is skipped.
    for (int step = int(Reduction::SmallButtons); step < int(Reduction::Count) && totalWidth > width; ++step)
    {
        const Reduction reduction = Reduction(step);

        for (size_t i = groups.size(); i > 0 && totalWidth > width; --i)
        {
            const size_t index = i - 1;
            const int reducedWidth = groups[index]->getReducedWidth(reduction);

            if (reducedWidth < widths[index])
            {
                totalWidth -= widths[index] - reducedWidth;
                widths[index] = reducedWidth;
                reductions[index] = reduction;
            }
        }
    }

    for (size_t i = 0; i < groups.size(); ++i)
    {
        groups[i]->setReduction(reductions[i]);
    }

    return totalWidth <= width;
}

std::vector<PDFRibbonGroup*> PDFRibbonPage::getGroups() const
{
    std::vector<PDFRibbonGroup*> groups;
    groups.reserve(m_groups.size());

    for (const auto& group : m_groups)
    {
        groups.push_back(group.second);
    }

    return groups;
}

PDFRibbon::PDFRibbon(QWidget* parent) :
    BaseClass(parent),
    m_fileButton(nullptr),
    m_tabBar(nullptr),
    m_tabRowLayout(nullptr),
    m_cornerLayout(nullptr),
    m_shrinkableMinimumWidth(0),
    m_shrinkablePreferredWidth(0),
    m_scrollArea(nullptr),
    m_stackedWidget(nullptr),
    m_scrollLeftButton(nullptr),
    m_scrollRightButton(nullptr),
    m_isCollapsed(false),
    m_isReductionScheduled(false)
{
    setObjectName("ribbon");
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* ribbonLayout = new QVBoxLayout(this);
    ribbonLayout->setContentsMargins(0, 0, 0, 0);
    ribbonLayout->setSpacing(0);

    QWidget* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* tabRowLayout = new QHBoxLayout(tabRow);
    m_tabRowLayout = tabRowLayout;
    tabRowLayout->setContentsMargins(0, 0, 6, 0);
    tabRowLayout->setSpacing(0);

    m_fileButton = new QToolButton(tabRow);
    m_fileButton->setObjectName("ribbonFileButton");
    m_fileButton->setPopupMode(QToolButton::InstantPopup);
    m_fileButton->setFocusPolicy(Qt::NoFocus);
    m_fileButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_fileButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_fileButton->hide();
    tabRowLayout->addWidget(m_fileButton);

    m_tabBar = new QTabBar(tabRow);
    m_tabBar->setObjectName("ribbonTabBar");
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setUsesScrollButtons(false);
    m_tabBar->setElideMode(Qt::ElideNone);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    tabRowLayout->addWidget(m_tabBar);
    tabRowLayout->addStretch(1);

    m_cornerLayout = new QHBoxLayout();
    m_cornerLayout->setContentsMargins(0, 2, 0, 2);
    m_cornerLayout->setSpacing(2);
    tabRowLayout->addLayout(m_cornerLayout);

    ribbonLayout->addWidget(tabRow);

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName("ribbonStack");

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("ribbonPages");
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_scrollArea->setWidget(m_stackedWidget);
    m_scrollArea->viewport()->installEventFilter(this);
    ribbonLayout->addWidget(m_scrollArea);

    // Buttons, which scroll a page wider than the window
    auto createScrollButton = [this](Qt::ArrowType arrowType, int direction)
    {
        QToolButton* button = new QToolButton(this);
        button->setProperty("ribbonScrollButton", true);
        button->setArrowType(arrowType);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRepeat(true);
        button->hide();
        connect(button, &QToolButton::clicked, this, [this, direction]() { scrollPage(direction); });
        return button;
    };
    m_scrollLeftButton = createScrollButton(Qt::LeftArrow, -1);
    m_scrollRightButton = createScrollButton(Qt::RightArrow, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, &PDFRibbon::onCurrentTabChanged);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, [this](int) { setCollapsed(!m_isCollapsed); });
    connect(m_tabBar, &QTabBar::tabBarClicked, this, [this](int)
    {
        if (m_isCollapsed)
        {
            setCollapsed(false);
        }
    });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &PDFRibbon::updateScrollButtons);
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this, &PDFRibbon::updateScrollButtons);
}

void PDFRibbon::setFileMenu(const QString& text, QMenu* menu)
{
    m_fileButton->setText(PDFRibbonButton::getCleanText(text));
    m_fileButton->setMenu(menu);
    m_fileButton->setVisible(menu != nullptr);
}

PDFRibbonPage* PDFRibbon::addPage(const QString& id, const QString& title)
{
    if (PDFRibbonPage* page = getPage(id))
    {
        return page;
    }

    PDFRibbonPage* page = new PDFRibbonPage(id, m_stackedWidget);
    page->setObjectName(QString("ribbonPage_%1").arg(id));
    m_pages.push_back(page);
    m_stackedWidget->addWidget(page);
    connect(page, &PDFRibbonPage::reductionInvalidated, this, &PDFRibbon::scheduleReduction);
    m_tabBar->addTab(PDFRibbonButton::getCleanText(title));
    updateTabRow();

    // Height of the pages is given by the height of an (empty) group
    PDFRibbonGroup* group = page->getGroup(QString(), QString());
    m_scrollArea->setFixedHeight(page->sizeHint().height());
    group->hide();

    onCurrentTabChanged(m_tabBar->currentIndex());
    return page;
}

PDFRibbonPage* PDFRibbon::getPage(const QString& id) const
{
    for (PDFRibbonPage* page : m_pages)
    {
        if (page->getId() == id)
        {
            return page;
        }
    }

    return nullptr;
}

PDFRibbonGroup* PDFRibbon::getGroup(const QString& pageId, const QString& groupId, const QString& title)
{
    if (PDFRibbonPage* page = getPage(pageId))
    {
        return page->getGroup(groupId, title);
    }

    return nullptr;
}

void PDFRibbon::addCornerWidget(QWidget* widget, int minimumWidth)
{
    widget->setParent(this);
    m_cornerLayout->addWidget(widget, 0, Qt::AlignVCenter);

    if (minimumWidth > 0)
    {
        m_shrinkableCornerWidget = widget;
        m_shrinkableMinimumWidth = minimumWidth;
        m_shrinkablePreferredWidth = qMax(widget->sizeHint().width(), minimumWidth);
    }

    updateTabRow();
}

PDFRibbonButton* PDFRibbon::addCornerAction(QAction* action)
{
    PDFRibbonButton* button = new PDFRibbonButton(PDFRibbonButton::Size::IconOnly, this);
    button->setAction(action);
    m_cornerLayout->addWidget(button, 0, Qt::AlignVCenter);
    updateTabRow();
    return button;
}

void PDFRibbon::setCurrentPage(const QString& id)
{
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        if (m_pages[i]->getId() == id)
        {
            m_tabBar->setCurrentIndex(int(i));
            break;
        }
    }
}

QString PDFRibbon::getCurrentPageId() const
{
    const int index = m_tabBar->currentIndex();
    if (index >= 0 && index < int(m_pages.size()))
    {
        return m_pages[index]->getId();
    }

    return QString();
}

void PDFRibbon::setCollapsed(bool collapsed)
{
    if (m_isCollapsed != collapsed)
    {
        m_isCollapsed = collapsed;
        m_scrollArea->setVisible(!collapsed);
        updateScrollButtons();
        Q_EMIT collapsedChanged(collapsed);
    }
}

void PDFRibbon::updateGroups()
{
    for (PDFRibbonPage* page : m_pages)
    {
        page->updateGroups();
    }

    updateReduction();
    updateScrollButtons();
}

QColor PDFRibbon::getAccentColor(bool isDarkTheme)
{
    return QColor(PDFRibbonHelper::getColors(isDarkTheme).accent);
}

QString PDFRibbon::createStyleSheet(bool isDarkTheme)
{
    const PDFRibbonHelper::Colors colors = PDFRibbonHelper::getColors(isDarkTheme);

    QString styleSheet = QStringLiteral(
        // Ribbon
        "QWidget#ribbon { background: @page; border-bottom: 1px solid @border; }"
        "QWidget#ribbonTabRow { background: @bar; border-bottom: 1px solid @border; }"
        "QScrollArea#ribbonPages, QStackedWidget#ribbonStack, QWidget[ribbonPage=\"true\"] { background: @page; }"
        "QToolButton#ribbonFileButton { background: @accent; color: #ffffff; border: none; padding: 0px 20px; font-weight: 600; }"
        "QToolButton#ribbonFileButton:hover, QToolButton#ribbonFileButton:pressed { background: @accentHover; }"
        "QToolButton#ribbonFileButton::menu-indicator { image: none; width: 0px; }"
        "QTabBar#ribbonTabBar { background: transparent; }"
        "QTabBar#ribbonTabBar::tab { background: transparent; color: @text; border: none; border-bottom: 3px solid transparent; padding: 7px 14px 5px 14px; margin: 0px; }"
        "QTabBar#ribbonTabBar::tab:hover { color: @accent; background: @hover; }"
        "QTabBar#ribbonTabBar::tab:selected { color: @accent; background: @page; border-bottom: 3px solid @accent; }"
        "QWidget[ribbonGroup=\"true\"] { background: transparent; border: none; border-right: 1px solid @border; }"
        "QLabel[ribbonGroupTitle=\"true\"] { color: @mutedText; font-size: 11px; border: none; padding: 1px 0px 2px 0px; }"
        "QFrame[ribbonSeparator=\"true\"] { background: @border; border: none; margin: 6px 0px; }"

        // Buttons of the ribbon
        "QToolButton[ribbonButton=\"true\"] { background: transparent; color: @text; border: 1px solid transparent; border-radius: 4px; padding: 2px 5px; }"
        "QToolButton[ribbonButton=\"true\"]:hover { background: @hover; border-color: @hoverBorder; }"
        "QToolButton[ribbonButton=\"true\"]:pressed, QToolButton[ribbonButton=\"true\"]:checked, QToolButton[ribbonButton=\"true\"]:open { background: @checked; border-color: @checkedBorder; }"
        "QToolButton[ribbonButton=\"true\"]:disabled { color: @disabledText; }"
        "QToolButton[ribbonSize=\"large\"] { padding: 4px 4px 2px 4px; }"
        "QToolButton[ribbonSize=\"large\"][ribbonMenu=\"split\"], QToolButton[ribbonSize=\"large\"][ribbonMenu=\"popup\"] { padding-bottom: 12px; }"
        "QToolButton[ribbonSize=\"large\"]::menu-button { subcontrol-origin: border; subcontrol-position: bottom center; width: 44px; height: 13px; border: none; border-top: 1px solid transparent; border-bottom-left-radius: 4px; border-bottom-right-radius: 4px; }"
        "QToolButton[ribbonSize=\"large\"]::menu-button:hover { background: @checked; border-top: 1px solid @hoverBorder; }"
        "QToolButton[ribbonSize=\"large\"]::menu-indicator { subcontrol-origin: border; subcontrol-position: bottom center; bottom: 3px; }"
        "QToolButton[ribbonSize=\"small\"][ribbonMenu=\"split\"], QToolButton[ribbonSize=\"small\"][ribbonMenu=\"popup\"] { padding-right: 16px; }"
        "QToolButton[ribbonSize=\"icon\"][ribbonMenu=\"split\"], QToolButton[ribbonSize=\"icon\"][ribbonMenu=\"popup\"] { padding-right: 14px; }"
        "QToolButton[ribbonSize=\"small\"]::menu-button, QToolButton[ribbonSize=\"icon\"]::menu-button { subcontrol-origin: border; subcontrol-position: center right; width: 14px; border: none; border-left: 1px solid transparent; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
        "QToolButton[ribbonSize=\"small\"]::menu-button:hover, QToolButton[ribbonSize=\"icon\"]::menu-button:hover { background: @checked; border-left: 1px solid @hoverBorder; }"
        "QToolButton[ribbonSize=\"small\"]::menu-indicator, QToolButton[ribbonSize=\"icon\"]::menu-indicator { subcontrol-origin: border; subcontrol-position: center right; right: 3px; }"
        "QToolButton[ribbonScrollButton=\"true\"] { background: @bar; border: 1px solid @border; }"
        "QToolButton[ribbonScrollButton=\"true\"]:hover { background: @hover; }"
        "QLineEdit#ribbonSearchBox { background: @page; color: @text; border: 1px solid @border; border-radius: 4px; padding: 2px 6px; }"
        "QLineEdit#ribbonSearchBox:focus { border-color: @accent; }"

        // Status bar
        "QStatusBar#ribbonStatusBar { background: @bar; color: @text; border-top: 1px solid @border; }"
        "QStatusBar#ribbonStatusBar::item { border: none; }"
        "QStatusBar#ribbonStatusBar QLabel { color: @text; }"
        "QStatusBar#ribbonStatusBar QToolButton { background: transparent; color: @text; border: 1px solid transparent; border-radius: 3px; padding: 2px; }"
        "QStatusBar#ribbonStatusBar QToolButton:hover { background: @hover; border-color: @hoverBorder; }"
        "QStatusBar#ribbonStatusBar QToolButton:pressed, QStatusBar#ribbonStatusBar QToolButton:checked { background: @checked; border-color: @checkedBorder; }"
        "QFrame[statusBarSeparator=\"true\"] { background: @border; border: none; margin: 3px 4px; }"
        "QSlider#zoomSlider::groove:horizontal { height: 2px; background: @mutedText; }"
        "QSlider#zoomSlider::handle:horizontal { width: 6px; margin: -7px 0px; background: @accent; border-radius: 2px; }"
        "QSlider#zoomSlider::handle:horizontal:hover { background: @accentHover; }"

        // Navigation pane
        "QDockWidget::title { background: @bar; padding: 5px 8px; border-bottom: 1px solid @border; }"
        "QWidget#sidebarNavigationStrip { background: @bar; border: none; border-right: 1px solid @border; }"
        "QToolButton[sidebarNavigation=\"true\"] { background: transparent; border: none; border-left: 3px solid transparent; border-radius: 0px; padding: 7px 6px 7px 3px; }"
        "QToolButton[sidebarNavigation=\"true\"]:hover { background: @hover; }"
        "QToolButton[sidebarNavigation=\"true\"]:checked { background: @page; border-left: 3px solid @accent; }"
        // Pages of the navigation pane have the colors of the ribbon (the dark palette
        // of the application is much darker than the ribbon)
        "QDockWidget#SidebarDockWidget QStackedWidget, QDockWidget#SidebarDockWidget QStackedWidget > QWidget { background: @page; color: @text; }"
        "QDockWidget#SidebarDockWidget QAbstractItemView { background: @page; color: @text; border: none; }"
        "QLabel#sidebarHeaderLabel { background: @page; color: @text; font-weight: 600; padding: 7px 8px; border-bottom: 1px solid @border; }"
        "QMainWindow::separator { background: @border; width: 1px; height: 1px; }"
        "QMainWindow::separator:hover { background: @accent; }");

    styleSheet.replace(QStringLiteral("@bar"), colors.bar);
    styleSheet.replace(QStringLiteral("@page"), colors.page);
    styleSheet.replace(QStringLiteral("@border"), colors.border);
    styleSheet.replace(QStringLiteral("@text"), colors.text);
    styleSheet.replace(QStringLiteral("@mutedText"), colors.mutedText);
    styleSheet.replace(QStringLiteral("@disabledText"), colors.disabledText);
    styleSheet.replace(QStringLiteral("@accentHover"), colors.accentHover);
    styleSheet.replace(QStringLiteral("@accent"), colors.accent);
    styleSheet.replace(QStringLiteral("@hoverBorder"), colors.hoverBorder);
    styleSheet.replace(QStringLiteral("@hover"), colors.hover);
    styleSheet.replace(QStringLiteral("@checkedBorder"), colors.checkedBorder);
    styleSheet.replace(QStringLiteral("@checked"), colors.checked);
    return styleSheet;
}

void PDFRibbon::resizeEvent(QResizeEvent* event)
{
    updateTabRow();
    updateReduction();
    BaseClass::resizeEvent(event);
    updateScrollButtons();
}

void PDFRibbon::updateReduction()
{
    m_isReductionScheduled = false;

    // Groups of the current page get smaller buttons, when the window is narrow.
    // The page is scrolled only, if even the collapsed groups do not fit.
    const int index = m_tabBar->currentIndex();
    if (index >= 0 && index < int(m_pages.size()))
    {
        m_pages[index]->fitToWidth(width());
    }
}

void PDFRibbon::scheduleReduction()
{
    if (!m_isReductionScheduled)
    {
        m_isReductionScheduled = true;
        QTimer::singleShot(0, this, [this]()
        {
            if (m_isReductionScheduled)
            {
                updateReduction();
            }
        });
    }
}

void PDFRibbon::updateTabRow()
{
    // The row of the tabs is filled in this order: the File button, the tabs, the corner
    // widgets. When the window is narrow, the shrinkable corner widget (the search box)
    // gets narrower first, then it is hidden, and at last the tabs get scroll buttons.
    // The widgets never overlap.
    const QMargins margins = m_tabRowLayout->contentsMargins();
    const int spacing = qMax(m_cornerLayout->spacing(), 0);

    int occupiedWidth = margins.left() + margins.right();

    if (!m_fileButton->isHidden())
    {
        occupiedWidth += m_fileButton->sizeHint().width();
    }

    occupiedWidth += m_tabBar->sizeHint().width();

    for (int i = 0; i < m_cornerLayout->count(); ++i)
    {
        QWidget* widget = m_cornerLayout->itemAt(i)->widget();
        if (widget && widget != m_shrinkableCornerWidget && !widget->isHidden())
        {
            occupiedWidth += widget->sizeHint().width() + spacing;
        }
    }

    int freeWidth = width() - occupiedWidth;

    if (m_shrinkableCornerWidget)
    {
        const int shrinkableWidth = qMin(freeWidth - spacing, m_shrinkablePreferredWidth);

        if (shrinkableWidth >= m_shrinkableMinimumWidth)
        {
            m_shrinkableCornerWidget->setFixedWidth(shrinkableWidth);
            m_shrinkableCornerWidget->setVisible(true);
            freeWidth -= shrinkableWidth + spacing;
        }
        else
        {
            m_shrinkableCornerWidget->setVisible(false);
        }
    }

    m_tabBar->setUsesScrollButtons(freeWidth < 0);
}

bool PDFRibbon::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_scrollArea->viewport() && event->type() == QEvent::Wheel)
    {
        // The wheel scrolls a page, which is wider than the window
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (m_scrollArea->horizontalScrollBar()->maximum() > 0 && wheelEvent->angleDelta().y() != 0)
        {
            scrollPage(wheelEvent->angleDelta().y() > 0 ? -1 : 1);
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFRibbon::onCurrentTabChanged(int index)
{
    if (index < 0 || index >= int(m_pages.size()))
    {
        return;
    }

    // Only the current page gives the width of the ribbon, wide pages
    // do not make the other pages scrollable.
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        const QSizePolicy::Policy policy = (int(i) == index) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        m_pages[i]->setSizePolicy(policy, QSizePolicy::Preferred);
    }

    m_stackedWidget->setCurrentIndex(index);
    updateReduction();
    m_scrollArea->horizontalScrollBar()->setValue(0);
    updateScrollButtons();
    Q_EMIT currentPageChanged(m_pages[index]->getId());
}

void PDFRibbon::updateScrollButtons()
{
    QScrollBar* scrollBar = m_scrollArea->horizontalScrollBar();
    const bool isScrollable = !m_isCollapsed && scrollBar->maximum() > scrollBar->minimum();
    const bool canScrollLeft = isScrollable && scrollBar->value() > scrollBar->minimum();
    const bool canScrollRight = isScrollable && scrollBar->value() < scrollBar->maximum();

    const QRect area = m_scrollArea->geometry();
    const int buttonWidth = pdf::PDFWidgetUtils::scaleDPI_x(this, 16);

    m_scrollLeftButton->setGeometry(area.left(), area.top(), buttonWidth, area.height());
    m_scrollRightButton->setGeometry(area.right() - buttonWidth + 1, area.top(), buttonWidth, area.height());
    m_scrollLeftButton->setVisible(canScrollLeft);
    m_scrollRightButton->setVisible(canScrollRight);

    if (canScrollLeft)
    {
        m_scrollLeftButton->raise();
    }
    if (canScrollRight)
    {
        m_scrollRightButton->raise();
    }
}

void PDFRibbon::scrollPage(int direction)
{
    QScrollBar* scrollBar = m_scrollArea->horizontalScrollBar();
    scrollBar->setValue(scrollBar->value() + direction * pdf::PDFWidgetUtils::scaleDPI_x(this, 120));
}

PDFStatusBarMessageFitter::PDFStatusBarMessageFitter(QStatusBar* statusBar) :
    BaseClass(statusBar),
    m_statusBar(statusBar),
    m_isUpdating(false)
{
    connect(m_statusBar, &QStatusBar::messageChanged, this, &PDFStatusBarMessageFitter::update);
    m_statusBar->installEventFilter(this);
}

void PDFStatusBarMessageFitter::addWidgets(const QList<QWidget*>& widgets)
{
    std::vector<QPointer<QWidget>> group;
    for (QWidget* widget : widgets)
    {
        if (widget)
        {
            group.emplace_back(widget);
        }
    }

    if (!group.empty())
    {
        m_groups.push_back(std::move(group));
    }

    update();
}

void PDFStatusBarMessageFitter::update()
{
    if (m_isUpdating)
    {
        return;
    }

    m_isUpdating = true;

    auto setGroupVisible = [](const std::vector<QPointer<QWidget>>& group, bool visible)
    {
        for (const QPointer<QWidget>& widget : group)
        {
            if (widget && widget->isHidden() == visible)
            {
                widget->setVisible(visible);
            }
        }
    };

    for (const auto& group : m_groups)
    {
        setGroupVisible(group, true);
    }

    const QString message = m_statusBar->currentMessage();
    if (!message.isEmpty() && m_statusBar->isVisible() && m_statusBar->layout())
    {
        // Groups are hidden one by one, until the message fits
        const int requiredWidth = getRequiredWidth(m_statusBar, message);
        m_statusBar->layout()->invalidate();
        m_statusBar->layout()->activate();

        for (const auto& group : m_groups)
        {
            if (getMessageWidth() >= requiredWidth)
            {
                break;
            }

            setGroupVisible(group, false);
            m_statusBar->layout()->invalidate();
            m_statusBar->layout()->activate();
        }
    }

    m_isUpdating = false;
}

int PDFStatusBarMessageFitter::getMessageWidth() const
{
    // The status bar draws the message from the left side to the first visible
    // permanent widget (the size grip lies right of the permanent widgets)
    int right = m_statusBar->width() - 12;

    for (QWidget* widget : m_statusBar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!widget->isWindow() && !widget->isHidden())
        {
            right = qMin(right, widget->x() - 2);
        }
    }

    return right - 6;
}

int PDFStatusBarMessageFitter::getRequiredWidth(const QStatusBar* statusBar, const QString& message)
{
    return statusBar->fontMetrics().horizontalAdvance(message) + 2;
}

bool PDFStatusBarMessageFitter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_statusBar && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        update();
    }

    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdfviewer
