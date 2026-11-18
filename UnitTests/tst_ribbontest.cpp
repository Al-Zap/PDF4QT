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
#include "pdfviewersettings.h"
#include "pdfribbon.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QtTest>
#include <QAction>
#include <QSlider>
#include <QTabBar>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QStandardPaths>

#include <cmath>
#include <memory>

/// Tests of the ribbon layout of the editor main window: the setting of the mouse wheel
/// (the wheel scrolls / the wheel zooms), the zoom slider of the status bar, the keyboard
/// shortcuts of the actions (there is no menu bar, which would own them) and the row of
/// the tabs in narrow windows.
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

QTEST_MAIN(RibbonTest)

#include "tst_ribbontest.moc"
