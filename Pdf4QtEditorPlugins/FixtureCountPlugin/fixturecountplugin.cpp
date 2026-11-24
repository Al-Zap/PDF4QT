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

#include "fixturecountplugin.h"
#include "fixturecountpanel.h"
#include "fixturecounttool.h"
#include "fixturesymbolfinder.h"

#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfdrawwidget.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdfutils.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineF>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>

namespace pdfplugin
{

FixtureCountPlugin::FixtureCountPlugin() :
    pdf::PDFPlugin(nullptr),
    m_countTool(nullptr),
    m_panel(nullptr),
    m_countAction(nullptr),
    m_addTypeAction(nullptr),
    m_editTypeAction(nullptr),
    m_removeTypeAction(nullptr),
    m_exportAction(nullptr),
    m_showPanelAction(nullptr),
    m_findSimilarAction(nullptr),
    m_currentTypeIndex(-1),
    m_markerSize(12.0)
{

}

void FixtureCountPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_countAction = new QAction(QIcon(":/pdfplugins/fixturecount/count.svg"), tr("&Count Fixtures"), this);
    m_addTypeAction = new QAction(QIcon(":/pdfplugins/fixturecount/add-type.svg"), tr("&Add Type..."), this);
    m_editTypeAction = new QAction(QIcon(":/pdfplugins/fixturecount/edit-type.svg"), tr("&Edit Type..."), this);
    m_removeTypeAction = new QAction(QIcon(":/pdfplugins/fixturecount/remove-type.svg"), tr("&Remove Type"), this);
    m_exportAction = new QAction(QIcon(":/pdfplugins/fixturecount/export.svg"), tr("E&xport Count..."), this);
    m_showPanelAction = new QAction(QIcon(":/pdfplugins/fixturecount/panel.svg"), tr("&Show Count Panel"), this);
    m_findSimilarAction = new QAction(QIcon(":/pdfplugins/fixturecount/find-similar.svg"), tr("&Find Similar..."), this);

    m_countAction->setObjectName("fixturecount_CountAction");
    m_addTypeAction->setObjectName("fixturecount_AddTypeAction");
    m_editTypeAction->setObjectName("fixturecount_EditTypeAction");
    m_removeTypeAction->setObjectName("fixturecount_RemoveTypeAction");
    m_exportAction->setObjectName("fixturecount_ExportAction");
    m_showPanelAction->setObjectName("fixturecount_ShowPanelAction");
    m_findSimilarAction->setObjectName("fixturecount_FindSimilarAction");
    m_findSimilarAction->setToolTip(tr("Find all symbols similar to one fixture on the sheet: hold Ctrl and drag a rectangle around one fixture while counting"));

    m_countAction->setCheckable(true);
    m_countAction->setShortcut(QKeySequence(Qt::Key_K));
    m_countAction->setToolTip(tr("Count fixtures: click each fixture on the sheet to tag it with the selected type"));
    m_exportAction->setToolTip(tr("Export the count per sheet and for the whole set to a CSV file (opens in Excel)"));
    m_removeTypeAction->setToolTip(tr("Remove the selected type and all its markers"));

    m_countTool = new FixtureCountTool(widget->getDrawWidgetProxy(), m_countAction, this);
    widget->getToolManager()->addTool(m_countTool);

    m_countTool->setMarkerHitTest([this](pdf::PDFInteger pageIndex, const QPointF& pagePoint)
    {
        return m_data.findMarker(pageIndex, pagePoint, m_markerSize * 0.25) != nullptr;
    });

    connect(m_countTool, &FixtureCountTool::toolActivityChanged, this, &FixtureCountPlugin::onCountToolActivityChanged);
    connect(m_countTool, &FixtureCountTool::markerRequested, this, &FixtureCountPlugin::onMarkerRequested);
    connect(m_countTool, &FixtureCountTool::markerRemoveRequested, this, &FixtureCountPlugin::onMarkerRemoveRequested);
    connect(m_countTool, &FixtureCountTool::typeRequested, this, &FixtureCountPlugin::onTypeRequested);
    connect(m_countTool, &FixtureCountTool::sampleSelected, this, &FixtureCountPlugin::onSampleSelected);
    connect(m_findSimilarAction, &QAction::triggered, this, &FixtureCountPlugin::onFindSimilarTriggered);
    connect(m_addTypeAction, &QAction::triggered, this, &FixtureCountPlugin::onAddTypeTriggered);
    connect(m_editTypeAction, &QAction::triggered, this, &FixtureCountPlugin::onEditTypeTriggered);
    connect(m_removeTypeAction, &QAction::triggered, this, &FixtureCountPlugin::onRemoveTypeTriggered);
    connect(m_exportAction, &QAction::triggered, this, &FixtureCountPlugin::onExportTriggered);
    connect(m_showPanelAction, &QAction::triggered, this, &FixtureCountPlugin::onShowPanelTriggered);
    connect(widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &FixtureCountPlugin::onDrawSpaceChanged);

    loadSettings();
    m_countTool->setMarkerSize(m_markerSize);
    updateUI();
}

void FixtureCountPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    // Markers are annotations, so the count is computed again after each change
    // of the document - also after undo/redo and after a marker is deleted
    // as an ordinary annotation
    m_data = FixtureCountData::scan(m_document);

    if (document.hasReset() || document.hasPageContentsChanged())
    {
        m_sheetNames = FixtureCountReport::getSheetNames(m_document);
    }

    mergeDocumentTypes();
    updateUI();
}

std::vector<QAction*> FixtureCountPlugin::getActions() const
{
    return { m_countAction, m_findSimilarAction, m_showPanelAction, m_exportAction };
}

std::vector<QAction*> FixtureCountPlugin::getToolbarActions() const
{
    return { m_countAction, m_findSimilarAction, m_showPanelAction };
}

QString FixtureCountPlugin::getPluginMenuName() const
{
    return tr("Fi&xture Count");
}

pdf::PDFPlugin::PluginMenuLocation FixtureCountPlugin::getPluginMenuLocation() const
{
    return PluginMenuLocation::Edit;
}

QString FixtureCountPlugin::getToolbarSection() const
{
    return QStringLiteral("count");
}

void FixtureCountPlugin::onCountToolActivityChanged(bool active)
{
    if (active)
    {
        showPanel();

        if (m_types.empty())
        {
            onAddTypeTriggered();
        }
    }
}

void FixtureCountPlugin::onMarkerRequested(pdf::PDFInteger pageIndex, QPointF pagePoint)
{
    if (!canModifyDocument())
    {
        return;
    }

    if (m_currentTypeIndex < 0 || m_currentTypeIndex >= int(m_types.size()))
    {
        onAddTypeTriggered();
        if (m_currentTypeIndex < 0)
        {
            return;
        }
    }

    const FixtureType& type = m_types[m_currentTypeIndex];
    pdf::PDFDocumentModifier modifier(m_document);

    // Click into an existing marker never counts the fixture twice - the marker
    // is changed to the current type instead (or nothing happens, if it already
    // has the current type)
    if (const FixtureMarker* marker = m_data.findMarker(pageIndex, pagePoint, 0.0))
    {
        if (marker->typeName == type.name)
        {
            Q_EMIT m_countTool->messageDisplayRequest(tr("This fixture is already counted as %1.").arg(type.name), 3000);
            return;
        }

        FixtureCountEditor::changeMarkerType(modifier.getBuilder(), m_document, *marker, type);
    }
    else
    {
        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        FixtureCountEditor::createMarker(modifier.getBuilder(), page->getPageReference(), pagePoint, m_markerSize, type);
    }

    finishModification(modifier);
}

void FixtureCountPlugin::onMarkerRemoveRequested(pdf::PDFInteger pageIndex, QPointF pagePoint)
{
    if (!canModifyDocument())
    {
        return;
    }

    const FixtureMarker* marker = m_data.findMarker(pageIndex, pagePoint, m_markerSize * 0.25);
    if (!marker)
    {
        return;
    }

    // The marker is owned by the data, which are scanned again by the modification
    const QString typeName = marker->typeName;

    pdf::PDFDocumentModifier modifier(m_document);
    FixtureCountEditor::removeMarker(modifier.getBuilder(), m_document, *marker);
    finishModification(modifier);

    Q_EMIT m_countTool->messageDisplayRequest(tr("Marker of %1 removed. Ctrl + Z puts it back.").arg(typeName), 4000);
}

void FixtureCountPlugin::onTypeRequested(int index)
{
    if (index >= 0 && index < int(m_types.size()))
    {
        onCurrentTypeChanged(index);
        Q_EMIT m_countTool->messageDisplayRequest(tr("Counting %1").arg(m_types[index].name), 2000);
    }
}

void FixtureCountPlugin::onAddTypeTriggered()
{
    FixtureType type = FixtureType::createDefaultType(int(m_types.size()));

    // Default name must not collide with an existing type
    const QStringList names = getTypeNames(-1);
    for (int i = int(m_types.size()) + 1; names.contains(type.name, Qt::CaseInsensitive); ++i)
    {
        type.name = QString("T%1").arg(i);
    }

    FixtureTypeDialog dialog(type, names, m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_types.push_back(dialog.getType());
        m_currentTypeIndex = int(m_types.size()) - 1;
        saveSettings();
        updateUI();
    }
}

void FixtureCountPlugin::onEditTypeTriggered()
{
    editType(m_currentTypeIndex);
}

void FixtureCountPlugin::onRemoveTypeTriggered()
{
    if (m_currentTypeIndex < 0 || m_currentTypeIndex >= int(m_types.size()))
    {
        return;
    }

    const FixtureType type = m_types[m_currentTypeIndex];
    const FixtureMarkers markers = m_data.getMarkersOfType(type.name);

    if (!markers.empty())
    {
        if (!canModifyDocument())
        {
            return;
        }

        if (QMessageBox::question(m_widget, tr("Remove Type"),
                                  tr("Remove type %1 and its %2 marker(s) from the document?").arg(type.name).arg(markers.size())) != QMessageBox::Yes)
        {
            return;
        }

        pdf::PDFDocumentModifier modifier(m_document);
        for (const FixtureMarker& marker : markers)
        {
            FixtureCountEditor::removeMarker(modifier.getBuilder(), m_document, marker);
        }

        // The type is removed first, so the document change does not bring it back
        m_types.erase(m_types.begin() + m_currentTypeIndex);
        m_currentTypeIndex = m_types.empty() ? -1 : qMin(m_currentTypeIndex, int(m_types.size()) - 1);
        saveSettings();
        finishModification(modifier);
        return;
    }

    m_types.erase(m_types.begin() + m_currentTypeIndex);
    m_currentTypeIndex = m_types.empty() ? -1 : qMin(m_currentTypeIndex, int(m_types.size()) - 1);
    saveSettings();
    updateUI();
}

void FixtureCountPlugin::onExportTriggered()
{
    if (!m_document)
    {
        return;
    }

    QString fileName = m_dataExchangeInterface ? m_dataExchangeInterface->getOriginalFileName() : QString();
    QString defaultFileName;
    if (!fileName.isEmpty())
    {
        QFileInfo fileInfo(fileName);
        defaultFileName = fileInfo.absoluteDir().filePath(fileInfo.completeBaseName() + tr(" - fixture count") + ".csv");
    }

    fileName = QFileDialog::getSaveFileName(m_widget, tr("Export Fixture Count"), defaultFileName, tr("CSV files (*.csv);;All files (*.*)"));
    if (fileName.isEmpty())
    {
        return;
    }

    // Types of other plan sets (they are remembered and offered for the next
    // file), which are not counted in this document, are not exported
    FixtureTypes countedTypes;
    std::copy_if(m_types.cbegin(), m_types.cend(), std::back_inserter(countedTypes), [this](const FixtureType& type) { return m_data.getCount(type.name) > 0; });

    const std::vector<QStringList> table = FixtureCountReport::createTable(countedTypes, m_data, m_sheetNames);
    // Excel in the locales with the decimal comma expects a semicolon,
    // with a comma all values would be in the first column
    const QString csv = FixtureCountReport::createCsv(table, pdf::PDFSysUtils::getSystemListSeparator());

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QMessageBox::critical(m_widget, tr("Error"), tr("Cannot write file '%1': %2").arg(fileName, file.errorString()));
        return;
    }

    // Byte order mark, so Excel recognizes the UTF-8 encoding
    file.write("\xEF\xBB\xBF");
    file.write(csv.toUtf8());
    file.close();

    Q_EMIT m_countTool->messageDisplayRequest(tr("Fixture count exported to %1").arg(QFileInfo(fileName).fileName()), 5000);
}

void FixtureCountPlugin::onShowPanelTriggered()
{
    showPanel();
}

void FixtureCountPlugin::onCurrentTypeChanged(int index)
{
    if (index >= 0 && index < int(m_types.size()) && index != m_currentTypeIndex)
    {
        m_currentTypeIndex = index;
        updateUI();
    }
}

void FixtureCountPlugin::onMarkerSizeChanged(pdf::PDFReal markerSize)
{
    m_markerSize = markerSize;
    m_countTool->setMarkerSize(markerSize);
    saveSettings();
}

void FixtureCountPlugin::onDrawSpaceChanged()
{
    if (m_panel && m_panel->isVisible())
    {
        updateUI();
    }
}

void FixtureCountPlugin::onSampleSelected(pdf::PDFInteger pageIndex, QRectF sampleRect)
{
    if (!canModifyDocument())
    {
        return;
    }

    if (m_currentTypeIndex < 0 || m_currentTypeIndex >= int(m_types.size()))
    {
        onAddTypeTriggered();
        if (m_currentTypeIndex < 0)
        {
            return;
        }
    }

    QElapsedTimer timer;
    timer.start();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<QPointF> centers = findSimilarSymbols(pageIndex, sampleRect);
    QApplication::restoreOverrideCursor();

    if (centers.empty())
    {
        Q_EMIT m_countTool->messageDisplayRequest(tr("No symbol was found in the selected rectangle. Drag the rectangle tightly around one fixture symbol."), 5000);
        return;
    }

    // So many matches are not fixtures, the sample is a part of the drawing
    // (a light level mark, a hatch, a piece of a line)
    if (centers.size() > MAXIMAL_FOUND_SYMBOLS)
    {
        Q_EMIT m_countTool->messageDisplayRequest(tr("%1 matches were found, so the selected rectangle is probably not a fixture symbol. "
                                                     "Nothing was added. Zoom in and drag the rectangle tightly around one fixture symbol.").arg(centers.size()), 10000);
        return;
    }

    // Fixtures, which are already counted (by any type), are skipped
    const FixtureType& type = m_types[m_currentTypeIndex];
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    pdf::PDFDocumentModifier modifier(m_document);
    int addedCount = 0;

    // A fixture counted by a click has its marker anywhere on the symbol,
    // not exactly in its center
    const pdf::PDFReal symbolSize = qMax(qMax(sampleRect.width(), sampleRect.height()), m_markerSize);
    const pdf::PDFReal minimalDistance = 0.75 * symbolSize;
    const FixtureMarkers& existingMarkers = m_data.getMarkers();

    for (const QPointF& center : centers)
    {
        const bool isCounted = std::any_of(existingMarkers.cbegin(), existingMarkers.cend(), [&](const FixtureMarker& marker)
        {
            return marker.pageIndex == pageIndex && QLineF(marker.getCenter(), center).length() < minimalDistance;
        });

        if (isCounted || m_data.findMarker(pageIndex, center, m_markerSize * 0.25))
        {
            continue;
        }

        FixtureCountEditor::createMarker(modifier.getBuilder(), page->getPageReference(), center, m_markerSize, type);
        ++addedCount;
    }

    if (addedCount > 0)
    {
        finishModification(modifier);
    }

    Q_EMIT m_countTool->messageDisplayRequest(tr("Found %1 matching symbol(s) in %2 s, %3 new marker(s) of %4 added. "
                                                 "Shift + click removes a wrong marker, Ctrl + Z undoes the whole search.")
                                              .arg(centers.size()).arg(timer.elapsed() / 1000.0, 0, 'f', 1).arg(addedCount).arg(type.name), 15000);
}

void FixtureCountPlugin::onFindSimilarTriggered()
{
    if (!m_countTool->isActive())
    {
        m_countAction->trigger();
    }

    QMessageBox::information(m_widget, tr("Find Similar"),
                             tr("Hold Ctrl and drag a rectangle tightly around one fixture symbol on the sheet. "
                                "All similar symbols on the sheet (also rotated) are counted as the selected type.\n\n"
                                "Fixtures that are already counted are skipped. Check the result, Shift + click removes "
                                "a wrong marker, Ctrl + Z undoes the whole search."));
}

std::vector<QPointF> FixtureCountPlugin::findSimilarSymbols(pdf::PDFInteger pageIndex, const QRectF& sampleRect) const
{
    std::vector<QPointF> result;

    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return result;
    }

    const pdf::PDFPage* page = catalog->getPage(pageIndex);

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();

    // Annotations (also the markers) are not drawn, only the drawing itself,
    // in its real colors (the colors of the symbols are used by the search)
    pdf::PDFRenderer::Features features = proxy->getFeatures();
    features.setFlag(pdf::PDFRenderer::DisplayAnnotations, false);
    features &= ~pdf::PDFRenderer::getColorFeatures();
    features.setFlag(pdf::PDFRenderer::Antialiasing, true);

    // The sheet is displayed, so it is usually already prepared (compiled)
    // by the view. Otherwise it is compiled now. The page of the view has
    // converted colors, when a color mode (inverted colors, dark mode...)
    // is active, so then the page is compiled again in its real colors.
    pdf::PDFPrecompiledPage ownCompiledPage;
    const pdf::PDFPrecompiledPage* compiledPage = proxy->getCompiler()->getCompiledPage(pageIndex, pdf::PDFAsynchronousPageCompiler::CompileMode::None);
    const bool hasConvertedColors = (proxy->getFeatures() & pdf::PDFRenderer::getColorFeatures()) != 0;
    if (!compiledPage || !compiledPage->isValid() || hasConvertedColors)
    {
        pdf::PDFOptionalContentActivity optionalContentActivity(m_document, pdf::OCUsage::View, nullptr);
        pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
        pdf::PDFRenderer renderer(m_document, proxy->getFontCache(), proxy->getIccProfileCache(), cms.data(), &optionalContentActivity, features, proxy->getMeshQualitySettings());
        renderer.compile(&ownCompiledPage, size_t(pageIndex));
        compiledPage = &ownCompiledPage;
    }

    for (const FixtureSymbolFinder::Match& match : FixtureSymbolFinder::findOnPage(*compiledPage, page, sampleRect, features, FixtureSymbolFinder::Settings()))
    {
        result.push_back(match.center);
    }

    return result;
}

void FixtureCountPlugin::editType(int index)
{
    if (index < 0 || index >= int(m_types.size()))
    {
        return;
    }

    const FixtureType oldType = m_types[index];
    FixtureTypeDialog dialog(oldType, getTypeNames(index), m_widget);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const FixtureType newType = dialog.getType();
    const FixtureMarkers markers = m_data.getMarkersOfType(oldType.name);
    const bool changed = newType.name != oldType.name ||
                         newType.description != oldType.description ||
                         newType.color != oldType.color ||
                         newType.shape != oldType.shape;

    m_types[index] = newType;
    saveSettings();

    if (changed && !markers.empty() && canModifyDocument())
    {
        // All markers of the type are changed in a single step of the undo history
        pdf::PDFDocumentModifier modifier(m_document);
        for (const FixtureMarker& marker : markers)
        {
            FixtureCountEditor::changeMarkerType(modifier.getBuilder(), m_document, marker, newType);
        }
        finishModification(modifier);
        return;
    }

    updateUI();
}

void FixtureCountPlugin::showPanel()
{
    if (!m_panel)
    {
        QMainWindow* mainWindow = m_dataExchangeInterface ? m_dataExchangeInterface->getMainWindow() : nullptr;
        if (!mainWindow)
        {
            return;
        }

        m_panel = new FixtureCountPanel({ m_countAction, m_findSimilarAction, nullptr, m_addTypeAction, m_editTypeAction, m_removeTypeAction, nullptr, m_exportAction }, mainWindow);
        m_panel->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_panel);
        m_panel->setFloating(false);

        // The sheet needs the space, the panel is a narrow column
        mainWindow->resizeDocks({ m_panel }, { m_panel->fontMetrics().horizontalAdvance(QChar('x')) * PANEL_WIDTH_IN_CHARACTERS }, Qt::Horizontal);
        m_panel->setMarkerSize(m_markerSize);

        connect(m_panel, &FixtureCountPanel::currentTypeChanged, this, &FixtureCountPlugin::onCurrentTypeChanged);
        connect(m_panel, &FixtureCountPanel::typeEditRequested, this, &FixtureCountPlugin::editType);
        connect(m_panel, &FixtureCountPanel::markerSizeChanged, this, &FixtureCountPlugin::onMarkerSizeChanged);
        connect(m_panel, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) { updateUI(); } });
    }

    m_panel->show();
    m_panel->raise();
    updateUI();
}

void FixtureCountPlugin::updateUI()
{
    const bool hasDocument = m_document != nullptr;
    const bool hasType = m_currentTypeIndex >= 0 && m_currentTypeIndex < int(m_types.size());

    m_addTypeAction->setEnabled(true);
    m_editTypeAction->setEnabled(hasType);
    m_removeTypeAction->setEnabled(hasType);
    m_exportAction->setEnabled(hasDocument);
    m_showPanelAction->setEnabled(true);
    m_findSimilarAction->setEnabled(hasDocument);

    if (m_countTool)
    {
        m_countTool->setCurrentType(hasType ? m_types[m_currentTypeIndex] : FixtureType());
    }

    if (m_panel && m_panel->isVisible())
    {
        const pdf::PDFInteger currentPageIndex = getCurrentPageIndex();
        const QString sheetName = (currentPageIndex >= 0 && currentPageIndex < m_sheetNames.size()) ? m_sheetNames[currentPageIndex] : QString();
        m_panel->setData(m_types, m_data, currentPageIndex, sheetName, m_currentTypeIndex);
    }
}

void FixtureCountPlugin::mergeDocumentTypes()
{
    for (const FixtureType& documentType : m_data.getTypes())
    {
        auto it = std::find_if(m_types.begin(), m_types.end(), [&documentType](const FixtureType& type) { return type.name == documentType.name; });
        if (it == m_types.end())
        {
            m_types.push_back(documentType);
        }
        else
        {
            // Markers of the document show its own symbol and color, so the remembered
            // type of the same name (from another plan set) must not differ from them
            it->color = documentType.color;
            it->shape = documentType.shape;
            if (!documentType.description.isEmpty())
            {
                it->description = documentType.description;
            }
        }
    }

    if (m_currentTypeIndex < 0 && !m_types.empty())
    {
        m_currentTypeIndex = 0;
    }
}

pdf::PDFInteger FixtureCountPlugin::getCurrentPageIndex() const
{
    if (!m_document || !m_widget)
    {
        return -1;
    }

    const std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    if (currentPages.empty())
    {
        return -1;
    }

    // More pages can be visible, then the page in the middle of the view
    // is the one, which the user looks at
    const QWidget* drawWidget = m_widget->getDrawWidget()->getWidget();
    const pdf::PDFInteger centerPageIndex = m_widget->getDrawWidgetProxy()->getPageUnderPoint(drawWidget->rect().center(), nullptr);
    if (centerPageIndex != -1)
    {
        return centerPageIndex;
    }

    return currentPages[currentPages.size() / 2];
}

QStringList FixtureCountPlugin::getTypeNames(int exceptIndex) const
{
    QStringList names;
    for (int i = 0; i < int(m_types.size()); ++i)
    {
        if (i != exceptIndex)
        {
            names << m_types[i].name;
        }
    }
    return names;
}

bool FixtureCountPlugin::canModifyDocument() const
{
    if (!m_document)
    {
        return false;
    }

    if (!m_document->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems))
    {
        QMessageBox::information(m_widget, tr("Fixture Count"), tr("The document does not allow adding annotations, so fixtures cannot be counted in it."));
        return false;
    }

    return true;
}

void FixtureCountPlugin::finishModification(pdf::PDFDocumentModifier& modifier)
{
    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void FixtureCountPlugin::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("FixtureCountPlugin");
    m_markerSize = qBound(2.0, settings.value("markerSize", 12.0).toDouble(), 144.0);

    // Types of the last counted set are offered again, plan sets of one project
    // are often split into more files with the same fixture schedule
    const int count = settings.beginReadArray("types");
    m_types.clear();
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        FixtureType type;
        type.name = settings.value("name").toString().trimmed();
        type.description = settings.value("description").toString();
        type.color = QColor(settings.value("color").toString());
        type.shape = FixtureType::getShapeFromKey(settings.value("shape").toString());

        if (!type.color.isValid())
        {
            type.color = FixtureType::createDefaultType(i).color;
        }

        if (type.isValid() && std::none_of(m_types.cbegin(), m_types.cend(), [&type](const FixtureType& item) { return item.name == type.name; }))
        {
            m_types.push_back(type);
        }
    }
    settings.endArray();
    settings.endGroup();

    m_currentTypeIndex = m_types.empty() ? -1 : 0;
}

void FixtureCountPlugin::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("FixtureCountPlugin");
    settings.setValue("markerSize", m_markerSize);
    settings.beginWriteArray("types", int(m_types.size()));
    for (int i = 0; i < int(m_types.size()); ++i)
    {
        const FixtureType& type = m_types[i];
        settings.setArrayIndex(i);
        settings.setValue("name", type.name);
        settings.setValue("description", type.description);
        settings.setValue("color", type.color.name());
        settings.setValue("shape", FixtureType::getShapeKey(type.shape));
    }
    settings.endArray();
    settings.endGroup();
}

}   // namespace pdfplugin
