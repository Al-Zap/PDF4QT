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

#ifndef PDFEDITORMAINWINDOW_H
#define PDFEDITORMAINWINDOW_H

#include "pdfglobal.h"
#include "pdfviewerglobal.h"
#include "pdfcatalog.h"
#include "pdfrenderer.h"
#include "pdfprogress.h"
#include "pdfdocument.h"
#include "pdfviewersettings.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfwidgettool.h"
#include "pdfrecentfilemanager.h"
#include "pdftexttospeech.h"
#include "pdfannotation.h"
#include "pdfform.h"
#include "pdfundoredomanager.h"
#include "pdfplugin.h"
#include "pdfprogramcontroller.h"
#include "pdfwintaskbarprogress.h"
#include "pdftoolbarslots.h"

#include <QFuture>
#include <QTreeView>
#include <QMainWindow>
#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>

#include <map>

class QLabel;
class QSpinBox;
class QSettings;
class QDoubleSpinBox;
class QToolButton;

namespace Ui
{
class PDFEditorMainWindow;
}

namespace pdf
{
class PDFAction;
class PDFWidget;
class PDFDocument;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{
class PDFSidebarWidget;
class PDFAdvancedFindWidget;

class PDF4QTLIBGUILIBSHARED_EXPORT PDFEditorMainWindow : public QMainWindow, public IMainWindow
{
    Q_OBJECT

public:
    explicit PDFEditorMainWindow(QWidget *parent = nullptr);
    virtual ~PDFEditorMainWindow() override;

    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

    PDFProgramController* getProgramController() const { return m_programController; }

    virtual void updateUI(bool fullUpdate) override;
    virtual QMenu* addToolMenu(QString name, pdf::PDFPlugin::PluginMenuLocation location) override;
    virtual void setStatusBarMessage(QString message, int time) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual void adjustToolbar(QToolBar* toolbar) override final;
    virtual pdf::PDFTextSelection getSelectedText() const override;
    virtual QMenu* addPluginUserInterface(pdf::PDFPlugin* plugin,
                                          const QString& pluginName,
                                          const std::vector<QAction*>& toolbarActions,
                                          bool& toolbarActionsPlaced) override;

    /// Returns the sections of the simplified main toolbar, into which the features
    /// place their actions. Returns nullptr, if the classic layout is used.
    PDFToolbarSlots* getToolbarSlots() const { return m_toolbarSlots; }

    /// Returns true, if the classic layout (all toolbars and menus of PDF4QT)
    /// is used instead of the simplified one. The setting takes effect after
    /// the application is restarted.
    static bool isClassicLayoutEnabled();

    /// Enables or disables the classic layout (takes effect after restart)
    static void setClassicLayoutEnabled(bool enabled);

protected:
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    void onSidebarVisibilityRequested();
    void onActionQuitTriggered();

    void onPageNumberSpinboxEditingFinished();
    void onPageZoomSpinboxEditingFinished();

    void onProgressStarted(pdf::ProgressStartupInfo info);
    void onProgressStep(int percentage);
    void onProgressFinished();

    void updateDeveloperMenu();

    void createClassicToolbar(QToolButton* insertStickyNoteButton);
    void createSimpleToolbar();
    void createAdvancedMenu();
    void setSimpleLayoutShortcuts();
    void updateHandToolAction();
    void onHandToolTriggered();
    void onClassicLayoutToggled(bool checked);

    QIcon createStickyNoteIcon(QString key) const;

    Ui::PDFEditorMainWindow* ui;
    PDFActionManager* m_actionManager;
    PDFProgramController* m_programController;

    PDFSidebarWidget* m_sidebarWidget;
    QDockWidget* m_sidebarDockWidget;
    PDFAdvancedFindWidget* m_advancedFindWidget;
    QDockWidget* m_advancedFindDockWidget;
    QSpinBox* m_pageNumberSpinBox;
    QLabel* m_pageNumberLabel;
    QDoubleSpinBox* m_pageZoomSpinBox;
    bool m_isLoadingUI;
    pdf::PDFProgress* m_progress;
    PDFWinTaskBarProgress* m_progressTaskbarIndicator;

    QProgressBar* m_progressBarOnStatusBar;
    QLabel* m_progressBarLeftLabelOnStatusBar;
    bool m_isChangingProgressStep;

    bool m_isClassicLayout;
    PDFToolbarSlots* m_toolbarSlots;
    QMenu* m_advancedMenu;
    QAction* m_handToolAction;
    QAction* m_measureShortcutAction;
    std::map<PDFToolbarSlots::Section, QMenu*> m_sectionMenus;
};

}   // namespace pdfviewer

#endif // PDFEDITORMAINWINDOW_H
