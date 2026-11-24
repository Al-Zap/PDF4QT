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
#include "pdfprogramcontroller.h"
#include "pdfbookmarkmanager.h"
#include "pdfsidebarwidget.h"
#include "pdftexttospeech.h"
#include "pdfviewersettings.h"
#include "pdfribbon.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QtTest>
#include <QAction>
#include <QListView>
#include <QSlider>
#include <QTabBar>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QFontMetrics>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QPainter>
#include <QMenu>
#include <QToolButton>
#include <QBoxLayout>
#include <QScrollArea>
#include <QSpinBox>

#include <cmath>
#include <memory>

/// Tests of the ribbon layout of the editor main window: the setting of the mouse wheel
/// (the wheel scrolls / the wheel zooms), the zoom slider of the status bar, the keyboard
/// shortcuts of the actions (there is no menu bar, which would own them), the row of
/// the tabs in narrow windows, groups of the ribbon reduced in narrow windows (small
/// buttons, buttons without texts, collapsed groups) and messages of the status bar,
/// which are not cut off by its widgets.
class RibbonTest : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
    }

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void wheelScrollsByDefault();
    void controlWheelZoomsByDefault();
    void wheelZoomsMode();
    void shiftWheelScrollsSideways();
    void wheelSettingIsPersisted();
    void wheelSettingIsAppliedImmediately();
    void zoomSliderFollowsZoom();
    void zoomSliderSetsZoom();
    void shortcutsBelongToWindow();
    void zoomShortcutWorksWithoutMenuBar();
    void tabRowFitsNarrowWindow();
    void largeButtonLabelsFit();
    void groupsShrinkStepByStep();
    void collapsedGroupOpensDropDown();
    void groupWidthFollowsButtonText();
    void editorRibbonFitsNarrowWindow();
    void statusBarMessageIsNotCutOff();
    void bookmarkListOutlivesBookmarkManager();
    void tabsAreOrganized();

private:
    pdfviewer::PDFProgramController* controller() const { return m_window->getProgramController(); }
    pdf::PDFWidget* pdfWidget() const { return controller()->getPdfWidget(); }
    pdf::PDFDrawWidgetProxy* proxy() const { return pdfWidget()->getDrawWidgetProxy(); }
    QWidget* drawWidget() const { return pdfWidget()->getDrawWidget()->getWidget(); }
    QSlider* zoomSlider() const { return m_window->findChild<QSlider*>("zoomSlider"); }

    void setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelMode mode);
    void sendWheel(int delta, Qt::KeyboardModifiers modifiers);
    void resetView(pdf::PDFReal zoom);

    static int expectedSliderValue(pdf::PDFReal zoom);
    static QIcon createIcon(QColor color);

    /// Standalone ribbon with one page of groups of large and small buttons
    struct TestRibbon
    {
        QWidget window;
        pdfviewer::PDFRibbon* ribbon = nullptr;
        pdfviewer::PDFRibbonPage* page = nullptr;
        std::vector<pdfviewer::PDFRibbonGroup*> groups;
        std::vector<QAction*> actions;
    };

    static void createTestRibbon(TestRibbon& testRibbon);
    static void resizeTestRibbon(TestRibbon& testRibbon, int width);
    static QString describeTestRibbon(const TestRibbon& testRibbon);
    static std::vector<pdfviewer::PDFRibbonGroup::Reduction> getReductions(const TestRibbon& testRibbon);

    QTemporaryDir m_directory;
    QString m_documentFileName;
    std::unique_ptr<pdfviewer::PDFEditorMainWindow> m_window;
};

void RibbonTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Ribbon Test");
    QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()).clear();

    // A document with many pages, so there is always something to scroll
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < 12; ++i)
    {
        builder.appendPage(QRectF(0, 0, 595, 842));
    }
    pdf::PDFDocument document = builder.build();

    m_documentFileName = m_directory.filePath("pages.pdf");
    pdf::PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(m_documentFileName, &document, false));
}

void RibbonTest::init()
{
    // The window is created once: the plugins are loaded as single instances, which keep
    // the widgets of the first window
    if (!m_window)
    {
        QVERIFY(!pdfviewer::PDFEditorMainWindow::isClassicLayoutEnabled());

        m_window = std::make_unique<pdfviewer::PDFEditorMainWindow>();
        m_window->resize(1400, 900);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(m_window->getRibbon());

        controller()->openDocument(m_documentFileName);
        QTRY_VERIFY_WITH_TIMEOUT(controller()->getDocument() != nullptr, 10000);

        // Bookmarks in the sidebar: the bookmark list must not touch the bookmark manager,
        // when the window is destroyed (the manager is deleted by the program controller)
        controller()->getBookmarkManager()->toggleBookmark(0);
        controller()->getBookmarkManager()->toggleBookmark(3);
    }

    m_window->resize(1400, 900);

    // Default wheel mode, immediate scrolling (the result of a wheel step is known at once)
    pdfviewer::PDFViewerSettings::Settings settings = controller()->getSettings()->getSettings();
    settings.m_mouseWheelMode = pdfviewer::PDFViewerSettings::MouseWheelScrolls;
    settings.m_smoothWheelScrolling = false;
    settings.m_wheelScrollHorizontalSpeedPercent = 100;
    settings.m_wheelScrollVerticalSpeedPercent = 100;
    controller()->getSettings()->setSettings(settings);

    proxy()->setPageLayout(pdf::PageLayout::OneColumn);
    QCoreApplication::processEvents();
}

void RibbonTest::cleanupTestCase()
{
    if (m_window)
    {
        // The document is not modified, the window closes without questions
        m_window->close();
        m_window.reset();
    }
}

void RibbonTest::setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelMode mode)
{
    pdfviewer::PDFViewerSettings::Settings settings = controller()->getSettings()->getSettings();
    settings.m_mouseWheelMode = mode;
    controller()->getSettings()->setSettings(settings);
}

void RibbonTest::sendWheel(int delta, Qt::KeyboardModifiers modifiers)
{
    QWidget* widget = drawWidget();
    const QPointF position(widget->width() / 2.0, widget->height() / 2.0);
    QWheelEvent event(position, widget->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}

void RibbonTest::resetView(pdf::PDFReal zoom)
{
    proxy()->zoom(zoom);
    pdfWidget()->getVerticalScrollbar()->setValue(pdfWidget()->getVerticalScrollbar()->maximum() / 2);
    pdfWidget()->getHorizontalScrollbar()->setValue(pdfWidget()->getHorizontalScrollbar()->maximum() / 2);
    QCoreApplication::processEvents();
}

int RibbonTest::expectedSliderValue(pdf::PDFReal zoom)
{
    const pdf::PDFReal minZoom = pdf::PDFDrawWidgetProxy::getMinZoom();
    const pdf::PDFReal maxZoom = pdf::PDFDrawWidgetProxy::getMaxZoom();
    return qRound(std::log(zoom / minZoom) / std::log(maxZoom / minZoom) * 1000.0);
}

void RibbonTest::wheelScrollsByDefault()
{
    QCOMPARE(controller()->getSettings()->getSettings().m_mouseWheelMode, pdfviewer::PDFViewerSettings::MouseWheelScrolls);

    resetView(1.0);
    const pdf::PDFReal zoom = proxy()->getZoom();
    const int scroll = pdfWidget()->getVerticalScrollbar()->value();

    sendWheel(-QWheelEvent::DefaultDeltasPerStep, Qt::NoModifier);

    QCOMPARE(proxy()->getZoom(), zoom);
    QVERIFY2(pdfWidget()->getVerticalScrollbar()->value() > scroll, "The wheel scrolls down");

    // Scrolling does not touch the zoom slider
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(zoom));
}

void RibbonTest::controlWheelZoomsByDefault()
{
    resetView(1.0);
    const int scroll = pdfWidget()->getVerticalScrollbar()->value();

    sendWheel(QWheelEvent::DefaultDeltasPerStep, Qt::ControlModifier);

    QVERIFY(qFuzzyCompare(proxy()->getZoom(), pdf::PDFDrawWidgetProxy::ZOOM_STEP));
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(proxy()->getZoom()));

    sendWheel(-QWheelEvent::DefaultDeltasPerStep, Qt::ControlModifier);
    QVERIFY(qFuzzyCompare(proxy()->getZoom(), 1.0));
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(1.0));
    Q_UNUSED(scroll);
}

void RibbonTest::wheelZoomsMode()
{
    setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelZooms);
    QVERIFY(dynamic_cast<pdf::PDFDrawWidget*>(pdfWidget()->getDrawWidget())->isWheelZooms());

    // The plain wheel zooms at the cursor and the slider follows
    resetView(1.0);
    sendWheel(QWheelEvent::DefaultDeltasPerStep, Qt::NoModifier);
    QVERIFY(qFuzzyCompare(proxy()->getZoom(), pdf::PDFDrawWidgetProxy::ZOOM_STEP));
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(proxy()->getZoom()));

    // Ctrl + wheel scrolls
    resetView(1.0);
    const int scroll = pdfWidget()->getVerticalScrollbar()->value();
    sendWheel(-QWheelEvent::DefaultDeltasPerStep, Qt::ControlModifier);
    QVERIFY(qFuzzyCompare(proxy()->getZoom(), 1.0));
    QVERIFY2(pdfWidget()->getVerticalScrollbar()->value() > scroll, "Ctrl + wheel scrolls down");
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(1.0));

    // Back to the default
    setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelScrolls);
    QVERIFY(!dynamic_cast<pdf::PDFDrawWidget*>(pdfWidget()->getDrawWidget())->isWheelZooms());
    resetView(1.0);
    sendWheel(QWheelEvent::DefaultDeltasPerStep, Qt::NoModifier);
    QVERIFY(qFuzzyCompare(proxy()->getZoom(), 1.0));
}

void RibbonTest::shiftWheelScrollsSideways()
{
    // The page is zoomed in, so it is wider than the window
    for (const pdfviewer::PDFViewerSettings::MouseWheelMode mode : { pdfviewer::PDFViewerSettings::MouseWheelScrolls, pdfviewer::PDFViewerSettings::MouseWheelZooms })
    {
        setWheelMode(mode);
        resetView(4.0);
        QVERIFY(pdfWidget()->getHorizontalScrollbar()->maximum() > 0);

        const int horizontal = pdfWidget()->getHorizontalScrollbar()->value();
        const int vertical = pdfWidget()->getVerticalScrollbar()->value();
        sendWheel(-QWheelEvent::DefaultDeltasPerStep, Qt::ShiftModifier);

        QVERIFY(qFuzzyCompare(proxy()->getZoom(), 4.0));
        QVERIFY2(pdfWidget()->getHorizontalScrollbar()->value() != horizontal, "Shift + wheel scrolls sideways");
        QCOMPARE(pdfWidget()->getVerticalScrollbar()->value(), vertical);
    }
}

void RibbonTest::wheelSettingIsPersisted()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fileName = directory.filePath("settings.ini");

    // Default for a new installation (no value stored) is the Kofax style
    {
        QSettings settings(fileName, QSettings::IniFormat);
        pdfviewer::PDFViewerSettings viewerSettings(nullptr);
        viewerSettings.readSettings(settings, pdf::PDFCMSSettings());
        QCOMPARE(viewerSettings.getSettings().m_mouseWheelMode, pdfviewer::PDFViewerSettings::MouseWheelScrolls);
    }

    {
        QSettings settings(fileName, QSettings::IniFormat);
        pdfviewer::PDFViewerSettings viewerSettings(nullptr);
        pdfviewer::PDFViewerSettings::Settings values = viewerSettings.getSettings();
        values.m_mouseWheelMode = pdfviewer::PDFViewerSettings::MouseWheelZooms;
        viewerSettings.setSettings(values);
        viewerSettings.writeSettings(settings);
    }

    {
        QSettings settings(fileName, QSettings::IniFormat);
        pdfviewer::PDFViewerSettings viewerSettings(nullptr);
        viewerSettings.readSettings(settings, pdf::PDFCMSSettings());
        QCOMPARE(viewerSettings.getSettings().m_mouseWheelMode, pdfviewer::PDFViewerSettings::MouseWheelZooms);
    }

    // An unknown value falls back to the default
    {
        QSettings settings(fileName, QSettings::IniFormat);
        settings.beginGroup("ViewerSettings");
        settings.setValue("mouseWheelMode", 42);
        settings.endGroup();
    }
    {
        QSettings settings(fileName, QSettings::IniFormat);
        pdfviewer::PDFViewerSettings viewerSettings(nullptr);
        viewerSettings.readSettings(settings, pdf::PDFCMSSettings());
        QCOMPARE(viewerSettings.getSettings().m_mouseWheelMode, pdfviewer::PDFViewerSettings::MouseWheelScrolls);
    }
}

void RibbonTest::wheelSettingIsAppliedImmediately()
{
    // The settings dialog changes the settings, the widget follows without a restart
    pdf::PDFDrawWidget* widget = dynamic_cast<pdf::PDFDrawWidget*>(pdfWidget()->getDrawWidget());
    QVERIFY(widget);
    QVERIFY(!widget->isWheelZooms());
    setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelZooms);
    QVERIFY(widget->isWheelZooms());
    setWheelMode(pdfviewer::PDFViewerSettings::MouseWheelScrolls);
    QVERIFY(!widget->isWheelZooms());
}

void RibbonTest::zoomSliderFollowsZoom()
{
    QSlider* slider = zoomSlider();
    QVERIFY(slider);

    for (const pdf::PDFReal zoom : { 0.25, 1.0, 2.5, 8.0 })
    {
        proxy()->zoom(zoom);
        QCoreApplication::processEvents();
        QCOMPARE(slider->value(), expectedSliderValue(zoom));
    }

    // Values outside of the range of the zoom stop at the ends of the slider
    proxy()->zoom(pdf::PDFDrawWidgetProxy::getMaxZoom());
    QCoreApplication::processEvents();
    QCOMPARE(slider->value(), slider->maximum());
    proxy()->zoom(pdf::PDFDrawWidgetProxy::getMinZoom());
    QCoreApplication::processEvents();
    QCOMPARE(slider->value(), slider->minimum());
}

void RibbonTest::zoomSliderSetsZoom()
{
    QSlider* slider = zoomSlider();
    QVERIFY(slider);

    proxy()->zoom(1.0);
    QCoreApplication::processEvents();

    // The slider is logarithmic: its middle is the geometric mean of the limits
    slider->setValue(slider->maximum() / 2);
    QCoreApplication::processEvents();
    const pdf::PDFReal middle = std::sqrt(pdf::PDFDrawWidgetProxy::getMinZoom() * pdf::PDFDrawWidgetProxy::getMaxZoom());
    QVERIFY2(std::abs(proxy()->getZoom() - middle) < middle * 0.01, qPrintable(QString("Zoom %1, expected %2").arg(proxy()->getZoom()).arg(middle)));

    // A step of the slider is the same ratio of the zoom anywhere on the slider
    slider->setValue(250);
    QCoreApplication::processEvents();
    const pdf::PDFReal zoom1 = proxy()->getZoom();
    slider->setValue(300);
    QCoreApplication::processEvents();
    const pdf::PDFReal zoom2 = proxy()->getZoom();
    slider->setValue(700);
    QCoreApplication::processEvents();
    const pdf::PDFReal zoom3 = proxy()->getZoom();
    slider->setValue(750);
    QCoreApplication::processEvents();
    const pdf::PDFReal zoom4 = proxy()->getZoom();
    QVERIFY(std::abs(zoom2 / zoom1 - zoom4 / zoom3) < 0.01);

    // The slider stays where the user put it (the zoom does not move it back)
    QCOMPARE(slider->value(), 750);
}

void RibbonTest::shortcutsBelongToWindow()
{
    // Without the menu bar, an action with a shortcut works only if it belongs to the window
    int checkedShortcuts = 0;
    for (QAction* action : m_window->findChildren<QAction*>())
    {
        if (!action || action->shortcut().isEmpty() || action->shortcutContext() != Qt::WindowShortcut)
        {
            continue;
        }

        bool isActive = false;
        for (QObject* object : action->associatedObjects())
        {
            QWidget* widget = qobject_cast<QWidget*>(object);
            if (widget && widget->window() == m_window.get() && widget->isVisible())
            {
                isActive = true;
                break;
            }
        }

        QVERIFY2(isActive, qPrintable(QString("Shortcut %1 of the action '%2' (%3) does not work in the window").arg(action->shortcut().toString(), action->text(), action->objectName())));
        ++checkedShortcuts;
    }

    QVERIFY(checkedShortcuts > 10);

    // No shortcut is used by two enabled actions of the window (ambiguous shortcuts do nothing).
    // Disabled actions do not take part, for example the tools of the page content editor,
    // which are enabled only while the page content is edited.
    QHash<QString, QString> usedShortcuts;
    for (QAction* action : m_window->findChildren<QAction*>())
    {
        if (!action || action->shortcut().isEmpty() || !action->isEnabled() || action->shortcutContext() != Qt::WindowShortcut)
        {
            continue;
        }

        const QString shortcut = action->shortcut().toString();
        QVERIFY2(!usedShortcuts.contains(shortcut), qPrintable(QString("Shortcut %1 is used by '%2' and '%3'").arg(shortcut, usedShortcuts.value(shortcut), action->objectName())));
        usedShortcuts.insert(shortcut, action->objectName());
    }
}

void RibbonTest::zoomShortcutWorksWithoutMenuBar()
{
    proxy()->zoom(1.0);
    QCoreApplication::processEvents();

    drawWidget()->setFocus();
    QTest::keySequence(m_window.get(), QKeySequence(QKeySequence::ZoomIn));
    QCoreApplication::processEvents();
    QVERIFY2(proxy()->getZoom() > 1.0, "Zoom In shortcut works");
    QCOMPARE(zoomSlider()->value(), expectedSliderValue(proxy()->getZoom()));

    const pdf::PDFReal zoomedIn = proxy()->getZoom();
    QTest::keySequence(m_window.get(), QKeySequence(QKeySequence::ZoomOut));
    QCoreApplication::processEvents();
    QVERIFY2(proxy()->getZoom() < zoomedIn, "Zoom Out shortcut works");
}

void RibbonTest::tabRowFitsNarrowWindow()
{
    QTabBar* tabBar = m_window->findChild<QTabBar*>("ribbonTabBar");
    QWidget* searchBox = m_window->findChild<QWidget*>("ribbonSearchBox");
    QVERIFY(tabBar);
    QVERIFY(searchBox);

    // Corner widgets (quick access buttons, search box, collapse button) never overlap the tabs
    for (const int width : { 1600, 1300, 1150, 1050, 1000, 900, 700 })
    {
        m_window->resize(width, 800);
        QCoreApplication::processEvents();
        QTest::qWait(20);

        QWidget* tabRow = tabBar->parentWidget();
        const int tabsRight = tabBar->geometry().right();

        for (QWidget* widget : tabRow->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (widget == tabBar || !widget->isVisible() || widget->objectName() == "ribbonFileButton")
            {
                continue;
            }

            QVERIFY2(widget->geometry().left() > tabsRight, qPrintable(QString("Width %1: '%2' overlaps the tabs").arg(width).arg(widget->objectName())));
            QVERIFY2(widget->geometry().right() <= tabRow->width(), qPrintable(QString("Width %1: '%2' is outside of the window").arg(width).arg(widget->objectName())));
        }

        // The last tab is either fully visible, or the tabs can be scrolled
        const QRect lastTab = tabBar->tabRect(tabBar->count() - 1);
        QVERIFY2(lastTab.right() <= tabBar->width() || tabBar->usesScrollButtons(), qPrintable(QString("Width %1: Help tab is cut off").arg(width)));
    }

    // The search box is visible in a wide window and it is hidden only in a narrow one
    m_window->resize(1600, 800);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QVERIFY(searchBox->isVisible());
    QVERIFY(!tabBar->usesScrollButtons());
}

void RibbonTest::largeButtonLabelsFit()
{
    // Buttons have their full size only in a window wide enough for all groups
    m_window->resize(3000, 900);
    QCoreApplication::processEvents();

    const QList<pdfviewer::PDFRibbonButton*> buttons = m_window->getRibbon()->findChildren<pdfviewer::PDFRibbonButton*>();
    QVERIFY(!buttons.isEmpty());

    int largeButtonsWithMenu = 0;
    for (pdfviewer::PDFRibbonButton* button : buttons)
    {
        if (button->property("ribbonSize").toString() != QLatin1String("large"))
        {
            continue;
        }

        button->ensurePolished();
        const QString text = button->text();

        // The arrow of the menu lies under the text, a second line of the text would be cut off (Sticky Note)
        if (button->menu())
        {
            ++largeButtonsWithMenu;
            QVERIFY2(!text.contains(QChar('\n')), qPrintable(QString("'%1' has a menu and a wrapped text").arg(text)));
        }

        // The fixed height of a large button holds its icon, all lines of its text and the arrow of its menu
        const QSize contentSize = button->QToolButton::sizeHint();
        QVERIFY2(contentSize.height() <= button->sizeHint().height(), qPrintable(QString("'%1' needs %2 px, the button has %3 px").arg(text).arg(contentSize.height()).arg(button->sizeHint().height())));
    }

    QVERIFY(largeButtonsWithMenu > 0);
}

void RibbonTest::bookmarkListOutlivesBookmarkManager()
{
    // The sidebar can outlive the bookmark manager (the window deletes the program controller
    // first). A style change then lays out the bookmark list again, which must not read
    // the bookmarks of the deleted manager.
    pdfviewer::PDFBookmarkManager* bookmarkManager = new pdfviewer::PDFBookmarkManager(nullptr);
    bookmarkManager->toggleBookmark(1);
    bookmarkManager->toggleBookmark(2);
    bookmarkManager->toggleBookmark(5);

    // Own text to speech: the sidebar registers its widgets in it, and the sidebar is destroyed before the window
    pdfviewer::PDFTextToSpeech textToSpeech(nullptr);
    textToSpeech.setSettings(controller()->getSettings());
    textToSpeech.setProxy(proxy());

    pdfviewer::PDFSidebarWidget sidebar(proxy(), &textToSpeech, controller()->getCertificateStore(), bookmarkManager, controller()->getSettings(), true, nullptr);
    sidebar.resize(300, 600);
    sidebar.selectPage(pdfviewer::PDFSidebarWidget::Bookmarks);
    sidebar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sidebar));

    QListView* bookmarksView = sidebar.findChild<QListView*>("bookmarksView");
    QVERIFY(bookmarksView);
    QCOMPARE(bookmarksView->model()->rowCount(), 3);
    QVERIFY(bookmarksView->sizeHintForRow(0) > 0);

    delete bookmarkManager;
    QCOMPARE(bookmarksView->model()->rowCount(), 0);

    // Style change, as the reparenting of the dock widget in the destructor of the window
    sidebar.setStyleSheet(QStringLiteral("QListView { padding: 1px; }"));
    bookmarksView->doItemsLayout();
    bookmarksView->repaint();
    QCoreApplication::processEvents();
    QCOMPARE(bookmarksView->model()->rowCount(), 0);
}

QIcon RibbonTest::createIcon(QColor color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(color);
    return QIcon(pixmap);
}

void RibbonTest::createTestRibbon(TestRibbon& testRibbon)
{
    // The ribbon is resized directly, not by the window: the platform can limit the
    // size of a window to the size of the screen, a child widget can be wider
    testRibbon.ribbon = new pdfviewer::PDFRibbon(&testRibbon.window);

    testRibbon.page = testRibbon.ribbon->addPage("home", "Home");

    const QStringList groupNames = { "Edit", "Pages", "Markup", "Measure" };
    for (int groupIndex = 0; groupIndex < groupNames.size(); ++groupIndex)
    {
        pdfviewer::PDFRibbonGroup* group = testRibbon.page->getGroup(groupNames[groupIndex], groupNames[groupIndex]);
        testRibbon.groups.push_back(group);

        for (int i = 0; i < 3; ++i)
        {
            QAction* action = new QAction(createIcon(QColor::fromHsv(groupIndex * 60 + i * 15, 200, 200)), QString("%1 Action %2").arg(groupNames[groupIndex]).arg(i + 1), &testRibbon.window);
            action->setObjectName(QString("action_%1_%2").arg(groupIndex).arg(i));
            testRibbon.actions.push_back(action);
            group->addLargeAction(action);
        }

        // Many small buttons: the buttons without the texts take more space than the
        // collapsed group with any font
        for (int i = 0; i < 12; ++i)
        {
            QAction* action = new QAction(createIcon(QColor::fromHsv(groupIndex * 60 + i * 5, 120, 230)), QString("Small %1").arg(i + 1), &testRibbon.window);
            testRibbon.actions.push_back(action);
            group->addSmallAction(action);
        }
    }

    testRibbon.ribbon->updateGroups();
    testRibbon.window.resize(400, 200);
    testRibbon.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&testRibbon.window));
    resizeTestRibbon(testRibbon, 10000);
}

QString RibbonTest::describeTestRibbon(const TestRibbon& testRibbon)
{
    using Reduction = pdfviewer::PDFRibbonGroup::Reduction;

    QStringList groups;
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        groups << QString("%1 [%2] %3/%4/%5/%6 px, now %7 px").arg(group->getTitle()).arg(int(group->getReduction()))
                  .arg(group->getReducedWidth(Reduction::None)).arg(group->getReducedWidth(Reduction::SmallButtons))
                  .arg(group->getReducedWidth(Reduction::IconButtons)).arg(group->getReducedWidth(Reduction::Collapsed)).arg(group->width());
    }

    return QString("ribbon %1 px, page %2 px (hint %3 px): %4").arg(testRibbon.ribbon->width()).arg(testRibbon.page->width()).arg(testRibbon.page->sizeHint().width()).arg(groups.join(", "));
}

void RibbonTest::resizeTestRibbon(TestRibbon& testRibbon, int width)
{
    testRibbon.ribbon->resize(width, testRibbon.ribbon->sizeHint().height());
    QCoreApplication::processEvents();
    QCOMPARE(testRibbon.ribbon->width(), width);
}

std::vector<pdfviewer::PDFRibbonGroup::Reduction> RibbonTest::getReductions(const TestRibbon& testRibbon)
{
    std::vector<pdfviewer::PDFRibbonGroup::Reduction> reductions;
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        reductions.push_back(group->getReduction());
    }
    return reductions;
}

void RibbonTest::groupsShrinkStepByStep()
{
    using Reduction = pdfviewer::PDFRibbonGroup::Reduction;

    TestRibbon testRibbon;
    createTestRibbon(testRibbon);
    if (QTest::currentTestFailed())
    {
        return;
    }

    // Wide window: groups have their full size
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        QVERIFY2(group->getReduction() == Reduction::None, qPrintable(describeTestRibbon(testRibbon)));
    }

    const int fullWidth = testRibbon.page->sizeHint().width();
    // Each reduction makes the groups narrower
    int collapsedWidth = 0;
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        collapsedWidth = qMax(collapsedWidth, group->getReducedWidth(Reduction::Collapsed));
        QVERIFY2(group->getReducedWidth(Reduction::Collapsed) < group->getReducedWidth(Reduction::IconButtons), qPrintable(describeTestRibbon(testRibbon)));
        QVERIFY2(group->getReducedWidth(Reduction::IconButtons) < group->getReducedWidth(Reduction::SmallButtons), qPrintable(describeTestRibbon(testRibbon)));
        QVERIFY2(group->getReducedWidth(Reduction::SmallButtons) < group->getReducedWidth(Reduction::None), qPrintable(describeTestRibbon(testRibbon)));
    }

    // Slightly narrower window: only the last group gets small buttons
    resizeTestRibbon(testRibbon, fullWidth - 10);
    std::vector<Reduction> reductions = getReductions(testRibbon);
    QCOMPARE(reductions.back(), Reduction::SmallButtons);
    QCOMPARE(reductions.front(), Reduction::None);

    // Shrinking the window step by step: no group is ever enlarged, all groups get small
    // buttons before any group loses the texts, and the page always fits into the window
    std::vector<Reduction> previousReductions = reductions;
    for (int width = fullWidth - 10; width >= 4 * collapsedWidth + 10; width -= 15)
    {
        resizeTestRibbon(testRibbon, width);

        reductions = getReductions(testRibbon);
        const Reduction maximum = *std::max_element(reductions.cbegin(), reductions.cend());
        const Reduction minimum = *std::min_element(reductions.cbegin(), reductions.cend());

        for (size_t i = 0; i < reductions.size(); ++i)
        {
            QVERIFY2(reductions[i] >= previousReductions[i], qPrintable(QString("Width %1: group %2 was enlarged").arg(width).arg(i)));

            // Groups on the right are reduced first
            if (i + 1 < reductions.size())
            {
                QVERIFY2(reductions[i] <= reductions[i + 1], qPrintable(QString("Width %1: group %2 is smaller than the group on its right").arg(width).arg(i)));
            }
        }

        QVERIFY2(int(maximum) - int(minimum) <= 1, qPrintable(QString("Width %1: reductions differ by more than one step").arg(width)));
        QVERIFY2(testRibbon.page->sizeHint().width() <= testRibbon.ribbon->width(), qPrintable(QString("Width %1: page needs %2 px").arg(width).arg(testRibbon.page->sizeHint().width())));
        QVERIFY2(!testRibbon.ribbon->findChild<QScrollArea*>("ribbonPages")->horizontalScrollBar()->maximum(), qPrintable(QString("Width %1: page is scrolled").arg(width)));

        // Buttons follow the reduction of their groups
        for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
        {
            const QList<pdfviewer::PDFRibbonButton*> buttons = group->getContentWidget()->findChildren<pdfviewer::PDFRibbonButton*>();
            for (pdfviewer::PDFRibbonButton* button : buttons)
            {
                switch (group->getReduction())
                {
                    case Reduction::None:
                        break;
                    case Reduction::SmallButtons:
                        QVERIFY(button->getButtonSize() == pdfviewer::PDFRibbonButton::Size::Small);
                        break;
                    case Reduction::IconButtons:
                        QVERIFY(button->getButtonSize() == pdfviewer::PDFRibbonButton::Size::IconOnly);
                        QVERIFY(!button->toolTip().isEmpty());
                        break;
                    case Reduction::Collapsed:
                        QVERIFY(!group->getContentWidget()->isVisible());
                        QVERIFY(group->getCollapsedButton() && group->getCollapsedButton()->isVisible());
                        break;
                    default:
                        QFAIL("Invalid reduction");
                }
            }
        }

        previousReductions = reductions;
    }

    // All groups are collapsed in a window, which is just wide enough for the collapsed groups
    int allCollapsedWidth = 4;
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        allCollapsedWidth += group->getReducedWidth(Reduction::Collapsed);
    }
    resizeTestRibbon(testRibbon, allCollapsedWidth + 2);
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        QCOMPARE(group->getReduction(), Reduction::Collapsed);
    }

    // Too narrow even for the collapsed groups: the page can be scrolled again
    resizeTestRibbon(testRibbon, allCollapsedWidth - 20);
    QVERIFY(testRibbon.ribbon->findChild<QScrollArea*>("ribbonPages")->horizontalScrollBar()->maximum() > 0);

    // Wide window again: full size of all groups, large buttons
    resizeTestRibbon(testRibbon, 10000);
    for (pdfviewer::PDFRibbonGroup* group : testRibbon.groups)
    {
        QCOMPARE(group->getReduction(), Reduction::None);
        QVERIFY(group->getContentWidget()->isVisible());
        QVERIFY(!group->getCollapsedButton() || !group->getCollapsedButton()->isVisible());
    }
    QCOMPARE(testRibbon.page->sizeHint().width(), fullWidth);
}

void RibbonTest::collapsedGroupOpensDropDown()
{
    using Reduction = pdfviewer::PDFRibbonGroup::Reduction;

    TestRibbon testRibbon;
    createTestRibbon(testRibbon);
    if (QTest::currentTestFailed())
    {
        return;
    }

    pdfviewer::PDFRibbonGroup* group = testRibbon.groups.back();
    const int collapsedWidth = group->getReducedWidth(Reduction::Collapsed);
    resizeTestRibbon(testRibbon, 4 * collapsedWidth + 10);
    QCOMPARE(group->getReduction(), Reduction::Collapsed);

    pdfviewer::PDFRibbonButton* collapsedButton = group->getCollapsedButton();
    QVERIFY(collapsedButton);
    QVERIFY(collapsedButton->isVisible());
    QCOMPARE(collapsedButton->text(), QString("Measure"));
    QVERIFY(!collapsedButton->icon().isNull());
    QCOMPARE(collapsedButton->menu(), group->getCollapsedMenu());

    // The drop down window shows the full group with large buttons
    QMenu* menu = group->getCollapsedMenu();
    menu->popup(collapsedButton->mapToGlobal(QPoint(0, collapsedButton->height())));
    QTRY_VERIFY(menu->isVisible());
    QWidget* content = group->getContentWidget();
    QCOMPARE(content->window(), static_cast<QWidget*>(menu));
    QVERIFY(content->isVisible());

    QAction* firstAction = testRibbon.window.findChild<QAction*>("action_3_0");
    QVERIFY(firstAction);
    pdfviewer::PDFRibbonButton* firstButton = nullptr;
    for (pdfviewer::PDFRibbonButton* button : content->findChildren<pdfviewer::PDFRibbonButton*>())
    {
        if (button->getAction() == firstAction)
        {
            firstButton = button;
        }
    }
    QVERIFY(firstButton);
    QVERIFY(firstButton->isVisible());
    QVERIFY(firstButton->getButtonSize() == pdfviewer::PDFRibbonButton::Size::Large);
    QVERIFY(menu->sizeHint().width() >= content->sizeHint().width());

    // The window is too narrow, but the window of the collapsed group keeps its content
    resizeTestRibbon(testRibbon, 4 * collapsedWidth + 20);
    QCOMPARE(content->window(), static_cast<QWidget*>(menu));

    // A click on a button triggers the action and closes the window, the buttons return to the group
    QSignalSpy triggeredSpy(firstAction, &QAction::triggered);
    QTest::mouseClick(firstButton, Qt::LeftButton);
    QCOMPARE(triggeredSpy.count(), 1);
    QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(content->parentWidget(), static_cast<QWidget*>(group));
    QVERIFY(!content->isVisible());
    QVERIFY(collapsedButton->isVisible());

    // The group gets its buttons back in a wide window
    resizeTestRibbon(testRibbon, 10000);
    QCOMPARE(group->getReduction(), Reduction::None);
    QVERIFY(content->isVisible());
    QVERIFY(!collapsedButton->isVisible());
    QVERIFY(firstButton->isVisible());
    QVERIFY(group->geometry().right() <= testRibbon.ribbon->width());
}

void RibbonTest::groupWidthFollowsButtonText()
{
    using Reduction = pdfviewer::PDFRibbonGroup::Reduction;

    TestRibbon testRibbon;
    createTestRibbon(testRibbon);
    if (QTest::currentTestFailed())
    {
        return;
    }

    const int fullWidth = testRibbon.page->sizeHint().width();
    resizeTestRibbon(testRibbon, fullWidth + 5);
    QCOMPARE(testRibbon.groups.back()->getReduction(), Reduction::None);

    // A longer text of a button makes the page wider, the ribbon reduces the last group
    testRibbon.actions.back()->setText("Small button with a very very long text");
    QTRY_COMPARE(testRibbon.groups.back()->getReduction(), Reduction::SmallButtons);
    QVERIFY(testRibbon.page->sizeHint().width() <= testRibbon.ribbon->width());

    // A hidden button makes it narrower again
    testRibbon.actions.back()->setText("S");
    QTRY_COMPARE(testRibbon.groups.back()->getReduction(), Reduction::None);
}

void RibbonTest::editorRibbonFitsNarrowWindow()
{
    using Reduction = pdfviewer::PDFRibbonGroup::Reduction;

    pdfviewer::PDFRibbon* ribbon = m_window->getRibbon();
    QTabBar* tabBar = m_window->findChild<QTabBar*>("ribbonTabBar");
    QScrollArea* scrollArea = ribbon->findChild<QScrollArea*>("ribbonPages");
    QVERIFY(tabBar);
    QVERIFY(scrollArea);

    QStringList pageIds;
    for (QObject* child : ribbon->findChildren<pdfviewer::PDFRibbonPage*>())
    {
        pageIds << static_cast<pdfviewer::PDFRibbonPage*>(child)->getId();
    }
    QVERIFY(pageIds.size() > 3);

    const QString currentPageId = ribbon->getCurrentPageId();

    // Windows about 1000 px wide (and narrower) show all groups of every page without
    // scrolling, the groups are reduced instead
    for (const int width : { 1400, 1000, 900, 800 })
    {
        m_window->resize(width, 800);
        QCoreApplication::processEvents();

        int reducedGroups = 0;
        for (const QString& pageId : pageIds)
        {
            ribbon->setCurrentPage(pageId);
            QCoreApplication::processEvents();

            pdfviewer::PDFRibbonPage* page = ribbon->getPage(pageId);
            QVERIFY2(page->sizeHint().width() <= ribbon->width(), qPrintable(QString("Width %1: page '%2' needs %3 px of %4 px").arg(width).arg(pageId).arg(page->sizeHint().width()).arg(ribbon->width())));
            QVERIFY2(scrollArea->horizontalScrollBar()->maximum() == 0, qPrintable(QString("Width %1: page '%2' scrolls").arg(width).arg(pageId)));

            for (pdfviewer::PDFRibbonGroup* group : page->getGroups())
            {
                if (group->isEmpty() || group->isHidden())
                {
                    continue;
                }

                // Every group is visible inside of the window
                const QRect groupRect(group->mapTo(ribbon, QPoint(0, 0)), group->size());
                QVERIFY2(groupRect.right() <= ribbon->width(), qPrintable(QString("Width %1: group '%2' is outside of the window").arg(width).arg(group->getTitle())));

                if (group->getReduction() != Reduction::None)
                {
                    ++reducedGroups;
                }
            }
        }

        if (width <= 1000)
        {
            QVERIFY2(reducedGroups > 0, qPrintable(QString("Width %1: no group is reduced").arg(width)));
        }
    }

    // A wide window shows all groups in full size, if they fit (the platform can limit
    // the size of the window to the size of the screen)
    m_window->resize(3000, 800);
    QCoreApplication::processEvents();
    for (const QString& pageId : pageIds)
    {
        ribbon->setCurrentPage(pageId);
        QCoreApplication::processEvents();

        int fullWidth = 4;
        for (pdfviewer::PDFRibbonGroup* group : ribbon->getPage(pageId)->getGroups())
        {
            if (!group->isEmpty() && !group->isHidden())
            {
                fullWidth += group->getReducedWidth(Reduction::None);
            }
        }

        for (pdfviewer::PDFRibbonGroup* group : ribbon->getPage(pageId)->getGroups())
        {
            QVERIFY2(group->getReduction() == Reduction::None || fullWidth > ribbon->width(), qPrintable(QString("Group '%1' of '%2' is reduced, the full page needs %3 px of %4 px").arg(group->getTitle(), pageId).arg(fullWidth).arg(ribbon->width())));
        }
        QVERIFY(ribbon->getPage(pageId)->sizeHint().width() <= ribbon->width());
    }

    ribbon->setCurrentPage(currentPageId);
}

void RibbonTest::statusBarMessageIsNotCutOff()
{
    QStatusBar* statusBar = m_window->statusBar();
    pdfviewer::PDFStatusBarMessageFitter* fitter = statusBar->findChild<pdfviewer::PDFStatusBarMessageFitter*>("statusBarMessageFitter");
    QSlider* slider = zoomSlider();
    QVERIFY(fitter);
    QVERIFY(slider);

    // Buttons of the first and the last page are hidden last
    QToolButton* firstPageButton = nullptr;
    for (QToolButton* button : statusBar->findChildren<QToolButton*>())
    {
        if (button->defaultAction() && button->defaultAction()->objectName() == "actionGoToDocumentStart")
        {
            firstPageButton = button;
        }
    }
    QVERIFY(firstPageButton);

    // The message fits, or all widgets, which can be hidden, are hidden (the progress
    // bar of a long operation takes the space too)
    auto checkMessageFits = [&](int requiredWidth)
    {
        const bool fits = fitter->getMessageWidth() >= requiredWidth;
        return fits || (!firstPageButton->isVisible() && !slider->isVisible());
    };

    const QString longMessage = "Click to add the next point of the polyline, double click or press Enter to finish it.";

    // In a window about 1000 px wide, the long message of a tool was cut off by the widgets of the status bar
    for (const int width : { 1000, 900, 1200 })
    {
        m_window->resize(width, 800);
        QCoreApplication::processEvents();
        statusBar->clearMessage();
        QVERIFY(slider->isVisible());

        // Other messages (errors of the rendering) can replace the message, while the events are processed
        statusBar->showMessage(longMessage);
        const int requiredWidth = pdfviewer::PDFStatusBarMessageFitter::getRequiredWidth(statusBar, longMessage);
        QVERIFY2(checkMessageFits(requiredWidth), qPrintable(QString("Width %1: message needs %2 px, it has %3 px").arg(width).arg(requiredWidth).arg(fitter->getMessageWidth())));
        if (width <= 1000)
        {
            QVERIFY(!slider->isVisible());
        }

        // Page number and zoom value stay visible
        for (QAbstractSpinBox* spinBox : statusBar->findChildren<QAbstractSpinBox*>())
        {
            QVERIFY(spinBox->isVisible());
        }

        // The window is resized while the message is shown
        m_window->resize(width - 50, 800);
        QCoreApplication::processEvents();
        statusBar->showMessage(longMessage);
        QVERIFY2(checkMessageFits(requiredWidth), qPrintable(QString("Width %1: message needs %2 px, it has %3 px, status bar %4 px").arg(width - 50).arg(requiredWidth).arg(fitter->getMessageWidth()).arg(statusBar->width())));

        // The widgets are shown again, when the message disappears
        statusBar->clearMessage();
        QVERIFY(slider->isVisible());
    }

    // A short message does not hide anything
    m_window->resize(1400, 800);
    QCoreApplication::processEvents();
    statusBar->showMessage("Ready");
    QVERIFY(slider->isVisible());
    statusBar->clearMessage();
    QVERIFY(slider->isVisible());
}

void RibbonTest::tabsAreOrganized()
{
    pdfviewer::PDFRibbon* ribbon = m_window->getRibbon();
    QTabBar* tabBar = m_window->findChild<QTabBar*>("ribbonTabBar");
    QVERIFY(tabBar);

    // Lighting tools have their own tab, right after the general measuring
    QStringList tabs;
    for (int i = 0; i < tabBar->count(); ++i)
    {
        tabs << tabBar->tabText(i).remove('&');
    }
    QCOMPARE(tabs, QStringList({ "Home", "Edit", "Comment", "Measure", "Lighting", "Pages", "Forms", "Convert", "Protect", "View", "Advanced", "Help" }));

    auto pageActions = [ribbon](const QString& pageId)
    {
        QSet<QAction*> actions;
        for (pdfviewer::PDFRibbonButton* button : ribbon->getPage(pageId)->findChildren<pdfviewer::PDFRibbonButton*>())
        {
            if (button->getAction())
            {
                actions.insert(button->getAction());
            }
        }
        return actions;
    };

    if (QAction* countAction = m_window->findChild<QAction*>("fixturecount_CountAction"))
    {
        QVERIFY(pageActions("lighting").contains(countAction));
        QVERIFY(!pageActions("measure").contains(countAction));
    }

    // A command has one place outside of the Home tab (Home is a selection of the commands
    // used most). The exceptions are the commands, which finish the work of a tab (save,
    // print, the tools for filling the forms) and the text tools used for comments and edits.
    const QStringList sharedCommands = { "actionSave", "actionSaveFlattenedCopy", "actionPrint", "actionHandTool",
                                         "actionCreateTypewriter", "actionInlineText", "actionSelectText" };
    QHash<QAction*, QString> placeOfAction;
    for (QObject* child : ribbon->findChildren<pdfviewer::PDFRibbonPage*>())
    {
        const QString pageId = static_cast<pdfviewer::PDFRibbonPage*>(child)->getId();
        if (pageId == "home")
        {
            continue;
        }

        for (QAction* action : pageActions(pageId))
        {
            if (sharedCommands.contains(action->objectName()))
            {
                continue;
            }

            QVERIFY2(!placeOfAction.contains(action), qPrintable(QString("'%1' is on the tabs '%2' and '%3'").arg(action->objectName(), placeOfAction.value(action), pageId)));
            placeOfAction.insert(action, pageId);
        }
    }

    // The View tab shows the texts of all its buttons in a maximized full HD window.
    // The width is given for a font 14 pixels high (9 points at 96 dpi), platforms
    // with a larger font (the offscreen platform of Windows) get a wider window.
    const QString currentPageId = ribbon->getCurrentPageId();
    const int fontHeight = QFontMetrics(ribbon->font()).height();
    const int fullHdWidth = qMax(1920, qRound(1920.0 * fontHeight / 14.0));
    m_window->resize(fullHdWidth, 1000);
    ribbon->setCurrentPage("view");
    QCoreApplication::processEvents();
    if (ribbon->width() >= fullHdWidth - 20)
    {
        // The page is fitted to its width after the layout, the widths of the groups
        // without reduction are reported, if it is not wide enough
        auto isNotReduced = [ribbon]()
        {
            for (pdfviewer::PDFRibbonGroup* group : ribbon->getPage("view")->getGroups())
            {
                if (group->getReduction() != pdfviewer::PDFRibbonGroup::Reduction::None)
                {
                    return false;
                }
            }
            return true;
        };
        const bool fitted = QTest::qWaitFor(isNotReduced, 2000);
        if (!fitted || qEnvironmentVariableIsSet("PDF4QT_RIBBON_DUMP"))
        {
            QStringList widths;
            for (pdfviewer::PDFRibbonGroup* group : ribbon->getPage("view")->getGroups())
            {
                widths << QString("%1=%2").arg(group->getTitle()).arg(group->getReducedWidth(pdfviewer::PDFRibbonGroup::Reduction::None));
            }
            const QFontMetrics fontMetrics(ribbon->font());
            QFAIL(qPrintable(QString("View tab reduced at the width %1: %2 (font %3, average character width %4, height %5)").arg(ribbon->width()).arg(widths.join(", "), ribbon->font().toString()).arg(fontMetrics.averageCharWidth()).arg(fontMetrics.height())));
        }
    }
    ribbon->setCurrentPage(currentPageId);
}

QTEST_MAIN(RibbonTest)

#include "tst_ribbontest.moc"
