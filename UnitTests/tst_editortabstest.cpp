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

#include "pdfeditortabwindow.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfsplitviewwidget.h"
#include "pdfribbon.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfplugin.h"

#include <QtTest>
#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QSplitter>
#include <QScrollBar>
#include <QTabWidget>
#include <QToolButton>

#include <set>

/// Tests of the window of the editor with several documents in tabs (PDFEditorTabWindow)
/// and of the split view of the editor (PDFSplitViewWidget): documents opened in tabs
/// (an opened document is shown, empty tab reused), documents requested from an editor
/// (open dialog, recent files, results of the dialogs) go to new tabs, own instances
/// of the plugins for each tab, closing of the tabs, split view with the same document
/// (follows the edits) and with another file (synchronized zoom and scrolling).
class EditorTabsTest : public QObject
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
    void cleanup();

    void documentsOpenInTabs();
    void requestedDocumentsOpenInNewTabs();
    void eachTabHasOwnPlugins();
    void closeActionClosesTab();
    void nextAndPreviousTab();
    void windowClosesAllTabs();
    void splitViewShowsSameDocument();
    void splitViewFollowsEdits();
    void splitViewSynchronizesOtherFile();
    void splitViewIsClosed();

private:
    void createWindow();
    void openAndWait(const QString& fileName);
    static bool hasDocument(pdfviewer::PDFEditorMainWindow* editor);
    static void addAnnotation(pdfviewer::PDFEditorMainWindow* editor);
    static std::set<QAction*> getPluginActionsInRibbon(pdfviewer::PDFEditorMainWindow* editor);
    static QString writeDocument(const QString& fileName, int pageCount, QSizeF pageSize);

    QTemporaryDir m_directory;
    QString m_fileA;
    QString m_fileB;
    QString m_fileC;
    std::unique_ptr<pdfviewer::PDFEditorTabWindow> m_window;
};

void EditorTabsTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Editor Tabs Test");
    QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()).clear();

    // Recovery copies of an interrupted run would be offered by a message box
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).removeRecursively();

    m_fileA = writeDocument(m_directory.filePath("a.pdf"), 12, QSizeF(595, 842));
    m_fileB = writeDocument(m_directory.filePath("b.pdf"), 12, QSizeF(595, 842));
    m_fileC = writeDocument(m_directory.filePath("c.pdf"), 3, QSizeF(842, 595));
    QVERIFY(!m_fileA.isEmpty() && !m_fileB.isEmpty() && !m_fileC.isEmpty());
}

void EditorTabsTest::cleanup()
{
    if (m_window)
    {
        // Edited documents are closed without the question about saving
        for (int i = 0; i < m_window->getEditorCount(); ++i)
        {
            m_window->getEditor(i)->getProgramController()->closeDocument();
        }
        m_window->close();
        m_window.reset();
    }
}

QString EditorTabsTest::writeDocument(const QString& fileName, int pageCount, QSizeF pageSize)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(QPointF(0, 0), pageSize));
    }
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentWriter writer(nullptr);
    if (!writer.write(fileName, &document, false))
    {
        return QString();
    }
    return fileName;
}

void EditorTabsTest::createWindow()
{
    m_window = std::make_unique<pdfviewer::PDFEditorTabWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    QCOMPARE(m_window->getEditorCount(), 1);
    QVERIFY(!m_window->getCurrentEditor()->isWindow());
}

void EditorTabsTest::openAndWait(const QString& fileName)
{
    m_window->openDocument(fileName);
    QTRY_VERIFY_WITH_TIMEOUT(hasDocument(m_window->getCurrentEditor()), 10000);
}

bool EditorTabsTest::hasDocument(pdfviewer::PDFEditorMainWindow* editor)
{
    return editor && editor->getProgramController()->getDocument() != nullptr;
}

void EditorTabsTest::addAnnotation(pdfviewer::PDFEditorMainWindow* editor)
{
    pdf::PDFDocument* document = editor->getProgramController()->getDocument();
    pdf::PDFDocumentModifier modifier(document);
    modifier.getBuilder()->createAnnotationSquare(document->getCatalog()->getPage(0)->getPageReference(), QRectF(50, 50, 100, 100), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    modifier.markAnnotationsChanged();
    QVERIFY(modifier.finalize());
    editor->getProgramController()->onDocumentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
}

std::set<QAction*> EditorTabsTest::getPluginActionsInRibbon(pdfviewer::PDFEditorMainWindow* editor)
{
    std::set<QAction*> actions;
    for (pdfviewer::PDFRibbonButton* button : editor->getRibbon()->findChildren<pdfviewer::PDFRibbonButton*>())
    {
        QAction* action = button->getAction();
        if (action && qobject_cast<pdf::PDFPlugin*>(action->parent()))
        {
            actions.insert(action);
        }
    }
    return actions;
}

void EditorTabsTest::documentsOpenInTabs()
{
    createWindow();
    QTabWidget* tabWidget = m_window->getTabWidget();
    QCOMPARE(tabWidget->tabText(0), tr("New Tab"));

    // The empty tab is used for the first document
    openAndWait(m_fileA);
    QCOMPARE(m_window->getEditorCount(), 1);
    QCOMPARE(tabWidget->tabText(0), QString("a.pdf"));
    QVERIFY(m_window->windowTitle().startsWith("a.pdf"));

    // Next document goes to a new tab
    openAndWait(m_fileB);
    QCOMPARE(m_window->getEditorCount(), 2);
    QCOMPARE(tabWidget->currentIndex(), 1);
    QCOMPARE(tabWidget->tabText(1), QString("b.pdf"));
    QVERIFY(m_window->windowTitle().startsWith("b.pdf"));

    // A document, which is already opened, is shown (also by another path to the file)
    m_window->openDocument(QDir(m_directory.path()).filePath("./a.pdf"));
    QCOMPARE(m_window->getEditorCount(), 2);
    QCOMPARE(tabWidget->currentIndex(), 0);
    QVERIFY(m_window->windowTitle().startsWith("a.pdf"));

    // Each tab has its own document and its own draw widget
    QVERIFY(m_window->getEditor(0)->getProgramController()->getDocument() != m_window->getEditor(1)->getProgramController()->getDocument());
    QVERIFY(m_window->getEditor(0)->getProgramController()->getPdfWidget() != m_window->getEditor(1)->getProgramController()->getPdfWidget());
}

void EditorTabsTest::requestedDocumentsOpenInNewTabs()
{
    createWindow();

    // The empty tab opens the requested document itself
    pdfviewer::PDFEditorMainWindow* first = m_window->getCurrentEditor();
    first->getProgramController()->requestOpenDocument(m_fileA);
    QTRY_VERIFY_WITH_TIMEOUT(hasDocument(first), 10000);
    QCOMPARE(m_window->getEditorCount(), 1);

    // A tab with a document opens the requested document in a new tab
    first->getProgramController()->requestOpenDocument(m_fileC);
    QCOMPARE(m_window->getEditorCount(), 2);
    pdfviewer::PDFEditorMainWindow* second = m_window->getEditor(1);
    QCOMPARE(m_window->getCurrentEditor(), second);
    QTRY_VERIFY_WITH_TIMEOUT(hasDocument(second), 10000);
    QCOMPARE(second->getProgramController()->getDocument()->getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(first->getProgramController()->getDocument()->getCatalog()->getPageCount(), size_t(12));

    // A document opened in another tab is shown
    second->getProgramController()->requestOpenDocument(m_fileA);
    QCOMPARE(m_window->getEditorCount(), 2);
    QCOMPARE(m_window->getCurrentEditor(), first);
}

void EditorTabsTest::eachTabHasOwnPlugins()
{
    createWindow();
    openAndWait(m_fileA);
    openAndWait(m_fileB);
    QCOMPARE(m_window->getEditorCount(), 2);

    // A plugin is bound to a single widget, so each tab has its own instances (the instance
    // of the library is used by the first window only)
    const std::set<QAction*> firstActions = getPluginActionsInRibbon(m_window->getEditor(0));
    const std::set<QAction*> secondActions = getPluginActionsInRibbon(m_window->getEditor(1));
    if (firstActions.empty())
    {
        QSKIP("Plugins are not built");
    }

    QCOMPARE(secondActions.size(), firstActions.size());
    for (QAction* action : secondActions)
    {
        QVERIFY2(!firstActions.count(action), qPrintable(action->objectName()));
    }

    // The plugins of the second tab work with the second document: their actions are enabled
    // independently of the first tab
    size_t enabledActions = 0;
    for (QAction* action : secondActions)
    {
        enabledActions += action->isEnabled() ? 1 : 0;
    }
    QVERIFY(enabledActions > 0);

    // A closed tab destroys its own plugins, the remaining tab still works
    QVERIFY(m_window->closeEditor(1));
    QCOMPARE(m_window->getEditorCount(), 1);
    QTest::qWait(0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    openAndWait(m_fileC);
    QCOMPARE(m_window->getEditorCount(), 2);
    QCOMPARE(getPluginActionsInRibbon(m_window->getEditor(1)).size(), firstActions.size());
}

void EditorTabsTest::closeActionClosesTab()
{
    createWindow();
    openAndWait(m_fileA);
    openAndWait(m_fileB);
    QCOMPARE(m_window->getEditorCount(), 2);

    // Close action of a tab closes the tab
    QAction* closeAction = m_window->getCurrentEditor()->findChild<QAction*>("actionClose");
    QVERIFY(closeAction);
    QVERIFY(closeAction->isEnabled());
    closeAction->trigger();
    QCOMPARE(m_window->getEditorCount(), 1);
    QCOMPARE(m_window->getTabWidget()->tabText(0), QString("a.pdf"));

    // The last tab is kept, only its document is closed
    m_window->getCurrentEditor()->findChild<QAction*>("actionClose")->trigger();
    QCOMPARE(m_window->getEditorCount(), 1);
    QVERIFY(!hasDocument(m_window->getCurrentEditor()));
    QCOMPARE(m_window->getTabWidget()->tabText(0), tr("New Tab"));

    // The close button of the last tab closes the document
    openAndWait(m_fileC);
    QCOMPARE(m_window->getEditorCount(), 1);
    QVERIFY(m_window->closeEditor(0));
    QCOMPARE(m_window->getEditorCount(), 1);
    QVERIFY(!hasDocument(m_window->getCurrentEditor()));
}

void EditorTabsTest::nextAndPreviousTab()
{
    createWindow();
    openAndWait(m_fileA);
    openAndWait(m_fileB);
    openAndWait(m_fileC);
    QCOMPARE(m_window->getEditorCount(), 3);
    QCOMPARE(m_window->getTabWidget()->currentIndex(), 2);

    QAction* nextTab = m_window->findChild<QAction*>("actionNextTab");
    QAction* previousTab = m_window->findChild<QAction*>("actionPreviousTab");
    QVERIFY(nextTab && previousTab);
    QCOMPARE(nextTab->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Tab));

    nextTab->trigger();
    QCOMPARE(m_window->getTabWidget()->currentIndex(), 0);
    previousTab->trigger();
    QCOMPARE(m_window->getTabWidget()->currentIndex(), 2);
    previousTab->trigger();
    QCOMPARE(m_window->getTabWidget()->currentIndex(), 1);

    // The shortcuts of the window do not collide with the shortcuts of the editor
    QAction* newTab = m_window->findChild<QAction*>("actionNewTab");
    QVERIFY(newTab);
    for (QAction* action : m_window->getCurrentEditor()->findChildren<QAction*>())
    {
        for (const QKeySequence& sequence : action->shortcuts())
        {
            QVERIFY2(sequence != newTab->shortcut() && sequence != nextTab->shortcut() && sequence != previousTab->shortcut(), qPrintable(action->objectName()));
        }
    }
}

void EditorTabsTest::windowClosesAllTabs()
{
    createWindow();
    openAndWait(m_fileA);
    openAndWait(m_fileB);
    QCOMPARE(m_window->getEditorCount(), 2);

    QVERIFY(m_window->close());
    QCOMPARE(m_window->getEditorCount(), 0);

    // The geometry is kept by the window with the tabs
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QVERIFY(!settings.value("geometry").toByteArray().isEmpty());
    m_window.reset();
}

void EditorTabsTest::splitViewShowsSameDocument()
{
    createWindow();
    openAndWait(m_fileA);
    pdfviewer::PDFEditorMainWindow* editor = m_window->getCurrentEditor();
    pdf::PDFWidget* mainWidget = editor->getProgramController()->getPdfWidget();

    QAction* splitViewAction = editor->getSplitViewAction();
    QVERIFY(splitViewAction);
    QVERIFY(splitViewAction->isCheckable());
    QVERIFY(!editor->getSplitViewWidget());

    // Split View is on the View tab of the ribbon
    pdfviewer::PDFRibbonPage* viewPage = editor->getRibbon()->getPage("view");
    QVERIFY(viewPage);
    bool isOnViewPage = false;
    for (pdfviewer::PDFRibbonButton* button : viewPage->findChildren<pdfviewer::PDFRibbonButton*>())
    {
        isOnViewPage = isOnViewPage || button->getAction() == splitViewAction;
    }
    QVERIFY(isOnViewPage);

    splitViewAction->trigger();
    pdfviewer::PDFSplitViewWidget* splitView = editor->getSplitViewWidget();
    QVERIFY(splitView);
    QVERIFY(splitView->isShowingMainDocument());
    QVERIFY(!splitView->isScrollingSynchronized());
    QCOMPARE(splitView->getPdfWidget()->getDrawWidgetProxy()->getDocument(), editor->getProgramController()->getDocument());

    // Both panes are side by side
    QSplitter* splitter = editor->findChild<QSplitter*>("splitViewSplitter");
    QVERIFY(splitter);
    QCOMPARE(splitter->count(), 2);
    QTRY_VERIFY(splitView->isVisible());
    QVERIFY(qAbs(splitter->sizes().at(0) - splitter->sizes().at(1)) <= 2);

    // The panes are scrolled independently
    mainWidget->getDrawWidgetProxy()->goToPage(8);
    QCoreApplication::processEvents();
    QVERIFY(mainWidget->getVerticalScrollbar()->value() != splitView->getPdfWidget()->getVerticalScrollbar()->value());
}

void EditorTabsTest::splitViewFollowsEdits()
{
    createWindow();
    openAndWait(m_fileA);
    pdfviewer::PDFEditorMainWindow* editor = m_window->getCurrentEditor();
    editor->setSplitViewVisible(true);
    pdfviewer::PDFSplitViewWidget* splitView = editor->getSplitViewWidget();
    QVERIFY(splitView);

    // A new version of the document (an annotation added) is shown in both panes
    pdf::PDFDocument* document = editor->getProgramController()->getDocument();
    addAnnotation(editor);

    pdf::PDFDocument* modifiedDocument = editor->getProgramController()->getDocument();
    QVERIFY(modifiedDocument != document);
    QCOMPARE(splitView->getPdfWidget()->getDrawWidgetProxy()->getDocument(), modifiedDocument);
    QCOMPARE(m_window->getTabWidget()->tabText(0), QString("a.pdf*"));

    // A closed document is removed from the split view
    editor->getProgramController()->closeDocument();
    QVERIFY(!splitView->getPdfWidget()->getDrawWidgetProxy()->getDocument());
}

void EditorTabsTest::splitViewSynchronizesOtherFile()
{
    createWindow();
    openAndWait(m_fileA);
    pdfviewer::PDFEditorMainWindow* editor = m_window->getCurrentEditor();
    pdf::PDFWidget* mainWidget = editor->getProgramController()->getPdfWidget();
    editor->setSplitViewVisible(true);
    pdfviewer::PDFSplitViewWidget* splitView = editor->getSplitViewWidget();
    QTRY_VERIFY(splitView->isVisible());

    QString errorMessage;
    QVERIFY(!splitView->openFile(m_directory.filePath("missing.pdf"), &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(splitView->isShowingMainDocument());

    // Another file is shown and scrolled together with the document
    QVERIFY2(splitView->openFile(m_fileB, &errorMessage), qPrintable(errorMessage));
    QVERIFY(!splitView->isShowingMainDocument());
    QCOMPARE(splitView->getFileName(), m_fileB);
    QVERIFY(splitView->isScrollingSynchronized());
    pdf::PDFDrawWidgetProxy* paneProxy = splitView->getPdfWidget()->getDrawWidgetProxy();
    QVERIFY(paneProxy->getDocument() && paneProxy->getDocument() != editor->getProgramController()->getDocument());
    QCOMPARE(splitView->findChild<QLabel*>("splitViewTitleLabel")->text(), QString("b.pdf"));

    mainWidget->getDrawWidgetProxy()->zoom(1.7);
    QCoreApplication::processEvents();
    QCOMPARE(paneProxy->getZoom(), mainWidget->getDrawWidgetProxy()->getZoom());

    mainWidget->getVerticalScrollbar()->setValue(mainWidget->getVerticalScrollbar()->maximum() / 2);
    mainWidget->getHorizontalScrollbar()->setValue(mainWidget->getHorizontalScrollbar()->maximum() / 3);
    QCoreApplication::processEvents();
    QVERIFY(mainWidget->getVerticalScrollbar()->value() > 0);
    QCOMPARE(splitView->getPdfWidget()->getVerticalScrollbar()->value(), mainWidget->getVerticalScrollbar()->value());
    QCOMPARE(splitView->getPdfWidget()->getHorizontalScrollbar()->value(), mainWidget->getHorizontalScrollbar()->value());

    // Both directions
    splitView->getPdfWidget()->getVerticalScrollbar()->setValue(splitView->getPdfWidget()->getVerticalScrollbar()->value() / 2);
    QCoreApplication::processEvents();
    QCOMPARE(mainWidget->getVerticalScrollbar()->value(), splitView->getPdfWidget()->getVerticalScrollbar()->value());

    // Without the synchronization, the panes are scrolled independently
    splitView->setScrollingSynchronized(false);
    const int paneValue = splitView->getPdfWidget()->getVerticalScrollbar()->value();
    mainWidget->getVerticalScrollbar()->setValue(mainWidget->getVerticalScrollbar()->maximum());
    QCoreApplication::processEvents();
    QCOMPARE(splitView->getPdfWidget()->getVerticalScrollbar()->value(), paneValue);

    // Edits of the document do not replace the other file
    addAnnotation(editor);
    QCOMPARE(splitView->getFileName(), m_fileB);

    // Back to the same document
    splitView->findChild<QToolButton*>("splitViewSameDocumentButton")->click();
    QVERIFY(splitView->isShowingMainDocument());
    QVERIFY(!splitView->isScrollingSynchronized());
    QCOMPARE(paneProxy->getDocument(), editor->getProgramController()->getDocument());
}

void EditorTabsTest::splitViewIsClosed()
{
    createWindow();
    openAndWait(m_fileA);
    pdfviewer::PDFEditorMainWindow* editor = m_window->getCurrentEditor();
    QAction* splitViewAction = editor->getSplitViewAction();

    splitViewAction->setChecked(true);
    QVERIFY(editor->getSplitViewWidget());

    // The close button of the pane closes the split view
    editor->getSplitViewWidget()->findChild<QToolButton*>("splitViewCloseButton")->click();
    QVERIFY(!editor->getSplitViewWidget());
    QVERIFY(!splitViewAction->isChecked());
    QCOMPARE(editor->findChild<QSplitter*>("splitViewSplitter")->count(), 1);

    // The action toggles it again, also in the second tab of the window
    splitViewAction->setChecked(true);
    QVERIFY(editor->getSplitViewWidget());
    splitViewAction->setChecked(false);
    QVERIFY(!editor->getSplitViewWidget());

    openAndWait(m_fileB);
    pdfviewer::PDFEditorMainWindow* second = m_window->getCurrentEditor();
    QVERIFY(second != editor);
    second->getSplitViewAction()->setChecked(true);
    QVERIFY(second->getSplitViewWidget());
    QVERIFY(!editor->getSplitViewWidget());
    QCOMPARE(second->getSplitViewWidget()->getPdfWidget()->getDrawWidgetProxy()->getDocument(), second->getProgramController()->getDocument());
}

QTEST_MAIN(EditorTabsTest)

#include "tst_editortabstest.moc"
