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

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include "dimensionsettings.h"
#include "dimensionunits.h"

#include "pdfdocumentbuilder.h"
#include "pdfmeasure.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentmanipulator.h"
#include "pdfdocumentreader.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdfsnapper.h"

class DimensionsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void test_unit_conversion_factors();
    void test_scale_factor();
    void test_scale_validity();
    void test_scale_serialization();
    void test_scale_from_measure();
    void test_scale_from_measure_without_unit();
    void test_scale_from_measure_user_unit();
    void test_scale_from_measure_rejected();
    void test_settings_persistence();
    void test_presets_persistence();
    void test_presets_can_be_emptied();
    void test_document_scale_by_path();
    void test_document_scale_by_permanent_id();
    void test_document_scale_path_has_priority();
    void test_document_identity_canonical_path();
    void test_feet_and_inches_format();
    void test_feet_and_inches_unit();
    void test_default_presets();
    void test_page_scales();
    void test_page_scales_persistence();
    void test_copy_document_scales();
    void test_page_keys();
    void test_readable_scale();
    void test_geometry_snap_priority();
    void test_geometry_snap_curve();
    void test_geometry_snap_limits();
    void test_geometry_snap_compiled_page();
    void geometrySnapBenchmark();

private:
    /// Creates a measure dictionary with a single number format on each axis
    /// \param unitLabel Unit label of the x axis
    /// \param factorX Count of units per one default user space unit along the x axis
    /// \param factorY Count of units per one default user space unit along the y axis, negative means no Y entry
    /// \param factorYX Value of the CYX entry
    static pdf::PDFObject createMeasure(const QString& unitLabel,
                                        pdf::PDFReal factorX,
                                        pdf::PDFReal factorY = -1.0,
                                        pdf::PDFReal factorYX = 1.0);

    static pdf::PDFObject createNumberFormat(const QString& unitLabel, pdf::PDFReal conversionFactor);

    /// Creates the scale of a sheet or of a document
    static pdfplugin::SheetScale sheetScale(const DimensionScale& scale, pdf::PDFReal drawingCorrection = 0.0)
    {
        pdfplugin::SheetScale result;
        result.scale = scale;
        result.drawingCorrection = drawingCorrection;
        return result;
    }

    /// Creates a file in the settings directory and returns its canonical path
    /// \param fileName Name of the file
    QString createFile(const QString& fileName);

    QTemporaryDir m_settingsDirectory;
    pdf::PDFObjectStorage m_storage;
};

pdf::PDFObject DimensionsTest::createNumberFormat(const QString& unitLabel, pdf::PDFReal conversionFactor)
{
    pdf::PDFObjectFactory factory;

    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("NumberFormat");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("U");
    factory << unitLabel;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("C");
    factory << conversionFactor;
    factory.endDictionaryItem();
    factory.endDictionary();

    return factory.takeObject();
}

pdf::PDFObject DimensionsTest::createMeasure(const QString& unitLabel,
                                             pdf::PDFReal factorX,
                                             pdf::PDFReal factorY,
                                             pdf::PDFReal factorYX)
{
    pdf::PDFObjectFactory factory;

    factory.beginDictionary();

    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Measure");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("R");
    factory << QString("1 mm = 1 mm");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory.beginArray();
    factory << createNumberFormat(unitLabel, factorX);
    factory.endArray();
    factory.endDictionaryItem();

    if (factorY > 0.0)
    {
        factory.beginDictionaryItem("Y");
        factory.beginArray();
        factory << createNumberFormat(unitLabel, factorY);
        factory.endArray();
        factory.endDictionaryItem();
    }

    factory.beginDictionaryItem("CYX");
    factory << factorYX;
    factory.endDictionaryItem();

    factory.endDictionary();

    return factory.takeObject();
}

QString DimensionsTest::createFile(const QString& fileName)
{
    const QString path = m_settingsDirectory.filePath(fileName);

    QFile file(path);
    if (file.open(QFile::WriteOnly))
    {
        file.write("%PDF-1.7\n");
        file.close();
    }

    return QFileInfo(path).canonicalFilePath();
}

void DimensionsTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());

    // The settings must not touch the real settings of the user
    QCoreApplication::setOrganizationName("PDF4QT_UnitTests");
    QCoreApplication::setApplicationName("DimensionsTest");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void DimensionsTest::test_unit_conversion_factors()
{
    // One point of the default user space is 1/72 inch, so the imperial units
    // must be derived from the inch by division (see issue #424)
    const DimensionUnit foot = DimensionUnit::getLengthUnit("ft");
    const DimensionUnit yard = DimensionUnit::getLengthUnit("yd");
    const DimensionUnit inch = DimensionUnit::getLengthUnit("in");

    QCOMPARE(foot.id, QByteArray("ft"));
    QCOMPARE(yard.id, QByteArray("yd"));

    // 1 foot is 864 points, 1 yard is 2592 points
    QVERIFY(qFuzzyCompare(1.0 / foot.scale, 864.0));
    QVERIFY(qFuzzyCompare(1.0 / yard.scale, 2592.0));
    QVERIFY(qFuzzyCompare(inch.scale / foot.scale, 12.0));

    // Millimeters must be consistent with the inch as well
    const DimensionUnit millimeter = DimensionUnit::getLengthUnit("mm");
    QVERIFY(qFuzzyCompare(millimeter.scale / inch.scale, 25.4));

    // Unknown identifier falls back to the default unit
    QCOMPARE(DimensionUnit::getLengthUnit("unknown").id, DimensionUnit::getLengthUnits().front().id);
}

void DimensionsTest::test_scale_factor()
{
    // Drawing in the scale 1:50 - a millimeter on the paper is 50 millimeters
    // in the reality
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "mm", 50.0, "mm").getScaleFactor(), 50.0));

    // The units of both sides may differ
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "mm", 1.0, "m").getScaleFactor(), 1000.0));
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "cm", 100.0, "m").getScaleFactor(), 10000.0));

    // Architectural scale 1/8" = 1'
    QVERIFY(qFuzzyCompare(DimensionScale(0.125, "in", 1.0, "ft").getScaleFactor(), 96.0));

    // Enlarged drawings must be possible as well
    QVERIFY(qFuzzyCompare(DimensionScale(2.0, "mm", 1.0, "mm").getScaleFactor(), 0.5));

    QVERIFY(qFuzzyCompare(DimensionScale::createIdentity().getScaleFactor(), 1.0));
}

void DimensionsTest::test_scale_validity()
{
    // A default constructed scale is the "no scale is known" value returned
    // from the whole plugin, it must never behave as the identity scale
    QVERIFY(!DimensionScale().isValid());
    QVERIFY(DimensionScale() != DimensionScale::createIdentity());
    QVERIFY(DimensionScale::createIdentity().isValid());

    // Unknown units are not accepted
    QVERIFY(!DimensionScale(1.0, "parsec", 1.0, "mm").isValid());
    QVERIFY(!DimensionScale(1.0, "mm", 1.0, "parsec").isValid());

    // Zero or negative values are not accepted
    QVERIFY(!DimensionScale(0.0, "mm", 1.0, "mm").isValid());
    QVERIFY(!DimensionScale(1.0, "mm", -1.0, "mm").isValid());
}

void DimensionsTest::test_scale_serialization()
{
    DimensionScale scale(1.0, "mm", 50.0, "mm", QString("Site plan; \"main\""), QString("Description, with a comma"));

    DimensionScale restored = DimensionScale::fromStringList(scale.toStringList());

    QVERIFY(restored.isValid());
    QCOMPARE(restored, scale);
    QCOMPARE(restored.getName(), scale.getName());
    QCOMPARE(restored.getDescription(), scale.getDescription());
    QVERIFY(qFuzzyCompare(restored.getScaleFactor(), 50.0));

    // Malformed data must not produce a valid scale
    QVERIFY(!DimensionScale::fromStringList(QStringList()).isValid());
    QVERIFY(!DimensionScale::fromStringList(QStringList() << "x" << "mm" << "1" << "mm").isValid());
    QVERIFY(!DimensionScale::fromStringList(QStringList() << "1" << "nonsense" << "1" << "mm").isValid());
}

void DimensionsTest::test_scale_from_measure()
{
    // One default user space unit is 0.5 mm, so the drawing is reduced
    const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 0.5));
    QVERIFY(measure.isValid());
    QVERIFY(!measure.isAnisotropic());

    const DimensionScale scale = DimensionScale::createFromMeasure(measure, 1.0);
    QVERIFY(scale.isValid());

    // A length of 100 points is 50 mm, which is 50 / (100 * PDF_POINT_TO_MM)
    // times the length on the paper
    const pdf::PDFReal expectedFactor = 0.5 / pdf::PDF_POINT_TO_MM;
    QVERIFY(qFuzzyCompare(scale.getScaleFactor(), expectedFactor));

    // The scale is expressed for a millimeter of the paper. The scale ratio
    // text of the document is a free text, it is not used as the name.
    QVERIFY(scale.getName().isEmpty());
    QCOMPARE(scale.getPaperUnitId(), QByteArray("mm"));
    QVERIFY(qAbs(scale.getPaperValue() - 1.0) < 1e-9);

    // Scale of a drawing in feet is expressed for an inch
    const DimensionScale feetScale = DimensionScale::createFromMeasure(pdf::PDFMeasure::parse(&m_storage, createMeasure("ft", 30.0 / 72.0)), 1.0);
    QVERIFY(feetScale.isValid());
    QVERIFY(feetScale.isImperial());
    QVERIFY(!scale.isImperial());
    QCOMPARE(feetScale.getRatioText(), QString("1 in = 30 ft"));
}

void DimensionsTest::test_scale_from_measure_without_unit()
{
    // Drawings plotted by AutoCAD define the conversion to the drawing units
    // with a blank unit label. Values are taken from real plans.
    auto createScale = [this](pdf::PDFReal unitsPerPoint, bool* isUnitAssumed = nullptr)
    {
        return DimensionScale::createFromMeasure(pdf::PDFMeasure::parse(&m_storage, createMeasure(" ", unitsPerPoint)), 1.0, isUnitAssumed);
    };

    // Site plan 1 in = 50 ft
    bool isUnitAssumed = false;
    const DimensionScale sitePlan = createScale(0.69444, &isUnitAssumed);
    QVERIFY(sitePlan.isValid());
    QVERIFY(isUnitAssumed);
    QCOMPARE(sitePlan.getRatioText(), QString("1 in = 50 ft"));
    QVERIFY(qFuzzyCompare(sitePlan.getScaleFactor(), 600.0));

    // Rounded coordinates, still 1 in = 50 ft and 1 in = 40 ft
    QCOMPARE(createScale(0.69366).getRatioText(), QString("1 in = 50 ft"));
    QCOMPARE(createScale(0.5555).getRatioText(), QString("1 in = 40 ft"));

    // The sheet itself (paper space), one drawing unit is an inch of the paper
    QVERIFY(!createScale(1.0 / 72.0, &isUnitAssumed).isValid());
    QVERIFY(!isUnitAssumed);

    // Floor plan 1/4 in = 1 ft drawn in inches
    const DimensionScale floorPlan = createScale(48.0 / 72.0);
    QCOMPARE(floorPlan.getRealUnitId(), QByteArray("in"));
    QVERIFY(qFuzzyCompare(floorPlan.getScaleFactor(), 48.0));

    // Metric drawing 1:100 drawn in millimeters
    const DimensionScale metricPlan = createScale(2540.0 / 72.0);
    QCOMPARE(metricPlan.getRealUnitId(), QByteArray("mm"));
    QVERIFY(qFuzzyCompare(metricPlan.getScaleFactor(), 100.0));

    // A drawing fitted to the sheet is not in a common scale, feet are assumed
    // and the ratio is kept as it is
    const DimensionScale fitted = createScale(0.31406, &isUnitAssumed);
    QVERIFY(isUnitAssumed);
    QCOMPARE(fitted.getRealUnitId(), QByteArray("ft"));
    QVERIFY(qAbs(fitted.getRealValue() - 0.31406 * 72.0) < 1e-9);

    // A unit told by the drawing is never estimated
    DimensionScale::createFromMeasure(pdf::PDFMeasure::parse(&m_storage, createMeasure("ft", 30.0 / 72.0)), 1.0, &isUnitAssumed);
    QVERIFY(!isUnitAssumed);
}

void DimensionsTest::test_scale_from_measure_user_unit()
{
    // Measured lengths are multiplied by the UserUnit of the page before the
    // scale is applied, so the scale must compensate it. Otherwise a page with
    // UserUnit 2 would measure twice the real length (see issue #424).
    const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0));

    const DimensionScale scaleWithoutUserUnit = DimensionScale::createFromMeasure(measure, 1.0);
    const DimensionScale scaleWithUserUnit = DimensionScale::createFromMeasure(measure, 2.0);

    QVERIFY(scaleWithoutUserUnit.isValid());
    QVERIFY(scaleWithUserUnit.isValid());
    QVERIFY(qFuzzyCompare(scaleWithoutUserUnit.getScaleFactor(), 2.0 * scaleWithUserUnit.getScaleFactor()));

    // A line of 10 coordinate units on a page with UserUnit 2 is measured as
    // 20 points and must still be 10 mm
    const pdf::PDFReal measuredValue = 10.0 * 2.0;
    const pdf::PDFReal displayedValue = measuredValue * scaleWithUserUnit.getScaleFactor() * DimensionUnit::getLengthUnit("mm").scale;
    QVERIFY(qFuzzyCompare(displayedValue, 10.0));
}

void DimensionsTest::test_scale_from_measure_rejected()
{
    // Unit label, which cannot be interpreted
    const pdf::PDFMeasure unknownUnit = pdf::PDFMeasure::parse(&m_storage, createMeasure("smoots", 1.0));
    QVERIFY(!DimensionScale::createFromMeasure(unknownUnit, 1.0).isValid());

    // Anisotropic coordinate system cannot be expressed by a single scale factor
    const pdf::PDFMeasure anisotropic = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0, 2.0));
    QVERIFY(anisotropic.isAnisotropic());
    QVERIFY(!DimensionScale::createFromMeasure(anisotropic, 1.0).isValid());

    // The CYX factor makes the coordinate system anisotropic as well
    const pdf::PDFMeasure withFactorYX = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0, 1.0, 2.0));
    QVERIFY(withFactorYX.isAnisotropic());
    QVERIFY(!DimensionScale::createFromMeasure(withFactorYX, 1.0).isValid());

    // Invalid measure
    QVERIFY(!DimensionScale::createFromMeasure(pdf::PDFMeasure(), 1.0).isValid());
}

void DimensionsTest::test_settings_persistence()
{
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.getSettings().lengthUnit = DimensionUnit::getLengthUnit("m");
        storage.getSettings().areaUnit = DimensionUnit::getAreaUnit("sqm");
        storage.getSettings().defaultScale = DimensionScale(1.0, "mm", 100.0, "mm", QString("Default"));
        storage.getSettings().storageMode = pdfplugin::DimensionsPluginSettings::StorageMode::Annotations;
        storage.getSettings().isScaleStoredPerDocument = false;
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QCOMPARE(storage.getSettings().lengthUnit.id, QByteArray("m"));
    QCOMPARE(storage.getSettings().areaUnit.id, QByteArray("sqm"));
    QCOMPARE(storage.getSettings().storageMode, pdfplugin::DimensionsPluginSettings::StorageMode::Annotations);
    QCOMPARE(storage.getSettings().isScaleStoredPerDocument, false);
    QVERIFY(qFuzzyCompare(storage.getSettings().defaultScale.getScaleFactor(), 100.0));
    QCOMPARE(storage.getSettings().defaultScale.getName(), QString("Default"));
}

void DimensionsTest::test_presets_persistence()
{
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.setPresets(std::vector<DimensionScale>());
        storage.addPreset(DimensionScale(1.0, "mm", 25.0, "mm", QString("Mine")));

        // A preset with the same name replaces the original one
        storage.addPreset(DimensionScale(1.0, "mm", 75.0, "mm", QString("Mine")));

        // An unnamed preset is not stored at all
        storage.addPreset(DimensionScale(1.0, "mm", 10.0, "mm"));

        QCOMPARE(storage.getPresets().size(), size_t(1));
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QCOMPARE(storage.getPresets().size(), size_t(1));
    QCOMPARE(storage.getPresets().front().getName(), QString("Mine"));
    QVERIFY(qFuzzyCompare(storage.getPresets().front().getScaleFactor(), 75.0));
}

void DimensionsTest::test_presets_can_be_emptied()
{
    // The user is allowed to delete all the presets and the empty list must
    // survive the restart of the application
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.setPresets(std::vector<DimensionScale>());
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QVERIFY(storage.getPresets().empty());
}

void DimensionsTest::test_document_scale_by_path()
{
    pdfplugin::DocumentIdentity firstDocument;
    firstDocument.filePath = "/documents/first.pdf";

    pdfplugin::DocumentIdentity secondDocument;
    secondDocument.filePath = "/documents/second.pdf";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(firstDocument, sheetScale(DimensionScale(1.0, "mm", 100.0, "mm")), false);

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(firstDocument).scale.getScaleFactor(), 100.0));

    // A document, which was never calibrated, must not inherit the scale
    // of another document (see issue #424)
    QVERIFY(!storage.getDocumentScale(secondDocument).isValid());
    QVERIFY(!storage.getDocumentScale(pdfplugin::DocumentIdentity()).isValid());
}

void DimensionsTest::test_document_scale_by_permanent_id()
{
    // A renamed file has to be recognized by its permanent identifier
    const QString originalPath = createFile("plan.pdf");
    const QString renamedPath = m_settingsDirectory.filePath("plan_renamed.pdf");

    pdfplugin::DocumentIdentity original;
    original.filePath = originalPath;
    original.permanentId = "abcdef";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(original, sheetScale(DimensionScale(1.0, "mm", 50.0, "mm")), false);

    QVERIFY(QFile::rename(originalPath, renamedPath));

    pdfplugin::DocumentIdentity renamed;
    renamed.filePath = renamedPath;
    renamed.permanentId = "abcdef";

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(renamed).scale.getScaleFactor(), 50.0));

    // Storing the scale under the new path must move the existing record
    // instead of creating a second one
    storage.setDocumentScale(renamed, sheetScale(DimensionScale(1.0, "mm", 20.0, "mm")), false);
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(renamed).scale.getScaleFactor(), 20.0));

    // A document without a file is identified by the identifier only
    pdfplugin::DocumentIdentity withoutPath;
    withoutPath.permanentId = "abcdef";
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(withoutPath).scale.getScaleFactor(), 20.0));

    // A different identifier must not match anything
    pdfplugin::DocumentIdentity other;
    other.permanentId = "123456";
    QVERIFY(!storage.getDocumentScale(other).isValid());
}

void DimensionsTest::test_document_scale_path_has_priority()
{
    // Two different files may share the permanent identifier, when one of them
    // was created as a copy of the other one. As long as both files exist, they
    // must not share the calibrated scale.
    pdfplugin::DocumentIdentity first;
    first.filePath = createFile("a.pdf");
    first.permanentId = "shared";

    pdfplugin::DocumentIdentity second;
    second.filePath = createFile("b.pdf");
    second.permanentId = "shared";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(first, sheetScale(DimensionScale(1.0, "mm", 100.0, "mm")), false);

    // The copy has not been calibrated yet and the original still exists,
    // so it must not silently use the scale of the original
    QVERIFY(!storage.getDocumentScale(second).isValid());

    storage.setDocumentScale(second, sheetScale(DimensionScale(1.0, "mm", 200.0, "mm")), false);

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(first).scale.getScaleFactor(), 100.0));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(second).scale.getScaleFactor(), 200.0));
}

void DimensionsTest::test_document_identity_canonical_path()
{
    const QString fileName = m_settingsDirectory.filePath("document.pdf");
    QVERIFY(!createFile("document.pdf").isEmpty());

    // The path is canonicalized, so the same file opened through a different
    // spelling of the path is recognized as the same document
    const pdfplugin::DocumentIdentity direct = pdfplugin::DocumentIdentity::create(nullptr, fileName);
    const pdfplugin::DocumentIdentity indirect = pdfplugin::DocumentIdentity::create(nullptr, m_settingsDirectory.filePath("./document.pdf"));

    QVERIFY(direct.isValid());
    QCOMPARE(direct, indirect);
    QVERIFY(direct.permanentId.isEmpty());

    // A document, which has neither a file name nor an identifier, cannot
    // be identified
    QVERIFY(!pdfplugin::DocumentIdentity::create(nullptr, QString()).isValid());
}

void DimensionsTest::test_feet_and_inches_format()
{
    QCOMPARE(DimensionUnit::formatFeetAndInches(0.0), QString("0'-0\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(12.0), QString("12'-0\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(12.5), QString("12'-6\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(12.0 + 6.5 / 12.0), QString("12'-6 1/2\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(1.0 / 12.0 * (3.0 + 3.0 / 16.0)), QString("0'-3 3/16\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(100.0), QString("100'-0\""));

    // Rounding to the nearest sixteenth carries over to the inches and to the feet
    QCOMPARE(DimensionUnit::formatFeetAndInches(11.0 + 11.999 / 12.0), QString("12'-0\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(2.0 + 5.999 / 12.0), QString("2'-6\""));

    // Other precision
    QCOMPARE(DimensionUnit::formatFeetAndInches(1.0 + 1.3 / 12.0, 4), QString("1'-1 1/4\""));
    QCOMPARE(DimensionUnit::formatFeetAndInches(-1.5), QString("-1'-6\""));
}

void DimensionsTest::test_feet_and_inches_unit()
{
    const DimensionUnit foot = DimensionUnit::getLengthUnit("ft");
    const DimensionUnit feetAndInches = DimensionUnit::getLengthUnit(DimensionUnit::FEET_AND_INCHES_ID);

    QVERIFY(feetAndInches.isFeetAndInches());
    QVERIFY(!foot.isFeetAndInches());
    QVERIFY(qFuzzyCompare(feetAndInches.scale, foot.scale));

    // 864 points is one foot
    QCOMPARE(feetAndInches.formatLength(864.0 * feetAndInches.scale * 10.25), QString("10'-3\""));
    QCOMPARE(foot.formatLength(10.25), QLocale().toString(10.25, 'f', 2) + QString(" ft"));
}

void DimensionsTest::test_default_presets()
{
    const std::vector<DimensionScale> presets = DimensionScale::getDefaultPresets();

    auto findFactor = [&presets](pdf::PDFReal factor)
    {
        return std::any_of(presets.cbegin(), presets.cend(), [factor](const DimensionScale& scale) { return scale.isValid() && qFuzzyCompare(scale.getScaleFactor(), factor); });
    };

    // Engineering scales 1" = 10' ... 200', i.e. 1:120 ... 1:2400
    for (int feet : { 10, 20, 30, 40, 50, 60, 100, 200 })
    {
        QVERIFY2(findFactor(12.0 * feet), qPrintable(QString("Engineering 1\" = %1'").arg(feet)));
    }

    // Architectural scales 1/8" = 1'-0" is 1:96, 1/4" = 1'-0" is 1:48, 3/32" = 1'-0" is 1:128
    QVERIFY(findFactor(96.0));
    QVERIFY(findFactor(48.0));
    QVERIFY(findFactor(128.0));
    QVERIFY(findFactor(4.0));

    // Each preset is named, so it can be recognized in the menu
    for (const DimensionScale& preset : presets)
    {
        QVERIFY(preset.isValid());
        QVERIFY(!preset.getName().isEmpty());
    }
}

void DimensionsTest::test_page_scales()
{
    pdfplugin::DocumentIdentity document;
    document.filePath = "/documents/plan-set.pdf";

    const DimensionScale siteScale(1.0, "in", 20.0, "ft");
    const DimensionScale detailScale(0.25, "in", 1.0, "ft");

    pdfplugin::DimensionsSettingsStorage storage;
    QVERIFY(storage.getPageScales(document).empty());

    // A sheet can be calibrated, even if the document was not
    storage.setPageScale(document, "sheet-3", sheetScale(detailScale));
    QVERIFY(!storage.getDocumentScale(document).isValid());
    QCOMPARE(storage.getPageScales(document).size(), size_t(1));
    QVERIFY(qFuzzyCompare(storage.getPageScales(document).at("sheet-3").scale.getScaleFactor(), 48.0));

    // Scale of the whole document applies to all sheets, if the exceptions are not kept
    storage.setDocumentScale(document, sheetScale(siteScale), false);
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(document).scale.getScaleFactor(), 240.0));
    QVERIFY(storage.getPageScales(document).empty());

    // Exception of a single sheet keeps the scale of the document
    storage.setPageScale(document, "sheet-5", sheetScale(detailScale));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(document).scale.getScaleFactor(), 240.0));
    QCOMPARE(storage.getPageScales(document).count("sheet-5"), size_t(1));

    // A new scale of the document can keep the exceptions
    storage.setDocumentScale(document, sheetScale(DimensionScale(1.0, "in", 30.0, "ft")), true);
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(document).scale.getScaleFactor(), 360.0));
    QCOMPARE(storage.getPageScales(document).count("sheet-5"), size_t(1));

    // Invalid scale removes the exception
    storage.setPageScale(document, "sheet-5", pdfplugin::SheetScale());
    QVERIFY(storage.getPageScales(document).empty());
    QVERIFY(storage.getDocumentScale(document).isValid());

    // A sheet can have the correction of the drawing only
    storage.setPageScale(document, "sheet-7", sheetScale(DimensionScale(), 2.0));
    QVERIFY(qFuzzyCompare(storage.getPageScales(document).at("sheet-7").drawingCorrection, 2.0));
    QVERIFY(!storage.getPageScales(document).at("sheet-7").scale.isValid());

    // Invalid scale of the document removes the scale of the whole document,
    // the sheets are kept
    storage.setDocumentScale(document, pdfplugin::SheetScale(), true);
    QVERIFY(!storage.getDocumentScale(document).isValid());
    QCOMPARE(storage.getPageScales(document).size(), size_t(1));

    // Invalid identity or page are ignored
    storage.setPageScale(pdfplugin::DocumentIdentity(), "sheet-0", sheetScale(detailScale));
    storage.setPageScale(document, QString(), sheetScale(detailScale));
    QCOMPARE(storage.getPageScales(document).size(), size_t(1));
}

void DimensionsTest::test_page_scales_persistence()
{
    pdfplugin::DocumentIdentity document;
    document.filePath = "/documents/lighting.pdf";

    pdfplugin::DocumentIdentity sheetsOnlyDocument;
    sheetsOnlyDocument.filePath = "/documents/details.pdf";

    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.load();
        storage.setDocumentScale(document, sheetScale(DimensionScale(0.125, "in", 1.0, "ft"), 2.0), false);
        storage.setPageScale(document, "first", sheetScale(DimensionScale(1.0, "in", 30.0, "ft")));
        storage.setPageScale(document, "detail", sheetScale(DimensionScale(0.5, "in", 1.0, "ft", QString("Detail")), 0.5));
        storage.setPageScale(sheetsOnlyDocument, "third", sheetScale(DimensionScale(1.0, "in", 40.0, "ft")));
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(document).scale.getScaleFactor(), 96.0));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(document).drawingCorrection, 2.0));

    const std::map<QString, pdfplugin::SheetScale> pageScales = storage.getPageScales(document);
    QCOMPARE(pageScales.size(), size_t(2));
    QVERIFY(qFuzzyCompare(pageScales.at("first").scale.getScaleFactor(), 360.0));
    QVERIFY(!pageScales.at("first").hasDrawingCorrection());
    QVERIFY(qFuzzyCompare(pageScales.at("detail").scale.getScaleFactor(), 24.0));
    QVERIFY(qFuzzyCompare(pageScales.at("detail").drawingCorrection, 0.5));
    QCOMPARE(pageScales.at("detail").scale.getName(), QString("Detail"));

    // A document with calibrated sheets only is remembered as well
    QVERIFY(!storage.getDocumentScale(sheetsOnlyDocument).isValid());
    QCOMPARE(storage.getPageScales(sheetsOnlyDocument).size(), size_t(1));
    QVERIFY(qFuzzyCompare(storage.getPageScales(sheetsOnlyDocument).at("third").scale.getScaleFactor(), 480.0));
}

void DimensionsTest::test_copy_document_scales()
{
    // Save As keeps the original file, the copy must get its calibration
    pdfplugin::DocumentIdentity original;
    original.filePath = createFile("original.pdf");
    original.permanentId = "same-id";

    pdfplugin::DocumentIdentity savedAs;
    savedAs.filePath = createFile("saved-as.pdf");
    savedAs.permanentId = "same-id";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(original, sheetScale(DimensionScale(1.0, "in", 20.0, "ft"), 2.0), false);
    storage.setPageScale(original, "detail", sheetScale(DimensionScale(0.25, "in", 1.0, "ft")));
    QVERIFY(!storage.getDocumentScale(savedAs).isValid());

    storage.copyDocumentScales(original, savedAs);
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(savedAs).scale.getScaleFactor(), 240.0));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(savedAs).drawingCorrection, 2.0));
    QCOMPARE(storage.getPageScales(savedAs).count("detail"), size_t(1));

    // Both files keep their own record
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(original).scale.getScaleFactor(), 240.0));
    storage.setDocumentScale(savedAs, sheetScale(DimensionScale(1.0, "in", 40.0, "ft")), true);
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(original).scale.getScaleFactor(), 240.0));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(savedAs).scale.getScaleFactor(), 480.0));

    // Nothing is copied from a document, which is not known
    pdfplugin::DocumentIdentity unknown;
    unknown.filePath = "/documents/unknown.pdf";
    pdfplugin::DocumentIdentity target;
    target.filePath = "/documents/target.pdf";
    storage.copyDocumentScales(unknown, target);
    QVERIFY(!storage.getDocumentScale(target).isValid());
    QVERIFY(storage.getPageScales(target).empty());
}

void DimensionsTest::test_readable_scale()
{
    // Scale of a viewport is defined for a single point of the paper
    const DimensionScale viewportScale(1.0, "pt", 20.0 / 72.0, "ft");
    const DimensionScale readable = viewportScale.getReadable();
    QCOMPARE(readable.getPaperUnitId(), QByteArray("in"));
    QCOMPARE(readable.getRealUnitId(), QByteArray("ft"));
    QVERIFY(qAbs(readable.getPaperValue() - 1.0) < 1e-9);
    QVERIFY(qAbs(readable.getRealValue() - 20.0) < 1e-9);
    QVERIFY(qAbs(readable.getScaleFactor() - viewportScale.getScaleFactor()) < 1e-6);

    // Metric scales are expressed for a millimeter
    const DimensionScale metricScale(1.0, "pt", 0.05, "m");
    const DimensionScale metricReadable = metricScale.getReadable();
    QCOMPARE(metricReadable.getPaperUnitId(), QByteArray("mm"));
    QCOMPARE(metricReadable.getRealUnitId(), QByteArray("m"));
    QVERIFY(qAbs(metricReadable.getScaleFactor() - metricScale.getScaleFactor()) < 1e-6 * metricScale.getScaleFactor());

    QVERIFY(!DimensionScale().getReadable().isValid());
}

void DimensionsTest::test_page_keys()
{
    // Three sheets, each draws a line at a different place
    pdf::PDFDocumentBuilder builder;
    pdf::PDFPageContentStreamBuilder content(&builder);
    for (int i = 0; i < 3; ++i)
    {
        QPainter* painter = content.beginNewPage(QRectF(0, 0, 612, 792));
        painter->setPen(QPen(Qt::red, 2.0));
        painter->drawLine(QPointF(100, 100 + 50 * i), QPointF(400, 100 + 50 * i));
        content.end(painter);
    }
    const pdf::PDFDocument document = builder.build();

    std::vector<QString> keys;
    for (pdf::PDFInteger i = 0; i < 3; ++i)
    {
        keys.push_back(pdfplugin::DocumentIdentity::createPageKey(&document, i));
        QVERIFY(!keys.back().isEmpty());
    }
    QVERIFY(keys[0] != keys[1] && keys[1] != keys[2] && keys[0] != keys[2]);
    QVERIFY(pdfplugin::DocumentIdentity::createPageKey(&document, 3).isEmpty());
    QVERIFY(pdfplugin::DocumentIdentity::createPageKey(&document, -1).isEmpty());
    QVERIFY(pdfplugin::DocumentIdentity::createPageKey(nullptr, 0).isEmpty());

    // The pages are reordered (the objects are renumbered by the optimizer), the
    // sheets keep their keys, so their scales follow them
    pdf::PDFDocumentManipulator manipulator;
    manipulator.addDocument(0, &document);
    pdf::PDFDocumentManipulator::AssembledPages pages = pdf::PDFDocumentManipulator::createAllDocumentPages(0, &document);
    std::swap(pages[0], pages[2]);
    pages.erase(pages.begin() + 1);
    QVERIFY(manipulator.assemble(pages));
    const pdf::PDFDocument reordered = manipulator.takeAssembledDocument();
    QCOMPARE(reordered.getCatalog()->getPageCount(), size_t(2));
    QCOMPARE(pdfplugin::DocumentIdentity::createPageKey(&reordered, 0), keys[2]);
    QCOMPARE(pdfplugin::DocumentIdentity::createPageKey(&reordered, 1), keys[0]);

    // Measuring on the sheet (a new annotation) does not change its key
    pdf::PDFDocumentBuilder annotationBuilder(&document);
    const pdf::PDFObjectReference pageReference = document.getCatalog()->getPage(1)->getPageReference();
    annotationBuilder.createAnnotationSquare(pageReference, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const pdf::PDFDocument annotated = annotationBuilder.build();
    QCOMPARE(annotated.getCatalog()->getPage(1)->getAnnotations().size(), size_t(1));
    QCOMPARE(pdfplugin::DocumentIdentity::createPageKey(&annotated, 1), keys[1]);
}

void DimensionsTest::test_geometry_snap_priority()
{
    using Kind = pdf::PDFPageGeometrySnapIndex::Kind;

    pdf::PDFPageGeometrySnapIndex index;

    // Horizontal line from (0, 0) to (100, 0) and a vertical line crossing it at (40, 0)
    index.addSegment(QPointF(0, 0), QPointF(100, 0), true, true, true);
    index.addSegment(QPointF(40, -50), QPointF(40, 50), true, true, true);
    index.build();

    QCOMPARE(index.getSegmentCount(), size_t(2));

    // End point
    pdf::PDFPageGeometrySnapIndex::Result result = index.find(QPointF(98, 1), 5.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(100, 0));

    // Intersection of the two lines
    result = index.find(QPointF(42, 2), 5.0);
    QCOMPARE(result.kind, Kind::Intersection);
    QVERIFY(QLineF(result.point, QPointF(40, 0)).length() < 1e-4);

    // Midpoint of the horizontal line
    result = index.find(QPointF(51, -2), 5.0);
    QCOMPARE(result.kind, Kind::Midpoint);
    QCOMPARE(result.point, QPointF(50, 0));

    // Nearest point on the line
    result = index.find(QPointF(75, 3), 5.0);
    QCOMPARE(result.kind, Kind::Nearest);
    QVERIFY(QLineF(result.point, QPointF(75, 0)).length() < 1e-4);
    QVERIFY(qAbs(result.distance - 3.0) < 1e-4);

    // Nothing within the tolerance
    QVERIFY(!index.find(QPointF(75, 10), 5.0).isValid());
    QVERIFY(!index.find(QPointF(1000, 1000), 5.0).isValid());

    // End point wins over an intersection, even if the intersection is nearer
    pdf::PDFPageGeometrySnapIndex cornerIndex;
    cornerIndex.addSegment(QPointF(0, 0), QPointF(10, 0), true, true, true);
    cornerIndex.addSegment(QPointF(2, -5), QPointF(2, 5), true, true, true);
    cornerIndex.build();
    result = cornerIndex.find(QPointF(1.5, 0.0), 3.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(0, 0));
}

void DimensionsTest::test_geometry_snap_curve()
{
    using Kind = pdf::PDFPageGeometrySnapIndex::Kind;

    // Quarter of a circle approximated by a cubic curve, its inner points
    // must not be snapped as end points
    QPainterPath path;
    path.moveTo(100, 0);
    path.cubicTo(100, 55.23, 55.23, 100, 0, 100);

    pdf::PDFPageGeometrySnapIndex index;
    index.addPath(path, QTransform());
    index.build();

    QVERIFY(index.getSegmentCount() > 1);

    pdf::PDFPageGeometrySnapIndex::Result result = index.find(QPointF(70.7, 70.7), 3.0);
    QCOMPARE(result.kind, Kind::Nearest);

    result = index.find(QPointF(99, 1), 3.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(100, 0));

    result = index.find(QPointF(1, 99), 3.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(0, 100));

    // Rectangle - its corners are end points, the transformation is applied
    QPainterPath rectangle;
    rectangle.addRect(QRectF(0, 0, 10, 20));
    pdf::PDFPageGeometrySnapIndex rectangleIndex;
    rectangleIndex.addPath(rectangle, QTransform::fromTranslate(100, 200));
    rectangleIndex.build();

    result = rectangleIndex.find(QPointF(109, 219), 2.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(110, 220));
}

void DimensionsTest::test_geometry_snap_limits()
{
    // Empty index snaps nothing
    pdf::PDFPageGeometrySnapIndex emptyIndex;
    emptyIndex.build();
    QVERIFY(emptyIndex.isEmpty());
    QVERIFY(!emptyIndex.find(QPointF(0, 0), 10.0).isValid());

    // Many segments - a dense grid of lines, the query is answered from the
    // cells around the point only and the result is still exact
    pdf::PDFPageGeometrySnapIndex gridIndex;
    for (int i = 0; i <= 200; ++i)
    {
        gridIndex.addSegment(QPointF(i * 10.0, 0.0), QPointF(i * 10.0, 2000.0), true, true, true);
        gridIndex.addSegment(QPointF(0.0, i * 10.0), QPointF(2000.0, i * 10.0), true, true, true);
    }
    gridIndex.addSegment(QPointF(0.0, 0.0), QPointF(2000.0, 2000.0), true, true, true);
    gridIndex.build();

    pdf::PDFPageGeometrySnapIndex::Result result = gridIndex.find(QPointF(1231.0, 569.0), 3.0);
    QCOMPARE(result.kind, pdf::PDFPageGeometrySnapIndex::Kind::Intersection);
    QVERIFY(QLineF(result.point, QPointF(1230.0, 570.0)).length() < 1e-3);

    // Between the lines only the nearest point on a line is found
    result = gridIndex.find(QPointF(1234.0, 567.0), 3.0);
    QCOMPARE(result.kind, pdf::PDFPageGeometrySnapIndex::Kind::Nearest);
    QVERIFY(QLineF(result.point, QPointF(1234.0, 570.0)).length() < 1e-3);

    // The long diagonal line is found in the middle of the page
    result = gridIndex.find(QPointF(1003.0, 1001.0), 2.0);
    QVERIFY(result.isValid());

    // Segment limit
    pdf::PDFPageGeometrySnapIndex limitedIndex = pdf::PDFPageGeometrySnapIndex::create(nullptr, 0);
    QVERIFY(limitedIndex.isEmpty());

    QPainterPath manyLines;
    for (int i = 0; i < 100; ++i)
    {
        manyLines.moveTo(i, 0);
        manyLines.lineTo(i, 10);
    }

    pdf::PDFPrecompiledPage page;
    page.addPath(QPen(Qt::black), QBrush(), manyLines, false);
    limitedIndex = pdf::PDFPageGeometrySnapIndex::create(&page, 10);
    QCOMPARE(limitedIndex.getSegmentCount(), size_t(10));
    QVERIFY(limitedIndex.isTruncated());
}

void DimensionsTest::test_geometry_snap_compiled_page()
{
    using Kind = pdf::PDFPageGeometrySnapIndex::Kind;

    QPainterPath line;
    line.moveTo(0, 0);
    line.lineTo(10, 0);

    QPainterPath glyph;
    glyph.moveTo(500, 500);
    glyph.lineTo(510, 500);

    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform::fromTranslate(100, 100));
    page.addPath(QPen(Qt::black), QBrush(), line, false);
    page.addSaveGraphicState();
    page.addSetWorldMatrix(QTransform::fromScale(2, 2));
    page.addPath(QPen(Qt::black), QBrush(), line, false);
    page.addRestoreGraphicState();

    // Restored world matrix applies again
    QPainterPath secondLine;
    secondLine.moveTo(0, 50);
    secondLine.lineTo(10, 50);
    page.addPath(QPen(Qt::black), QBrush(), secondLine, false);

    // Text is never snapped
    page.addPath(QPen(Qt::black), QBrush(Qt::black), glyph, true);

    const pdf::PDFPageGeometrySnapIndex index = pdf::PDFPageGeometrySnapIndex::create(&page);
    QCOMPARE(index.getSegmentCount(), size_t(3));

    pdf::PDFPageGeometrySnapIndex::Result result = index.find(QPointF(110, 101), 2.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(110, 100));

    result = index.find(QPointF(20, 1), 2.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(20, 0));

    result = index.find(QPointF(110, 149), 2.0);
    QCOMPARE(result.kind, Kind::Endpoint);
    QCOMPARE(result.point, QPointF(110, 150));

    QVERIFY(!index.find(QPointF(500, 500), 5.0).isValid());
}

void DimensionsTest::geometrySnapBenchmark()
{
    // Measures the time needed to build the geometry index of the pages of a real
    // drawing and the time of a single query, i.e. of a single mouse move. It runs
    // only when PDF4QT_GEOMETRY_SNAP_BENCHMARK is set to a PDF file. When the page
    // has a viewport with a measure, the snapped points are converted to the real
    // units, set PDF4QT_GEOMETRY_SNAP_POINTS to "x1,y1,x2,y2" (page coordinates)
    // to measure the distance between the points snapped near them.
    const QString fileName = qEnvironmentVariable("PDF4QT_GEOMETRY_SNAP_BENCHMARK");
    if (fileName.isEmpty())
    {
        QSKIP("Set PDF4QT_GEOMETRY_SNAP_BENCHMARK to a PDF file to run the benchmark");
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    const pdf::PDFDocument document = reader.readFromFile(fileName);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(&document), &optionalContentActivity));
    iccProfileCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(&document), &optionalContentActivity));
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFRenderer renderer(&document, &fontCache, &iccProfileCache, cms.data(), &optionalContentActivity, pdf::PDFRenderer::getDefaultFeatures(), pdf::PDFMeshQualitySettings());

    const QStringList pointsText = qEnvironmentVariable("PDF4QT_GEOMETRY_SNAP_POINTS").split(QChar(','), Qt::SkipEmptyParts);

    for (size_t pageIndex = 0; pageIndex < document.getCatalog()->getPageCount(); ++pageIndex)
    {
        QElapsedTimer timer;
        timer.start();
        pdf::PDFPrecompiledPage compiledPage;
        renderer.compile(&compiledPage, pageIndex);
        const qint64 compileTime = timer.elapsed();

        timer.restart();
        const pdf::PDFPageGeometrySnapIndex index = pdf::PDFPageGeometrySnapIndex::create(&compiledPage);
        const qint64 buildTime = timer.elapsed();

        // Queries along the diagonal of the page, as the mouse would move
        const QRectF mediaBox = document.getCatalog()->getPage(pageIndex)->getMediaBox();
        constexpr int QUERY_COUNT = 10000;
        int snappedCount = 0;
        timer.restart();
        for (int i = 0; i < QUERY_COUNT; ++i)
        {
            const QPointF point = mediaBox.topLeft() + (mediaBox.bottomRight() - mediaBox.topLeft()) * (qreal(i) / QUERY_COUNT);
            snappedCount += index.find(point, 5.0).isValid() ? 1 : 0;
        }
        const qint64 queryTime = timer.nsecsElapsed() / QUERY_COUNT;

        qInfo().noquote() << QString("page %1: compile %2 ms, %3 segments%4, index %5 ms, query %6 us, snapped %7 of %8")
                                 .arg(pageIndex + 1).arg(compileTime).arg(index.getSegmentCount()).arg(index.isTruncated() ? " (truncated)" : "")
                                 .arg(buildTime).arg(queryTime / 1000.0, 0, 'f', 1).arg(snappedCount).arg(QUERY_COUNT);

        if (pointsText.size() == 4)
        {
            const QPointF p1(pointsText[0].toDouble(), pointsText[1].toDouble());
            const QPointF p2(pointsText[2].toDouble(), pointsText[3].toDouble());
            const pdf::PDFPageGeometrySnapIndex::Result r1 = index.find(p1, 5.0);
            const pdf::PDFPageGeometrySnapIndex::Result r2 = index.find(p2, 5.0);
            QVERIFY(r1.isValid() && r2.isValid());

            const pdf::PDFObjectStorage& storage = document.getStorage();
            const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
            const std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(&storage, page->getViewports(&storage));
            const pdf::PDFViewport* viewport = pdf::PDFViewport::findViewportForPoint(viewports, r1.point);
            const DimensionScale scale = viewport ? DimensionScale::createFromMeasure(viewport->getMeasure(), page->getUserUnit()) : DimensionScale();
            const DimensionUnit foot = DimensionUnit::getLengthUnit("ft");
            const pdf::PDFReal length = QLineF(r1.point, r2.point).length() * page->getUserUnit() * scale.getScaleFactor() * foot.scale;

            qInfo().noquote() << QString("  snapped (%1, %2) and (%3, %4), scale %5, distance %6 ft = %7")
                                     .arg(r1.point.x()).arg(r1.point.y()).arg(r2.point.x()).arg(r2.point.y())
                                     .arg(scale.getDisplayName()).arg(length, 0, 'f', 4).arg(DimensionUnit::formatFeetAndInches(length));
        }
    }
}

QTEST_MAIN(DimensionsTest)

#include "tst_dimensionstest.moc"
