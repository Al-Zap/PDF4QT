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


#include "lightingtools.h"
#include "countlegend.h"
#include "lightingshapetool.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffont.h"
#include "pdfplugin.h"
#include "pdfsecurityhandler.h"
#include "pdftextlayoutgenerator.h"
#include "pdfutils.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace pdfplugin
{

LightingTools::LightingTools(QObject* parent) :
    QObject(parent),
    m_widget(nullptr),
    m_dataExchange(nullptr),
    m_document(nullptr),
    m_placeSheetLegendAction(nullptr),
    m_placeSetLegendAction(nullptr),
    m_updateTablesAction(nullptr),
    m_footcandleAction(nullptr),
    m_scheduleAction(nullptr),
    m_wireRunAction(nullptr),
    m_wireTakeoffAction(nullptr),
    m_placeAction(nullptr),
    m_placeTool(nullptr),
    m_footcandleTool(nullptr),
    m_scheduleTool(nullptr),
    m_wireRunTool(nullptr),
    m_placement(Placement::None),
    m_lastConductors(3),
    m_manualPaperInches(1.0),
    m_manualRealLength(20.0),
    m_manualRealUnit(QStringLiteral("ft"))
{

}

void LightingTools::initialize(pdf::PDFWidget* widget, pdf::IPluginDataExchange* dataExchange)
{
    m_widget = widget;
    m_dataExchange = dataExchange;

    m_placeSheetLegendAction = new QAction(QIcon(":/pdfplugins/fixturecount/legend.svg"), tr("Place Sheet &Legend"), this);
    m_placeSetLegendAction = new QAction(QIcon(":/pdfplugins/fixturecount/legend-set.svg"), tr("Place &Set Legend"), this);
    m_updateTablesAction = new QAction(QIcon(":/pdfplugins/fixturecount/update-legend.svg"), tr("&Update Tables"), this);
    m_footcandleAction = new QAction(QIcon(":/pdfplugins/fixturecount/footcandle.svg"), tr("&Footcandle Grid Stats"), this);
    m_scheduleAction = new QAction(QIcon(":/pdfplugins/fixturecount/schedule.svg"), tr("Read Fixture &Schedule"), this);
    m_wireRunAction = new QAction(QIcon(":/pdfplugins/fixturecount/wire-run.svg"), tr("&Wire Run"), this);
    m_wireTakeoffAction = new QAction(QIcon(":/pdfplugins/fixturecount/takeoff.svg"), tr("Wire &Takeoff..."), this);
    m_placeAction = new QAction(tr("Place Table"), this);

    m_placeSheetLegendAction->setObjectName("lighting_PlaceSheetLegendAction");
    m_placeSetLegendAction->setObjectName("lighting_PlaceSetLegendAction");
    m_updateTablesAction->setObjectName("lighting_UpdateTablesAction");
    m_footcandleAction->setObjectName("lighting_FootcandleStatsAction");
    m_scheduleAction->setObjectName("lighting_ReadScheduleAction");
    m_wireRunAction->setObjectName("lighting_WireRunAction");
    m_wireTakeoffAction->setObjectName("lighting_WireTakeoffAction");
    m_placeAction->setObjectName("lighting_PlaceTableAction");

    m_placeSheetLegendAction->setToolTip(tr("Place a legend of the fixture types counted on this sheet: click where its top left corner goes. It updates itself with the count."));
    m_placeSetLegendAction->setToolTip(tr("Place a legend of the fixture types counted on all sheets: click where its top left corner goes. It updates itself with the count."));
    m_updateTablesAction->setToolTip(tr("Rewrite the legends and wire takeoff tables on the sheets with the current count and runs (needed only after markers were deleted as ordinary comments)"));
    m_footcandleAction->setToolTip(tr("Footcandle grid statistics: drag a box or click the corners of an area over the calculation points to get Avg, Max, Min, Avg:Min and Max:Min"));
    m_scheduleAction->setToolTip(tr("Read a fixture schedule: drag a box around the schedule on the sheet to export it to Excel or to create the fixture types of the count"));
    m_wireRunAction->setToolTip(tr("Wire run: click the points of a conduit run between the fixtures of a circuit, double click or Enter to finish"));
    m_wireTakeoffAction->setToolTip(tr("Wire and conduit takeoff by circuit with the drops per fixture and the waste, export to Excel or place on the sheet"));

    m_footcandleAction->setCheckable(true);
    m_scheduleAction->setCheckable(true);
    m_wireRunAction->setCheckable(true);
    m_placeAction->setCheckable(true);

    pdf::PDFDrawWidgetProxy* proxy = widget->getDrawWidgetProxy();
    m_placeTool = new LightingShapeTool(proxy, LightingShapeTool::Mode::Point, m_placeAction, this);
    m_footcandleTool = new LightingShapeTool(proxy, LightingShapeTool::Mode::Area, m_footcandleAction, this);
    m_scheduleTool = new LightingShapeTool(proxy, LightingShapeTool::Mode::Area, m_scheduleAction, this);
    m_wireRunTool = new LightingShapeTool(proxy, LightingShapeTool::Mode::Polyline, m_wireRunAction, this);

    m_footcandleTool->setColor(QColor(230, 161, 0));
    m_footcandleTool->setHelpMessage(tr("Drag a box over the calculation points, or click the corners of the area and double click to finish. Backspace removes the last corner, Esc cancels."));
    m_scheduleTool->setHelpMessage(tr("Drag a box around the fixture schedule (include its header row)."));
    m_wireRunTool->setHelpMessage(tr("Click the points of the run from fixture to fixture, double click, Enter or right click to finish. Backspace removes the last point, Esc cancels."));

    m_footcandleTool->setLabelProvider([this](pdf::PDFInteger pageIndex, const QPolygonF& shape) -> QString
    {
        if (shape.size() < 3)
        {
            return QString();
        }

        auto it = m_pageValues.find(pageIndex);
        if (it == m_pageValues.end())
        {
            const QRectF pageBox = m_document ? m_document->getCatalog()->getPage(pageIndex)->getMediaBox() : QRectF();
            it = m_pageValues.emplace(pageIndex, FootcandleGrid::collectValues(getTextLayout(pageIndex), QPolygonF(pageBox.adjusted(-1e5, -1e5, 1e5, 1e5)))).first;
        }

        FootcandleValues values;
        std::copy_if(it->second.cbegin(), it->second.cend(), std::back_inserter(values), [&shape](const FootcandleValue& value) { return shape.containsPoint(value.position, Qt::OddEvenFill); });
        const FootcandleStatistics statistics = FootcandleStatistics::compute(values, m_footcandleSettings.ignoreZeros);
        if (!statistics.isValid())
        {
            return tr("No values");
        }
        return tr("%1 points, avg %2").arg(statistics.count).arg(statistics.average, 0, 'f', statistics.decimals);
    });

    m_wireRunTool->setLabelProvider([this](pdf::PDFInteger pageIndex, const QPolygonF& shape) -> QString
    {
        WireRun run;
        run.points = shape;
        run.scale = WireTakeoff::findScale(m_document, pageIndex, shape.front());
        if (!run.scale.isValid())
        {
            return QString();
        }
        return QString("%1 %2").arg(run.getPaperLength() * run.scale.unitsPerUserSpaceUnit, 0, 'f', 1).arg(run.scale.unit);
    });

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    toolManager->addTool(m_placeTool);
    toolManager->addTool(m_footcandleTool);
    toolManager->addTool(m_scheduleTool);
    toolManager->addTool(m_wireRunTool);

    connect(m_placeSheetLegendAction, &QAction::triggered, this, &LightingTools::onPlaceSheetLegendTriggered);
    connect(m_placeSetLegendAction, &QAction::triggered, this, &LightingTools::onPlaceSetLegendTriggered);
    connect(m_updateTablesAction, &QAction::triggered, this, &LightingTools::onUpdateTablesTriggered);
    connect(m_wireTakeoffAction, &QAction::triggered, this, &LightingTools::onWireTakeoffTriggered);
    connect(m_placeTool, &LightingShapeTool::shapePicked, this, &LightingTools::onPlacePointPicked);
    connect(m_footcandleTool, &LightingShapeTool::shapePicked, this, &LightingTools::onFootcandleAreaPicked);
    connect(m_scheduleTool, &LightingShapeTool::shapePicked, this, &LightingTools::onScheduleAreaPicked);
    connect(m_wireRunTool, &LightingShapeTool::shapePicked, this, &LightingTools::onWireRunPicked);
    connect(m_placeTool, &LightingShapeTool::toolActivityChanged, this, [this](bool active)
    {
        if (!active)
        {
            m_placement = Placement::None;
        }
    });

    loadSettings();
    updateActions();
}

void LightingTools::setDocument(pdf::PDFDocument* document)
{
    m_document = document;
    m_textLayouts.clear();
    m_pageValues.clear();
    updateActions();
}

std::vector<QAction*> LightingTools::getActions() const
{
    return { m_placeSheetLegendAction, m_placeSetLegendAction, m_updateTablesAction,
             m_footcandleAction,
             m_scheduleAction,
             m_wireRunAction, m_wireTakeoffAction };
}

int LightingTools::updateTables(pdf::PDFDocumentBuilder* builder, const FixtureTypes& types, const WireTakeoffSettings& takeoffSettings)
{
    // Snapshot of the modified document, the counts and the runs are taken from it
    const pdf::PDFDocument document = builder->build();
    const LightingTableAnnotations tables = LightingTableWriter::scan(&document);

    const bool hasLegends = std::any_of(tables.cbegin(), tables.cend(), [](const LightingTableAnnotation& table) { return table.kind == LightingTableKind::Legend; });
    const bool hasTakeoffs = std::any_of(tables.cbegin(), tables.cend(), [](const LightingTableAnnotation& table) { return table.kind == LightingTableKind::WireTakeoff; });
    if (!hasLegends && !hasTakeoffs)
    {
        return 0;
    }

    const FixtureCountData data = hasLegends ? FixtureCountData::scan(&document) : FixtureCountData();
    const QStringList sheetNames = hasLegends ? FixtureCountReport::getSheetNames(&document) : QStringList();
    const LightingTable takeoffTable = hasTakeoffs ? WireTakeoff::createTable(WireTakeoff::createTotals(WireTakeoff::scan(&document), takeoffSettings), takeoffSettings) : LightingTable();

    int count = 0;
    for (const LightingTableAnnotation& tableAnnotation : tables)
    {
        LightingTable table;
        switch (tableAnnotation.kind)
        {
            case LightingTableKind::Legend:
            {
                const QString sheetName = tableAnnotation.pageIndex < sheetNames.size() ? sheetNames[tableAnnotation.pageIndex] : QString();
                table = CountLegend::createTable(types, data, tableAnnotation.scope, tableAnnotation.pageIndex, sheetName);
                break;
            }

            case LightingTableKind::WireTakeoff:
                table = takeoffTable;
                break;

            default:
                // Footcandle statistics belong to their area, they do not change
                continue;
        }

        if (LightingTableWriter::updateTable(builder, &document, tableAnnotation, table))
        {
            ++count;
        }
    }

    return count;
}

WireTakeoffSettings LightingTools::loadTakeoffSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("LightingTools");

    WireTakeoffSettings takeoffSettings;
    takeoffSettings.dropLength = qBound(0.0, settings.value("dropLength", takeoffSettings.dropLength).toDouble(), 1000.0);
    takeoffSettings.wastePercent = qBound(0.0, settings.value("wastePercent", takeoffSettings.wastePercent).toDouble(), 200.0);
    takeoffSettings.defaultConductors = qBound(1, settings.value("defaultConductors", takeoffSettings.defaultConductors).toInt(), 99);
    takeoffSettings.metric = settings.value("metric", takeoffSettings.metric).toBool();
    return takeoffSettings;
}

void LightingTools::saveTakeoffSettings(const WireTakeoffSettings& takeoffSettings)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("LightingTools");
    settings.setValue("dropLength", takeoffSettings.dropLength);
    settings.setValue("wastePercent", takeoffSettings.wastePercent);
    settings.setValue("defaultConductors", takeoffSettings.defaultConductors);
    settings.setValue("metric", takeoffSettings.metric);
}

QPointF LightingTools::getTablePositionNextToArea(const QPolygonF& area, const QSizeF& tableSize, const QRectF& pageBox, pdf::PageRotation rotation)
{
    // Bounding box of the area in the upright coordinate system of the table
    const QTransform tableToPage = LightingTableWriter::getRotationMatrix(rotation);
    const QTransform pageToTable = tableToPage.inverted();
    const QRectF areaBox = pageToTable.map(area).boundingRect();
    const QRectF uprightPageBox = pageToTable.mapRect(pageBox);
    const pdf::PDFReal gap = 0.5 * LightingTableWriter::getDefaultFontSize(pageBox.size());

    // The y axis points up, so the top of the area is its largest y
    pdf::PDFReal left = areaBox.right() + gap;
    if (left + tableSize.width() > uprightPageBox.right() && areaBox.left() - gap - tableSize.width() >= uprightPageBox.left())
    {
        left = areaBox.left() - gap - tableSize.width();
    }
    left = qBound(uprightPageBox.left(), left, qMax(uprightPageBox.left(), uprightPageBox.right() - tableSize.width()));
    const pdf::PDFReal top = qBound(qMin(uprightPageBox.bottom(), uprightPageBox.top() + tableSize.height()), areaBox.bottom(), uprightPageBox.bottom());

    return tableToPage.map(QPointF(left, top));
}

void LightingTools::onPlaceSheetLegendTriggered()
{
    startPlacement(Placement::SheetLegend);
}

void LightingTools::onPlaceSetLegendTriggered()
{
    startPlacement(Placement::SetLegend);
}

void LightingTools::onUpdateTablesTriggered()
{
    if (!canModifyDocument())
    {
        return;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    const int count = updateTables(modifier.getBuilder(), m_typesProvider ? m_typesProvider() : FixtureTypes(), loadTakeoffSettings());
    if (count == 0)
    {
        showMessage(tr("There is no legend or takeoff table on the sheets."), 5000);
        return;
    }

    finishModification(modifier);
    showMessage(tr("%n table(s) updated.", nullptr, count), 4000);
}

void LightingTools::onWireTakeoffTriggered()
{
    if (!m_document)
    {
        return;
    }

    const WireRuns runs = WireTakeoff::scan(m_document);
    if (runs.empty())
    {
        QMessageBox::information(m_widget, tr("Wire Takeoff"), tr("There are no wire runs on the sheets yet. Use Wire Run to draw the runs of each circuit, "
                                                                 "measured polylines are counted too."));
        return;
    }

    WireTakeoffDialog dialog(runs, loadTakeoffSettings(), m_widget);
    connect(&dialog, &WireTakeoffDialog::exportRequested, this, [this](const LightingTable& table)
    {
        std::vector<QStringList> rows;
        for (const LightingTableRow& row : table.rows)
        {
            rows.push_back(row.cells);
        }
        exportCsv(rows, tr(" - wire takeoff"), tr("Export Wire Takeoff"));
    });

    const int result = dialog.exec();
    const WireTakeoffSettings settings = dialog.getSettings();
    saveTakeoffSettings(settings);

    if (result == WireTakeoffDialog::PlaceResult)
    {
        m_pendingTable = WireTakeoff::createTable(WireTakeoff::createTotals(runs, settings), settings);
        startPlacement(Placement::Takeoff);
    }
}

void LightingTools::startPlacement(Placement placement)
{
    if (!canModifyDocument())
    {
        return;
    }

    pdf::PDFToolManager* toolManager = m_widget->getToolManager();
    toolManager->setActiveTool(m_placeTool);
    m_placement = placement;
    showMessage(tr("Click where the top left corner of the table goes. The table can be moved later like any markup."), 10000);
}

void LightingTools::onPlacePointPicked(pdf::PDFInteger pageIndex, QPolygonF points)
{
    const Placement placement = m_placement;
    m_widget->getToolManager()->setActiveTool(nullptr);

    if (placement == Placement::None || points.isEmpty() || !canModifyDocument())
    {
        return;
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return;
    }

    LightingTable table;
    LightingTableKind kind = LightingTableKind::Legend;
    LightingLegendScope scope = LightingLegendScope::Sheet;

    switch (placement)
    {
        case Placement::SheetLegend:
        case Placement::SetLegend:
        {
            scope = placement == Placement::SetLegend ? LightingLegendScope::Document : LightingLegendScope::Sheet;
            const FixtureCountData data = FixtureCountData::scan(m_document);
            const QStringList sheetNames = FixtureCountReport::getSheetNames(m_document);
            const QString sheetName = pageIndex < sheetNames.size() ? sheetNames[pageIndex] : QString();
            table = CountLegend::createTable(m_typesProvider ? m_typesProvider() : FixtureTypes(), data, scope, pageIndex, sheetName);
            break;
        }

        case Placement::Takeoff:
            table = m_pendingTable;
            kind = LightingTableKind::WireTakeoff;
            break;

        default:
            Q_ASSERT(false);
            return;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    LightingTableWriter::createTable(modifier.getBuilder(), page->getPageReference(), page->getPageRotation(), points.front(), table, kind, scope,
                                     LightingTableWriter::getDefaultFontSize(page->getRotatedMediaBox().size()));
    finishModification(modifier);

    if (kind == LightingTableKind::Legend)
    {
        showMessage(tr("Legend placed. It is rewritten with each change of the count."), 5000);
    }
}

void LightingTools::onFootcandleAreaPicked(pdf::PDFInteger pageIndex, QPolygonF points)
{
    if (!m_document)
    {
        return;
    }

    const FootcandleValues values = FootcandleGrid::collectValues(getTextLayout(pageIndex), points);
    if (values.empty())
    {
        showMessage(tr("No calculation point values were found in the area. The values must be text (exports of AGi32 or Visual are), not a scanned image."), 8000);
        return;
    }

    FootcandleStatsDialog dialog(values, m_footcandleSettings, m_widget);
    const int result = dialog.exec();
    m_footcandleSettings = dialog.getSettings();
    saveSettings();

    if (result != FootcandleStatsDialog::PlaceResult || !canModifyDocument())
    {
        return;
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const pdf::PDFReal fontSize = LightingTableWriter::getDefaultFontSize(page->getRotatedMediaBox().size());
    const LightingTable table = FootcandleGrid::createTable(dialog.getStatistics(), m_footcandleSettings.label, m_footcandleSettings.unit);
    const QPointF topLeft = getTablePositionNextToArea(points, LightingTableWriter::getTableSize(table, fontSize), page->getCropBox(), page->getPageRotation());

    pdf::PDFDocumentModifier modifier(m_document);
    LightingTableWriter::createTable(modifier.getBuilder(), page->getPageReference(), page->getPageRotation(), topLeft, table,
                                     LightingTableKind::FootcandleStats, LightingLegendScope::Sheet, fontSize);
    finishModification(modifier);
}

void LightingTools::onScheduleAreaPicked(pdf::PDFInteger pageIndex, QPolygonF points)
{
    if (!m_document)
    {
        return;
    }

    const FixtureScheduleTable table = FixtureSchedule::readTable(getTextLayout(pageIndex), points);
    if (table.empty())
    {
        showMessage(tr("No text was found in the box. The schedule must be text, not a scanned image."), 8000);
        return;
    }

    FixtureScheduleDialog dialog(table, m_widget);
    connect(&dialog, &FixtureScheduleDialog::exportRequested, this, [this](const FixtureScheduleTable& exportedTable)
    {
        exportCsv(exportedTable, tr(" - fixture schedule"), tr("Export Fixture Schedule"));
    });

    if (dialog.exec() == FixtureScheduleDialog::CreateTypesResult)
    {
        const FixtureTypes existingTypes = m_typesProvider ? m_typesProvider() : FixtureTypes();
        const FixtureTypes types = FixtureSchedule::createTypes(dialog.getTable(), dialog.getHeaderRow(), dialog.getTypeColumn(), dialog.getDescriptionColumn(), existingTypes);
        if (types.empty())
        {
            showMessage(tr("All fixture types of the schedule already exist."), 5000);
        }
        else
        {
            Q_EMIT typesImported(types);
            showMessage(tr("%n fixture type(s) created from the schedule.", nullptr, int(types.size())), 5000);
        }
    }

    m_widget->getToolManager()->setActiveTool(nullptr);
}

void LightingTools::onWireRunPicked(pdf::PDFInteger pageIndex, QPolygonF points)
{
    if (!canModifyDocument())
    {
        return;
    }

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return;
    }

    WireRun run;
    run.points = points;

    const WireScale drawingScale = WireTakeoff::findScale(m_document, pageIndex, points.front());
    WireRunDialog dialog(getCircuits(), m_lastCircuit, m_lastConductors, run.getPaperLength(), drawingScale, page->getUserUnit(), m_widget);
    dialog.setManualScale(m_manualPaperInches, m_manualRealLength, m_manualRealUnit);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    m_lastCircuit = dialog.getCircuit();
    m_lastConductors = dialog.getConductors();
    if (!drawingScale.isValid())
    {
        m_manualPaperInches = dialog.getPaperInches();
        m_manualRealLength = dialog.getRealLength();
        m_manualRealUnit = dialog.getRealUnit();
    }
    saveSettings();

    pdf::PDFDocumentModifier modifier(m_document);
    WireTakeoff::createRun(modifier.getBuilder(), page->getPageReference(), points, dialog.getCircuit(), dialog.getFixtures(), dialog.getConductors(), dialog.getScale());
    updateTables(modifier.getBuilder(), m_typesProvider ? m_typesProvider() : FixtureTypes(), loadTakeoffSettings());
    finishModification(modifier);
}

const pdf::PDFTextLayout& LightingTools::getTextLayout(pdf::PDFInteger pageIndex)
{
    auto it = m_textLayouts.find(pageIndex);
    if (it != m_textLayouts.end())
    {
        return it->second;
    }

    pdf::PDFTextLayout layout;
    const pdf::PDFPage* page = m_document ? m_document->getCatalog()->getPage(pageIndex) : nullptr;
    if (page)
    {
        pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
        pdf::PDFFontCacheShrinkGuard guard(proxy->getFontCache(), this);
        pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
        pdf::PDFTextLayoutGenerator generator(proxy->getFeatures(), page, m_document, proxy->getFontCache(), proxy->getIccProfileCache(), cms.data(),
                                              proxy->getOptionalContentActivity(), QTransform(), proxy->getMeshQualitySettings());
        generator.processContents();
        layout = generator.createTextLayout();
    }

    return m_textLayouts.emplace(pageIndex, std::move(layout)).first->second;
}

bool LightingTools::canModifyDocument() const
{
    if (!m_document)
    {
        return false;
    }

    if (!m_document->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems))
    {
        QMessageBox::information(m_widget, tr("Lighting"), tr("The document does not allow adding annotations."));
        return false;
    }

    return true;
}

void LightingTools::finishModification(pdf::PDFDocumentModifier& modifier)
{
    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void LightingTools::exportCsv(const std::vector<QStringList>& rows, const QString& suffix, const QString& title)
{
    QString fileName = m_dataExchange ? m_dataExchange->getOriginalFileName() : QString();
    QString defaultFileName;
    if (!fileName.isEmpty())
    {
        QFileInfo fileInfo(fileName);
        defaultFileName = fileInfo.absoluteDir().filePath(fileInfo.completeBaseName() + suffix + ".csv");
    }

    fileName = QFileDialog::getSaveFileName(m_widget, title, defaultFileName, tr("CSV files (*.csv);;All files (*.*)"));
    if (fileName.isEmpty())
    {
        return;
    }

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QMessageBox::critical(m_widget, tr("Error"), tr("Cannot write file '%1': %2").arg(fileName, file.errorString()));
        return;
    }

    // Byte order mark, so Excel recognizes the UTF-8 encoding; the separator of the
    // locale, so Excel in the locales with the decimal comma splits the columns
    file.write("\xEF\xBB\xBF");
    file.write(FixtureCountReport::createCsv(rows, pdf::PDFSysUtils::getSystemListSeparator()).toUtf8());
    file.close();

    showMessage(tr("Exported to %1").arg(QFileInfo(fileName).fileName()), 5000);
}

void LightingTools::showMessage(const QString& text, int timeout)
{
    Q_EMIT m_placeTool->messageDisplayRequest(text, timeout);
}

QStringList LightingTools::getCircuits() const
{
    QStringList circuits;
    for (const WireRun& run : WireTakeoff::scan(m_document))
    {
        if (!run.circuit.isEmpty() && !circuits.contains(run.circuit))
        {
            circuits << run.circuit;
        }
    }
    return circuits;
}

void LightingTools::updateActions()
{
    if (!m_placeSheetLegendAction)
    {
        return;
    }

    const bool hasDocument = m_document != nullptr;
    for (QAction* action : getActions())
    {
        action->setEnabled(hasDocument);
    }
}

void LightingTools::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("LightingTools");
    m_footcandleSettings.ignoreZeros = settings.value("ignoreZeros", false).toBool();
    m_footcandleSettings.unit = settings.value("illuminanceUnit", QStringLiteral("fc")).toString();
    m_lastCircuit = settings.value("lastCircuit", QStringLiteral("1")).toString();
    m_lastConductors = qBound(1, settings.value("lastConductors", 3).toInt(), 99);
    m_manualPaperInches = qBound(0.001, settings.value("manualPaperInches", 1.0).toDouble(), 1000.0);
    m_manualRealLength = qBound(0.001, settings.value("manualRealLength", 20.0).toDouble(), 1000000.0);
    m_manualRealUnit = settings.value("manualRealUnit", QStringLiteral("ft")).toString();
}

void LightingTools::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("LightingTools");
    settings.setValue("ignoreZeros", m_footcandleSettings.ignoreZeros);
    settings.setValue("illuminanceUnit", m_footcandleSettings.unit);
    settings.setValue("lastCircuit", m_lastCircuit);
    settings.setValue("lastConductors", m_lastConductors);
    settings.setValue("manualPaperInches", m_manualPaperInches);
    settings.setValue("manualRealLength", m_manualRealLength);
    settings.setValue("manualRealUnit", m_manualRealUnit);
}

}   // namespace pdfplugin
