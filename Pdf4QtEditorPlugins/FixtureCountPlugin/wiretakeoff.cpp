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


#include "wiretakeoff.h"

#include "pdfannotation.h"
#include "pdfutils.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfmeasure.h"

#include <QCoreApplication>
#include <QLineF>

#include <map>

namespace pdfplugin
{

/// Helper functions of the takeoff
class WireTakeoffHelper
{
public:
    WireTakeoffHelper() = delete;

    /// Returns the scale of the measure dictionary (invalid, if it is not a rectilinear measure)
    static WireScale getScale(const pdf::PDFObjectStorage* storage, const pdf::PDFObject& measureObject);

    /// Returns the points of the polyline annotation
    static QPolygonF getVertices(const pdf::PDFDocumentDataLoaderDecorator& loader, const pdf::PDFDictionary* dictionary);

    /// Formats the length in the units of the report
    static QString formatLength(pdf::PDFReal feet, const WireTakeoffSettings& settings);

    /// Returns the contents of the run annotation
    static QString getContents(const QString& circuit, int fixtures, pdf::PDFReal feet);
};

pdf::PDFReal WireScale::feetPerUnit() const
{
    const QString key = unit.trimmed().toLower();
    if (key == "ft" || key == "'" || key == "feet" || key == "foot" || key == "ft-in")
    {
        return 1.0;
    }
    if (key == "in" || key == "\"" || key == "inch" || key == "inches")
    {
        return 1.0 / 12.0;
    }
    if (key == "yd")
    {
        return 3.0;
    }
    if (key == "mi")
    {
        return 5280.0;
    }
    if (key == "m")
    {
        return 1.0 / WireTakeoff::METERS_PER_FOOT;
    }
    if (key == "cm")
    {
        return 0.01 / WireTakeoff::METERS_PER_FOOT;
    }
    if (key == "mm")
    {
        return 0.001 / WireTakeoff::METERS_PER_FOOT;
    }
    if (key == "km")
    {
        return 1000.0 / WireTakeoff::METERS_PER_FOOT;
    }

    return 0.0;
}

WireScale WireScale::fromPaperScale(pdf::PDFReal paperInches, pdf::PDFReal realLength, const QString& realUnit, pdf::PDFReal userUnit)
{
    WireScale scale;
    if (paperInches <= 0.0 || realLength <= 0.0 || userUnit <= 0.0)
    {
        return scale;
    }

    // One inch of the paper has 72 units of the default user space (multiplied by the user unit)
    scale.unitsPerUserSpaceUnit = realLength / paperInches / 72.0 * userUnit;
    scale.unit = realUnit;
    scale.ratio = QString("%1 in = %2 %3").arg(paperInches, 0, 'g', 6).arg(realLength, 0, 'g', 6).arg(realUnit);
    return scale;
}

pdf::PDFReal WireRun::getPaperLength() const
{
    pdf::PDFReal length = 0.0;
    for (int i = 1; i < points.size(); ++i)
    {
        length += QLineF(points[i - 1], points[i]).length();
    }
    return length;
}

pdf::PDFReal WireRun::getLengthInFeet() const
{
    if (!scale.isValid())
    {
        return 0.0;
    }

    return getPaperLength() * scale.unitsPerUserSpaceUnit * scale.feetPerUnit();
}

WireScale WireTakeoffHelper::getScale(const pdf::PDFObjectStorage* storage, const pdf::PDFObject& measureObject)
{
    WireScale scale;

    const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(storage, measureObject);
    if (!measure.isRectilinear() || measure.isAnisotropic())
    {
        return scale;
    }

    scale.unitsPerUserSpaceUnit = measure.getUnitsPerUserSpaceUnit();
    scale.unit = measure.getUnitLabel();
    scale.ratio = measure.getScaleRatio();
    return scale;
}

QPolygonF WireTakeoffHelper::getVertices(const pdf::PDFDocumentDataLoaderDecorator& loader, const pdf::PDFDictionary* dictionary)
{
    QPolygonF polygon;
    const std::vector<pdf::PDFReal> vertices = loader.readNumberArrayFromDictionary(dictionary, "Vertices");
    for (size_t i = 0; i + 1 < vertices.size(); i += 2)
    {
        polygon << QPointF(vertices[i], vertices[i + 1]);
    }
    return polygon;
}

QString WireTakeoffHelper::formatLength(pdf::PDFReal feet, const WireTakeoffSettings& settings)
{
    const pdf::PDFReal value = settings.metric ? feet * WireTakeoff::METERS_PER_FOOT : feet;
    return QString::number(qRound64(value));
}

QString WireTakeoffHelper::getContents(const QString& circuit, int fixtures, pdf::PDFReal feet)
{
    QString text = QCoreApplication::translate("pdfplugin::WireTakeoff", "Circuit %1: %2 ft").arg(circuit).arg(feet, 0, 'f', 1);
    if (fixtures > 0)
    {
        text += QCoreApplication::translate("pdfplugin::WireTakeoff", ", %n fixture(s)", nullptr, fixtures);
    }
    return text;
}

WireRuns WireTakeoff::scan(const pdf::PDFDocument* document)
{
    WireRuns runs;

    if (!document)
    {
        return runs;
    }

    pdf::PDFDocumentDataLoaderDecorator loader(document);
    const pdf::PDFCatalog* catalog = document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);
        for (const pdf::PDFObjectReference& annotationReference : page->getAnnotations())
        {
            const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));
            if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") != "PolyLine")
            {
                continue;
            }

            // Only the measured polylines are runs, a polyline with three points
            // and without an explicit length quantity is an angle measurement
            const bool hasCircuit = dictionary->hasKey(CIRCUIT_KEY);
            const QByteArray quantity = loader.readNameFromDictionary(dictionary, pdf::PDFPolygonalGeometryAnnotation::MEASURED_QUANTITY_KEY);
            WireRun run;
            run.points = WireTakeoffHelper::getVertices(loader, dictionary);
            run.scale = WireTakeoffHelper::getScale(&document->getStorage(), dictionary->get("Measure"));
            if (run.points.size() < 2 || (!hasCircuit && !run.scale.isValid()))
            {
                continue;
            }
            if (!hasCircuit && run.points.size() == 3 && quantity != pdf::PDFPolygonalGeometryAnnotation::MEASURED_LENGTH)
            {
                continue;
            }

            run.pageIndex = pdf::PDFInteger(pageIndex);
            run.annotation = annotationReference;
            run.circuit = loader.readTextStringFromDictionary(dictionary, CIRCUIT_KEY, QString()).trimmed();
            run.fixtures = int(qBound(pdf::PDFInteger(0), loader.readIntegerFromDictionary(dictionary, FIXTURES_KEY, 0), pdf::PDFInteger(10000)));
            run.conductors = int(qBound(pdf::PDFInteger(0), loader.readIntegerFromDictionary(dictionary, CONDUCTORS_KEY, 0), pdf::PDFInteger(100)));
            runs.push_back(run);
        }
    }

    return runs;
}

WireScale WireTakeoff::findScale(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex, const QPointF& point)
{
    if (!document)
    {
        return WireScale();
    }

    const pdf::PDFCatalog* catalog = document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return WireScale();
    }

    const pdf::PDFPage* page = catalog->getPage(pageIndex);
    const pdf::PDFObjectStorage* storage = &document->getStorage();

    // Scale of the drawing defined by the document itself
    const std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(storage, page->getViewports(storage));
    if (const pdf::PDFViewport* viewport = pdf::PDFViewport::findViewportForPoint(viewports, point))
    {
        WireScale scale;
        const pdf::PDFMeasure& measure = viewport->getMeasure();
        if (measure.isRectilinear() && !measure.isAnisotropic())
        {
            scale.unitsPerUserSpaceUnit = measure.getUnitsPerUserSpaceUnit();
            scale.unit = measure.getUnitLabel();
            scale.ratio = measure.getScaleRatio();
            if (scale.isValid())
            {
                return scale;
            }
        }
    }

    // Scale of the measurements on the page, the newest one first
    const std::vector<pdf::PDFObjectReference>& annotations = page->getAnnotations();
    for (auto it = annotations.crbegin(); it != annotations.crend(); ++it)
    {
        const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(*it));
        if (!dictionary || !dictionary->hasKey("Measure"))
        {
            continue;
        }

        const WireScale scale = WireTakeoffHelper::getScale(storage, dictionary->get("Measure"));
        if (scale.isValid())
        {
            return scale;
        }
    }

    return WireScale();
}

pdf::PDFObjectReference WireTakeoff::createRun(pdf::PDFDocumentBuilder* builder,
                                               pdf::PDFObjectReference pageReference,
                                               const QPolygonF& points,
                                               const QString& circuit,
                                               int fixtures,
                                               int conductors,
                                               const WireScale& scale)
{
    WireRun run;
    run.points = points;
    run.scale = scale;

    const QColor color = getCircuitColor(circuit);
    const QString subject = QCoreApplication::translate("pdfplugin::WireTakeoff", "Wire Run");
    const QString contents = WireTakeoffHelper::getContents(circuit, fixtures, run.getLengthInFeet());
    const pdf::PDFObjectReference annotation = builder->createAnnotationPolyline(pageReference, points, 2.0, QColor(), color,
                                                                                 pdf::PDFAuthorSettings::getAuthorName(), subject, contents,
                                                                                 pdf::AnnotationLineEnding::Circle, pdf::AnnotationLineEnding::Circle);

    auto writeNumberFormat = [](pdf::PDFObjectFactory& factory, const QString& unit, pdf::PDFReal conversion)
    {
        factory.beginArray();
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("NumberFormat");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("U");
        factory << unit;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << conversion;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("F");
        factory << pdf::WrapName("D");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        factory << pdf::PDFInteger(10);
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endArray();
    };

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("IT");
    factory << pdf::WrapName("PolyLineDimension");
    factory.endDictionaryItem();
    factory.beginDictionaryItem(pdf::PDFPolygonalGeometryAnnotation::MEASURED_QUANTITY_KEY);
    factory << pdf::WrapName(pdf::PDFPolygonalGeometryAnnotation::MEASURED_LENGTH);
    factory.endDictionaryItem();
    factory.beginDictionaryItem(CIRCUIT_KEY);
    factory << circuit;
    factory.endDictionaryItem();
    factory.beginDictionaryItem(FIXTURES_KEY);
    factory << pdf::PDFInteger(qMax(fixtures, 0));
    factory.endDictionaryItem();
    factory.beginDictionaryItem(CONDUCTORS_KEY);
    factory << pdf::PDFInteger(qMax(conductors, 0));
    factory.endDictionaryItem();

    if (scale.isValid())
    {
        factory.beginDictionaryItem("Measure");
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Measure");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << pdf::WrapName("RL");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("R");
        factory << scale.ratio;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("X");
        writeNumberFormat(factory, scale.unit, scale.unitsPerUserSpaceUnit);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        writeNumberFormat(factory, scale.unit, 1.0);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("A");
        writeNumberFormat(factory, QString("sq %1").arg(scale.unit), 1.0);
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
    }

    factory.endDictionary();
    builder->mergeTo(annotation, factory.takeObject());
    builder->updateAnnotationAppearanceStreams(annotation);
    return annotation;
}

WireCircuitTotals WireTakeoff::createTotals(const WireRuns& runs, const WireTakeoffSettings& settings)
{
    std::map<QString, WireCircuitTotal> totalsMap;
    const pdf::PDFReal wasteFactor = 1.0 + qMax(settings.wastePercent, 0.0) / 100.0;
    const QString unassigned = getUnassignedCircuitName();

    for (const WireRun& run : runs)
    {
        if (!run.scale.isValid())
        {
            continue;
        }

        const QString circuit = run.circuit.isEmpty() ? unassigned : run.circuit;
        WireCircuitTotal& total = totalsMap[circuit];
        total.circuit = circuit;

        const int conductors = run.conductors > 0 ? run.conductors : settings.defaultConductors;
        const pdf::PDFReal runLength = run.getLengthInFeet();
        const pdf::PDFReal dropLength = run.fixtures * qMax(settings.dropLength, 0.0);
        const pdf::PDFReal conductorLength = (runLength + dropLength) * wasteFactor;

        total.runs += 1;
        total.fixtures += run.fixtures;
        total.runLength += runLength;
        total.dropLength += dropLength;
        total.conductorLength += conductorLength;
        total.wireLength += conductorLength * conductors;
        total.conductors = qMax(total.conductors, conductors);
    }

    WireCircuitTotals totals;
    for (auto& item : totalsMap)
    {
        totals.push_back(item.second);
    }

    std::sort(totals.begin(), totals.end(), [&](const WireCircuitTotal& l, const WireCircuitTotal& r)
    {
        const bool lUnassigned = l.circuit == unassigned;
        const bool rUnassigned = r.circuit == unassigned;
        if (lUnassigned != rUnassigned)
        {
            return rUnassigned;
        }
        return pdf::PDFStringUtils::isNaturallyLess(l.circuit, r.circuit);
    });

    return totals;
}

LightingTable WireTakeoff::createTable(const WireCircuitTotals& totals, const WireTakeoffSettings& settings)
{
    const QString unit = settings.metric ? QString("m") : QString("ft");

    LightingTable table;
    table.title = QCoreApplication::translate("pdfplugin::WireTakeoff", "WIRE TAKEOFF (DROP %1 %2 PER FIXTURE, WASTE %3%)")
                      .arg(WireTakeoffHelper::formatLength(settings.dropLength, settings), unit)
                      .arg(settings.wastePercent, 0, 'g', 4);

    LightingTableRow header;
    header.bold = true;
    header.cells << QCoreApplication::translate("pdfplugin::WireTakeoff", "Circuit")
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Fixtures")
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Conduit (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Drops (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Per Cond. (%1)").arg(unit)
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Cond.")
                 << QCoreApplication::translate("pdfplugin::WireTakeoff", "Total Wire (%1)").arg(unit);
    table.rows.push_back(header);

    WireCircuitTotal sum;
    for (const WireCircuitTotal& total : totals)
    {
        LightingTableRow row;
        row.cells << total.circuit
                  << QString::number(total.fixtures)
                  << WireTakeoffHelper::formatLength(total.runLength, settings)
                  << WireTakeoffHelper::formatLength(total.dropLength, settings)
                  << WireTakeoffHelper::formatLength(total.conductorLength, settings)
                  << QString::number(total.conductors)
                  << WireTakeoffHelper::formatLength(total.wireLength, settings);
        table.rows.push_back(row);

        sum.fixtures += total.fixtures;
        sum.runLength += total.runLength;
        sum.dropLength += total.dropLength;
        sum.conductorLength += total.conductorLength;
        sum.wireLength += total.wireLength;
    }

    LightingTableRow totalRow;
    totalRow.bold = true;
    totalRow.cells << QCoreApplication::translate("pdfplugin::WireTakeoff", "Total")
                   << QString::number(sum.fixtures)
                   << WireTakeoffHelper::formatLength(sum.runLength, settings)
                   << WireTakeoffHelper::formatLength(sum.dropLength, settings)
                   << WireTakeoffHelper::formatLength(sum.conductorLength, settings)
                   << QString()
                   << WireTakeoffHelper::formatLength(sum.wireLength, settings);
    table.rows.push_back(totalRow);

    return table;
}

QColor WireTakeoff::getCircuitColor(const QString& circuit)
{
    static const QColor colors[] = { QColor(0, 102, 204), QColor(204, 0, 0), QColor(0, 153, 51), QColor(153, 51, 204),
                                     QColor(230, 115, 0), QColor(0, 153, 153), QColor(204, 0, 153), QColor(102, 102, 0) };

    if (circuit.isEmpty())
    {
        return QColor(96, 96, 96);
    }

    // Circuits are usually numbers, consecutive numbers get different colors
    bool ok = false;
    const int number = circuit.toInt(&ok);
    const size_t index = ok ? size_t(qAbs(number)) : size_t(qHash(circuit.toUpper()));
    return colors[index % std::size(colors)];
}

QString WireTakeoff::getUnassignedCircuitName()
{
    return QCoreApplication::translate("pdfplugin::WireTakeoff", "(no circuit)");
}

}   // namespace pdfplugin
