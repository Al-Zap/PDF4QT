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


#ifndef PDFTOOLBARSLOTS_H
#define PDFTOOLBARSLOTS_H

#include "pdfviewerglobal.h"

#include <QObject>
#include <QIcon>
#include <QList>
#include <QString>

#include <array>
#include <optional>

class QMenu;
class QAction;
class QWidget;
class QToolBar;
class QToolButton;

namespace pdfviewer
{

/// Simplified main toolbar, divided into fixed sections ("slots"). Each feature
/// places its actions into the section it belongs to, so the toolbar keeps the
/// same order regardless of the order, in which the features are created.
/// Empty sections take no space (their separators are hidden).
///
/// The class also owns the Advanced menu, where the specialist functions are
/// kept, so they stay available, but do not clutter the everyday interface.
///
/// Usage from a feature:
/// \code
///   PDFToolbarSlots* slots = mainWindow->getToolbarSlots();
///   slots->addAction(PDFToolbarSlots::Section::Count, myCountAction);
///   slots->addActionGroup(PDFToolbarSlots::Section::Markup, "cloud", tr("Cloud"), icon, { a, b });
///   slots->addToAdvancedMenu(myOcrAction, tr("OCR"));
/// \endcode
class PDF4QTLIBGUILIBSHARED_EXPORT PDFToolbarSlots : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Sections of the toolbar, in the order they appear on it.
    enum class Section
    {
        File,       ///< Open, save, print, undo, redo
        View,       ///< Hand, select, page navigation, zoom, fit
        Measure,    ///< Scale, distance, area, perimeter, angle
        Count,      ///< Fixture count
        Forms,      ///< Form filling helpers
        Organize,   ///< Combine, insert, delete, rotate, extract pages
        Markup,     ///< Highlight, text box, note, shapes, cloud, stamp
        End
    };

    /// Creates the sections on the given toolbar. The toolbar should be empty,
    /// actions already present on it are kept in front of all sections.
    /// \param toolbar Main toolbar
    /// \param advancedMenu Menu used as the Advanced menu
    /// \param parent Parent object
    explicit PDFToolbarSlots(QToolBar* toolbar, QMenu* advancedMenu, QObject* parent);

    /// Converts section name ("view", "measure", "count", "forms", "organize",
    /// "markup", case insensitive) to a section. Returns nullopt for an unknown name.
    static std::optional<Section> getSectionFromName(const QString& name);

    /// Returns the name of the section (lowercase, same as recognized by getSectionFromName)
    static QString getSectionName(Section section);

    /// Appends an action to the end of the section
    void addAction(Section section, QAction* action);

    /// Appends a widget to the end of the section. Returns the toolbar action
    /// of the widget, which can be used to hide or show the widget.
    QAction* addWidget(Section section, QWidget* widget);

    /// Appends a separator inside the section
    void addSeparator(Section section);

    /// Appends a drop down button, which offers given actions. The button shows
    /// and triggers the most recently used action, the arrow opens the list.
    /// A nullptr in \p actions is a separator in the list. The default action
    /// of the button is the first checkable action (or the first action).
    /// \param section Section
    /// \param id Identifier of the group (object name of the button is "toolbarGroup_<id>")
    /// \param text Text (tooltip) of the group
    /// \param actions Actions in the group
    /// \param rememberLastUsed If true, the button switches to the last triggered action
    /// \returns The created button
    QToolButton* addActionGroup(Section section,
                                const QString& id,
                                const QString& text,
                                const QList<QAction*>& actions,
                                bool rememberLastUsed = true);

    /// Returns the drop down button created by addActionGroup, or nullptr
    QToolButton* getActionGroup(const QString& id) const;

    /// Returns true, if the section contains no visible item
    bool isSectionEmpty(Section section) const;

    /// Returns the toolbar
    QToolBar* getToolBar() const { return m_toolbar; }

    /// Returns the Advanced menu
    QMenu* getAdvancedMenu() const { return m_advancedMenu; }

    /// Returns a submenu of the Advanced menu with a given title, it is created,
    /// if it doesn't exist. An empty title returns the Advanced menu itself.
    QMenu* getAdvancedSubmenu(const QString& title);

    /// Moves the action into the Advanced menu (or its submenu). The action is
    /// removed from all other menus and toolbars of the main window.
    void addToAdvancedMenu(QAction* action, const QString& submenuTitle = QString());

    /// Moves the whole menu into the Advanced menu as a submenu
    void addMenuToAdvancedMenu(QMenu* menu);

    /// Removes the action from all menus and toolbars it is associated with
    static void detachAction(QAction* action);

signals:
    /// Emitted, when the contents of a section is changed
    void sectionChanged(pdfviewer::PDFToolbarSlots::Section section);

private:
    void updateSeparators();
    QAction* getInsertionPoint(Section section) const;

    struct SectionData
    {
        QAction* leadingSeparator = nullptr;    ///< Separator in front of the section
        QList<QAction*> items;                  ///< Items of the section (including inner separators)
    };

    QToolBar* m_toolbar;
    QMenu* m_advancedMenu;
    QAction* m_endAnchor;
    std::array<SectionData, size_t(Section::End)> m_sections;
    QList<QToolButton*> m_groups;
};

}   // namespace pdfviewer

#endif // PDFTOOLBARSLOTS_H
