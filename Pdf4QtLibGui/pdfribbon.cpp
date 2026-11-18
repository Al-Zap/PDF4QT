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
#include <QWheelEvent>
#include <QStackedWidget>

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

    connect(this, &QToolButton::clicked, this, &PDFRibbonButton::onClicked);
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
    setVisible(m_action->isVisible());

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

    if (m_size == Size::Large)
    {
        displayedText = getWrappedText(displayedText);
    }

    // The ampersand is not a mnemonic on the button
    displayedText.replace(QChar('&'), QStringLiteral("&&"));

    if (displayedText != this->text())
    {
        setText(displayedText);
        updateGeometry();
    }
}

PDFRibbonGroup::PDFRibbonGroup(const QString& title, QWidget* parent) :
    BaseClass(parent),
    m_contentLayout(nullptr),
    m_smallColumnLayout(nullptr),
    m_iconRowLayout(nullptr),
    m_titleLabel(nullptr),
    m_smallColumnCount(0),
    m_itemCount(0)
{
    setProperty("ribbonGroup", true);
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QVBoxLayout* groupLayout = new QVBoxLayout(this);
    groupLayout->setContentsMargins(6, 3, 7, 1);
    groupLayout->setSpacing(0);

    m_contentLayout = new QHBoxLayout();
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    m_contentLayout->addStrut(pdf::PDFWidgetUtils::scaleDPI_y(this, PDFRibbonHelper::LARGE_BUTTON_HEIGHT));
    groupLayout->addLayout(m_contentLayout);

    m_titleLabel = new QLabel(PDFRibbonButton::getCleanText(title), this);
    m_titleLabel->setProperty("ribbonGroupTitle", true);
    m_titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    groupLayout->addWidget(m_titleLabel);
}

PDFRibbonButton* PDFRibbonGroup::addLargeAction(QAction* action, const QString& text)
{
    if (!action)
    {
        return nullptr;
    }

    PDFRibbonButton* button = createButton(PDFRibbonButton::Size::Large);
    button->setAction(action, text);
    placeButton(button);
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
    placeButton(button);
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
    placeButton(button);
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
    placeButton(button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, const QList<QAction*>& actions)
{
    PDFRibbonButton* button = createButton(size);
    button->setCaption(text, icon);
    button->setActionList(actions, false);
    placeButton(button);
    return button;
}

PDFRibbonButton* PDFRibbonGroup::addMenu(PDFRibbonButton::Size size, const QString& text, const QIcon& icon, QMenu* menu)
{
    PDFRibbonButton* button = createButton(size);
    button->setCaption(text, icon);
    button->setDropDownMenu(menu);
    placeButton(button);
    return button;
}

void PDFRibbonGroup::addWidget(QWidget* widget)
{
    widget->setParent(this);
    m_contentLayout->addWidget(widget, 0, Qt::AlignVCenter);
    m_smallColumnLayout = nullptr;
    m_iconRowLayout = nullptr;
    ++m_itemCount;
}

void PDFRibbonGroup::addSmallWidget(QWidget* widget)
{
    widget->setParent(this);
    placeSmallWidget(widget);
    m_iconRowLayout = nullptr;
    ++m_itemCount;
}

void PDFRibbonGroup::breakColumn()
{
    m_smallColumnLayout = nullptr;
    m_iconRowLayout = nullptr;
}

void PDFRibbonGroup::addSeparator()
{
    QFrame* line = new QFrame(this);
    line->setProperty("ribbonSeparator", true);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedWidth(1);
    m_contentLayout->addSpacing(2);
    m_contentLayout->addWidget(line);
    m_contentLayout->addSpacing(2);
    breakColumn();
}

QString PDFRibbonGroup::getTitle() const
{
    return m_titleLabel->text();
}

PDFRibbonButton* PDFRibbonGroup::createButton(PDFRibbonButton::Size size)
{
    return new PDFRibbonButton(size, this);
}

void PDFRibbonGroup::placeButton(PDFRibbonButton* button)
{
    switch (button->getButtonSize())
    {
        case PDFRibbonButton::Size::Large:
            m_contentLayout->addWidget(button, 0, Qt::AlignTop);
            m_smallColumnLayout = nullptr;
            m_iconRowLayout = nullptr;
            break;

        case PDFRibbonButton::Size::Small:
            placeSmallWidget(button);
            m_iconRowLayout = nullptr;
            break;

        case PDFRibbonButton::Size::IconOnly:
        {
            if (!m_iconRowLayout || m_iconRowLayout->count() > PDFRibbonHelper::ICON_BUTTONS_IN_ROW)
            {
                QWidget* row = new QWidget(this);
                m_iconRowLayout = new QHBoxLayout(row);
                m_iconRowLayout->setContentsMargins(0, 0, 0, 0);
                m_iconRowLayout->setSpacing(0);
                m_iconRowLayout->addStretch(1);
                QHBoxLayout* rowLayout = m_iconRowLayout;
                placeSmallWidget(row);
                m_iconRowLayout = rowLayout;
            }

            // The stretch stays the last item of the row
            m_iconRowLayout->insertWidget(m_iconRowLayout->count() - 1, button);
            break;
        }
    }

    ++m_itemCount;
}

void PDFRibbonGroup::placeSmallWidget(QWidget* widget)
{
    if (!m_smallColumnLayout || m_smallColumnCount >= PDFRibbonHelper::SMALL_BUTTONS_IN_COLUMN)
    {
        m_smallColumnLayout = new QVBoxLayout();
        m_smallColumnLayout->setContentsMargins(0, 0, 0, 0);
        m_smallColumnLayout->setSpacing(2);
        m_smallColumnLayout->addStretch(1);
        m_contentLayout->addLayout(m_smallColumnLayout);
        m_smallColumnCount = 0;
    }

    // The stretch stays the last item of the column
    m_smallColumnLayout->insertWidget(m_smallColumnLayout->count() - 1, widget);
    ++m_smallColumnCount;
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

PDFRibbon::PDFRibbon(QWidget* parent) :
    BaseClass(parent),
    m_fileButton(nullptr),
    m_tabBar(nullptr),
    m_cornerLayout(nullptr),
    m_scrollArea(nullptr),
    m_stackedWidget(nullptr),
    m_scrollLeftButton(nullptr),
    m_scrollRightButton(nullptr),
    m_isCollapsed(false)
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
    m_tabBar->addTab(PDFRibbonButton::getCleanText(title));

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

void PDFRibbon::addCornerWidget(QWidget* widget)
{
    widget->setParent(this);
    m_cornerLayout->addWidget(widget, 0, Qt::AlignVCenter);
}

PDFRibbonButton* PDFRibbon::addCornerAction(QAction* action)
{
    PDFRibbonButton* button = new PDFRibbonButton(PDFRibbonButton::Size::IconOnly, this);
    button->setAction(action);
    m_cornerLayout->addWidget(button, 0, Qt::AlignVCenter);
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
    BaseClass::resizeEvent(event);
    updateScrollButtons();
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

}   // namespace pdfviewer
