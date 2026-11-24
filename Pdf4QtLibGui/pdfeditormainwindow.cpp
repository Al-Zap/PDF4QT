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
#include "pdfmarkupslistwidget.h"
#include "pdftoolchestwidget.h"
#include "pdfplacemarkuptool.h"
#include "pdfwidgetannotation.h"
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
#include "pdfsplitviewwidget.h"

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

#include <QSlider>
#include <QStatusBar>
#include <QSplitter>

#include <algorithm>
#include <cmath>

#include "pdfdbgheap.h"

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdfviewer
{

/// Helper functions of the ribbon layout of the main window
class PDFEditorMainWindowHelper
{
public:
    PDFEditorMainWindowHelper() = delete;

    static constexpr int ZOOM_SLIDER_STEPS = 1000;

    static QIcon getIcon(const QString& name)
    {
        return QIcon(QString(":/resources/%1.svg").arg(name));
    }

    /// Returns the icon converted for the dark theme, if it is active. It is used
    /// for the buttons without actions (icons of the actions are converted with the actions).
    static QIcon getThemedIcon(const QString& name)
    {
        QIcon icon = getIcon(name);
        if (pdf::PDFWidgetUtils::isDarkTheme())
        {
            icon = pdf::PDFWidgetUtils::convertIconForDarkTheme(icon, QSize(32, 32), qGuiApp->devicePixelRatio());
        }
        return icon;
    }

    /// Sets the icon of the action, if the action has no icon
    static void setActionIcon(QAction* action, const QString& name)
    {
        if (action && action->icon().isNull())
        {
            action->setIcon(getIcon(name));
        }
    }

    static void replaceActionIcon(QAction* action, const QString& name)
    {
        if (action)
        {
            action->setIcon(getIcon(name));
        }
    }

    static QAction* findAction(const std::vector<QAction*>& actions, const char* objectName)
    {
        for (QAction* action : actions)
        {
            if (action && action->objectName() == QLatin1String(objectName))
            {
                return action;
            }
        }

        return nullptr;
    }

    /// Places the actions with given names into the group as large and small buttons,
    /// placed actions are removed from the list of the actions.
    static void placeActions(PDFRibbonGroup* group,
                             std::vector<QAction*>& actions,
                             const QStringList& largeNames,
                             const QStringList& smallNames,
                             const QHash<QString, QString>& texts = QHash<QString, QString>())
    {
        auto takeAction = [&actions](const QString& name) -> QAction*
        {
            auto it = std::find_if(actions.begin(), actions.end(), [&name](QAction* action) { return action && action->objectName() == name; });
            if (it == actions.end())
            {
                return nullptr;
            }

            QAction* action = *it;
            actions.erase(it);
            return action;
        };

        for (const QString& name : largeNames)
        {
            group->addLargeAction(takeAction(name), texts.value(name));
        }

        group->breakColumn();
        for (const QString& name : smallNames)
        {
            QAction* action = takeAction(name);
            setActionIcon(action, QStringLiteral("placeholder"));
            group->addSmallAction(action, texts.value(name));
        }
    }

    /// Places all actions, which were not placed yet (except the actions with the names
    /// in \p keptNames): a few actions as large buttons, more actions as a drop down list.
    static void placeRemainingActions(PDFRibbonGroup* group,
                                      std::vector<QAction*>& actions,
                                      const QString& menuText,
                                      const QIcon& menuIcon,
                                      const QStringList& keptNames = QStringList())
    {
        QList<QAction*> remainingActions;
        std::vector<QAction*> keptActions;

        for (QAction* action : actions)
        {
            if (action && keptNames.contains(action->objectName()))
            {
                keptActions.push_back(action);
            }
            else if (action || (!remainingActions.isEmpty() && remainingActions.back()))
            {
                // Separators are kept, but not the leading and doubled ones
                remainingActions.push_back(action);
            }
        }

        while (!remainingActions.isEmpty() && !remainingActions.back())
        {
            remainingActions.pop_back();
        }

        actions = std::move(keptActions);

        const auto actionCount = std::count_if(remainingActions.cbegin(), remainingActions.cend(), [](QAction* action) { return action != nullptr; });
        if (actionCount == 0)
        {
            return;
        }

        if (actionCount <= 3)
        {
            for (QAction* action : remainingActions)
            {
                group->addLargeAction(action);
            }
        }
        else if (actionCount <= 6)
        {
            group->breakColumn();
            for (QAction* action : remainingActions)
            {
                setActionIcon(action, QStringLiteral("placeholder"));
                group->addSmallAction(action);
            }
        }
        else
        {
            group->addMenu(PDFRibbonButton::Size::Large, menuText, menuIcon, remainingActions);
        }
    }

    static QToolButton* createStatusBarButton(QAction* action, QWidget* parent)
    {
        QToolButton* button = new QToolButton(parent);
        const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(parent, 18);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(iconSize, iconSize));
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    static QFrame* createStatusBarSeparator(QWidget* parent)
    {
        QFrame* separator = new QFrame(parent);
        separator->setProperty("statusBarSeparator", true);
        separator->setFrameShape(QFrame::NoFrame);
        separator->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(parent, 9));
        return separator;
    }

    /// The slider of the zoom is logarithmic, so each step is the same change of the zoom
    static int getSliderFromZoom(pdf::PDFReal zoom)
    {
        const pdf::PDFReal minZoom = pdf::PDFDrawWidgetProxy::getMinZoom();
        const pdf::PDFReal maxZoom = pdf::PDFDrawWidgetProxy::getMaxZoom();
        const pdf::PDFReal position = std::log(qBound(minZoom, zoom, maxZoom) / minZoom) / std::log(maxZoom / minZoom);
        return qRound(position * ZOOM_SLIDER_STEPS);
    }

    static pdf::PDFReal getZoomFromSlider(int value)
    {
        const pdf::PDFReal minZoom = pdf::PDFDrawWidgetProxy::getMinZoom();
        const pdf::PDFReal maxZoom = pdf::PDFDrawWidgetProxy::getMaxZoom();
        const pdf::PDFReal position = pdf::PDFReal(qBound(0, value, ZOOM_SLIDER_STEPS)) / pdf::PDFReal(ZOOM_SLIDER_STEPS);
        return minZoom * std::pow(maxZoom / minZoom, position);
    }
};

PDFEditorMainWindow::PDFEditorMainWindow(QWidget* parent, bool isEmbedded) :
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
    m_ribbon(nullptr),
    m_developerGroup(nullptr),
    m_zoomSlider(nullptr),
    m_handToolAction(nullptr),
    m_measureShortcutAction(nullptr),
    m_navigationPaneAction(nullptr),
    m_collapseRibbonAction(nullptr),
    m_sidebarWidth(0)
{
    if (isEmbedded)
    {
        // A page of the window with the tabs, the geometry is not restored
        // from the settings (see PDFProgramController::readSettings).
        setWindowFlags(Qt::Widget);
    }

    ui->setupUi(this);

    setAcceptDrops(true);

    if (!m_isClassicLayout)
    {
        // The ribbon replaces the menu bar and the toolbar. The menus are kept (the File
        // menu and the drop down lists of the ribbon), so they must not be deleted
        // together with the menu bar.
        const QList<QMenu*> menus = menuBar()->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
        for (QMenu* menu : menus)
        {
            menu->setParent(this, menu->windowFlags());
        }

        removeToolBar(ui->mainToolBar);
        ui->mainToolBar->setObjectName("classicToolBar");
        ui->mainToolBar->hide();
    }

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

    // Visual comparison of two revisions (it is also available without an open document)
    m_compareDocumentsAction = new QAction(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-compare")), tr("Compare &Documents..."), this);
    m_compareDocumentsAction->setObjectName("actionCompareDocuments");
    m_compareDocumentsAction->setToolTip(tr("Overlay two revisions of a document and mark the changes by revision clouds"));
    ui->menuTools->addAction(m_compareDocumentsAction);
    m_actionManager->setAction(PDFActionManager::CompareDocuments, m_compareDocumentsAction);

    // Second pane next to the document (another place of the document, or another file)
    m_splitViewAction = new QAction(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-split-view")), tr("S&plit View"), this);
    m_splitViewAction->setObjectName("actionSplitView");
    m_splitViewAction->setCheckable(true);
    m_splitViewAction->setToolTip(tr("Show a second pane next to the document: another page of the document, or another file (for example the previous revision of a sheet)"));
    connect(m_splitViewAction, &QAction::toggled, this, &PDFEditorMainWindow::setSplitViewVisible);
    ui->menuView->addAction(m_splitViewAction);
    m_actionManager->setAction(PDFActionManager::Optimize, ui->actionOptimize);
    m_actionManager->setAction(PDFActionManager::OptimizeImages, ui->actionOptimizeImages);
    m_actionManager->setAction(PDFActionManager::Sanitize, ui->actionSanitize);
    m_actionManager->setAction(PDFActionManager::RemoveExternalLinks, ui->actionRemoveExternalLinks);

    // Links of the sheet references of a plan set ("SEE SHEET E-102") to the sheets
    m_sheetLinksAction = new QAction(PDFEditorMainWindowHelper::getThemedIcon(QStringLiteral("hyperlink")), tr("S&heet Links..."), this);
    m_sheetLinksAction->setObjectName("actionSheetLinks");
    m_sheetLinksAction->setToolTip(tr("Link the references to other sheets (sheet numbers like E-102) to those sheets"));
    m_removeSheetLinksAction = new QAction(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-link-remove")), tr("Remove Sheet Links"), this);
    m_removeSheetLinksAction->setObjectName("actionRemoveSheetLinks");
    m_removeSheetLinksAction->setToolTip(tr("Remove the sheet links created by Sheet Links, other links are kept"));
    ui->menuInsert->addSeparator();
    ui->menuInsert->addAction(m_sheetLinksAction);
    ui->menuInsert->addAction(m_removeSheetLinksAction);
    m_actionManager->setAction(PDFActionManager::SheetLinks, m_sheetLinksAction);
    m_actionManager->setAction(PDFActionManager::RemoveSheetLinks, m_removeSheetLinksAction);
    m_actionManager->setAction(PDFActionManager::HeaderFooter, ui->actionHeaderFooter);
    m_actionManager->setAction(PDFActionManager::BatesNumbering, ui->actionBatesNumbering);
    m_actionManager->setAction(PDFActionManager::Watermark, ui->actionWatermark);
    m_actionManager->setAction(PDFActionManager::RemoveStamps, ui->actionRemoveStamps);
    m_actionManager->setAction(PDFActionManager::PageGeometry, ui->actionPageGeometry);
    m_actionManager->setAction(PDFActionManager::CreateBitonalDocument, ui->actionCreateBitonalDocument);
    m_actionManager->setAction(PDFActionManager::RecognizeText, ui->actionRecognizeText);
    m_actionManager->setAction(PDFActionManager::ManageOCRLanguages, ui->actionManageOCRLanguages);
    m_actionManager->setAction(PDFActionManager::PrepareScannedPages, ui->actionPrepareScannedPages);
    m_actionManager->setAction(PDFActionManager::BatchRecognizeText, ui->actionBatchRecognizeText);
    m_actionManager->setAction(PDFActionManager::BatchProcessing, ui->actionBatchProcessing);
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
        createRibbon();
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
    if (m_ribbon)
    {
        // A preference of the window, it is next to the options
        m_ribbon->getGroup("help", "settings", tr("Settings"))->addSmallAction(classicLayoutAction, tr("Classic Menus"));
    }

    m_programController->initialize(PDFProgramController::AllFeatures, this, this, m_actionManager, m_progress);

    // The document is in a splitter, the second pane of the split view is added on demand
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("splitViewSplitter");
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_programController->getPdfWidget());
    setCentralWidget(m_splitter);

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
    if (m_ribbon)
    {
        // Navigation pane: a strip of icons, which is always visible, the pages are opened by the icons
        m_sidebarWidget->setCompactNavigation(true);
        m_sidebarWidget->setNavigationCollapsed(true);
        m_sidebarDockWidget->setFeatures(QDockWidget::NoDockWidgetFeatures);
        m_sidebarDockWidget->setTitleBarWidget(new QWidget(m_sidebarDockWidget));
        m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea);
        connect(m_sidebarWidget, &PDFSidebarWidget::navigationCollapsedChanged, this, &PDFEditorMainWindow::onNavigationCollapsedChanged);
        connect(m_navigationPaneAction, &QAction::triggered, this, [this](bool checked) { m_sidebarWidget->setNavigationCollapsed(!checked); });
        connect(m_commentsListAction, &QAction::triggered, this, &PDFEditorMainWindow::onCommentsListTriggered);
        onNavigationCollapsedChanged(true);
        m_sidebarDockWidget->show();
    }
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

        if (m_ribbon)
        {
            PDFEditorMainWindowHelper::setActionIcon(outlineNewItemAction, QStringLiteral("bookmark"));
            m_ribbon->getGroup("view", "bookmarks", tr("Bookmarks"))->addSmallAction(outlineNewItemAction);
        }
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
        if (m_ribbon)
        {
            PDFRibbonGroup* organizeGroup = m_ribbon->getGroup("pages", "organize", tr("Organize"));
            PDFRibbonGroup* rotateGroup = m_ribbon->getGroup("pages", "rotate", tr("Rotate"));

            auto findPageAction = [&pageActions](const char* name) -> QAction*
            {
                auto it = std::find_if(pageActions.cbegin(), pageActions.cend(), [name](QAction* action) { return action && action->objectName() == QLatin1String(name); });
                return it != pageActions.cend() ? *it : nullptr;
            };

            PDFEditorMainWindowHelper::replaceActionIcon(findPageAction("actionPagesInsertFromFile"), QStringLiteral("ribbon-page-insert-file"));
            PDFEditorMainWindowHelper::replaceActionIcon(findPageAction("actionPagesInsertBlankPage"), QStringLiteral("ribbon-page-blank"));
            PDFEditorMainWindowHelper::replaceActionIcon(findPageAction("actionPagesExtract"), QStringLiteral("ribbon-page-extract"));

            organizeGroup->addLargeAction(findPageAction("actionPagesInsertFromFile"), tr("Insert from File"));
            organizeGroup->addLargeAction(findPageAction("actionPagesInsertBlankPage"), tr("Blank Page"));
            organizeGroup->addLargeAction(findPageAction("actionPagesDelete"), tr("Delete"));
            organizeGroup->addLargeAction(findPageAction("actionPagesExtract"), tr("Extract"));
            organizeGroup->addLargeAction(findPageAction("actionPagesSplit"), tr("Split"));
            rotateGroup->addLargeAction(findPageAction("actionPagesRotateLeft"), tr("Rotate Left"));
            rotateGroup->addLargeAction(findPageAction("actionPagesRotateRight"), tr("Rotate Right"));
        }
        else
        {
            menuBar()->insertMenu(ui->menuInsert->menuAction(), pagesMenu);
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
    if (m_ribbon)
    {
        m_ribbon->getGroup("home", "find", tr("Find"))->addSmallAction(toggleAdvancedFindAction, tr("Advanced Find"));
    }

    // Markups list: table of all markups below the pages
    m_markupsListWidget = new PDFMarkupsListWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), this);
    m_markupsListDockWidget = new QDockWidget(tr("Markups List"), this);
    m_markupsListDockWidget->setObjectName("MarkupsListDockWidget");
    m_markupsListDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_markupsListDockWidget->setWidget(m_markupsListWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_markupsListDockWidget);
    m_markupsListDockWidget->hide();
    connect(m_markupsListWidget, &PDFMarkupsListWidget::documentModified, m_programController, &PDFProgramController::onDocumentModified);
    if (!m_markupsListAction)
    {
        // Classic layout: the action of the dock widget is used
        m_markupsListAction = m_markupsListDockWidget->toggleViewAction();
        m_markupsListAction->setObjectName("actionMarkupsList");
        m_markupsListAction->setText(tr("&Markups List"));
        m_actionManager->addAdditionalAction(m_markupsListAction);
    }
    else
    {
        connect(m_markupsListAction, &QAction::toggled, m_markupsListDockWidget, &QDockWidget::setVisible);
        connect(m_markupsListDockWidget->toggleViewAction(), &QAction::toggled, m_markupsListAction, &QAction::setChecked);
    }
    ui->menuView->addSeparator();
    ui->menuView->addAction(m_markupsListAction);

    createToolChest();

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");
    m_actionManager->addAdditionalAction(m_sidebarDockWidget->toggleViewAction());

    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFEditorMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFEditorMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFEditorMainWindow::onProgressFinished);

    PDFActionComboBox* actionComboBox = new PDFActionComboBox(this);
    if (m_ribbon)
    {
        actionComboBox->setObjectName("ribbonSearchBox");
        actionComboBox->setPlaceholderText(tr("Tell me what you want to do..."));
        m_ribbon->addCornerAction(ui->actionSave);
        m_ribbon->addCornerAction(ui->actionUndo);
        m_ribbon->addCornerAction(ui->actionRedo);
        m_ribbon->addCornerWidget(actionComboBox, pdf::PDFWidgetUtils::scaleDPI_x(actionComboBox, 120));
        m_ribbon->addCornerAction(m_collapseRibbonAction);
    }
    else
    {
        menuBar()->setCornerWidget(actionComboBox);
    }

    // On the first start (no saved window geometry), plan sheets get the whole screen
    const bool isFirstStart = !QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()).contains("geometry");

    m_programController->finishInitialization();
    m_programController->setPlanNavigation(!m_isClassicLayout);

    if (!m_isClassicLayout && isFirstStart)
    {
        setWindowState(windowState() | Qt::WindowMaximized);
    }

    if (m_ribbon)
    {
        // Plugins have placed their widgets on the status bar, page and zoom controls are the last ones
        createRibbonStatusBar();
        m_ribbon->updateGroups();
        finishRibbon();
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
    // The sidebar uses objects of the program controller (bookmark manager, draw widget proxy,
    // settings, ...), so it is destroyed first. Otherwise the QWidget destructor deletes it
    // later, and the reparenting of the dock widget lays out the bookmarks of the deleted
    // bookmark manager.
    QDockWidget* sidebarDockWidget = m_sidebarDockWidget;
    m_sidebarDockWidget = nullptr;
    m_sidebarWidget = nullptr;
    delete sidebarDockWidget;

    // The split view uses the draw widget of the program controller
    PDFSplitViewWidget* splitViewWidget = m_splitViewWidget;
    m_splitViewWidget = nullptr;
    delete splitViewWidget;


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

void PDFEditorMainWindow::createRibbon()
{
    using Size = PDFRibbonButton::Size;
    using Helper = PDFEditorMainWindowHelper;

    // Icons of the actions, which had no icon in the menus
    Helper::setActionIcon(ui->actionPageLayoutSinglePage, QStringLiteral("ribbon-layout-single-page"));
    Helper::setActionIcon(ui->actionPageLayoutContinuous, QStringLiteral("ribbon-layout-continuous"));
    Helper::setActionIcon(ui->actionPageLayoutTwoPages, QStringLiteral("ribbon-layout-two-pages"));
    Helper::setActionIcon(ui->actionPageLayoutTwoColumns, QStringLiteral("ribbon-layout-two-columns"));
    Helper::setActionIcon(ui->actionFirstPageOnRightSide, QStringLiteral("ribbon-layout-first-page-right"));
    Helper::setActionIcon(ui->actionFullscreenMode, QStringLiteral("ribbon-fullscreen"));
    Helper::setActionIcon(ui->actionExport_to_Word, QStringLiteral("ribbon-export-word"));
    Helper::setActionIcon(ui->actionRender_to_Images, QStringLiteral("ribbon-export-images"));
    Helper::setActionIcon(ui->actionBatchRecognizeText, QStringLiteral("ribbon-ocr-batch"));
    Helper::setActionIcon(ui->actionBatchProcessing, QStringLiteral("ribbon-batch"));
    Helper::setActionIcon(ui->actionManageOCRLanguages, QStringLiteral("ribbon-ocr-languages"));
    Helper::setActionIcon(ui->actionPrepareScannedPages, QStringLiteral("ribbon-scan-prepare"));
    Helper::setActionIcon(ui->actionDeleteAnnotation, QStringLiteral("ribbon-annot-delete"));
    Helper::setActionIcon(ui->actionRemoveExternalLinks, QStringLiteral("ribbon-link-remove"));
    Helper::setActionIcon(ui->actionAutomaticDocumentRefresh, QStringLiteral("ribbon-auto-refresh"));
    Helper::setActionIcon(ui->actionResetToFactorySettings, QStringLiteral("ribbon-reset-settings"));
    Helper::setActionIcon(ui->actionShow_Text_Blocks, QStringLiteral("ribbon-text-blocks"));
    Helper::setActionIcon(ui->actionShow_Text_Lines, QStringLiteral("ribbon-text-blocks"));
    Helper::replaceActionIcon(ui->actionCreateTypewriter, QStringLiteral("ribbon-typewriter"));
    Helper::replaceActionIcon(ui->actionGoToDocumentStart, QStringLiteral("ribbon-first-page"));
    Helper::replaceActionIcon(ui->actionGoToPreviousPage, QStringLiteral("ribbon-prev-page"));
    Helper::replaceActionIcon(ui->actionGoToNextPage, QStringLiteral("ribbon-next-page-arrow"));
    Helper::replaceActionIcon(ui->actionGoToDocumentEnd, QStringLiteral("ribbon-last-page"));

    // Icons of the sticky notes are drawn from the characters of the font, which
    // are not available everywhere. The kinds of the notes are told apart by their names.
    if (QActionGroup* stickyNoteGroup = m_actionManager->getActionGroup(PDFActionManager::CreateStickyNoteGroup))
    {
        for (QAction* action : stickyNoteGroup->actions())
        {
            Helper::replaceActionIcon(action, QStringLiteral("annot-sticky-note"));
        }
    }

    m_ribbon = new PDFRibbon(this);

    // The menu bar is deleted, when it is replaced by the ribbon (the menus were moved to the window)
    setMenuWidget(m_ribbon);

    // File menu: the options and the settings are a part of it, as there is no Tools menu
    ui->menuFile->insertAction(ui->actionQuit, ui->actionOptions);
    ui->menuFile->insertSeparator(ui->actionQuit);
    m_ribbon->setFileMenu(tr("&File"), ui->menuFile);

    m_ribbon->addPage("home", tr("Home"));
    m_ribbon->addPage("edit", tr("Edit"));
    m_ribbon->addPage("comment", tr("Comment"));
    m_ribbon->addPage("measure", tr("Measure"));
    m_ribbon->addPage("lighting", tr("Lighting"));
    m_ribbon->addPage("pages", tr("Pages"));
    m_ribbon->addPage("forms", tr("Forms"));
    m_ribbon->addPage("convert", tr("Convert"));
    m_ribbon->addPage("protect", tr("Protect"));
    m_ribbon->addPage("view", tr("View"));
    m_ribbon->addPage("advanced", tr("Advanced"));
    m_ribbon->addPage("help", tr("Help"));

    // Hand: the state without an active tool
    m_handToolAction = new QAction(QIcon(":/resources/hand-tool.svg"), tr("&Hand"), this);
    m_handToolAction->setObjectName("actionHandTool");
    m_handToolAction->setCheckable(true);
    m_handToolAction->setChecked(true);
    m_handToolAction->setToolTip(tr("Hand: drag to pan, click to fill form fields and follow links"));
    connect(m_handToolAction, &QAction::triggered, this, &PDFEditorMainWindow::onHandToolTriggered);
    m_actionManager->addAdditionalAction(m_handToolAction);

    m_navigationPaneAction = new QAction(Helper::getIcon(QStringLiteral("ribbon-navigation-pane")), tr("&Navigation Pane"), this);
    m_navigationPaneAction->setObjectName("actionNavigationPane");
    m_navigationPaneAction->setCheckable(true);
    m_navigationPaneAction->setShortcut(QKeySequence(Qt::Key_F4));
    m_navigationPaneAction->setToolTip(tr("Show or hide the pane with the page thumbnails, bookmarks and comments"));
    m_actionManager->addAdditionalAction(m_navigationPaneAction);

    m_commentsListAction = new QAction(Helper::getIcon(QStringLiteral("ribbon-comments-list")), tr("Comments &List"), this);
    m_commentsListAction->setObjectName("actionCommentsList");
    m_commentsListAction->setToolTip(tr("Show the list of all comments and markups in the navigation pane"));
    m_actionManager->addAdditionalAction(m_commentsListAction);

    m_markupsListAction = new QAction(Helper::getIcon(QStringLiteral("ribbon-markups-list")), tr("&Markups List"), this);
    m_markupsListAction->setObjectName("actionMarkupsList");
    m_markupsListAction->setCheckable(true);
    m_markupsListAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_L));
    m_markupsListAction->setToolTip(tr("Show the table of all markups, counts and measurements of all sheets with their status, the takeoff summary and the export to CSV or PDF"));
    m_actionManager->addAdditionalAction(m_markupsListAction);

    m_collapseRibbonAction = new QAction(Helper::getIcon(QStringLiteral("ribbon-ribbon-collapse")), tr("&Collapse the Ribbon"), this);
    m_collapseRibbonAction->setObjectName("actionCollapseRibbon");
    m_collapseRibbonAction->setCheckable(true);
    m_collapseRibbonAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F1));
    m_collapseRibbonAction->setToolTip(tr("Show only the names of the tabs (double click on a tab does the same)"));
    connect(m_collapseRibbonAction, &QAction::triggered, m_ribbon, &PDFRibbon::setCollapsed);
    connect(m_ribbon, &PDFRibbon::collapsedChanged, m_collapseRibbonAction, &QAction::setChecked);
    m_actionManager->addAdditionalAction(m_collapseRibbonAction);

    // Shortcut for the measure tool used last
    m_measureShortcutAction = new QAction(tr("Measure (Last Used Tool)"), this);
    m_measureShortcutAction->setObjectName("actionMeasureLastUsed");
    connect(m_measureShortcutAction, &QAction::triggered, this, [this]()
    {
        if (m_lastMeasureAction)
        {
            m_lastMeasureAction->trigger();
        }
    });
    m_actionManager->addAdditionalAction(m_measureShortcutAction);

    const QList<QAction*> highlightActions = { ui->actionCreateTextHighlight, ui->actionCreateTextUnderline, ui->actionCreateTextStrikeout, ui->actionCreateTextSquiggly };
    QList<QAction*> stickyNoteActions;
    if (QActionGroup* stickyNoteGroup = m_actionManager->getActionGroup(PDFActionManager::CreateStickyNoteGroup))
    {
        stickyNoteActions = stickyNoteGroup->actions();
    }
    QList<QAction*> stampActions;
    if (QActionGroup* stampGroup = m_actionManager->getActionGroup(PDFActionManager::CreateStampGroup))
    {
        stampActions = stampGroup->actions();
    }

    // Home
    PDFRibbonGroup* group = m_ribbon->getGroup("home", "file", tr("File"));
    group->addLargeAction(ui->actionOpen);
    group->addActionList(Size::Large, { ui->actionSave, ui->actionSave_As, ui->actionSaveFlattenedCopy }, false);
    group->addLargeAction(ui->actionPrint);
    group->addSmallAction(ui->actionSend_by_E_Mail, tr("E-Mail"));
    group->addSmallAction(ui->actionProperties);
    group->addSmallAction(ui->actionClose);

    group = m_ribbon->getGroup("home", "tools", tr("Tools"));
    group->addLargeAction(m_handToolAction);
    group->addLargeAction(ui->actionSelectText, tr("Select Text"));
    group->addSmallAction(ui->actionSelectTable, tr("Select Table"));
    group->addSmallAction(ui->actionScreenshot, tr("Snapshot"));
    group->addSmallAction(ui->actionMagnifier);

    group = m_ribbon->getGroup("home", "find", tr("Find"));
    group->addLargeAction(ui->actionFind);
    group->addSmallAction(ui->actionFindNext);
    group->addSmallAction(ui->actionFindPrevious);

    group = m_ribbon->getGroup("home", "zoom", tr("View"));
    group->addLargeAction(ui->actionFitPage);
    group->addLargeAction(ui->actionFitWidth);
    group->addSmallAction(ui->actionFitHeight);
    group->addSmallAction(ui->actionRotateLeft, tr("Rotate View Left"));
    group->addSmallAction(ui->actionRotateRight, tr("Rotate View Right"));

    group = m_ribbon->getGroup("home", "comment", tr("Comment"));
    group->addActionList(Size::Large, highlightActions);
    group->addActionList(Size::Large, stickyNoteActions, true, tr("Sticky Note"));
    group->addLargeAction(ui->actionInlineText, tr("Text Box"));
    group->addActionList(Size::Large, { ui->actionCreateCloudRectangle, ui->actionCreateCloudPolygon });

    group = m_ribbon->getGroup("home", "convert", tr("Convert"));
    group->addLargeAction(ui->actionMakeSearchable, tr("Make Searchable"));
    group->addLargeAction(ui->actionExport_to_Word, tr("To Word"));

    // Edit
    group = m_ribbon->getGroup("edit", "undo", tr("Undo"));
    group->addLargeAction(ui->actionUndo);
    group->addLargeAction(ui->actionRedo);

    group = m_ribbon->getGroup("edit", "text", tr("Text"));
    group->addLargeAction(ui->actionSelectText, tr("Select Text"));
    group->addSmallAction(ui->actionCopyText, tr("Copy"));
    group->addSmallAction(ui->actionSelectTextAll);
    group->addSmallAction(ui->actionDeselectText);

    group = m_ribbon->getGroup("edit", "insert", tr("Insert"));
    group->addLargeAction(ui->actionCreateTypewriter);
    group->addLargeAction(ui->actionInlineText, tr("Text Box"));
    group->addLargeAction(ui->actionCreateHyperlink, tr("Web Link"));
    group->addMenu(Size::Large, tr("Link to Page"), Helper::getThemedIcon(QStringLiteral("hyperlink")), ui->menuHyperlinkToThisPDF);
    group->addSmallAction(m_removeSheetLinksAction, tr("Remove Sheet Links"));

    // Group of the page content editor (plugin) is in front of the document group
    m_ribbon->getGroup("edit", "content", tr("Page Content"));

    group = m_ribbon->getGroup("edit", "document", tr("Document"));
    group->addLargeAction(ui->actionRemoveExternalLinks, tr("Remove Links"));

    // Comment
    group = m_ribbon->getGroup("comment", "markup", tr("Text Markup"));
    group->addLargeAction(ui->actionCreateTextHighlight);
    group->addSmallAction(ui->actionCreateTextUnderline);
    group->addSmallAction(ui->actionCreateTextStrikeout);
    group->addSmallAction(ui->actionCreateTextSquiggly);

    group = m_ribbon->getGroup("comment", "notes", tr("Notes"));
    group->addActionList(Size::Large, stickyNoteActions, true, tr("Sticky Note"));
    group->addLargeAction(ui->actionInlineText, tr("Text Box"));
    group->addLargeAction(ui->actionCreateTypewriter);

    group = m_ribbon->getGroup("comment", "drawing", tr("Drawing"));
    group->addActionList(Size::Large, { ui->actionCreateCloudRectangle, ui->actionCreateCloudPolygon });
    group->addSmallAction(ui->actionCreateStraightLine, tr("Line"));
    group->addSmallAction(ui->actionCreateArrow);
    group->addSmallAction(ui->actionCreatePolyline);
    group->addSmallAction(ui->actionCreateRectangle);
    group->addSmallAction(ui->actionCreateEllipse);
    group->addSmallAction(ui->actionCreatePolygon);
    group->addSmallAction(ui->actionCreateFreehandCurve, tr("Freehand"));

    group = m_ribbon->getGroup("comment", "stamps", tr("Stamps"));
    m_stampMenu = new QMenu(this);
    m_stampMenu->setObjectName("ribbonStampMenu");
    m_stampMenu->addActions(stampActions);
    group->addMenu(Size::Large, tr("Stamp"), Helper::getThemedIcon(QStringLiteral("ribbon-stamp")), m_stampMenu);

    // Filled, when the tool chest is created (custom stamps manager, tool chest)
    m_ribbon->getGroup("comment", "toolchest", tr("Tool Chest"));

    group = m_ribbon->getGroup("comment", "manage", tr("Manage"));
    group->addLargeAction(m_markupsListAction);
    group->addLargeAction(m_commentsListAction);
    group->addLargeAction(ui->actionDeleteAnnotation, tr("Delete Comments"));

    group = m_ribbon->getGroup("comment", "compare", tr("Compare"));
    group->addLargeAction(m_compareDocumentsAction, tr("Compare Documents"));

    // Measure: filled by the measuring plugin (scale, lengths, areas, angles)
    m_ribbon->getGroup("measure", "scale", tr("Scale"));
    m_ribbon->getGroup("measure", "measure", tr("Measure"));
    m_ribbon->getGroup("measure", "results", tr("Measurements"));

    // Lighting: tools made for the lighting plans (fixture count plugin)
    m_ribbon->getGroup("lighting", "count", tr("Fixture Count"));
    m_ribbon->getGroup("lighting", "legend", tr("Legend"));
    m_ribbon->getGroup("lighting", "footcandles", tr("Footcandles"));
    m_ribbon->getGroup("lighting", "schedule", tr("Schedule"));
    m_ribbon->getGroup("lighting", "wire", tr("Wire Takeoff"));

    // Pages: organizing actions are added, when the navigation pane is created
    m_ribbon->getGroup("pages", "organize", tr("Organize"));
    m_ribbon->getGroup("pages", "rotate", tr("Rotate"));

    group = m_ribbon->getGroup("pages", "setup", tr("Page Setup"));
    group->addLargeAction(ui->actionPageGeometry);
    group->addLargeAction(ui->actionPrepareScannedPages, tr("Crop, Split and Deskew"));

    group = m_ribbon->getGroup("pages", "design", tr("Page Design"));
    group->addLargeAction(ui->actionHeaderFooter, tr("Header/Footer"));
    group->addLargeAction(ui->actionWatermark, tr("Watermark"));
    group->addLargeAction(ui->actionBatesNumbering, tr("Bates Numbering"));
    group->addSmallAction(ui->actionInsertPageNumbers, tr("Page Numbers"));
    group->addSmallAction(ui->actionRemoveStamps, tr("Remove"));

    // Forms
    group = m_ribbon->getGroup("forms", "fill", tr("Fill In"));
    group->addLargeAction(m_handToolAction, tr("Fill Form"));
    group->addLargeAction(ui->actionCreateTypewriter);

    group = m_ribbon->getGroup("forms", "finish", tr("Finish"));
    group->addLargeAction(ui->actionSave);
    group->addLargeAction(ui->actionSaveFlattenedCopy, tr("Save Flattened Copy"));
    group->addLargeAction(ui->actionPrint);

    // Convert
    group = m_ribbon->getGroup("convert", "ocr", tr("Text Recognition (OCR)"));
    group->addLargeAction(ui->actionMakeSearchable, tr("Make Searchable"));
    group->addLargeAction(ui->actionRecognizeText, tr("Recognize and Review"));
    group->addSmallAction(ui->actionBatchRecognizeText, tr("Batch of Files"));
    group->addSmallAction(ui->actionManageOCRLanguages, tr("Languages"));

    group = m_ribbon->getGroup("convert", "export", tr("Export"));
    group->addLargeAction(ui->actionExport_to_Word, tr("To Word"));
    group->addLargeAction(ui->actionRender_to_Images, tr("To Images"));
    group->addLargeAction(ui->actionSend_by_E_Mail, tr("E-Mail"));

    // Scanner (plugin) is in front of the optimization
    m_ribbon->getGroup("convert", "create", tr("Create"));

    group = m_ribbon->getGroup("convert", "batch", tr("Batch"));
    group->addLargeAction(ui->actionBatchProcessing, tr("Batch Process"));

    group = m_ribbon->getGroup("convert", "optimize", tr("Reduce Size"));
    group->addLargeAction(ui->actionOptimize);
    group->addLargeAction(ui->actionOptimizeImages);
    group->addLargeAction(ui->actionCreateBitonalDocument, tr("Black and White"));

    // Protect
    group = m_ribbon->getGroup("protect", "security", tr("Security"));
    group->addLargeAction(ui->actionEncryption, tr("Password"));
    group->addLargeAction(ui->actionSanitize);
    group->addLargeAction(ui->actionCertificateManager);

    // View
    group = m_ribbon->getGroup("view", "panes", tr("Show"));
    group->addLargeAction(m_navigationPaneAction);
    group->addLargeAction(ui->actionFullscreenMode, tr("Full Screen"));
    group->addLargeAction(m_splitViewAction, tr("Split View"));

    group = m_ribbon->getGroup("view", "layout", tr("Page Layout"));
    group->addSmallAction(ui->actionPageLayoutSinglePage);
    group->addSmallAction(ui->actionPageLayoutContinuous);
    group->addSmallAction(ui->actionPageLayoutTwoPages);
    group->addSmallAction(ui->actionPageLayoutTwoColumns, tr("Two Columns"));
    group->addSmallAction(ui->actionFirstPageOnRightSide, tr("First Page on Right"));

    group = m_ribbon->getGroup("view", "zoom", tr("Zoom"));
    group->addSmallAction(ui->actionZoom_In);
    group->addSmallAction(ui->actionZoom_Out);
    group->addSmallAction(ui->actionFitPage);
    group->addSmallAction(ui->actionFitWidth);
    group->addSmallAction(ui->actionFitHeight);

    group = m_ribbon->getGroup("view", "bookmarks", tr("Bookmarks"));
    group->addLargeAction(ui->actionBookmarkPage);
    group->addSmallAction(ui->actionGotoNextBookmark, tr("Next"));
    group->addSmallAction(ui->actionGotoPreviousBookmark, tr("Previous"));
    group->addMenu(Size::Small, tr("Settings"), Helper::getThemedIcon(QStringLiteral("settings")), ui->menuBookmarkSettings);

    group = m_ribbon->getGroup("view", "display", tr("Display"));
    group->addSmallAction(ui->actionColorInvert, tr("Inverted Colors"));
    group->addSmallAction(ui->actionColorGrayscale, tr("Grayscale"));
    group->addSmallAction(ui->actionColorHighContrast, tr("High Contrast"));
    group->addSmallAction(ui->actionColorBitonal, tr("Monochromatic"));
    group->addSmallAction(ui->actionColorCustom, tr("Custom Colors"));
    group->addMenu(Size::Small, tr("Rendering"), Helper::getThemedIcon(QStringLiteral("rendering")), ui->menuRendering_Options);


    // Advanced
    group = m_ribbon->getGroup("advanced", "tools", tr("Tools"));
    group->addLargeAction(ui->actionExtractImage);

    group = m_ribbon->getGroup("advanced", "document", tr("Document"));
    group->addLargeAction(ui->actionProperties);
    group->addLargeAction(ui->actionRendering_Errors);
    group->addLargeAction(ui->actionAutomaticDocumentRefresh, tr("Automatic Refresh"));

    m_developerGroup = m_ribbon->getGroup("advanced", "developer", tr("Developer"));
    m_developerGroup->addSmallAction(ui->actionShow_Text_Blocks);
    m_developerGroup->addSmallAction(ui->actionShow_Text_Lines);

    // Help
    group = m_ribbon->getGroup("help", "settings", tr("Settings"));
    group->addLargeAction(ui->actionOptions);
    group->addLargeAction(ui->actionResetToFactorySettings, tr("Factory Settings"));

    group = m_ribbon->getGroup("help", "about", tr("About"));
    group->addLargeAction(ui->actionAbout);
    group->addLargeAction(ui->actionGetSource);
    group->addLargeAction(ui->actionBecomeASponsor, tr("Sponsor"));

    setRibbonShortcuts();
}

void PDFEditorMainWindow::setRibbonShortcuts()
{
    // Shortcuts follow the common conventions of PDF applications, so habits carry
    // over. Single letter shortcuts do not interfere with typing into form fields,
    // the form editor overrides them. Shortcuts changed by the user (Options) are
    // restored after this.
    ui->actionZoom_In->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal) });
    ui->actionZoom_Out->setShortcuts({ QKeySequence::ZoomOut });
    ui->actionFitPage->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_0) });
    ui->actionFitWidth->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_2), QKeySequence(Qt::Key_W) });
    ui->actionFitHeight->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_3) });

    // Ctrl+1..5 are used by the zoom commands above, the page layout is
    // changed by the buttons of the status bar.
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

void PDFEditorMainWindow::createRibbonStatusBar()
{
    using Helper = PDFEditorMainWindowHelper;

    QStatusBar* bar = statusBar();
    bar->setObjectName("ribbonStatusBar");

    // Widgets of the status bar, which are hidden, when a message would be cut off by them
    QList<QWidget*> navigationWidgets;
    QList<QWidget*> pageLayoutWidgets;
    QList<QWidget*> zoomButtonWidgets;
    QList<QWidget*> zoomSliderWidgets;

    auto addStatusBarWidget = [bar](QWidget* widget, QList<QWidget*>* hiddenWidgets = nullptr)
    {
        bar->addPermanentWidget(widget);
        if (hiddenWidgets)
        {
            hiddenWidgets->append(widget);
        }
    };

    // Page navigation
    addStatusBarWidget(Helper::createStatusBarSeparator(bar));
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionGoToDocumentStart, bar), &navigationWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionGoToPreviousPage, bar));
    addStatusBarWidget(m_pageNumberSpinBox);
    addStatusBarWidget(m_pageNumberLabel);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionGoToNextPage, bar));
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionGoToDocumentEnd, bar), &navigationWidgets);
    m_pageNumberSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageNumberSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 52));
    m_pageNumberSpinBox->setToolTip(tr("Current page"));

    // Page layout
    addStatusBarWidget(Helper::createStatusBarSeparator(bar), &pageLayoutWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionPageLayoutSinglePage, bar), &pageLayoutWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionPageLayoutContinuous, bar), &pageLayoutWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionPageLayoutTwoPages, bar), &pageLayoutWidgets);

    // Zoom
    addStatusBarWidget(Helper::createStatusBarSeparator(bar));
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionFitPage, bar), &zoomButtonWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionFitWidth, bar), &zoomButtonWidgets);
    addStatusBarWidget(Helper::createStatusBarButton(ui->actionZoom_Out, bar), &zoomSliderWidgets);

    m_zoomSlider = new QSlider(Qt::Horizontal, bar);
    m_zoomSlider->setObjectName("zoomSlider");
    m_zoomSlider->setRange(0, Helper::ZOOM_SLIDER_STEPS);
    m_zoomSlider->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_zoomSlider, 110));
    m_zoomSlider->setFocusPolicy(Qt::NoFocus);
    m_zoomSlider->setToolTip(tr("Zoom"));
    connect(m_zoomSlider, &QSlider::valueChanged, this, &PDFEditorMainWindow::onZoomSliderValueChanged);
    addStatusBarWidget(m_zoomSlider, &zoomSliderWidgets);

    addStatusBarWidget(Helper::createStatusBarButton(ui->actionZoom_In, bar), &zoomSliderWidgets);
    addStatusBarWidget(m_pageZoomSpinBox);
    m_pageZoomSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageZoomSpinBox->setDecimals(0);
    m_pageZoomSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageZoomSpinBox, 58));
    m_pageZoomSpinBox->setToolTip(tr("Zoom"));

    // A long message (hint of a tool) is not cut off in a narrow window: the page layout
    // buttons are hidden first, then the zoom slider, the fit buttons and the buttons
    // of the first and last page. Page number and zoom value stay visible.
    PDFStatusBarMessageFitter* messageFitter = new PDFStatusBarMessageFitter(bar);
    messageFitter->setObjectName("statusBarMessageFitter");
    messageFitter->addWidgets(pageLayoutWidgets);
    messageFitter->addWidgets(zoomSliderWidgets);
    messageFitter->addWidgets(zoomButtonWidgets);
    messageFitter->addWidgets(navigationWidgets);

    updateZoomSlider();
}

void PDFEditorMainWindow::finishRibbon()
{
    // The toolbar of the classic layout can be shown by the restored state of the window
    ui->mainToolBar->hide();

    // There is no menu bar, so the shortcuts of the actions work only, if the actions
    // belong to the window. Shortcuts limited to a widget stay with their widgets.
    for (QAction* action : m_actionManager->getActions())
    {
        if (action && action->shortcutContext() == Qt::WindowShortcut && !actions().contains(action))
        {
            addAction(action);
        }
    }

    m_navigationPaneAction->setEnabled(false);
    m_sidebarDockWidget->show();
    onNavigationCollapsedChanged(m_sidebarWidget->isNavigationCollapsed());

    setStyleSheet(PDFRibbon::createStyleSheet(pdf::PDFWidgetUtils::isDarkTheme()));
}

void PDFEditorMainWindow::onNavigationCollapsedChanged(bool collapsed)
{
    if (!m_sidebarDockWidget || !m_sidebarWidget)
    {
        return;
    }

    const int stripWidth = m_sidebarWidget->getNavigationStripWidth();

    if (collapsed)
    {
        if (m_sidebarDockWidget->width() > stripWidth)
        {
            m_sidebarWidth = m_sidebarDockWidget->width();
        }

        m_sidebarDockWidget->setFixedWidth(stripWidth);
    }
    else
    {
        m_sidebarDockWidget->setMinimumWidth(stripWidth + pdf::PDFWidgetUtils::scaleDPI_x(this, 120));
        m_sidebarDockWidget->setMaximumWidth(QWIDGETSIZE_MAX);

        const int defaultWidth = stripWidth + pdf::PDFWidgetUtils::scaleDPI_x(this, 220);
        resizeDocks({ m_sidebarDockWidget }, { qMax(m_sidebarWidth, defaultWidth) }, Qt::Horizontal);
    }

    if (m_navigationPaneAction)
    {
        m_navigationPaneAction->setChecked(!collapsed);
    }
}

void PDFEditorMainWindow::onCommentsListTriggered()
{
    if (m_sidebarWidget->isEmpty(PDFSidebarWidget::Notes))
    {
        statusBar()->showMessage(tr("The document has no comments."), 5000);
        return;
    }

    m_sidebarWidget->setNavigationCollapsed(false);
    m_sidebarWidget->selectPage(PDFSidebarWidget::Notes);
}

void PDFEditorMainWindow::onZoomSliderValueChanged(int value)
{
    if (m_isLoadingUI)
    {
        return;
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->zoom(PDFEditorMainWindowHelper::getZoomFromSlider(value));
}

void PDFEditorMainWindow::updateZoomSlider()
{
    if (m_zoomSlider && m_programController->getPdfWidget())
    {
        QSignalBlocker blocker(m_zoomSlider);
        m_zoomSlider->setValue(PDFEditorMainWindowHelper::getSliderFromZoom(m_programController->getPdfWidget()->getDrawWidgetProxy()->getZoom()));
    }
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
    if (m_isClassicLayout || !m_ribbon)
    {
        return IMainWindow::addPluginUserInterface(plugin, pluginName, toolbarActions, toolbarActionsPlaced);
    }

    using Helper = PDFEditorMainWindowHelper;

    // Plugins have no toolbars and no menus of their own, their actions are on the ribbon
    toolbarActionsPlaced = true;

    const std::vector<QAction*> actions = plugin->getActions();
    const QString pluginTitle = PDFRibbonButton::getCleanText(plugin->getPluginMenuName());
    const QIcon moreIcon = Helper::getThemedIcon(QStringLiteral("ribbon-comments-list"));

    if (pluginName == QLatin1String("Dimensions"))
    {
        Helper::setActionIcon(Helper::findAction(actions, "dimensiontool_ScalePerSheetAction"), QStringLiteral("ribbon-scale-sheet"));
        Helper::setActionIcon(Helper::findAction(actions, "dimensiontool_SnapToDrawingAction"), QStringLiteral("ribbon-snap"));

        const QStringList measureTools = { "dimensiontool_LinearAction", "dimensiontool_PolylineAction", "dimensiontool_AreaAction",
                                           "dimensiontool_LinearHorizontalAction", "dimensiontool_LinearVerticalAction", "dimensiontool_AngleAction",
                                           "dimensiontool_PerimeterAction", "dimensiontool_RectanglePerimeterAction", "dimensiontool_RectangleAreaAction" };

        std::vector<QAction*> remainingActions = actions;
        const QHash<QString, QString> texts = { { "dimensiontool_CalibrateAction", tr("Calibrate") },
                                                { "dimensiontool_ScaleAction", tr("Drawing Scale") },
                                                { "dimensiontool_ScalePerSheetAction", tr("This Sheet Only") },
                                                { "dimensiontool_LinearAction", tr("Length") },
                                                { "dimensiontool_PolylineAction", tr("Polyline") },
                                                { "dimensiontool_LinearHorizontalAction", tr("Horizontal") },
                                                { "dimensiontool_LinearVerticalAction", tr("Vertical") },
                                                { "dimensiontool_RectanglePerimeterAction", tr("Rect. Perimeter") },
                                                { "dimensiontool_RectangleAreaAction", tr("Rect. Area") },
                                                { "dimensiontool_ShowDimensionsAction", tr("Show") },
                                                { "dimensiontool_ExportAction", tr("Export") },
                                                { "dimensiontool_ConvertToAnnotationsAction", tr("Save as Markups") },
                                                { "dimensiontool_ClearDimensionsAction", tr("Clear") } };

        Helper::placeActions(m_ribbon->getGroup("measure", "scale", tr("Scale")), remainingActions,
                             { "dimensiontool_CalibrateAction", "dimensiontool_ScaleAction" },
                             { "dimensiontool_ScalePerSheetAction", "dimensiontool_SnapToDrawingAction" }, texts);
        Helper::placeActions(m_ribbon->getGroup("measure", "measure", tr("Measure")), remainingActions, measureTools.mid(0, 3), measureTools.mid(3), texts);
        Helper::placeActions(m_ribbon->getGroup("measure", "results", tr("Measurements")), remainingActions,
                             { "dimensiontool_ShowDimensionsAction", "dimensiontool_ExportAction" },
                             { "dimensiontool_ConvertToAnnotationsAction", "dimensiontool_ClearDimensionsAction", "dimensiontool_SettingsAction" }, texts);
        Helper::placeRemainingActions(m_ribbon->getGroup("measure", "results", tr("Measurements")), remainingActions, tr("More"), moreIcon);

        // Measure tool used last is activated by the shortcut
        for (const QString& name : measureTools)
        {
            if (QAction* action = Helper::findAction(actions, name.toLatin1().constData()))
            {
                if (!m_lastMeasureAction)
                {
                    m_lastMeasureAction = action;
                }

                connect(action, &QAction::triggered, this, [this, action]() { m_lastMeasureAction = action; });
            }
        }
    }
    else if (pluginName == QLatin1String("FixtureCount"))
    {
        std::vector<QAction*> remainingActions = actions;
        PDFRibbonGroup* group = m_ribbon->getGroup("lighting", "count", tr("Fixture Count"));
        Helper::placeActions(group, remainingActions,
                             { "fixturecount_CountAction", "fixturecount_FindSimilarAction", "fixturecount_ShowPanelAction", "fixturecount_ExportAction" }, { },
                             { { "fixturecount_CountAction", tr("Count") }, { "fixturecount_ShowPanelAction", tr("Count Panel") }, { "fixturecount_ExportAction", tr("Export") } });

        // Lighting tools of the same plugin have their own groups on the Lighting tab
        Helper::placeActions(m_ribbon->getGroup("lighting", "legend", tr("Legend")), remainingActions,
                             { "lighting_PlaceSheetLegendAction", "lighting_PlaceSetLegendAction" }, { "lighting_UpdateTablesAction" },
                             { { "lighting_PlaceSheetLegendAction", tr("Sheet Legend") }, { "lighting_PlaceSetLegendAction", tr("Set Legend") },
                               { "lighting_UpdateTablesAction", tr("Update Tables") } });
        Helper::placeActions(m_ribbon->getGroup("lighting", "footcandles", tr("Footcandles")), remainingActions,
                             { "lighting_FootcandleStatsAction" }, { }, { { "lighting_FootcandleStatsAction", tr("Grid Stats") } });
        Helper::placeActions(m_ribbon->getGroup("lighting", "schedule", tr("Schedule")), remainingActions,
                             { "lighting_ReadScheduleAction" }, { }, { { "lighting_ReadScheduleAction", tr("Read Schedule") } });
        Helper::placeActions(m_ribbon->getGroup("lighting", "wire", tr("Wire Takeoff")), remainingActions,
                             { "lighting_WireRunAction", "lighting_WireTakeoffAction" }, { },
                             { { "lighting_WireRunAction", tr("Wire Run") }, { "lighting_WireTakeoffAction", tr("Takeoff") } });
        Helper::placeRemainingActions(group, remainingActions, tr("More"), moreIcon);
    }
    else if (pluginName == QLatin1String("Editor"))
    {
        std::vector<QAction*> remainingActions = actions;
        PDFRibbonGroup* group = m_ribbon->getGroup("edit", "content", tr("Page Content"));
        Helper::placeActions(group, remainingActions, { "editortool_activateAction", "editortool_createTextAction" }, { });
        Helper::placeRemainingActions(group, remainingActions, tr("Insert"), Helper::getThemedIcon(QStringLiteral("annot-polygon")), { "editortool_undoAction", "editortool_redoAction", "editortool_clearAction" });
        Helper::placeActions(group, remainingActions, { }, { "editortool_undoAction", "editortool_redoAction", "editortool_clearAction" });
    }
    else if (pluginName == QLatin1String("Signature"))
    {
        std::vector<QAction*> remainingActions = actions;
        PDFRibbonGroup* group = m_ribbon->getGroup("protect", "sign", tr("Sign"));
        Helper::placeActions(group, remainingActions,
                             { "signaturetool_signDigitallyAction", "signaturetool_signElectronicallyAction", "signaturetool_activateAction" },
                             { }, { { "signaturetool_activateAction", tr("Signature Tools") } });
        Helper::placeRemainingActions(group, remainingActions, tr("Draw Signature"), Helper::getThemedIcon(QStringLiteral("signature")), { "signaturetool_certificatesAction" });
        Helper::placeActions(group, remainingActions, { }, { "signaturetool_certificatesAction" });
    }
    else if (pluginName == QLatin1String("Redact"))
    {
        std::vector<QAction*> remainingActions = actions;
        PDFRibbonGroup* group = m_ribbon->getGroup("protect", "redact", tr("Redact"));
        Helper::placeActions(group, remainingActions,
                             { "redactplugin_RedactRectangle", "redactplugin_RedactText", "redactplugin_SearchRedact" },
                             { "redactplugin_RedactTextSelection", "redactplugin_RedactPage" });
        Helper::placeActions(group, remainingActions, { "redactplugin_ApplyRedactions" }, { "redactplugin_CreateRedactedDocument" });
        Helper::placeRemainingActions(group, remainingActions, tr("More"), moreIcon);
    }
    else if (pluginName == QLatin1String("Scanner"))
    {
        std::vector<QAction*> remainingActions = actions;
        Helper::placeRemainingActions(m_ribbon->getGroup("convert", "create", tr("Create")), remainingActions, tr("More"), moreIcon);
    }
    else if (pluginName == QLatin1String("AudioBook"))
    {
        std::vector<QAction*> remainingActions = actions;
        PDFRibbonGroup* group = m_ribbon->getGroup("advanced", "plugin_" + pluginName, pluginTitle);
        Helper::placeActions(group, remainingActions, { "actionAudioBook_CreateTextStream", "actionAudioBook_CreateAudioBook" },
                             { "actionAudioBook_SynchronizeFromTableToGraphics", "actionAudioBook_SynchronizeFromGraphicsToTable", "actionAudioBook_Clear" },
                             { { "actionAudioBook_CreateTextStream", tr("Create Text Stream") },
                               { "actionAudioBook_SynchronizeFromTableToGraphics", tr("Sync Table to Page") },
                               { "actionAudioBook_SynchronizeFromGraphicsToTable", tr("Sync Page to Table") },
                               { "actionAudioBook_Clear", tr("Clear Text Stream") } });
        Helper::placeRemainingActions(group, remainingActions, tr("Text Stream"), moreIcon);
    }
    else
    {
        // Other plugins have their own groups on the Advanced tab
        std::vector<QAction*> remainingActions = actions;
        Helper::placeRemainingActions(m_ribbon->getGroup("advanced", "plugin_" + pluginName, pluginTitle), remainingActions, tr("More"), moreIcon);
    }

    // The program controller fills a menu of the plugin, which is not displayed
    QMenu* pluginMenu = new QMenu(plugin->getPluginMenuName(), this);
    pluginMenu->setObjectName(QString("menuPlugin_%1").arg(pluginName));
    return pluginMenu;
}

void PDFEditorMainWindow::onSidebarVisibilityRequested()
{
    if (!m_sidebarDockWidget->isVisible())
    {
        m_sidebarDockWidget->show();
    }

    m_sidebarWidget->setNavigationCollapsed(false);
    m_sidebarDockWidget->raise();
}

void PDFEditorMainWindow::onActionQuitTriggered()
{
    // A tab of the window with several documents closes the whole window
    window()->close();
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

    if (m_developerGroup)
    {
        m_developerGroup->setVisible(isDeveloperMode);
    }
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

        if (m_sidebarWidget)
        {
            m_sidebarWidget->setCurrentPages(currentPages);
        }
    }

    m_pageZoomSpinBox->setValue(m_programController->getPdfWidget()->getDrawWidgetProxy()->getZoom() * 100);
    updateZoomSlider();
}

QMenu* PDFEditorMainWindow::addToolMenu(QString name, pdf::PDFPlugin::PluginMenuLocation location)
{
    QMenu* parentMenu = (location == pdf::PDFPlugin::PluginMenuLocation::Edit) ? ui->menuEdit : ui->menuTools;
    return parentMenu->addMenu(name);
}

void PDFEditorMainWindow::createToolChest()
{
    pdf::PDFToolManager* toolManager = m_programController->getToolManager();
    m_toolChestWidget = new PDFToolChestWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), toolManager, this);
    m_toolChestDockWidget = new QDockWidget(tr("Tool Chest"), this);
    m_toolChestDockWidget->setObjectName("ToolChestDockWidget");
    m_toolChestDockWidget->setWidget(m_toolChestWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_toolChestDockWidget);
    m_toolChestDockWidget->hide();

    m_toolChestAction = m_toolChestDockWidget->toggleViewAction();
    m_toolChestAction->setObjectName("actionToolChest");
    m_toolChestAction->setText(tr("&Tool Chest"));
    m_toolChestAction->setIcon(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-tool-chest")));
    m_toolChestAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_T));
    m_toolChestAction->setToolTip(tr("Show the custom stamps and the markups saved for reuse (styled clouds, callouts with standard texts, ...)"));
    m_actionManager->addAdditionalAction(m_toolChestAction);

    m_addToToolChestAction = new QAction(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-tool-chest-add")), tr("&Add to Tool Chest"), this);
    m_addToToolChestAction->setObjectName("actionAddToToolChest");
    m_addToToolChestAction->setToolTip(tr("Save the selected markups (with their style, text and appearance) to the tool chest to place them again later"));
    connect(m_addToToolChestAction, &QAction::triggered, this, [this]()
    {
        if (m_toolChestWidget->addSelectedMarkups())
        {
            m_toolChestDockWidget->show();
            m_toolChestDockWidget->raise();
        }
    });
    m_actionManager->addAdditionalAction(m_addToToolChestAction);

    m_customStampsAction = new QAction(PDFEditorMainWindowHelper::getIcon(QStringLiteral("ribbon-stamp-custom")), tr("&Custom Stamps..."), this);
    m_customStampsAction->setObjectName("actionManageCustomStamps");
    m_customStampsAction->setToolTip(tr("Create and edit the custom stamps (for example REVIEWED with the company, the author, the date and a logo)"));
    connect(m_customStampsAction, &QAction::triggered, m_toolChestWidget, &PDFToolChestWidget::editStamps);
    m_actionManager->addAdditionalAction(m_customStampsAction);

    if (pdf::PDFWidgetAnnotationManager* annotationManager = m_programController->getPdfWidget()->getDrawWidgetProxy()->getAnnotationManager())
    {
        connect(annotationManager, &pdf::PDFWidgetAnnotationManager::selectionChanged, this, &PDFEditorMainWindow::updateToolChestActions);
    }

    // Custom stamps are listed in the menus of the stamps, they are created again
    // each time the menu is shown (the stamps can be edited)
    QMenu* stampMenu = m_stampMenu ? m_stampMenu : ui->menuStamp;
    QAction* customStampsSeparator = stampMenu->addSeparator();
    stampMenu->addAction(m_customStampsAction);
    connect(stampMenu, &QMenu::aboutToShow, this, [this, stampMenu, customStampsSeparator]()
    {
        qDeleteAll(m_customStampActions);
        m_customStampActions = m_toolChestWidget->createStampActions(stampMenu);
        stampMenu->insertActions(customStampsSeparator, m_customStampActions);
        if (!m_customStampActions.isEmpty())
        {
            stampMenu->insertSeparator(m_customStampActions.front());
        }
    });

    if (m_ribbon)
    {
        m_ribbon->getGroup("comment", "stamps", tr("Stamps"))->addSmallAction(m_customStampsAction, tr("Custom Stamps"));

        PDFRibbonGroup* group = m_ribbon->getGroup("comment", "toolchest", tr("Tool Chest"));
        group->addLargeAction(m_toolChestAction);
        group->addSmallAction(m_addToToolChestAction, tr("Add Selected"));
    }
    else
    {
        ui->menuStamp->addAction(m_addToToolChestAction);
        ui->menuView->addAction(m_toolChestAction);
    }

    updateToolChestActions();
}

void PDFEditorMainWindow::updateToolChestActions()
{
    if (!m_toolChestWidget)
    {
        return;
    }

    pdf::PDFWidgetAnnotationManager* annotationManager = m_programController->getPdfWidget()->getDrawWidgetProxy()->getAnnotationManager();
    const bool hasSelection = annotationManager && !annotationManager->getSelectedAnnotations().empty();
    m_addToToolChestAction->setEnabled(m_programController->getDocument() && hasSelection);
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

    if (m_splitViewWidget)
    {
        m_splitViewWidget->setMainDocument(document, m_programController->getOriginalFileName());
    }

    if (m_markupsListWidget)
    {
        m_markupsListWidget->setDocument(document);
    }

    if (m_toolChestWidget)
    {
        m_toolChestWidget->setDocument(document);
        updateToolChestActions();
    }

    if (m_sidebarWidget && m_ribbon)
    {
        // The strip of the navigation pane is always visible, the pane is opened with a new document
        if (!document || m_sidebarWidget->isEmpty())
        {
            m_sidebarWidget->setNavigationCollapsed(true);
        }
        else if (document.hasReset() && !document.hasPreserveUndoRedo())
        {
            const bool showSidebar = m_programController->getSettings()->getSettings().m_showSidebarOnDocumentOpen;
            m_sidebarWidget->setNavigationCollapsed(!showSidebar);
        }

        m_navigationPaneAction->setEnabled(document && !m_sidebarWidget->isEmpty());
        m_sidebarDockWidget->show();
    }
    else if (m_sidebarWidget)
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
    m_progressTaskbarIndicator->setWindow(window()->windowHandle());
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
            m_programController->requestOpenDocument(urls.front().toLocalFile());
            event->acceptProposedAction();
        }
        else if (urls.size() > 1 && m_openDocumentElsewhereHandler)
        {
            // Several files are opened in the tabs
            for (const QUrl& url : urls)
            {
                m_programController->requestOpenDocument(url.toLocalFile());
            }
            event->acceptProposedAction();
        }
    }
}

bool PDFEditorMainWindow::openDocumentElsewhere(const QString& fileName)
{
    return m_openDocumentElsewhereHandler && m_openDocumentElsewhereHandler(fileName);
}

void PDFEditorMainWindow::setSplitViewVisible(bool visible)
{
    if (visible == (m_splitViewWidget != nullptr))
    {
        return;
    }

    if (visible)
    {
        pdf::PDFWidget* pdfWidget = m_programController->getPdfWidget();
        m_splitViewWidget = new PDFSplitViewWidget(pdfWidget, m_splitter);
        connect(m_splitViewWidget, &PDFSplitViewWidget::closeRequested, this, [this]() { setSplitViewVisible(false); });
        m_splitter->addWidget(m_splitViewWidget);

        if (pdf::PDFDocument* document = m_programController->getDocument())
        {
            m_splitViewWidget->setMainDocument(pdf::PDFModifiedDocument(document, m_programController->getOptionalContentActivity()), m_programController->getOriginalFileName());
        }

        const int width = qMax(m_splitter->width(), 2);
        m_splitter->setSizes({ width / 2, width - width / 2 });
    }
    else
    {
        PDFSplitViewWidget* splitViewWidget = m_splitViewWidget;
        m_splitViewWidget = nullptr;
        delete splitViewWidget;
        m_programController->getPdfWidget()->setFocus();
    }

    QSignalBlocker blocker(m_splitViewAction);
    m_splitViewAction->setChecked(visible);
}


}   // namespace pdfviewer
