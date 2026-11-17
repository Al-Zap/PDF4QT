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

#ifndef DIMENSIONUNITS_H
#define DIMENSIONUNITS_H

#include "pdfglobal.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{
class PDFMeasure;
}

struct DimensionUnit;
using DimensionUnits = std::vector<DimensionUnit>;

/// Unit of measurement. The identifier is a stable, non-translated string,
/// which is used to store the unit in the application settings, while the
/// symbol is a translated text presented to the user.
struct DimensionUnit
{
    explicit inline DimensionUnit() = default;
    explicit inline DimensionUnit(QByteArray id, pdf::PDFReal scale, QString symbol) :
        id(qMove(id)),
        scale(scale),
        symbol(qMove(symbol))
    {

    }

    /// Returns true, if the unit is a real unit and not a default constructed one
    bool isValid() const { return !id.isEmpty() && scale > 0.0; }

    QByteArray id;
    pdf::PDFReal scale = 1.0;
    QString symbol;

    /// Identifier of the length unit, which is displayed as feet and inches
    /// (for example 12'-6 1/2"). Internally it is the same unit as the foot.
    static constexpr const char* FEET_AND_INCHES_ID = "ftin";

    /// Returns true, if the unit is the length unit displayed as feet and inches
    bool isFeetAndInches() const;

    /// Formats the length expressed in this unit, for example "12.50 m", or
    /// "12'-6\"" for the feet and inches
    /// \param value Length in this unit
    /// \param decimals Count of the decimal places (not used for feet and inches)
    QString formatLength(pdf::PDFReal value, int decimals = 2) const;

    /// Formats the length in feet as feet and inches rounded to the given
    /// fraction of an inch, for example 12.541666 is formatted as 12'-6 1/2"
    /// \param feet Length in feet
    /// \param denominator Fractions of an inch, to which the value is rounded
    static QString formatFeetAndInches(pdf::PDFReal feet, int denominator = 16);

    static DimensionUnits getLengthUnits();
    static DimensionUnits getAreaUnits();
    static DimensionUnits getAngleUnits();

    /// Returns the length unit with the given identifier. If no such unit exists,
    /// then the default length unit is returned.
    static DimensionUnit getLengthUnit(const QByteArray& id);

    /// Returns the area unit with the given identifier. If no such unit exists,
    /// then the default area unit is returned.
    static DimensionUnit getAreaUnit(const QByteArray& id);

    /// Returns the angle unit with the given identifier. If no such unit exists,
    /// then the default angle unit is returned.
    static DimensionUnit getAngleUnit(const QByteArray& id);

    /// Returns the length unit matching the unit label used in a measure
    /// dictionary of a document (for example "mi" or "feet"). If the label
    /// is not recognized, then an invalid unit is returned.
    /// \param label Unit label
    static DimensionUnit findLengthUnitByLabel(const QString& label);
};

/// Scale of a drawing. It is defined as an equality between a distance measured
/// on the paper and the real world distance it represents, for example
/// "1 mm = 50 mm" for a drawing in the scale 1:50. Optionally, the scale can be
/// named, and such named scales are stored as presets in the application settings.
class DimensionScale
{
    Q_DECLARE_TR_FUNCTIONS(DimensionScale)

public:
    explicit inline DimensionScale() = default;
    explicit DimensionScale(pdf::PDFReal paperValue,
                            QByteArray paperUnitId,
                            pdf::PDFReal realValue,
                            QByteArray realUnitId,
                            QString name = QString(),
                            QString description = QString());

    /// Returns true, if the scale is usable, i.e. both values are positive
    /// and both units are known
    bool isValid() const;

    /// Returns the factor, by which a length measured in the default user space
    /// units must be multiplied to obtain the real length, expressed also in the
    /// default user space units. For a drawing in the scale 1:50 the factor is 50.
    /// Areas must be multiplied by the square of this factor.
    pdf::PDFReal getScaleFactor() const;

    pdf::PDFReal getPaperValue() const { return m_paperValue; }
    const QByteArray& getPaperUnitId() const { return m_paperUnitId; }
    pdf::PDFReal getRealValue() const { return m_realValue; }
    const QByteArray& getRealUnitId() const { return m_realUnitId; }

    DimensionUnit getPaperUnit() const { return DimensionUnit::getLengthUnit(m_paperUnitId); }
    DimensionUnit getRealUnit() const { return DimensionUnit::getLengthUnit(m_realUnitId); }

    const QString& getName() const { return m_name; }
    void setName(QString name) { m_name = qMove(name); }

    const QString& getDescription() const { return m_description; }
    void setDescription(QString description) { m_description = qMove(description); }

    /// Returns the scale as a human readable text, for example "1 mm = 50 mm"
    QString getRatioText() const;

    /// Returns the name of the scale, or the ratio text, if the scale is unnamed
    QString getDisplayName() const;

    /// Serializes the scale so it can be stored in the application settings
    QStringList toStringList() const;

    /// Restores the scale from the data created by \p toStringList. If the data
    /// cannot be interpreted, then an invalid scale is returned.
    /// \param data Serialized scale
    static DimensionScale fromStringList(const QStringList& data);

    /// Returns the default scale, in which the drawing is in the real size
    static DimensionScale createIdentity();

    /// Creates the scale from a measure dictionary of a document. Returns
    /// an invalid scale, if the unit of the measure dictionary is not recognized,
    /// or if the coordinate system is anisotropic, because such a coordinate
    /// system cannot be expressed by a single scale factor.
    /// \param measure Measure dictionary
    /// \param userUnit Value of the UserUnit entry of the page, for which is
    ///        the measure defined. Measured lengths are multiplied by it before
    ///        the scale is applied, so it must be removed from the factor here.
    static DimensionScale createFromMeasure(const pdf::PDFMeasure& measure, pdf::PDFReal userUnit);

    /// Returns the same scale expressed for a single paper unit, which people
    /// use on drawings - an inch for the imperial real units, a millimeter
    /// otherwise - for example 1 pt = 0.2778 ft becomes 1 in = 20 ft. The name
    /// and the description are not kept, because they may describe another ratio.
    DimensionScale getReadable() const;

    /// Returns the list of the predefined scales, which are offered to the user,
    /// if no scale presets were stored yet
    static std::vector<DimensionScale> getDefaultPresets();

    bool operator==(const DimensionScale& other) const;
    bool operator!=(const DimensionScale& other) const { return !(*this == other); }

private:
    // A default constructed scale must be invalid - it is returned everywhere,
    // where a scale is not known, and the callers distinguish that case by
    // isValid(). Use createIdentity() to get the scale 1:1.
    pdf::PDFReal m_paperValue = 0.0;
    QByteArray m_paperUnitId;
    pdf::PDFReal m_realValue = 0.0;
    QByteArray m_realUnitId;
    QString m_name;
    QString m_description;
};

#endif // DIMENSIONUNITS_H
