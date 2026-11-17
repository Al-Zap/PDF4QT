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

#include "pdfeditormainwindow.h"
#include "ui_pdfeditormainwindow.h"

#include "pdfaboutdialog.h"
#include "pdfsidebarwidget.h"
#include "pdfadvancedfindwidget.h"
#include "pdfviewersettingsdialog.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfrendertoimagesdialog.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"
#include "pdfstreamfilters.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfrenderingerrorswidget.h"
#include "pdffont.h"
#include "pdfitemmodels.h"
#include "pdfutils.h"
#include "pdfsendmail.h"
#include "pdfexecutionpolicy.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfsignaturehandler.h"
#include "pdfadvancedtools.h"
#include "pdfwidgetutils.h"
#include "pdfactioncombobox.h"
#include "pdftoolbarslots.h"

#include <QPainter>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QStandardPaths>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTreeView>
#include <QLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QSpinBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLockFile>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtConcurrent/QtConcurrent>
#include <QToolButton>
#include <QActionGroup>
#include <QSettings>
#include <QMenuBar>

#include <algorithm>

#include "pdfdbgheap.h"

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdfviewer
{

PDFEditorMainWindow::PDFEditorMainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::PDFEditorMainWindow),
    m_actionManager(new PDFActionManager(this)),
    m_programController(new PDFProgramController(this)),
    m_sidebarWidget(nullptr),
    m_sidebarDockWidget(nullptr),
    m_advancedFindWidget(nullptr),
    m_advancedFindDockWidget(nullptr),
    m_pageNumberSpinBox(nullptr),
    m_pageNumberLabel(nullptr),
    m_pageZoomSpinBox(nullptr),
    m_isLoadingUI(false),
    m_progress(new pdf::PDFProgress(this)),
    m_progressTaskbarIndicator(new PDFWinTaskBarProgress(this)),
    m_progressBarOnStatusBar(nullptr),
    m_progressBarLeftLabelOnStatusBar(nullptr),
    m_isChangingProgressStep(false),
    m_isClassicLayout(isClassicLayoutEnabled()),
    m_toolbarSlots(nullptr),
    m_advancedMenu(nullptr),
    m_handToolAction(nullptr),
    m_measureShortcutAction(nullptr)
{
    ui->setupUi(this);

    setAcceptDrops(true);

    // Initialize toolbar icon size
    adjustToolbar(ui->mainToolBar);
    ui->mainToolBar->setWindowTitle(tr("Standard"));

    // Initialize status bar
    m_progressBarOnStatusBar = new QProgressBar(this);
    m_progressBarOnStatusBar->setHidden(true);
    m_progressBarLeftLabelOnStatusBar = new QLabel(this);
    m_progressBarLeftLabelOnStatusBar->setHidden(true);
    statusBar()->addPermanentWidget(m_progressBarLeftLabelOnStatusBar);
    statusBar()->addPermanentWidget(m_progressBarOnStatusBar);

    // Initialize actions
    m_actionManager->setAction(PDFActionManager::Open, ui->actionOpen);
    m_actionManager->setAction(PDFActionManager::Close, ui->actionClose);
    m_actionManager->setAction(PDFActionManager::AutomaticDocumentRefresh, ui->actionAutomaticDocumentRefresh);
    m_actionManager->setAction(PDFActionManager::Quit, ui->actionQuit);
    m_actionManager->setAction(PDFActionManager::ZoomIn, ui->actionZoom_In);
    m_actionManager->setAction(PDFActionManager::ZoomOut, ui->actionZoom_Out);
    m_actionManager->setAction(PDFActionManager::Find, ui->actionFind);
    m_actionManager->setAction(PDFActionManager::FindPrevious, ui->actionFindPrevious);
    m_actionManager->setAction(PDFActionManager::FindNext, ui->actionFindNext);
    m_actionManager->setAction(PDFActionManager::SelectTextAll, ui->actionSelectTextAll);
    m_actionManager->setAction(PDFActionManager::DeselectText, ui->actionDeselectText);
    m_actionManager->setAction(PDFActionManager::CopyText, ui->actionCopyText);
    m_actionManager->setAction(PDFActionManager::RotateRight, ui->actionRotateRight);
    m_actionManager->setAction(PDFActionManager::RotateLeft, ui->actionRotateLeft);
    m_actionManager->setAction(PDFActionManager::Print, ui->actionPrint);
    m_actionManager->setAction(PDFActionManager::Undo, ui->actionUndo);
    m_actionManager->setAction(PDFActionManager::Redo, ui->actionRedo);
    m_actionManager->setAction(PDFActionManager::Save, ui->actionSave);
    m_actionManager->setAction(PDFActionManager::SaveAs, ui->actionSave_As);
    m_actionManager->setAction(PDFActionManager::SaveFlattenedCopy, ui->actionSaveFlattenedCopy);
    m_actionManager->setAction(PDFActionManager::GoToDocumentStart, ui->actionGoToDocumentStart);
    m_actionManager->setAction(PDFActionManager::GoToDocumentEnd, ui->actionGoToDocumentEnd);
    m_actionManager->setAction(PDFActionManager::GoToNextPage, ui->actionGoToNextPage);
    m_actionManager->setAction(PDFActionManager::GoToPreviousPage, ui->actionGoToPreviousPage);
    m_actionManager->setAction(PDFActionManager::GoToNextLine, ui->actionGoToNextLine);
    m_actionManager->setAction(PDFActionManager::GoToPreviousLine, ui->actionGoToPreviousLine);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteComment, ui->actionStickyNoteComment);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteHelp, ui->actionStickyNoteHelp);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteInsert, ui->actionStickyNoteInsert);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteKey, ui->actionStickyNoteKey);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNewParagraph, ui->actionStickyNoteNewParagraph);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNote, ui->actionStickyNoteNote);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteParagraph, ui->actionStickyNoteParagraph);
    m_actionManager->setAction(PDFActionManager::CreateTextHighlight, ui->actionCreateTextHighlight);
    m_actionManager->setAction(PDFActionManager::CreateTextUnderline, ui->actionCreateTextUnderline);
    m_actionManager->setAction(PDFActionManager::CreateTextStrikeout, ui->actionCreateTextStrikeout);
    m_actionManager->setAction(PDFActionManager::CreateTextSquiggly, ui->actionCreateTextSquiggly);
    m_actionManager->setAction(PDFActionManager::CreateHyperlink, ui->actionCreateHyperlink);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFit, ui->actionCreateHyperlinkToThisPDFFit);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitH, ui->actionCreateHyperlinkToThisPDFFitH);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitV, ui->actionCreateHyperlinkToThisPDFFitV);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitR, ui->actionCreateHyperlinkToThisPDFFitR);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitB, ui->actionCreateHyperlinkToThisPDFFitB);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitBH, ui->actionCreateHyperlinkToThisPDFFitBH);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitBV, ui->actionCreateHyperlinkToThisPDFFitBV);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFXYZ, ui->actionCreateHyperlinkToThisPDFXYZ);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFXYZInheritZoom, ui->actionCreateHyperlinkToThisPDFXYZInheritZoom);
    m_actionManager->setAction(PDFActionManager::InsertPageNumbers, ui->actionInsertPageNumbers);
    m_actionManager->setAction(PDFActionManager::CreateInlineText, ui->actionInlineText);
    m_actionManager->setAction(PDFActionManager::CreateTypewriter, ui->actionCreateTypewriter);
    m_actionManager->setAction(PDFActionManager::CreateStraightLine, ui->actionCreateStraightLine);
    m_actionManager->setAction(PDFActionManager::CreatePolyline, ui->actionCreatePolyline);
    m_actionManager->setAction(PDFActionManager::CreateRectangle, ui->actionCreateRectangle);
    m_actionManager->setAction(PDFActionManager::CreatePolygon, ui->actionCreatePolygon);
    m_actionManager->setAction(PDFActionManager::CreateEllipse, ui->actionCreateEllipse);
    m_actionManager->setAction(PDFActionManager::CreateArrow, ui->actionCreateArrow);
    m_actionManager->setAction(PDFActionManager::CreateCloudRectangle, ui->actionCreateCloudRectangle);
    m_actionManager->setAction(PDFActionManager::CreateCloudPolygon, ui->actionCreateCloudPolygon);
    m_actionManager->setAction(PDFActionManager::CreateFreehandCurve, ui->actionCreateFreehandCurve);
    m_actionManager->setAction(PDFActionManager::DeleteAnnotation, ui->actionDeleteAnnotation);
    m_actionManager->setAction(PDFActionManager::RenderOptionAntialiasing, ui->actionRenderOptionAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionTextAntialiasing, ui->actionRenderOptionTextAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionSmoothPictures, ui->actionRenderOptionSmoothPictures);
    m_actionManager->setAction(PDFActionManager::RenderOptionIgnoreOptionalContentSettings, ui->actionRenderOptionIgnoreOptionalContentSettings);
    m_actionManager->setAction(PDFActionManager::RenderOptionDisplayRenderTimes, ui->actionRenderOptionDisplayRenderTimes);
    m_actionManager->setAction(PDFActionManager::RenderOptionDisplayAnnotations, ui->actionRenderOptionDisplayAnnotations);
    m_actionManager->setAction(PDFActionManager::RenderOptionInvertColors, ui->actionColorInvert);
    m_actionManager->setAction(PDFActionManager::RenderOptionGrayscale, ui->actionColorGrayscale);
    m_actionManager->setAction(PDFActionManager::RenderOptionHighContrast, ui->actionColorHighContrast);
    m_actionManager->setAction(PDFActionManager::RenderOptionBitonal, ui->actionColorBitonal);
    m_actionManager->setAction(PDFActionManager::RenderOptionCustomColors, ui->actionColorCustom);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextBlocks, ui->actionShow_Text_Blocks);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextLines, ui->actionShow_Text_Lines);
    m_actionManager->setAction(PDFActionManager::Properties, ui->actionProperties);
    m_actionManager->setAction(PDFActionManager::Options, ui->actionOptions);
    m_actionManager->setAction(PDFActionManager::ResetToFactorySettings, ui->actionResetToFactorySettings);
    m_actionManager->setAction(PDFActionManager::ClearRecentFileHistory, ui->actionClearRecentFileHistory);
    m_actionManager->setAction(PDFActionManager::CertificateManager, ui->actionCertificateManager);
    m_actionManager->setAction(PDFActionManager::GetSource, ui->actionGetSource);
    m_actionManager->setAction(PDFActionManager::BecomeSponsor, ui->actionBecomeASponsor);
    m_actionManager->setAction(PDFActionManager::About, ui->actionAbout);
    m_actionManager->setAction(PDFActionManager::SendByMail, ui->actionSend_by_E_Mail);
    m_actionManager->setAction(PDFActionManager::RenderToImages, ui->actionRender_to_Images);
    m_actionManager->setAction(PDFActionManager::ExportToWord, ui->actionExport_to_Word);
    m_actionManager->setAction(PDFActionManager::Optimize, ui->actionOptimize);
    m_actionManager->setAction(PDFActionManager::OptimizeImages, ui->actionOptimizeImages);
    m_actionManager->setAction(PDFActionManager::Sanitize, ui->actionSanitize);
    m_actionManager->setAction(PDFActionManager::RemoveExternalLinks, ui->actionRemoveExternalLinks);
    m_actionManager->setAction(PDFActionManager::PageGeometry, ui->actionPageGeometry);
    m_actionManager->setAction(PDFActionManager::CreateBitonalDocument, ui->actionCreateBitonalDocument);
    m_actionManager->setAction(PDFActionManager::RecognizeText, ui->actionRecognizeText);
    m_actionManager->setAction(PDFActionManager::ManageOCRLanguages, ui->actionManageOCRLanguages);
    m_actionManager->setAction(PDFActionManager::PrepareScannedPages, ui->actionPrepareScannedPages);
    m_actionManager->setAction(PDFActionManager::BatchRecognizeText, ui->actionBatchRecognizeText);
    m_actionManager->setAction(PDFActionManager::MakeSearchable, ui->actionMakeSearchable);
    m_actionManager->setAction(PDFActionManager::Encryption, ui->actionEncryption);
    m_actionManager->setAction(PDFActionManager::FitPage, ui->actionFitPage);
    m_actionManager->setAction(PDFActionManager::FitWidth, ui->actionFitWidth);
    m_actionManager->setAction(PDFActionManager::FitHeight, ui->actionFitHeight);
    m_actionManager->setAction(PDFActionManager::ShowRenderingErrors, ui->actionRendering_Errors);
    m_actionManager->setAction(PDFActionManager::PageLayoutSinglePage, ui->actionPageLayoutSinglePage);
    m_actionManager->setAction(PDFActionManager::PageLayoutContinuous, ui->actionPageLayoutContinuous);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoPages, ui->actionPageLayoutTwoPages);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoColumns, ui->actionPageLayoutTwoColumns);
    m_actionManager->setAction(PDFActionManager::PageLayoutFirstPageOnRightSide, ui->actionFirstPageOnRightSide);
    m_actionManager->setAction(PDFActionManager::FullscreenMode, ui->actionFullscreenMode);
    m_actionManager->setAction(PDFActionManager::ToolSelectText, ui->actionSelectText);
    m_actionManager->setAction(PDFActionManager::ToolSelectTable, ui->actionSelectTable);
    m_actionManager->setAction(PDFActionManager::ToolMagnifier, ui->actionMagnifier);
    m_actionManager->setAction(PDFActionManager::ToolScreenshot, ui->actionScreenshot);
    m_actionManager->setAction(PDFActionManager::ToolExtractImage, ui->actionExtractImage);
    m_actionManager->setAction(PDFActionManager::BookmarkPage, ui->actionBookmarkPage);
    m_actionManager->setAction(PDFActionManager::BookmarkGoToNext, ui->actionGotoNextBookmark);
    m_actionManager->setAction(PDFActionManager::BookmarkGoToPrevious, ui->actionGotoPreviousBookmark);
    m_actionManager->setAction(PDFActionManager::BookmarkExport, ui->actionBookmarkExport);
    m_actionManager->setAction(PDFActionManager::BookmarkImport, ui->actionBookmarkImport);
    m_actionManager->setAction(PDFActionManager::BookmarkGenerateAutomatically, ui->actionBookmarkAutoGenerate);
    m_actionManager->initActions(pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24)), true);

    for (QAction* action : m_programController->getRecentFileManager()->getActions())
    {
        ui->menuFile->insertAction(ui->actionClearRecentFileHistory, action);
    }
    m_programController->getRecentFileManager()->setClearRecentFileHistoryAction(ui->actionClearRecentFileHistory);
    ui->menuFile->insertSeparator(ui->actionQuit);

    connect(ui->actionQuit, &QAction::triggered, this, &PDFEditorMainWindow::onActionQuitTriggered);

    m_pageNumberSpinBox = new QSpinBox(this);
    m_pageNumberSpinBox->setObjectName("pageNumberSpinBox");
    m_pageNumberLabel = new QLabel(this);
    m_pageNumberLabel->setObjectName("pageNumberLabel");
    m_pageNumberSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
    m_pageNumberSpinBox->setAlignment(Qt::AlignCenter);
    connect(m_pageNumberSpinBox, &QSpinBox::editingFinished, this, &PDFEditorMainWindow::onPageNumberSpinboxEditingFinished);

    m_pageZoomSpinBox = new QDoubleSpinBox(this);
    m_pageZoomSpinBox->setObjectName("pageZoomSpinBox");
    m_pageZoomSpinBox->setMinimum(pdf::PDFDrawWidgetProxy::getMinZoom() * 100);
    m_pageZoomSpinBox->setMaximum(pdf::PDFDrawWidgetProxy::getMaxZoom() * 100);
    m_pageZoomSpinBox->setDecimals(2);
    m_pageZoomSpinBox->setSuffix(tr("%"));
    m_pageZoomSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
    m_pageZoomSpinBox->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    connect(m_pageZoomSpinBox, &QDoubleSpinBox::editingFinished, this, &PDFEditorMainWindow::onPageZoomSpinboxEditingFinished);

    for (QAction* action : m_actionManager->getActionGroup(PDFActionManager::CreateStampGroup)->actions())
    {
        ui->menuStamp->addAction(action);
    }

    if (m_isClassicLayout)
    {
        QToolButton* insertStickyNoteButton = m_actionManager->createToolButtonForActionGroup(PDFActionManager::CreateStickyNoteGroup, ui->mainToolBar);
        createClassicToolbar(insertStickyNoteButton);
    }
    else
    {
        createSimpleToolbar();
    }

    // Switch between the simplified and the classic layout
    QAction* classicLayoutAction = new QAction(tr("C&lassic Toolbars and Menus"), this);
    classicLayoutAction->setObjectName("actionClassicLayout");
    classicLayoutAction->setCheckable(true);
    classicLayoutAction->setChecked(m_isClassicLayout);
    classicLayoutAction->setToolTip(tr("Show all toolbars and menus of PDF4QT instead of the simplified toolbar (takes effect after restart)"));
    connect(classicLayoutAction, &QAction::toggled, this, &PDFEditorMainWindow::onClassicLayoutToggled);
    ui->menuView->addSeparator();
    ui->menuView->addAction(classicLayoutAction);

    m_programController->initialize(PDFProgramController::AllFeatures, this, this, m_actionManager, m_progress);
    setCentralWidget(m_programController->getPdfWidget());

    if (pdf::PDFToolManager* toolManager = m_programController->getToolManager())
    {
        connect(toolManager, &pdf::PDFToolManager::activeToolChanged, this, &PDFEditorMainWindow::updateHandToolAction);
        updateHandToolAction();
    }
    setFocusProxy(m_programController->getPdfWidget());

    m_sidebarWidget = new PDFSidebarWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), m_programController->getTextToSpeech(), m_programController->getCertificateStore(), m_programController->getBookmarkManager(), m_programController->getSettings(), true, this);
    m_sidebarDockWidget = new QDockWidget(tr("&Sidebar"), this);
    m_sidebarDockWidget->setObjectName("SidebarDockWidget");
    m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDockWidget->setWidget(m_sidebarWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDockWidget);
    m_sidebarDockWidget->hide();
    connect(m_sidebarWidget, &PDFSidebarWidget::actionTriggered, m_programController, &PDFProgramController::onActionTriggered);
    connect(m_sidebarWidget, &PDFSidebarWidget::documentModified, m_programController, &PDFProgramController::onDocumentModified);
    connect(m_sidebarWidget, &PDFSidebarWidget::sidebarVisibilityRequested, this, &PDFEditorMainWindow::onSidebarVisibilityRequested);
    for (QAction* action : m_sidebarWidget->getOutlineActions())
    {
        m_actionManager->addAdditionalAction(action);
    }

    // Outline items are created directly from the document being read, so the action
    // must be reachable from the menu (and by its shortcut) without opening the sidebar
    if (QAction* outlineNewItemAction = m_sidebarWidget->getOutlineNewItemAction())
    {
        ui->menuInsert->addSeparator();
        ui->menuInsert->addAction(outlineNewItemAction);
    }

    // Organizing of the pages (rotate, delete, insert, extract, split) works on the selected
    // thumbnails or on the current page, so it is reachable also from the menu.
    const std::vector<QAction*> pageActions = m_sidebarWidget->getPageActions();
    if (!pageActions.empty())
    {
        QMenu* pagesMenu = new QMenu(tr("&Pages"), this);
        pagesMenu->setObjectName("menuPages");
        for (size_t i = 0; i < pageActions.size(); ++i)
        {
            QAction* action = pageActions[i];
            const QString name = action->objectName();
            if (name == "actionPagesInsertFromFile" || name == "actionPagesExtract" || name == "actionPagesDelete")
            {
                pagesMenu->addSeparator();
            }
            pagesMenu->addAction(action);
            m_actionManager->addAdditionalAction(action);
        }
        menuBar()->insertMenu(ui->menuInsert->menuAction(), pagesMenu);

        // Simplified toolbar: rotation as buttons, other page actions in a drop-down
        if (m_toolbarSlots && pageActions.size() >= 2)
        {
            using Section = PDFToolbarSlots::Section;
            m_toolbarSlots->addAction(Section::Organize, pageActions[0]);
            m_toolbarSlots->addAction(Section::Organize, pageActions[1]);
            m_toolbarSlots->addActionGroup(Section::Organize, "pages", tr("Pages"), QList<QAction*>(std::next(pageActions.cbegin(), 2), pageActions.cend()), false);
        }
    }

    m_advancedFindWidget = new PDFAdvancedFindWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), this);
    m_advancedFindDockWidget = new QDockWidget(tr("Advanced find"), this);
    m_advancedFindDockWidget->setObjectName("AdvancedFind");
    m_advancedFindDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_advancedFindDockWidget->setWidget(m_advancedFindWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_advancedFindDockWidget);
    m_advancedFindDockWidget->hide();
    QAction* toggleAdvancedFindAction = m_advancedFindDockWidget->toggleViewAction();
    toggleAdvancedFindAction->setObjectName("actionAdvancedFind");
    toggleAdvancedFindAction->setText(tr("Ad&vanced Find..."));
    toggleAdvancedFindAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    toggleAdvancedFindAction->setIcon(QIcon(":/resources/find-advanced.svg"));
    ui->menuEdit->insertAction(nullptr, toggleAdvancedFindAction);
    m_actionManager->addAdditionalAction(m_advancedFindDockWidget->toggleViewAction());

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");
    m_actionManager->addAdditionalAction(m_sidebarDockWidget->toggleViewAction());

    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFEditorMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFEditorMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFEditorMainWindow::onProgressFinished);

    PDFActionComboBox* actionComboBox = new PDFActionComboBox(this);
    menuBar()->setCornerWidget(actionComboBox);

    // On the first start (no saved window geometry), plan sheets get the whole screen
    const bool isFirstStart = !QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()).contains("geometry");

    m_programController->finishInitialization();

    if (!m_isClassicLayout && isFirstStart)
    {
        setWindowState(windowState() | Qt::WindowMaximized);
    }
    updateDeveloperMenu();

    if (pdf::PDFToolManager* toolManager = m_programController->getToolManager())
    {
        connect(toolManager, &pdf::PDFToolManager::messageDisplayRequest, statusBar(), &QStatusBar::showMessage);
    }

    m_actionManager->styleActions();
    m_programController->initActionComboBox(actionComboBox);

#ifndef NDEBUG
    pdf::PDFWidgetUtils::checkMenuAccessibility(this);
#endif
}

PDFEditorMainWindow::~PDFEditorMainWindow()
{
    delete m_programController;
    m_programController = nullptr;

    delete m_actionManager;
    m_actionManager = nullptr;

    delete ui;
}

void PDFEditorMainWindow::createClassicToolbar(QToolButton* insertStickyNoteButton)
{
    // Page control
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionGoToDocumentStart);
    ui->mainToolBar->addAction(ui->actionGoToPreviousPage);
    ui->mainToolBar->addWidget(m_pageNumberSpinBox);
    ui->mainToolBar->addWidget(m_pageNumberLabel);
    ui->mainToolBar->addAction(ui->actionGoToNextPage);
    ui->mainToolBar->addAction(ui->actionGoToDocumentEnd);

    // Zoom
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionZoom_In);
    ui->mainToolBar->addAction(ui->actionZoom_Out);

    ui->mainToolBar->addWidget(m_pageZoomSpinBox);

    // Fit page, width, height
    ui->mainToolBar->addAction(ui->actionFitPage);
    ui->mainToolBar->addAction(ui->actionFitWidth);
    ui->mainToolBar->addAction(ui->actionFitHeight);
    ui->mainToolBar->addSeparator();

    // Tools
    ui->mainToolBar->addAction(ui->actionSelectText);
    ui->mainToolBar->addAction(ui->actionSelectTable);
    ui->mainToolBar->addAction(ui->actionCreateTextHighlight);
    ui->mainToolBar->addAction(ui->actionCreateTextUnderline);
    ui->mainToolBar->addAction(ui->actionCreateTextStrikeout);
    ui->mainToolBar->addAction(ui->actionCreateTextSquiggly);
    ui->mainToolBar->addAction(ui->actionMagnifier);
    ui->mainToolBar->addAction(ui->actionScreenshot);
    ui->mainToolBar->addAction(ui->actionExtractImage);
    ui->mainToolBar->addSeparator();

    // Special tools
    ui->mainToolBar->addWidget(insertStickyNoteButton);
    ui->mainToolBar->addSeparator();
}

void PDFEditorMainWindow::createSimpleToolbar()
{
    using Section = PDFToolbarSlots::Section;

    QToolBar* toolbar = ui->mainToolBar;
    toolbar->clear();
    toolbar->setObjectName("simpleToolBar");
    toolbar->setWindowTitle(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);

    m_advancedMenu = new QMenu(tr("&Advanced"), this);
    m_advancedMenu->setObjectName("menuAdvanced");
    m_advancedMenu->setToolTipsVisible(true);
    menuBar()->insertMenu(ui->menuHelp->menuAction(), m_advancedMenu);

    m_toolbarSlots = new PDFToolbarSlots(toolbar, m_advancedMenu, this);

    // File: open, save, print, undo, redo
    m_toolbarSlots->addAction(Section::File, ui->actionOpen);
    m_toolbarSlots->addAction(Section::File, ui->actionSave);
    m_toolbarSlots->addAction(Section::File, ui->actionPrint);
    m_toolbarSlots->addAction(Section::File, ui->actionMakeSearchable);
    m_toolbarSlots->addSeparator(Section::File);
    m_toolbarSlots->addAction(Section::File, ui->actionUndo);
    m_toolbarSlots->addAction(Section::File, ui->actionRedo);

    // View: hand, select text, page navigation, zoom
    m_handToolAction = new QAction(QIcon(":/resources/hand-tool.svg"), tr("&Hand"), this);
    m_handToolAction->setObjectName("actionHandTool");
    m_handToolAction->setCheckable(true);
    m_handToolAction->setChecked(true);
    m_handToolAction->setToolTip(tr("Hand: drag to pan, click to fill form fields and follow links (H)"));
    connect(m_handToolAction, &QAction::triggered, this, &PDFEditorMainWindow::onHandToolTriggered);
    m_actionManager->addAdditionalAction(m_handToolAction);
    ui->menuEdit->insertAction(ui->actionSelectText, m_handToolAction);

    m_toolbarSlots->addAction(Section::View, m_handToolAction);
    m_toolbarSlots->addAction(Section::View, ui->actionSelectText);
    m_toolbarSlots->addSeparator(Section::View);
    m_toolbarSlots->addAction(Section::View, ui->actionGoToPreviousPage);
    m_toolbarSlots->addWidget(Section::View, m_pageNumberSpinBox);
    m_toolbarSlots->addWidget(Section::View, m_pageNumberLabel);
    m_toolbarSlots->addAction(Section::View, ui->actionGoToNextPage);
    m_toolbarSlots->addSeparator(Section::View);
    m_toolbarSlots->addAction(Section::View, ui->actionZoom_Out);
    m_toolbarSlots->addWidget(Section::View, m_pageZoomSpinBox);
    m_toolbarSlots->addAction(Section::View, ui->actionZoom_In);
    m_toolbarSlots->addActionGroup(Section::View, "fit", tr("Fit"), { ui->actionFitPage, ui->actionFitWidth, ui->actionFitHeight });

    // Forms
    m_toolbarSlots->addAction(Section::Forms, ui->actionCreateTypewriter);
    m_toolbarSlots->addAction(Section::Forms, ui->actionSaveFlattenedCopy);

    // Measure, Count, Organize and further Forms items are filled by the features (plugins
    // return the section by PDFPlugin::getToolbarSection, other code uses
    // getToolbarSlots()). Empty sections are not displayed.

    // Markup
    m_toolbarSlots->addActionGroup(Section::Markup, "highlight", tr("Highlight"),
                                   { ui->actionCreateTextHighlight, ui->actionCreateTextUnderline, ui->actionCreateTextStrikeout, ui->actionCreateTextSquiggly });
    m_toolbarSlots->addAction(Section::Markup, ui->actionInlineText);
    if (QActionGroup* stickyNoteGroup = m_actionManager->getActionGroup(PDFActionManager::CreateStickyNoteGroup))
    {
        m_toolbarSlots->addActionGroup(Section::Markup, "stickyNote", tr("Sticky Note"), stickyNoteGroup->actions());
    }
    m_toolbarSlots->addActionGroup(Section::Markup, "cloud", tr("Revision Cloud"),
                                   { ui->actionCreateCloudRectangle, ui->actionCreateCloudPolygon });
    m_toolbarSlots->addActionGroup(Section::Markup, "shapes", tr("Shapes"),
                                   { ui->actionCreateStraightLine, ui->actionCreateArrow, ui->actionCreatePolyline, ui->actionCreateRectangle,
                                     ui->actionCreatePolygon, ui->actionCreateEllipse, ui->actionCreateFreehandCurve });
    // Stamps have no icons, they stay in the Markup menu only

    // Shortcut for the measure tool used last (the button of the measure section)
    m_measureShortcutAction = new QAction(tr("Measure (Last Used Tool)"), this);
    m_measureShortcutAction->setObjectName("actionMeasureLastUsed");
    connect(m_measureShortcutAction, &QAction::triggered, this, [this]()
    {
        if (QToolButton* button = m_toolbarSlots->getActionGroup(PDFToolbarSlots::getSectionName(PDFToolbarSlots::Section::Measure)))
        {
            if (QAction* action = button->defaultAction())
            {
                action->trigger();
            }
        }
    });
    addAction(m_measureShortcutAction);
    m_actionManager->addAdditionalAction(m_measureShortcutAction);

    ui->menuInsert->setTitle(tr("&Markup"));

    createAdvancedMenu();
    setSimpleLayoutShortcuts();

    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar#simpleToolBar { border: none; border-bottom: 1px solid palette(mid); padding: 2px 6px; spacing: 2px; }"
        "QToolBar#simpleToolBar QToolButton { padding: 3px; border-radius: 4px; }"
        "QToolBar#simpleToolBar QToolButton:checked { background: rgba(0, 120, 215, 40); border: 1px solid rgba(0, 120, 215, 140); }"));
}

void PDFEditorMainWindow::createAdvancedMenu()
{
    // Specialist functions are kept, but moved out of the everyday menus.
    // Submenus are created in this order, plugins without a toolbar section
    // are placed into the Plugins submenu.
    const QString ocrMenu = tr("&OCR (Scanned Documents)");
    const QString documentMenu = tr("&Document");
    const QString linksMenu = tr("&Links");
    const QString toolsMenu = tr("&Tools");
    const QString displayMenu = tr("Di&splay");
    const QString exportMenu = tr("&Export");

    m_toolbarSlots->addToAdvancedMenu(ui->actionPrepareScannedPages, ocrMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionRecognizeText, ocrMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionBatchRecognizeText, ocrMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionManageOCRLanguages, ocrMenu);

    // One click OCR stays in the File menu and on the toolbar, it is offered
    // also at the top of the OCR submenu (not detached from the File menu)
    ui->menuEdit->removeAction(ui->actionMakeSearchable);
    if (QMenu* ocrSubmenu = m_toolbarSlots->getAdvancedSubmenu(ocrMenu))
    {
        ocrSubmenu->insertAction(ui->actionPrepareScannedPages, ui->actionMakeSearchable);
    }

    m_toolbarSlots->addToAdvancedMenu(ui->actionEncryption, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionOptimize, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionOptimizeImages, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionSanitize, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionRemoveExternalLinks, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionPageGeometry, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionCreateBitonalDocument, documentMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionInsertPageNumbers, documentMenu);

    m_toolbarSlots->addToAdvancedMenu(ui->actionCreateHyperlink, linksMenu);
    PDFToolbarSlots::detachAction(ui->menuHyperlinkToThisPDF->menuAction());
    m_toolbarSlots->getAdvancedSubmenu(linksMenu)->addMenu(ui->menuHyperlinkToThisPDF);

    m_toolbarSlots->addToAdvancedMenu(ui->actionSelectTable, toolsMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionMagnifier, toolsMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionScreenshot, toolsMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionExtractImage, toolsMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionCertificateManager, toolsMenu);

    PDFToolbarSlots::detachAction(ui->menuRendering_Options->menuAction());
    m_toolbarSlots->getAdvancedSubmenu(displayMenu)->addMenu(ui->menuRendering_Options);
    m_toolbarSlots->addToAdvancedMenu(ui->actionColorInvert, displayMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionColorGrayscale, displayMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionColorHighContrast, displayMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionColorBitonal, displayMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionColorCustom, displayMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionRendering_Errors, displayMenu);

    m_toolbarSlots->addToAdvancedMenu(ui->actionRender_to_Images, exportMenu);
    m_toolbarSlots->addToAdvancedMenu(ui->actionSend_by_E_Mail, exportMenu);

    m_toolbarSlots->getAdvancedSubmenu(tr("&Plugins"));
}

void PDFEditorMainWindow::setSimpleLayoutShortcuts()
{
    // Shortcuts follow the common conventions of PDF viewers (Acrobat, Bluebeam,
    // Power PDF), so habits carry over. Single letter shortcuts do not interfere
    // with typing into form fields, the form editor overrides them. Shortcuts
    // changed by the user (Tools > Options) are restored after this.
    ui->actionZoom_In->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal) });
    ui->actionZoom_Out->setShortcuts({ QKeySequence::ZoomOut });
    ui->actionFitPage->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_0) });
    ui->actionFitWidth->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_2), QKeySequence(Qt::Key_W) });
    ui->actionFitHeight->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_3) });

    // Ctrl+1..5 are used by the zoom commands above, the page layout is
    // changed rarely, it stays available in the View menu.
    ui->actionPageLayoutSinglePage->setShortcut(QKeySequence());
    ui->actionPageLayoutContinuous->setShortcut(QKeySequence());
    ui->actionPageLayoutTwoPages->setShortcut(QKeySequence());
    ui->actionPageLayoutTwoColumns->setShortcut(QKeySequence());
    ui->actionFirstPageOnRightSide->setShortcut(QKeySequence());

    m_handToolAction->setShortcut(QKeySequence(Qt::Key_H));
    ui->actionSelectText->setShortcut(QKeySequence(Qt::Key_V));
    m_measureShortcutAction->setShortcut(QKeySequence(Qt::Key_M));

    ui->actionCreateTextHighlight->setShortcut(QKeySequence(Qt::Key_U));
    ui->actionInlineText->setShortcut(QKeySequence(Qt::Key_T));
    ui->actionStickyNoteComment->setShortcut(QKeySequence(Qt::Key_N));
    ui->actionCreateStraightLine->setShortcut(QKeySequence(Qt::Key_L));
    ui->actionCreateCloudRectangle->setShortcut(QKeySequence(Qt::Key_C));
    ui->actionCreateRectangle->setShortcut(QKeySequence(Qt::Key_R));
    ui->actionCreateEllipse->setShortcut(QKeySequence(Qt::Key_E));
    ui->actionCreateFreehandCurve->setShortcut(QKeySequence(Qt::Key_F));
    ui->actionCreatePolyline->setShortcut(QKeySequence(Qt::Key_P));
}

void PDFEditorMainWindow::updateHandToolAction()
{
    if (!m_handToolAction)
    {
        return;
    }

    pdf::PDFToolManager* toolManager = m_programController->getToolManager();
    m_handToolAction->setChecked(!toolManager || !toolManager->getActiveTool());
}

void PDFEditorMainWindow::onHandToolTriggered()
{
    if (pdf::PDFToolManager* toolManager = m_programController->getToolManager())
    {
        toolManager->setActiveTool(nullptr);
    }

    // Hand is the state without an active tool, it cannot be unchecked by clicking
    updateHandToolAction();
}

bool PDFEditorMainWindow::isClassicLayoutEnabled()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("UserInterface");
    return settings.value("classicLayout", false).toBool();
}

void PDFEditorMainWindow::setClassicLayoutEnabled(bool enabled)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("UserInterface");
    settings.setValue("classicLayout", enabled);
}

void PDFEditorMainWindow::onClassicLayoutToggled(bool checked)
{
    setClassicLayoutEnabled(checked);
    QMessageBox::information(this, tr("Toolbars and Menus"), tr("The change takes effect after the application is restarted."));
}

QMenu* PDFEditorMainWindow::addPluginUserInterface(pdf::PDFPlugin* plugin,
                                                   const QString& pluginName,
                                                   const std::vector<QAction*>& toolbarActions,
                                                   bool& toolbarActionsPlaced)
{
    if (m_isClassicLayout || !m_toolbarSlots)
    {
        return IMainWindow::addPluginUserInterface(plugin, pluginName, toolbarActions, toolbarActionsPlaced);
    }

    // In the simplified layout, plugins never get a toolbar of their own
    toolbarActionsPlaced = true;

    std::optional<PDFToolbarSlots::Section> section = PDFToolbarSlots::getSectionFromName(plugin->getToolbarSection());
    if (!section)
    {
        // Advanced plugin: menu only, in the Advanced menu
        return m_toolbarSlots->getAdvancedSubmenu(tr("&Plugins"))->addMenu(plugin->getPluginMenuName());
    }

    // Toolbar actions without leading, trailing and doubled separators
    QList<QAction*> actions;
    for (QAction* action : toolbarActions)
    {
        if (!action && (actions.isEmpty() || !actions.back()))
        {
            continue;
        }

        actions.push_back(action);
    }
    while (!actions.isEmpty() && !actions.back())
    {
        actions.pop_back();
    }

    const auto actionCount = std::count_if(actions.cbegin(), actions.cend(), [](QAction* action) { return action != nullptr; });
    if (actionCount > 3)
    {
        // Many actions: one drop down button, which remembers the last used tool.
        // The first plugin of a section names the group after the section, so the
        // measure tools can be triggered by the measure shortcut.
        const QString sectionName = PDFToolbarSlots::getSectionName(*section);
        const QString groupId = m_toolbarSlots->getActionGroup(sectionName) ? pluginName : sectionName;
        QString text = plugin->getPluginMenuName();
        text.remove(QChar('&'));
        m_toolbarSlots->addActionGroup(*section, groupId, text, actions);
    }
    else
    {
        for (QAction* action : actions)
        {
            m_toolbarSlots->addAction(*section, action);
        }
    }

    // Menu of the plugin: View and Markup sections use the existing menus,
    // other sections get a top level menu named after the section.
    switch (*section)
    {
        case PDFToolbarSlots::Section::View:
            return ui->menuView->addMenu(plugin->getPluginMenuName());

        case PDFToolbarSlots::Section::Markup:
            return ui->menuInsert->addMenu(plugin->getPluginMenuName());

        default:
            break;
    }

    QMenu*& sectionMenu = m_sectionMenus[*section];
    if (!sectionMenu)
    {
        QString title;
        switch (*section)
        {
            case PDFToolbarSlots::Section::Measure:
                title = tr("Mea&sure");
                break;
            case PDFToolbarSlots::Section::Count:
                title = tr("&Count");
                break;
            case PDFToolbarSlots::Section::Forms:
                title = tr("Fo&rms");
                break;
            case PDFToolbarSlots::Section::Organize:
                title = tr("&Organize");
                break;
            default:
                title = plugin->getPluginMenuName();
                break;
        }

        sectionMenu = new QMenu(title, this);
        sectionMenu->setObjectName(QString("menuSection_%1").arg(PDFToolbarSlots::getSectionName(*section)));
        sectionMenu->setToolTipsVisible(true);
        menuBar()->insertMenu(ui->menuTools->menuAction(), sectionMenu);
    }
    else
    {
        sectionMenu->addSeparator();
    }

    return sectionMenu;
}

void PDFEditorMainWindow::onSidebarVisibilityRequested()
{
    if (!m_sidebarDockWidget->isVisible())
    {
        m_sidebarDockWidget->show();
    }

    m_sidebarDockWidget->raise();
}

void PDFEditorMainWindow::onActionQuitTriggered()
{
    close();
}

void PDFEditorMainWindow::onPageNumberSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageNumberSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->goToPage(m_pageNumberSpinBox->value() - 1);
}

void PDFEditorMainWindow::onPageZoomSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageZoomSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->zoom(m_pageZoomSpinBox->value() / 100.0);
}

void PDFEditorMainWindow::onProgressStarted(pdf::ProgressStartupInfo info)
{
    m_progressBarLeftLabelOnStatusBar->setText(info.text);
    m_progressBarLeftLabelOnStatusBar->setVisible(!info.text.isEmpty());

    m_progressBarOnStatusBar->setRange(0, 100);
    m_progressBarOnStatusBar->reset();
    m_progressBarOnStatusBar->show();

    m_progressTaskbarIndicator->setRange(0, 100);
    m_progressTaskbarIndicator->reset();
    m_progressTaskbarIndicator->show();

    m_programController->setIsBusy(true);
    m_programController->updateActionsAvailability();
}

void PDFEditorMainWindow::onProgressStep(int percentage)
{
    if (m_isChangingProgressStep)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isChangingProgressStep, true);
    m_progressBarOnStatusBar->setValue(percentage);
    m_progressTaskbarIndicator->setValue(percentage);
}

void PDFEditorMainWindow::onProgressFinished()
{
    m_progressBarLeftLabelOnStatusBar->hide();
    m_progressBarOnStatusBar->hide();
    m_progressTaskbarIndicator->hide();

    m_programController->setIsBusy(false);
    m_programController->updateActionsAvailability();
}

void PDFEditorMainWindow::updateDeveloperMenu()
{
    bool isDeveloperMode = m_programController->getSettings()->getSettings().m_allowDeveloperMode;
    ui->menuDeveloper->menuAction()->setVisible(isDeveloperMode);
}

void PDFEditorMainWindow::updateUI(bool fullUpdate)
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingUI, true);

    if (fullUpdate)
    {
        if (pdf::PDFDocument* document = m_programController->getDocument())
        {
            size_t pageCount = document->getCatalog()->getPageCount();
            m_pageNumberSpinBox->setMinimum(1);
            m_pageNumberSpinBox->setMaximum(static_cast<int>(pageCount));
            m_pageNumberSpinBox->setEnabled(true);
            m_pageNumberLabel->setText(tr(" / %1").arg(pageCount));
        }
        else
        {
            m_pageNumberSpinBox->setEnabled(false);
            m_pageNumberLabel->setText(QString());
        }
    }
    else
    {
        std::vector<pdf::PDFInteger> currentPages = m_programController->getPdfWidget()->getDrawWidget()->getCurrentPages();
        if (!currentPages.empty())
        {
            m_pageNumberSpinBox->setValue(currentPages.front() + 1);

            // Prefetch pages, if it is enabled
            if (m_programController->getSettings()->isPagePrefetchingEnabled())
            {
                m_programController->getPdfWidget()->getDrawWidgetProxy()->prefetchPages(currentPages.back());
            }
        }

        m_sidebarWidget->setCurrentPages(currentPages);
    }

    m_pageZoomSpinBox->setValue(m_programController->getPdfWidget()->getDrawWidgetProxy()->getZoom() * 100);
}

QMenu* PDFEditorMainWindow::addToolMenu(QString name, pdf::PDFPlugin::PluginMenuLocation location)
{
    QMenu* parentMenu = (location == pdf::PDFPlugin::PluginMenuLocation::Edit) ? ui->menuEdit : ui->menuTools;
    return parentMenu->addMenu(name);
}

void PDFEditorMainWindow::setStatusBarMessage(QString message, int time)
{
    statusBar()->showMessage(message, time);
}

void PDFEditorMainWindow::setDocument(const pdf::PDFModifiedDocument& document)
{
    if (m_sidebarWidget)
    {
        m_sidebarWidget->setDocument(document, *m_programController->getSignatures());
    }

    if (m_advancedFindWidget)
    {
        m_advancedFindWidget->setDocument(document);
    }

    if (m_sidebarWidget)
    {
        if (!document || m_sidebarWidget->isEmpty())
        {
            m_sidebarDockWidget->hide();
        }
        else if (document.hasReset() && !document.hasPreserveUndoRedo())
        {
            const bool showSidebar = m_programController->getSettings()->getSettings().m_showSidebarOnDocumentOpen;
            m_sidebarDockWidget->setVisible(showSidebar);
        }
    }

    if (!document && m_advancedFindDockWidget)
    {
        m_advancedFindDockWidget->hide();
    }
}

void PDFEditorMainWindow::adjustToolbar(QToolBar* toolbar)
{
    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    toolbar->setIconSize(iconSize);
}

pdf::PDFTextSelection PDFEditorMainWindow::getSelectedText() const
{
    if (!m_advancedFindWidget)
    {
        return pdf::PDFTextSelection();
    }

    return m_advancedFindWidget->getSelectedText();
}

void PDFEditorMainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_programController->canClose())
    {
        // Jakub Melka: Do not allow to close the application, if document
        // reading is running.
        event->ignore();
    }
    else
    {
        if (!m_programController->askForSaveDocumentBeforeClose())
        {
            // User cancelled close operation
            event->ignore();
            return;
        }

        if (!m_programController->isFactorySettingsBeingRestored())
        {
            m_programController->writeSettings();
        }

        m_programController->closeDocument();
        event->accept();
    }
}

void PDFEditorMainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    m_progressTaskbarIndicator->setWindow(windowHandle());
}

void PDFEditorMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorMainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorMainWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1)
        {
            m_programController->openDocument(urls.front().toLocalFile());
            event->acceptProposedAction();
        }
    }
}

}   // namespace pdfviewer
