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


#include "pdftoolbarslots.h"

#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QToolButton>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFToolbarSlots::PDFToolbarSlots(QToolBar* toolbar, QMenu* advancedMenu, QObject* parent) :
    BaseClass(parent),
    m_toolbar(toolbar),
    m_advancedMenu(advancedMenu),
    m_endAnchor(nullptr)
{
    Q_ASSERT(m_toolbar);

    for (SectionData& sectionData : m_sections)
    {
        sectionData.leadingSeparator = m_toolbar->addSeparator();
    }

    // Invisible anchor behind the last section, new items of the last
    // section are inserted in front of it.
    m_endAnchor = m_toolbar->addSeparator();
    m_endAnchor->setVisible(false);

    updateSeparators();
}

std::optional<PDFToolbarSlots::Section> PDFToolbarSlots::getSectionFromName(const QString& name)
{
    const QString normalizedName = name.trimmed().toLower();

    for (int i = 0; i < int(Section::End); ++i)
    {
        const Section section = static_cast<Section>(i);
        if (getSectionName(section) == normalizedName)
        {
            return section;
        }
    }

    return std::nullopt;
}

QString PDFToolbarSlots::getSectionName(Section section)
{
    switch (section)
    {
        case Section::File:
            return QStringLiteral("file");
        case Section::View:
            return QStringLiteral("view");
        case Section::Measure:
            return QStringLiteral("measure");
        case Section::Count:
            return QStringLiteral("count");
        case Section::Forms:
            return QStringLiteral("forms");
        case Section::Organize:
            return QStringLiteral("organize");
        case Section::Markup:
            return QStringLiteral("markup");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QAction* PDFToolbarSlots::getInsertionPoint(Section section) const
{
    const int nextSectionIndex = int(section) + 1;
    if (nextSectionIndex < int(Section::End))
    {
        return m_sections[nextSectionIndex].leadingSeparator;
    }

    return m_endAnchor;
}

void PDFToolbarSlots::addAction(Section section, QAction* action)
{
    Q_ASSERT(section != Section::End);

    if (!action)
    {
        addSeparator(section);
        return;
    }

    m_toolbar->insertAction(getInsertionPoint(section), action);
    m_sections[size_t(section)].items.push_back(action);
    connect(action, &QAction::changed, this, &PDFToolbarSlots::updateSeparators, Qt::UniqueConnection);
    updateSeparators();
    Q_EMIT sectionChanged(section);
}

QAction* PDFToolbarSlots::addWidget(Section section, QWidget* widget)
{
    Q_ASSERT(section != Section::End);

    QAction* widgetAction = m_toolbar->insertWidget(getInsertionPoint(section), widget);
    m_sections[size_t(section)].items.push_back(widgetAction);
    connect(widgetAction, &QAction::changed, this, &PDFToolbarSlots::updateSeparators, Qt::UniqueConnection);
    updateSeparators();
    Q_EMIT sectionChanged(section);
    return widgetAction;
}

void PDFToolbarSlots::addSeparator(Section section)
{
    Q_ASSERT(section != Section::End);

    QAction* separator = m_toolbar->insertSeparator(getInsertionPoint(section));
    m_sections[size_t(section)].items.push_back(separator);
    updateSeparators();
}

QToolButton* PDFToolbarSlots::addActionGroup(Section section,
                                             const QString& id,
                                             const QString& text,
                                             const QList<QAction*>& actions,
                                             bool rememberLastUsed)
{
    QToolButton* button = new QToolButton(m_toolbar);
    button->setObjectName(QString("toolbarGroup_%1").arg(id));
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setAutoRaise(true);
    button->setIconSize(m_toolbar->iconSize());
    button->setToolButtonStyle(m_toolbar->toolButtonStyle());
    connect(m_toolbar, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
    connect(m_toolbar, &QToolBar::toolButtonStyleChanged, button, &QToolButton::setToolButtonStyle);

    QMenu* menu = new QMenu(text, button);
    menu->setToolTipsVisible(true);
    QAction* defaultAction = nullptr;

    for (QAction* action : actions)
    {
        if (!action)
        {
            menu->addSeparator();
            continue;
        }

        menu->addAction(action);

        // Default action is the first checkable action (a tool), or the first action
        if (!defaultAction || (!defaultAction->isCheckable() && action->isCheckable()))
        {
            defaultAction = action;
        }

        if (rememberLastUsed)
        {
            connect(action, &QAction::triggered, button, [button, action]()
            {
                if (button->defaultAction() != action)
                {
                    button->setDefaultAction(action);
                }
            });
        }
    }

    button->setMenu(menu);
    if (defaultAction)
    {
        button->setDefaultAction(defaultAction);
    }

    m_groups.push_back(button);
    addWidget(section, button);
    return button;
}

QToolButton* PDFToolbarSlots::getActionGroup(const QString& id) const
{
    const QString objectName = QString("toolbarGroup_%1").arg(id);
    for (QToolButton* button : m_groups)
    {
        if (button->objectName() == objectName)
        {
            return button;
        }
    }

    return nullptr;
}

bool PDFToolbarSlots::isSectionEmpty(Section section) const
{
    for (QAction* item : m_sections[size_t(section)].items)
    {
        if (item->isVisible() && !item->isSeparator())
        {
            return false;
        }
    }

    return true;
}

QMenu* PDFToolbarSlots::getAdvancedSubmenu(const QString& title)
{
    if (!m_advancedMenu || title.isEmpty())
    {
        return m_advancedMenu;
    }

    for (QAction* action : m_advancedMenu->actions())
    {
        if (QMenu* menu = action->menu())
        {
            if (menu->title() == title)
            {
                return menu;
            }
        }
    }

    return m_advancedMenu->addMenu(title);
}

void PDFToolbarSlots::addToAdvancedMenu(QAction* action, const QString& submenuTitle)
{
    if (!action || !m_advancedMenu)
    {
        return;
    }

    detachAction(action);
    getAdvancedSubmenu(submenuTitle)->addAction(action);
}

void PDFToolbarSlots::addMenuToAdvancedMenu(QMenu* menu)
{
    if (!menu || !m_advancedMenu)
    {
        return;
    }

    detachAction(menu->menuAction());
    m_advancedMenu->addMenu(menu);
}

void PDFToolbarSlots::detachAction(QAction* action)
{
    if (!action)
    {
        return;
    }

    const QObjectList associatedObjects = action->associatedObjects();
    for (QObject* object : associatedObjects)
    {
        if (QMenu* menu = qobject_cast<QMenu*>(object))
        {
            menu->removeAction(action);
        }
        else if (QToolBar* toolbar = qobject_cast<QToolBar*>(object))
        {
            toolbar->removeAction(action);
        }
    }
}

void PDFToolbarSlots::updateSeparators()
{
    // The separator in front of a section is visible only, if the section
    // is not empty and some section in front of it is not empty too. So
    // empty sections take no space and there are no doubled separators.
    bool hasVisibleItemBefore = false;

    for (int i = 0; i < int(Section::End); ++i)
    {
        const Section section = static_cast<Section>(i);
        const bool isEmpty = isSectionEmpty(section);
        SectionData& sectionData = m_sections[i];

        QSignalBlocker blocker(sectionData.leadingSeparator);
        sectionData.leadingSeparator->setVisible(!isEmpty && hasVisibleItemBefore);
        hasVisibleItemBefore = hasVisibleItemBefore || !isEmpty;
    }
}

}   // namespace pdfviewer
