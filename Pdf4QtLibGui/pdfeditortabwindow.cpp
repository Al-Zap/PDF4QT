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

#include <QDir>
#include <QAction>
#include <QScreen>
#include <QMimeData>
#include <QSettings>
#include <QFileInfo>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QCloseEvent>
#include <QApplication>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFEditorTabWindow::PDFEditorTabWindow(QWidget* parent) :
    BaseClass(parent),
    m_tabWidget(new QTabWidget(this)),
    m_newTabButton(new QToolButton(m_tabWidget))
{
    setObjectName("PDFEditorTabWindow");
    setAcceptDrops(true);
    setWindowTitle(QApplication::applicationDisplayName());

    m_tabWidget->setObjectName("editorTabWidget");
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setElideMode(Qt::ElideMiddle);
    m_tabWidget->tabBar()->setExpanding(false);
    setCentralWidget(m_tabWidget);

    m_newTabButton->setObjectName("newTabButton");
    m_newTabButton->setText(QStringLiteral("+"));
    m_newTabButton->setAutoRaise(true);
    m_newTabButton->setToolTip(tr("Open a document in a new tab (Ctrl+T)"));
    m_tabWidget->setCornerWidget(m_newTabButton, Qt::TopRightCorner);
    connect(m_newTabButton, &QToolButton::clicked, this, &PDFEditorTabWindow::onNewTabTriggered);

    QAction* newTabAction = new QAction(tr("New Tab"), this);
    newTabAction->setObjectName("actionNewTab");
    newTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(newTabAction, &QAction::triggered, this, &PDFEditorTabWindow::onNewTabTriggered);
    addAction(newTabAction);

    QAction* nextTabAction = new QAction(tr("Next Tab"), this);
    nextTabAction->setObjectName("actionNextTab");
    nextTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    connect(nextTabAction, &QAction::triggered, this, [this]() { selectNextTab(1); });
    addAction(nextTabAction);

    QAction* previousTabAction = new QAction(tr("Previous Tab"), this);
    previousTabAction->setObjectName("actionPreviousTab");
    previousTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
    connect(previousTabAction, &QAction::triggered, this, [this]() { selectNextTab(-1); });
    addAction(previousTabAction);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &PDFEditorTabWindow::closeEditor);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index)
    {
        updateWindowTitle();
        if (PDFEditorMainWindow* editor = getEditor(index))
        {
            editor->setFocus();
        }
    });

    readGeometry();
    addEditor();
}

PDFEditorTabWindow::~PDFEditorTabWindow()
{
    // The editors are destroyed before the window, each of them destroys
    // its program controller and its plugins.
    while (m_tabWidget->count() > 0)
    {
        QWidget* widget = m_tabWidget->widget(0);
        m_tabWidget->removeTab(0);
        delete widget;
    }
}

PDFEditorMainWindow* PDFEditorTabWindow::addEditor()
{
    PDFEditorMainWindow* editor = new PDFEditorMainWindow(m_tabWidget, true);
    editor->setOpenDocumentElsewhereHandler([this, editor](const QString& fileName) { return openDocumentElsewhere(editor, fileName); });
    connect(editor, &QWidget::windowTitleChanged, this, [this, editor]() { updateTab(editor); });

    // The Close action closes the tab, the last tab stays without a document
    connect(editor->getProgramController(), &PDFProgramController::documentClosedByUser, this, [this, editor]()
    {
        const int index = m_tabWidget->indexOf(editor);
        if (index >= 0 && getEditorCount() > 1)
        {
            closeEditor(index);
        }
    });

    const int index = m_tabWidget->addTab(editor, QString());
    updateTab(editor);
    m_tabWidget->setCurrentIndex(index);
    return editor;
}

PDFEditorMainWindow* PDFEditorTabWindow::getCurrentEditor() const
{
    return getEditor(m_tabWidget->currentIndex());
}

PDFEditorMainWindow* PDFEditorTabWindow::getEditor(int index) const
{
    return qobject_cast<PDFEditorMainWindow*>(m_tabWidget->widget(index));
}

int PDFEditorTabWindow::getEditorCount() const
{
    return m_tabWidget->count();
}

void PDFEditorTabWindow::openDocument(const QString& fileName)
{
    const int index = findEditor(fileName);
    if (index >= 0)
    {
        m_tabWidget->setCurrentIndex(index);
        return;
    }

    PDFEditorMainWindow* editor = getCurrentEditor();
    if (!editor || editor->getProgramController()->getDocument() || !editor->getProgramController()->canClose())
    {
        editor = addEditor();
    }

    editor->getProgramController()->openDocument(fileName);
}

bool PDFEditorTabWindow::closeEditor(int index)
{
    PDFEditorMainWindow* editor = getEditor(index);
    if (!editor)
    {
        return true;
    }

    m_tabWidget->setCurrentIndex(index);

    if (getEditorCount() == 1)
    {
        // The window keeps one tab, only the document is closed
        PDFProgramController* programController = editor->getProgramController();
        if (!programController->canClose() || !programController->askForSaveDocumentBeforeClose())
        {
            return false;
        }

        programController->closeDocument();
        return true;
    }

    if (!editor->close())
    {
        return false;
    }

    m_tabWidget->removeTab(m_tabWidget->indexOf(editor));

    // The closing can be requested by the editor itself (Close action)
    editor->deleteLater();
    return true;
}

QString PDFEditorTabWindow::getTabText(const PDFEditorMainWindow* editor)
{
    PDFProgramController* programController = editor->getProgramController();
    const QString fileName = programController->getOriginalFileName();

    if (fileName.isEmpty() || (!programController->getDocument() && programController->canClose()))
    {
        return tr("New Tab");
    }

    QString text = QFileInfo(fileName).fileName();
    if (programController->isDocumentModified())
    {
        text += QStringLiteral("*");
    }
    return text;
}

void PDFEditorTabWindow::closeEvent(QCloseEvent* event)
{
    writeGeometry();

    bool isFactorySettingsBeingRestored = false;

    // Each tab asks for saving of its document, the current tab first
    while (getEditorCount() > 0)
    {
        const int index = m_tabWidget->currentIndex();
        PDFEditorMainWindow* editor = getEditor(index);
        isFactorySettingsBeingRestored = isFactorySettingsBeingRestored || editor->getProgramController()->isFactorySettingsBeingRestored();

        if (!editor->close())
        {
            event->ignore();
            return;
        }

        m_tabWidget->removeTab(index);
        delete editor;
    }

    if (isFactorySettingsBeingRestored)
    {
        // The other tabs have written their settings again
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
        settings.clear();
    }

    event->accept();
}

void PDFEditorTabWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorTabWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorTabWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls)
        {
            if (url.isLocalFile())
            {
                openDocument(url.toLocalFile());
            }
        }
        event->acceptProposedAction();
    }
}

bool PDFEditorTabWindow::openDocumentElsewhere(PDFEditorMainWindow* editor, const QString& fileName)
{
    const int index = findEditor(fileName);
    if (index >= 0 && getEditor(index) != editor)
    {
        // The document is already opened in another tab
        m_tabWidget->setCurrentIndex(index);
        return true;
    }

    PDFProgramController* programController = editor->getProgramController();
    if (index >= 0 || (!programController->getDocument() && programController->canClose()))
    {
        // The document of the tab is opened again, or the tab is empty
        return false;
    }

    PDFEditorMainWindow* newEditor = addEditor();
    newEditor->getProgramController()->openDocument(fileName);
    return true;
}

int PDFEditorTabWindow::findEditor(const QString& fileName) const
{
    auto normalizedPath = [](const QString& path)
    {
        QFileInfo fileInfo(path);
        QString result = fileInfo.canonicalFilePath();
        if (result.isEmpty())
        {
            result = fileInfo.absoluteFilePath();
        }
        return QDir::cleanPath(result);
    };

    if (fileName.isEmpty())
    {
        return -1;
    }

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif

    const QString path = normalizedPath(fileName);
    for (int i = 0; i < getEditorCount(); ++i)
    {
        PDFProgramController* programController = getEditor(i)->getProgramController();
        const QString editorFileName = programController->getOriginalFileName();
        const bool hasDocument = programController->getDocument() || !programController->canClose();
        if (hasDocument && !editorFileName.isEmpty() && normalizedPath(editorFileName).compare(path, caseSensitivity) == 0)
        {
            return i;
        }
    }

    return -1;
}

void PDFEditorTabWindow::updateTab(PDFEditorMainWindow* editor)
{
    const int index = m_tabWidget->indexOf(editor);
    if (index < 0)
    {
        return;
    }

    m_tabWidget->setTabText(index, getTabText(editor));

    const QString fileName = editor->getProgramController()->getOriginalFileName();
    m_tabWidget->setTabToolTip(index, editor->getProgramController()->getDocument() ? QDir::toNativeSeparators(fileName) : QString());

    if (index == m_tabWidget->currentIndex())
    {
        updateWindowTitle();
    }
}

void PDFEditorTabWindow::updateWindowTitle()
{
    if (PDFEditorMainWindow* editor = getCurrentEditor())
    {
        setWindowTitle(editor->windowTitle());
    }
    else
    {
        setWindowTitle(QApplication::applicationDisplayName());
    }
}

void PDFEditorTabWindow::onNewTabTriggered()
{
    // The open dialog of the current editor opens the document in a new tab,
    // if the current tab already shows a document
    if (PDFEditorMainWindow* editor = getCurrentEditor())
    {
        if (QAction* openAction = editor->findChild<QAction*>("actionOpen"))
        {
            openAction->trigger();
        }
    }
}

void PDFEditorTabWindow::selectNextTab(int step)
{
    const int count = getEditorCount();
    if (count > 1)
    {
        m_tabWidget->setCurrentIndex((m_tabWidget->currentIndex() + step + count) % count);
    }
}

void PDFEditorTabWindow::readGeometry()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty() || !restoreGeometry(geometry))
    {
        QRect availableGeometry = QApplication::primaryScreen()->availableGeometry();
        QRect windowRect(0, 0, availableGeometry.width() / 2, availableGeometry.height() / 2);
        windowRect = windowRect.translated(availableGeometry.center() - windowRect.center());
        setGeometry(windowRect);
    }
}

void PDFEditorTabWindow::writeGeometry()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("geometry", saveGeometry());
}

}   // namespace pdfviewer
