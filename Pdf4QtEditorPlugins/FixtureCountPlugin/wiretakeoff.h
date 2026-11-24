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


#ifndef WIRETAKEOFF_H
#define WIRETAKEOFF_H

#include "lightingtable.h"

#include <QPolygonF>

namespace pdfplugin
{

/// Scale of a wire run: count of the length units per one unit of the default
/// user space of the page (the conversion factor of a measure dictionary)
struct WireScale
{
    pdf::PDFReal unitsPerUserSpaceUnit = 0.0;

    /// Unit of the length ("ft", "m", ...)
    QString unit;

    /// Human readable scale ("1 in = 20 ft")
    QString ratio;

    bool isValid() const { return unitsPerUserSpaceUnit > 0.0 && feetPerUnit() > 0.0; }

    /// Returns count of the feet in one unit of the scale, or zero, if the unit is unknown
    pdf::PDFReal feetPerUnit() const;

    /// Creates the scale from the scale of the paper ("1 in = 20 ft")
    /// \param paperInches Length on the paper in inches
    /// \param realLength Real length
    /// \param realUnit Unit of the real length ("ft" or "m")
    /// \param userUnit User unit of the page
    static WireScale fromPaperScale(pdf::PDFReal paperInches, pdf::PDFReal realLength, const QString& realUnit, pdf::PDFReal userUnit);
};

/// Wire or conduit run between the fixtures of one circuit, stored in the
/// document as a measured polyline annotation (measurement of the length,
/// so other viewers display its length too) with the circuit, the count
/// of the fixtures fed by the run and the count of the conductors.
/// Measured polylines without a circuit (made by the measuring tools)
/// are runs too, they are reported as unassigned.
struct WireRun
{
    pdf::PDFInteger pageIndex = -1;
    pdf::PDFObjectReference annotation;
    QString circuit;
    int fixtures = 0;
    int conductors = 0;
    QPolygonF points;
    WireScale scale;

    /// Returns the length of the polyline in the default user space
    pdf::PDFReal getPaperLength() const;

    /// Returns the real length in feet (zero, if the scale is unknown)
    pdf::PDFReal getLengthInFeet() const;
};

using WireRuns = std::vector<WireRun>;

/// Settings of the takeoff
struct WireTakeoffSettings
{
    /// Length of the wire added for each fixture (pole or wall drop,
    /// handhole loop), in feet
    pdf::PDFReal dropLength = 10.0;

    /// Waste and slack added to the wire, in percent
    pdf::PDFReal wastePercent = 10.0;

    /// Count of the conductors of the runs, which do not have their own count
    int defaultConductors = 3;

    /// Report the lengths in meters instead of feet
    bool metric = false;
};

/// Totals of a single circuit
struct WireCircuitTotal
{
    QString circuit;
    int runs = 0;
    int fixtures = 0;

    /// Measured length of the runs (conduit), in feet
    pdf::PDFReal runLength = 0.0;

    /// Length of the drops, in feet
    pdf::PDFReal dropLength = 0.0;

    /// Length of a single conductor including the drops and the waste, in feet
    pdf::PDFReal conductorLength = 0.0;

    /// Total length of all conductors, in feet
    pdf::PDFReal wireLength = 0.0;

    /// Count of the conductors (the largest count of the runs)
    int conductors = 0;
};

using WireCircuitTotals = std::vector<WireCircuitTotal>;

/// Wire and conduit takeoff of the document
class WireTakeoff
{
public:
    WireTakeoff() = delete;

    /// Key of the annotation dictionary, which holds the circuit of the run
    static constexpr const char* CIRCUIT_KEY = "PDF4QT_Circuit";

    /// Key of the annotation dictionary, which holds the count of the fixtures fed by the run
    static constexpr const char* FIXTURES_KEY = "PDF4QT_Fixtures";

    /// Key of the annotation dictionary, which holds the count of the conductors
    static constexpr const char* CONDUCTORS_KEY = "PDF4QT_Conductors";

    /// Scans all pages and returns the runs (measured polylines)
    /// \param document Document (can be nullptr)
    static WireRuns scan(const pdf::PDFDocument* document);

    /// Returns the scale of the drawing at the point of the page: the scale of
    /// the viewport of the drawing, otherwise the scale of the measurements
    /// (measured annotations, also the earlier runs) on the page, otherwise
    /// an invalid scale
    /// \param document Document
    /// \param pageIndex Page index
    /// \param point Point in the page coordinate system
    static WireScale findScale(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex, const QPointF& point);

    /// Creates the run annotation
    /// \param builder Document builder
    /// \param pageReference Page
    /// \param points Points of the polyline (page coordinate system)
    /// \param circuit Circuit
    /// \param fixtures Count of the fixtures fed by the run
    /// \param conductors Count of the conductors
    /// \param scale Scale of the drawing
    static pdf::PDFObjectReference createRun(pdf::PDFDocumentBuilder* builder,
                                             pdf::PDFObjectReference pageReference,
                                             const QPolygonF& points,
                                             const QString& circuit,
                                             int fixtures,
                                             int conductors,
                                             const WireScale& scale);

    /// Returns the totals by circuit, sorted by the circuit (naturally, so "2"
    /// is before "10"), runs without a circuit are the last ones
    /// \param runs Runs
    /// \param settings Settings
    static WireCircuitTotals createTotals(const WireRuns& runs, const WireTakeoffSettings& settings);

    /// Creates the table of the takeoff (header, circuits, total). Runs without
    /// a known scale are not counted.
    /// \param totals Totals
    /// \param settings Settings
    static LightingTable createTable(const WireCircuitTotals& totals, const WireTakeoffSettings& settings);

    /// Returns the color of the circuit (consecutive circuits get different colors)
    static QColor getCircuitColor(const QString& circuit);

    /// Returns the name of the runs without a circuit
    static QString getUnassignedCircuitName();

    /// Count of meters in one foot
    static constexpr pdf::PDFReal METERS_PER_FOOT = 0.3048;
};

}   // namespace pdfplugin

#endif // WIRETAKEOFF_H
