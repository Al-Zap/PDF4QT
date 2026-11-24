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

#include "pdfvisualcompare.h"
#include "pdfvisualcomparedialog.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfribbon.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdfoperationcontrol.h"
#include "pdfoutline.h"

#include <QtTest>
#include <QTimer>
#include <QAction>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QPushButton>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QDoubleSpinBox>
#include <QRandomGenerator>

#include <functional>
#include <memory>

/// Tests of the visual comparison of revisions (PDFVisualCompare, PDFVisualCompareReport),
/// of its dialog (PDFVisualCompareDialog), of the ribbon action of the editor and of
/// the command 'compare' of PdfTool
class VisualCompareTest : public QObject
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
    void cleanupTestCase();

    // Comparison of the images
    void identicalImages();
    void addedRemovedAndChanged();
    void colorToleranceZeroIgnoresColors();
    void inkOverOtherInk();
    void positionTolerance();
    void automaticAlignment_data();
    void automaticAlignment();
    void automaticAlignmentWithChange();
    void automaticAlignmentOfBlankPage();
    void pageCenterAlignment();
    void mergeDistanceAndMinimumSize();
    void addedAndRemovedPages();
    void pageCoordinates();

    // Comparison of the documents
    void rotatedPage();
    void documentPairsAndCallback();
    void pagePairs();
    void cancellation();
    void scaleToFit();
    void invalidPage();

    // Report
    void reportDocument();
    void reportSummaryPagination();

    // Command line
    void pdfToolCompare();

    // Dialog and ribbon
    void dialogDefaults();
    void dialogComparison();
    void dialogErrors();
    void dialogCancel();
    void ribbonAction();

    // Real document (only with PDF4QT_VISUAL_COMPARE_TEST_FILE)
    void realDocument();

private:
    using DrawFunction = std::function<void(QPainter*)>;

    class CancelledOperation : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCancelled() const override { return true; }
    };

    static QImage createImage(QSize size, const DrawFunction& draw);
    static void drawRandomDrawing(QPainter* painter, QSize size);
    static pdf::PDFDocument createDocument(const std::vector<std::pair<QSizeF, DrawFunction>>& pages);
    static pdf::PDFVisualCompareSettings createSettings(pdf::PDFVisualCompareSettings::Alignment alignment = pdf::PDFVisualCompareSettings::Alignment::PageOrigin);
    static bool isColor(QRgb pixel, QColor color, int tolerance = 2);
    static pdf::PDFDocument readDocument(const QString& fileName);
    static QString writeDocument(const QTemporaryDir& directory, const QString& name, const pdf::PDFDocument& document);
    static void handleNextModalWidget(std::function<void(QWidget*)> function);

    template<typename T>
    static T* child(QWidget* widget, const char* name)
    {
        T* result = widget->findChild<T*>(QString::fromLatin1(name));
        if (!result)
        {
            qWarning() << "Widget not found:" << name;
        }
        return result;
    }

    /// Old document: 3 pages, new document: 4 pages, page 2 has an added square, page 4 is added
    void createRevisions();

    pdfviewer::PDFProgramController* controller() const { return m_window->getProgramController(); }
    QAction* windowAction(const char* name) const { return m_window->findChild<QAction*>(QString::fromLatin1(name)); }

    QTemporaryDir m_directory;
    pdf::PDFDocument m_oldDocument;
    pdf::PDFDocument m_newDocument;
    QString m_oldFileName;
    QString m_newFileName;
    std::unique_ptr<pdfviewer::PDFEditorMainWindow> m_window;
};

QImage VisualCompareTest::createImage(QSize size, const DrawFunction& draw)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    draw(&painter);
    painter.end();
    return image;
}

void VisualCompareTest::drawRandomDrawing(QPainter* painter, QSize size)
{
    QRandomGenerator generator(42);
    painter->setPen(QPen(Qt::black, 2));
    for (int i = 0; i < 150; ++i)
    {
        const QPoint p1(generator.bounded(size.width()), generator.bounded(size.height()));
        const QPoint p2 = p1 + QPoint(generator.bounded(-80, 80), generator.bounded(-80, 80));
        painter->drawLine(p1, p2);
    }
    for (int i = 0; i < 40; ++i)
    {
        painter->fillRect(QRect(generator.bounded(size.width()), generator.bounded(size.height()), generator.bounded(4, 30), generator.bounded(4, 12)), Qt::black);
    }
}

pdf::PDFDocument VisualCompareTest::createDocument(const std::vector<std::pair<QSizeF, DrawFunction>>& pages)
{
    pdf::PDFDocumentBuilder builder;
    for (const auto& page : pages)
    {
        pdf::PDFPageContentStreamBuilder contentBuilder(&builder);
        QPainter* painter = contentBuilder.beginNewPage(QRectF(QPointF(0, 0), page.first));
        if (page.second)
        {
            page.second(painter);
        }
        contentBuilder.end(painter);
    }
    return builder.build();
}

pdf::PDFVisualCompareSettings VisualCompareTest::createSettings(pdf::PDFVisualCompareSettings::Alignment alignment)
{
    pdf::PDFVisualCompareSettings settings;
    settings.resolution = 100.0;
    settings.alignment = alignment;
    return settings;
}

bool VisualCompareTest::isColor(QRgb pixel, QColor color, int tolerance)
{
    return qAbs(qRed(pixel) - color.red()) <= tolerance && qAbs(qGreen(pixel) - color.green()) <= tolerance && qAbs(qBlue(pixel) - color.blue()) <= tolerance;
}

pdf::PDFDocument VisualCompareTest::readDocument(const QString& fileName)
{
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        qWarning() << "Cannot read" << fileName << reader.getErrorMessage();
    }
    return document;
}

QString VisualCompareTest::writeDocument(const QTemporaryDir& directory, const QString& name, const pdf::PDFDocument& document)
{
    const QString fileName = directory.filePath(name);
    pdf::PDFDocumentWriter writer(nullptr);
    if (!writer.write(fileName, &document, false))
    {
        qWarning() << "Cannot write" << fileName;
    }
    return fileName;
}

void VisualCompareTest::handleNextModalWidget(std::function<void(QWidget*)> function)
{
    QTimer* timer = new QTimer();
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, function]()
    {
        if (QWidget* widget = QApplication::activeModalWidget())
        {
            timer->stop();
            timer->deleteLater();
            function(widget);
        }
    });
    timer->start();
}

void VisualCompareTest::createRevisions()
{
    const QSizeF letter(612, 792);
    auto frame = [](QPainter* painter)
    {
        painter->setPen(QPen(Qt::black, 3));
        painter->drawRect(QRectF(36, 36, 540, 720));
        painter->drawLine(QPointF(36, 680), QPointF(576, 680));
    };
    auto withSquare = [frame](QPainter* painter)
    {
        frame(painter);
        painter->fillRect(QRectF(300, 300, 50, 50), Qt::black);
    };
    m_oldDocument = createDocument({ { letter, frame }, { letter, frame }, { letter, frame } });
    m_newDocument = createDocument({ { letter, frame }, { letter, withSquare }, { letter, frame }, { letter, frame } });
}

void VisualCompareTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Visual Compare Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.filePath("settings"));
    QSettings().clear();

    createRevisions();
    m_oldFileName = writeDocument(m_directory, "plan-rev1.pdf", m_oldDocument);
    m_newFileName = writeDocument(m_directory, "plan-rev2.pdf", m_newDocument);
}

void VisualCompareTest::cleanupTestCase()
{
    if (m_window)
    {
        handleNextModalWidget([](QWidget* widget)
        {
            if (QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget))
            {
                messageBox->button(QMessageBox::No)->click();
            }
        });
        m_window->close();
        m_window.reset();
    }
}

void VisualCompareTest::identicalImages()
{
    const QSize size(800, 600);
    const QImage image = createImage(size, [size](QPainter* painter) { drawRandomDrawing(painter, size); });

    for (auto alignment : { pdf::PDFVisualCompareSettings::Alignment::PageOrigin, pdf::PDFVisualCompareSettings::Alignment::PageCenter, pdf::PDFVisualCompareSettings::Alignment::Automatic })
    {
        const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(image, image, createSettings(alignment), QSizeF());
        QVERIFY(result.changes.empty());
        QCOMPARE(result.offset, QPoint());
        QCOMPARE(result.addedPixels + result.removedPixels + result.changedPixels, qint64(0));
        QCOMPARE(result.overlay.size(), size);

        // Page size is derived from the resolution, when it is not given
        QCOMPARE(result.pageSize, QSizeF(576.0, 432.0));
    }
}

void VisualCompareTest::addedRemovedAndChanged()
{
    const QSize size(800, 600);
    auto base = [](QPainter* painter)
    {
        painter->setPen(QPen(Qt::black, 2));
        painter->drawRect(QRect(10, 10, 780, 580));
        painter->fillRect(QRect(100, 98, 200, 4), Qt::black);
    };

    const QImage oldImage = createImage(size, [&](QPainter* painter)
    {
        base(painter);
        painter->fillRect(QRect(100, 398, 200, 4), Qt::black);
        painter->fillRect(QRect(500, 300, 80, 60), QColor(0, 0, 255));
    });
    const QImage newImage = createImage(size, [&](QPainter* painter)
    {
        base(painter);
        painter->fillRect(QRect(600, 100, 50, 50), Qt::black);
        painter->fillRect(QRect(500, 300, 80, 60), QColor(255, 0, 0));
    });

    const pdf::PDFVisualCompareSettings settings = createSettings();
    const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QCOMPARE(result.changes.size(), size_t(3));

    // Changes are sorted from the top to the bottom
    const pdf::PDFVisualCompareChange& added = result.changes[0];
    QCOMPARE(added.type, pdf::PDFVisualCompareChange::Type::Added);
    QCOMPARE(added.pixelRect, QRect(600, 100, 50, 50));
    QCOMPARE(added.addedPixels, 2500);
    QCOMPARE(added.removedPixels, 0);

    const pdf::PDFVisualCompareChange& changed = result.changes[1];
    QCOMPARE(changed.type, pdf::PDFVisualCompareChange::Type::Changed);
    QVERIFY(QRect(499, 299, 82, 62).contains(changed.pixelRect));
    QVERIFY(changed.changedPixels > 4000);

    const pdf::PDFVisualCompareChange& removed = result.changes[2];
    QCOMPARE(removed.type, pdf::PDFVisualCompareChange::Type::Removed);
    QCOMPARE(removed.pixelRect, QRect(100, 398, 200, 4));
    QCOMPARE(removed.removedPixels, 800);

    QCOMPARE(result.getChangeCount(pdf::PDFVisualCompareChange::Type::Added), size_t(1));
    QCOMPARE(result.getChangeCount(pdf::PDFVisualCompareChange::Type::Removed), size_t(1));
    QCOMPARE(result.getChangeCount(pdf::PDFVisualCompareChange::Type::Changed), size_t(1));

    // Overlay colors
    const QImage& overlay = result.overlay;
    QVERIFY(isColor(overlay.pixel(625, 125), settings.addedColor));
    QVERIFY(isColor(overlay.pixel(200, 400), settings.removedColor));
    QVERIFY(isColor(overlay.pixel(540, 330), settings.changedColor));
    QVERIFY(isColor(overlay.pixel(200, 100), settings.unchangedColor));
    QVERIFY(isColor(overlay.pixel(400, 250), Qt::white));

    // Automatic alignment doesn't shift pages, which are not shifted
    const pdf::PDFVisualComparePageResult automatic = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic), QSizeF());
    QCOMPARE(automatic.offset, QPoint());
    QCOMPARE(automatic.changes.size(), size_t(3));
}

void VisualCompareTest::colorToleranceZeroIgnoresColors()
{
    const QSize size(400, 300);
    const QImage oldImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(100, 100, 80, 60), QColor(0, 0, 255)); });
    const QImage newImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(100, 100, 80, 60), QColor(255, 0, 0)); });

    pdf::PDFVisualCompareSettings settings = createSettings();
    QCOMPARE(pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF()).changes.size(), size_t(1));

    settings.colorTolerance = 0;
    QVERIFY(pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF()).changes.empty());

    // Small difference of the color (other color profile, other renderer) is not a change
    const QImage similarImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(100, 100, 80, 60), QColor(30, 20, 230)); });
    settings.colorTolerance = 96;
    QVERIFY(pdf::PDFVisualCompare::compareImages(oldImage, similarImage, settings, QSizeF()).changes.empty());
}

void VisualCompareTest::inkOverOtherInk()
{
    // Gray area (hatch, aerial photo) in both pages: a line drawn over it in the new page
    // is added, a line, which was drawn over it in the old page, is removed
    const QSize size(800, 600);
    auto background = [](QPainter* painter) { painter->fillRect(QRect(100, 100, 600, 400), QColor(200, 200, 200)); };
    const QImage oldImage = createImage(size, [&](QPainter* painter)
    {
        background(painter);
        painter->fillRect(QRect(150, 400, 300, 4), Qt::black);
    });
    const QImage newImage = createImage(size, [&](QPainter* painter)
    {
        background(painter);
        painter->fillRect(QRect(150, 200, 300, 4), Qt::black);
    });

    pdf::PDFVisualCompareSettings settings = createSettings();
    for (int colorTolerance : { 96, 0 })
    {
        settings.colorTolerance = colorTolerance;
        const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
        QCOMPARE(result.changes.size(), size_t(2));
        QCOMPARE(result.changes[0].type, pdf::PDFVisualCompareChange::Type::Added);
        QCOMPARE(result.changes[0].pixelRect, QRect(150, 200, 300, 4));
        QCOMPARE(result.changes[1].type, pdf::PDFVisualCompareChange::Type::Removed);
        QCOMPARE(result.changes[1].pixelRect, QRect(150, 400, 300, 4));
        QVERIFY(isColor(result.overlay.pixel(300, 201), settings.addedColor));
        QVERIFY(isColor(result.overlay.pixel(300, 401), settings.removedColor));
    }

    // Slightly darker background is not a change
    const QImage darkerImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(100, 100, 600, 400), QColor(170, 170, 170)); });
    const QImage lighterImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(100, 100, 600, 400), QColor(200, 200, 200)); });
    QVERIFY(pdf::PDFVisualCompare::compareImages(lighterImage, darkerImage, settings, QSizeF()).changes.empty());
}

void VisualCompareTest::positionTolerance()
{
    const QSize size(800, 600);
    const QImage oldImage = createImage(size, [size](QPainter* painter) { drawRandomDrawing(painter, size); });
    // Content, which is moved into the page from outside, is clipped (it would be added content)
    const QImage newImage = createImage(size, [size](QPainter* painter) { painter->setClipRect(QRect(1, 0, size.width() - 1, size.height())); painter->translate(1, 0); drawRandomDrawing(painter, size); });

    pdf::PDFVisualCompareSettings settings = createSettings();
    settings.tolerancePixels = 1;
    QVERIFY(pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF()).changes.empty());

    settings.tolerancePixels = 0;
    QVERIFY(!pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF()).changes.empty());
}

void VisualCompareTest::automaticAlignment_data()
{
    QTest::addColumn<QPoint>("shift");

    QTest::newRow("none") << QPoint(0, 0);
    QTest::newRow("small") << QPoint(3, -2);
    QTest::newRow("right down") << QPoint(37, 21);
    QTest::newRow("left up") << QPoint(-60, -35);
    QTest::newRow("odd") << QPoint(-13, 47);
}

void VisualCompareTest::automaticAlignment()
{
    QFETCH(QPoint, shift);

    const QSize size(900, 700);
    const QImage oldImage = createImage(size, [size](QPainter* painter) { drawRandomDrawing(painter, size); });
    const QImage newImage = createImage(size, [size, shift](QPainter* painter) { painter->translate(shift); drawRandomDrawing(painter, size); });

    const pdf::PDFVisualCompareSettings settings = createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic);
    QCOMPARE(pdf::PDFVisualCompare::findOffset(oldImage, newImage, settings), shift);

    // Content moved out of the page is a change, the rest is the same
    const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QCOMPARE(result.offset, shift);
    for (const pdf::PDFVisualCompareChange& change : result.changes)
    {
        const QRect inner = QRect(QPoint(), size).adjusted(qAbs(shift.x()) + 90, qAbs(shift.y()) + 90, -qAbs(shift.x()) - 90, -qAbs(shift.y()) - 90);
        QVERIFY2(!inner.contains(change.pixelRect), qPrintable(QString("Change in the middle of the page: %1 %2").arg(change.pixelRect.x()).arg(change.pixelRect.y())));
    }

    // Shift larger than the maximum is not searched
    pdf::PDFVisualCompareSettings limitedSettings = settings;
    limitedSettings.maximumShiftMM = 0.0;
    QCOMPARE(pdf::PDFVisualCompare::findOffset(oldImage, newImage, limitedSettings).manhattanLength() <= 4, true);
}

void VisualCompareTest::automaticAlignmentWithChange()
{
    const QSize size(900, 700);
    const QPoint shift(25, -18);
    const QImage oldImage = createImage(size, [size](QPainter* painter) { drawRandomDrawing(painter, size); });
    const QImage newImage = createImage(size, [size, shift](QPainter* painter)
    {
        painter->translate(shift);
        drawRandomDrawing(painter, size);
        painter->resetTransform();
        painter->fillRect(QRect(400, 300, 40, 40), QColor(0, 160, 0));
    });

    const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic), QSizeF());
    QCOMPARE(result.offset, shift);

    bool found = false;
    for (const pdf::PDFVisualCompareChange& change : result.changes)
    {
        if (change.pixelRect.intersects(QRect(400, 300, 40, 40)))
        {
            found = true;
            QVERIFY(change.pixelRect.contains(QRect(400, 300, 40, 40)));
            QVERIFY(change.addedPixels > 0);
        }
    }
    QVERIFY(found);
}

void VisualCompareTest::automaticAlignmentOfBlankPage()
{
    const QSize size(400, 300);
    const QImage blank = createImage(size, [](QPainter*) { });
    const QImage drawing = createImage(size, [size](QPainter* painter) { drawRandomDrawing(painter, size); });
    const pdf::PDFVisualCompareSettings settings = createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic);

    QCOMPARE(pdf::PDFVisualCompare::findOffset(blank, drawing, settings), QPoint());
    QCOMPARE(pdf::PDFVisualCompare::findOffset(drawing, blank, settings), QPoint());
    QCOMPARE(pdf::PDFVisualCompare::findOffset(QImage(), drawing, settings), QPoint());

    // Everything is added
    const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(blank, drawing, settings, QSizeF());
    QVERIFY(!result.changes.empty());
    for (const pdf::PDFVisualCompareChange& change : result.changes)
    {
        QCOMPARE(change.type, pdf::PDFVisualCompareChange::Type::Added);
    }
}

void VisualCompareTest::pageCenterAlignment()
{
    const QImage oldImage = createImage(QSize(400, 300), [](QPainter* painter) { drawRandomDrawing(painter, QSize(400, 300)); });
    const QImage newImage = createImage(QSize(600, 500), [](QPainter* painter) { painter->translate(100, 100); painter->setClipRect(QRect(0, 0, 400, 300)); drawRandomDrawing(painter, QSize(400, 300)); });

    const pdf::PDFVisualComparePageResult centered = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(pdf::PDFVisualCompareSettings::Alignment::PageCenter), QSizeF());
    QCOMPARE(centered.offset, QPoint(100, 100));
    QVERIFY(centered.changes.empty());
    QCOMPARE(centered.overlay.size(), QSize(600, 500));

    const pdf::PDFVisualComparePageResult origin = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(pdf::PDFVisualCompareSettings::Alignment::PageOrigin), QSizeF());
    QCOMPARE(origin.offset, QPoint());
    QVERIFY(!origin.changes.empty());

    // Automatic alignment tries the centers of pages of different sizes
    const pdf::PDFVisualComparePageResult automatic = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic), QSizeF());
    QCOMPARE(automatic.offset, QPoint(100, 100));
    QVERIFY(automatic.changes.empty());
}

void VisualCompareTest::mergeDistanceAndMinimumSize()
{
    const QSize size(800, 600);
    const QImage oldImage = createImage(size, [](QPainter*) { });
    const QImage newImage = createImage(size, [](QPainter* painter)
    {
        // Two squares 1.5 mm, 3 mm apart, and a single pixel
        painter->fillRect(QRect(200, 200, 6, 6), Qt::black);
        painter->fillRect(QRect(218, 200, 6, 6), Qt::black);
        painter->fillRect(QRect(500, 400, 1, 1), Qt::black);
    });

    pdf::PDFVisualCompareSettings settings = createSettings();
    settings.mergeDistanceMM = 4.0;
    settings.minimumChangeSizeMM = 0.5;
    pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QCOMPARE(result.changes.size(), size_t(1));
    QCOMPARE(result.changes.front().pixelRect, QRect(200, 200, 24, 6));

    // The ignored pixel is drawn as unchanged content
    QVERIFY(isColor(result.overlay.pixel(500, 400), settings.unchangedColor));
    QVERIFY(isColor(result.overlay.pixel(202, 202), settings.addedColor));

    settings.mergeDistanceMM = 1.0;
    result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QCOMPARE(result.changes.size(), size_t(2));
    QCOMPARE(result.changes[0].pixelRect, QRect(200, 200, 6, 6));
    QCOMPARE(result.changes[1].pixelRect, QRect(218, 200, 6, 6));

    settings.minimumChangeSizeMM = 0.0;
    result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QCOMPARE(result.changes.size(), size_t(3));

    settings.minimumChangeSizeMM = 10.0;
    result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, settings, QSizeF());
    QVERIFY(result.changes.empty());
}

void VisualCompareTest::addedAndRemovedPages()
{
    const QSize size(400, 300);
    const QImage drawing = createImage(size, [](QPainter* painter)
    {
        painter->fillRect(QRect(50, 50, 20, 20), Qt::black);
        painter->fillRect(QRect(300, 200, 20, 20), Qt::black);
    });
    const pdf::PDFVisualCompareSettings settings = createSettings();

    // The whole page is a single change
    const pdf::PDFVisualComparePageResult added = pdf::PDFVisualCompare::compareImages(QImage(), drawing, settings, QSizeF());
    QCOMPARE(added.changes.size(), size_t(1));
    QCOMPARE(added.changes.front().type, pdf::PDFVisualCompareChange::Type::Added);
    QCOMPARE(added.changes.front().pixelRect, QRect(50, 50, 270, 170));
    QVERIFY(isColor(added.overlay.pixel(60, 60), settings.addedColor));
    QVERIFY(isColor(added.overlay.pixel(310, 210), settings.addedColor));

    const pdf::PDFVisualComparePageResult removed = pdf::PDFVisualCompare::compareImages(drawing, QImage(), settings, QSizeF());
    QCOMPARE(removed.changes.size(), size_t(1));
    QCOMPARE(removed.changes.front().type, pdf::PDFVisualCompareChange::Type::Removed);
    QCOMPARE(removed.overlay.size(), size);
    QVERIFY(isColor(removed.overlay.pixel(60, 60), settings.removedColor));

    // Blank added page has no change, two null images give an empty result
    QVERIFY(pdf::PDFVisualCompare::compareImages(QImage(), createImage(size, [](QPainter*) { }), settings, QSizeF()).changes.empty());
    QVERIFY(pdf::PDFVisualCompare::compareImages(QImage(), QImage(), settings, QSizeF()).overlay.isNull());

    // Images in other formats are accepted
    const QImage argb = drawing.convertToFormat(QImage::Format_ARGB32);
    QVERIFY(pdf::PDFVisualCompare::compareImages(argb, drawing, settings, QSizeF()).changes.empty());
}

void VisualCompareTest::pageCoordinates()
{
    const QSize size(800, 600);
    const QImage oldImage = createImage(size, [](QPainter*) { });
    const QImage newImage = createImage(size, [](QPainter* painter) { painter->fillRect(QRect(600, 100, 50, 50), Qt::black); });

    // 100 ppi: one pixel is 0.72 point, origin of the page is the bottom left corner
    const pdf::PDFVisualComparePageResult result = pdf::PDFVisualCompare::compareImages(oldImage, newImage, createSettings(), QSizeF(576.0, 432.0));
    QCOMPARE(result.changes.size(), size_t(1));
    const QRectF rect = result.changes.front().pageRect;
    QVERIFY(qFuzzyCompare(rect.left(), 432.0));
    QVERIFY(qFuzzyCompare(rect.top(), 324.0));
    QVERIFY(qFuzzyCompare(rect.width(), 36.0));
    QVERIFY(qFuzzyCompare(rect.height(), 36.0));

    pdf::PDFVisualCompareSettings settings;
    settings.resolution = 254.0;
    QCOMPARE(settings.toPixels(1.0), 10);
    QCOMPARE(settings.toPixels(-1.0), 0);
}

void VisualCompareTest::rotatedPage()
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFPageContentStreamBuilder contentBuilder(&builder);
    QPainter* painter = contentBuilder.beginNewPage(QRectF(0, 0, 612, 792));
    painter->fillRect(QRectF(50, 50, 100, 30), Qt::black);
    contentBuilder.end(painter);
    builder.flattenPageTree();
    builder.setPageRotation(builder.getPages().front(), pdf::PageRotation::Rotate90);
    const pdf::PDFDocument document = builder.build();

    pdf::PDFVisualCompare compare(&document, &document, createSettings());
    QCOMPARE(pdf::PDFVisualCompare::getPageSize(&document, 0), QSizeF(792, 612));

    QString errorMessage;
    const QImage image = compare.renderPage(&document, 0, 1.0, &errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(image.size(), QSize(1100, 850));

    const pdf::PDFVisualComparePageResult result = compare.comparePages(0, 0);
    QVERIFY(result.errorMessage.isEmpty());
    QVERIFY(result.changes.empty());
    QCOMPARE(result.pageSize, QSizeF(792, 612));
}

void VisualCompareTest::documentPairsAndCallback()
{
    pdf::PDFVisualCompare compare(&m_oldDocument, &m_newDocument, createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic));
    const pdf::PDFVisualCompare::PagePairs pairs = compare.createPagePairs();
    QCOMPARE(pairs.size(), size_t(4));
    QCOMPARE(pairs[3], pdf::PDFVisualCompare::PagePair(-1, 3));

    std::vector<pdf::PDFInteger> calledPages;
    bool hadOverlays = true;
    const pdf::PDFVisualCompareResult result = compare.compare(pairs, [&](const pdf::PDFVisualComparePageResult& page)
    {
        calledPages.push_back(page.newPageIndex);
        hadOverlays = hadOverlays && !page.overlay.isNull();
    });

    QVERIFY(result.isSuccess());
    QCOMPARE(calledPages, std::vector<pdf::PDFInteger>({ 0, 1, 2, 3 }));
    QVERIFY(hadOverlays);
    QCOMPARE(result.pages.size(), size_t(4));
    for (const pdf::PDFVisualComparePageResult& page : result.pages)
    {
        // Overlays are released, when a callback is used
        QVERIFY(page.overlay.isNull());
        QVERIFY(page.errorMessage.isEmpty());
        QCOMPARE(page.pageSize, QSizeF(612, 792));
    }

    QVERIFY(!result.pages[0].hasChanges());
    QCOMPARE(result.pages[1].changes.size(), size_t(1));
    QCOMPARE(result.pages[1].changes.front().type, pdf::PDFVisualCompareChange::Type::Added);

    // Square 50 x 50 points at (300, 300) from the top left corner
    const QRectF rect = result.pages[1].changes.front().pageRect;
    QVERIFY(qAbs(rect.left() - 300.0) < 1.5);
    QVERIFY(qAbs(rect.top() - (792.0 - 350.0)) < 1.5);
    QVERIFY(qAbs(rect.width() - 50.0) < 2.0);

    QVERIFY(!result.pages[2].hasChanges());
    QVERIFY(result.pages[3].isPageAdded());
    QCOMPARE(result.pages[3].changes.size(), size_t(1));
    QCOMPARE(result.getChangeCount(), size_t(2));
    QCOMPARE(result.getChangedPageCount(), size_t(2));

    // Without a callback, overlays are kept
    const pdf::PDFVisualCompareResult resultWithImages = compare.compare({ { 1, 1 } });
    QCOMPARE(resultWithImages.pages.size(), size_t(1));
    QCOMPARE(resultWithImages.pages.front().overlay.size(), QSize(850, 1100));
}

void VisualCompareTest::pagePairs()
{
    using Pairs = pdf::PDFVisualCompare::PagePairs;
    QCOMPARE(pdf::PDFVisualCompare::createPagePairs({ 0, 1 }, { 4, 2, 7 }), Pairs({ { 0, 4 }, { 1, 2 }, { -1, 7 } }));
    QCOMPARE(pdf::PDFVisualCompare::createPagePairs({ 3, 2, 1 }, { 0 }), Pairs({ { 3, 0 }, { 2, -1 }, { 1, -1 } }));
    QVERIFY(pdf::PDFVisualCompare::createPagePairs({}, {}).empty());

    // Removed page
    pdf::PDFVisualCompare compare(&m_newDocument, &m_oldDocument, createSettings());
    const pdf::PDFVisualComparePageResult removed = compare.comparePages(3, -1);
    QVERIFY(removed.isPageRemoved());
    QCOMPARE(removed.changes.size(), size_t(1));
    QCOMPARE(removed.changes.front().type, pdf::PDFVisualCompareChange::Type::Removed);
}

void VisualCompareTest::cancellation()
{
    CancelledOperation operation;
    pdf::PDFVisualCompare compare(&m_oldDocument, &m_newDocument, createSettings());
    compare.setOperationControl(&operation);

    const pdf::PDFVisualCompareResult result = compare.compare(compare.createPagePairs());
    QVERIFY(result.cancelled);
    QVERIFY(!result.isSuccess());
    QVERIFY(result.pages.empty());
}

void VisualCompareTest::scaleToFit()
{
    auto draw = [](QPainter* painter)
    {
        painter->fillRect(QRectF(50, 50, 200, 20), Qt::black);
        painter->fillRect(QRectF(300, 400, 100, 100), Qt::black);
        painter->setPen(QPen(Qt::black, 6));
        painter->drawLine(QPointF(60, 700), QPointF(550, 700));
    };
    const pdf::PDFDocument oldDocument = createDocument({ { QSizeF(306, 396), [draw](QPainter* painter) { painter->scale(0.5, 0.5); draw(painter); } } });
    const pdf::PDFDocument newDocument = createDocument({ { QSizeF(612, 792), draw } });

    pdf::PDFVisualCompareSettings settings = createSettings();
    settings.scaleToFit = true;
    settings.tolerancePixels = 2;
    pdf::PDFVisualCompare compare(&oldDocument, &newDocument, settings);
    const pdf::PDFVisualComparePageResult result = compare.comparePages(0, 0);
    QCOMPARE(result.oldPageScale, 2.0);
    QVERIFY(result.changes.empty());

    settings.scaleToFit = false;
    pdf::PDFVisualCompare unscaledCompare(&oldDocument, &newDocument, settings);
    const pdf::PDFVisualComparePageResult unscaled = unscaledCompare.comparePages(0, 0);
    QCOMPARE(unscaled.oldPageScale, 1.0);
    QVERIFY(!unscaled.changes.empty());
}

void VisualCompareTest::invalidPage()
{
    pdf::PDFVisualCompare compare(&m_oldDocument, &m_newDocument, createSettings());
    const pdf::PDFVisualComparePageResult result = compare.comparePages(10, 0);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(result.changes.empty());

    QString errorMessage;
    QVERIFY(compare.renderPage(&m_oldDocument, -1, 1.0, &errorMessage).isNull());
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(pdf::PDFVisualCompare::getPageSize(nullptr, 0).isEmpty());

    // Page too large for the resolution
    const pdf::PDFDocument hugeDocument = createDocument({ { QSizeF(14400, 14400), nullptr } });
    pdf::PDFVisualCompareSettings settings = createSettings();
    settings.resolution = 600.0;
    pdf::PDFVisualCompare hugeCompare(&hugeDocument, &hugeDocument, settings);
    errorMessage.clear();
    QVERIFY(hugeCompare.renderPage(&hugeDocument, 0, 1.0, &errorMessage).isNull());
    QVERIFY(errorMessage.contains("too large"));
}

void VisualCompareTest::reportDocument()
{
    const pdf::PDFVisualCompareSettings settings = createSettings(pdf::PDFVisualCompareSettings::Alignment::Automatic);
    pdf::PDFVisualCompare compare(&m_oldDocument, &m_newDocument, settings);

    pdf::PDFVisualCompareReport::Information information;
    information.oldDocumentName = "plan-rev1.pdf";
    information.newDocumentName = "plan-rev2.pdf";
    information.dateTime = QDateTime(QDate(2026, 11, 24), QTime(10, 30));

    pdf::PDFVisualCompareReport report(settings, information);
    QSize firstOverlaySize;
    const pdf::PDFVisualCompareResult result = compare.compare(compare.createPagePairs(), [&](const pdf::PDFVisualComparePageResult& page)
    {
        if (firstOverlaySize.isEmpty())
        {
            firstOverlaySize = page.overlay.size();
        }
        report.addPage(page);
    });
    QVERIFY(result.isSuccess());

    const pdf::PDFDocument built = report.build(result);
    const QString fileName = writeDocument(m_directory, "report.pdf", built);
    const pdf::PDFDocument document = readDocument(fileName);
    const pdf::PDFCatalog* catalog = document.getCatalog();

    // Summary page and one page per pair
    QCOMPARE(catalog->getPageCount(), size_t(5));
    QCOMPARE(catalog->getPage(0)->getMediaBox().size(), QSizeF(612, 792));
    for (size_t i = 1; i < 5; ++i)
    {
        QCOMPARE(catalog->getPage(i)->getMediaBox(), QRectF(0, 0, 612, 792));
    }
    QVERIFY(document.getInfo()->title.contains("plan-rev1.pdf"));
    QVERIFY(document.getInfo()->title.contains("plan-rev2.pdf"));

    // Overlay is a losslessly compressed image covering the page
    const pdf::PDFPage* overlayPage = catalog->getPage(1);
    const pdf::PDFDictionary* resources = document.getDictionaryFromObject(overlayPage->getResources());
    QVERIFY(resources);
    const pdf::PDFDictionary* xobjects = document.getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xobjects);
    const pdf::PDFObject& imageObject = document.getObject(xobjects->get("Im0"));
    QVERIFY(imageObject.isStream());
    const pdf::PDFDictionary* imageDictionary = imageObject.getStream()->getDictionary();
    pdf::PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readNameFromDictionary(imageDictionary, "Filter"), QByteArray("FlateDecode"));
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), pdf::PDFInteger(firstOverlaySize.width()));
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Height", 0), pdf::PDFInteger(firstOverlaySize.height()));

    // Revision clouds: none on the unchanged page, one on the changed and on the added page
    QVERIFY(catalog->getPage(1)->getAnnotations().empty());
    QCOMPARE(catalog->getPage(2)->getAnnotations().size(), size_t(1));
    QVERIFY(catalog->getPage(3)->getAnnotations().empty());
    QCOMPARE(catalog->getPage(4)->getAnnotations().size(), size_t(1));

    const pdf::PDFDictionary* annotation = document.getDictionaryFromObject(document.getObjectByReference(catalog->getPage(2)->getAnnotations().front()));
    QVERIFY(annotation);
    QCOMPARE(loader.readNameFromDictionary(annotation, "Subtype"), QByteArray("Square"));
    QCOMPARE(loader.readTextStringFromDictionary(annotation, "Contents", QString()), QString("Change 1 of 1: Added"));
    QCOMPARE(loader.readTextStringFromDictionary(annotation, "Subj", QString()), pdf::PDFVisualCompareReport::getChangeSubject());
    const pdf::PDFDictionary* borderEffect = document.getDictionaryFromObject(annotation->get("BE"));
    QVERIFY(borderEffect);
    QCOMPARE(loader.readNameFromDictionary(borderEffect, "S"), QByteArray("C"));
    const std::vector<pdf::PDFReal> color = loader.readNumberArrayFromDictionary(annotation, "C");
    QCOMPARE(color.size(), size_t(3));
    QVERIFY(qAbs(color[0] - settings.addedColor.redF()) < 0.01);
    QVERIFY(qAbs(color[1] - settings.addedColor.greenF()) < 0.01);
    QVERIFY(annotation->hasKey("AP"));

    // The cloud surrounds the change
    const QRectF annotationRect = loader.readRectangle(annotation->get("Rect"), QRectF());
    QVERIFY(annotationRect.contains(result.pages[1].changes.front().pageRect));

    // Outline: summary, pages, changes
    QSharedPointer<pdf::PDFOutlineItem> outline = catalog->getOutlineRootPtr();
    QVERIFY(outline);
    QCOMPARE(outline->getChildCount(), size_t(5));
    QCOMPARE(outline->getChild(0)->getTitle(), QString("Summary"));
    QCOMPARE(outline->getChild(1)->getTitle(), QString("Page 1 - 0 changes"));
    QCOMPARE(outline->getChild(2)->getChildCount(), size_t(1));
    QCOMPARE(outline->getChild(2)->getChild(0)->getTitle(), QString("Change 1: Added"));
    QCOMPARE(outline->getChild(4)->getTitle(), QString("New page 4 (added) - 1 changes"));

    const pdf::PDFActionGoTo* action = dynamic_cast<const pdf::PDFActionGoTo*>(outline->getChild(2)->getChild(0)->getAction());
    QVERIFY(action);
    QCOMPARE(action->getDestination().getDestinationType(), pdf::DestinationType::FitR);
    QCOMPARE(action->getDestination().getPageReference(), catalog->getPage(2)->getPageReference());

    // Page titles
    pdf::PDFVisualComparePageResult page;
    page.oldPageIndex = 1;
    page.newPageIndex = 2;
    QCOMPARE(pdf::PDFVisualCompareReport::getPageTitle(page), QString("Old page 2 / new page 3"));
    page.newPageIndex = -1;
    QCOMPARE(pdf::PDFVisualCompareReport::getPageTitle(page), QString("Old page 2 (removed)"));
}

void VisualCompareTest::reportSummaryPagination()
{
    pdf::PDFVisualCompareResult result;
    pdf::PDFVisualCompareReport report(createSettings(), pdf::PDFVisualCompareReport::Information());
    for (int i = 0; i < 120; ++i)
    {
        pdf::PDFVisualComparePageResult page;
        page.oldPageIndex = i;
        page.newPageIndex = i;
        page.pageSize = QSizeF(1728, 2592);
        if (i == 5)
        {
            page.errorMessage = "Broken page";
        }
        report.addPage(page);
        result.pages.push_back(page);
    }

    const pdf::PDFDocument document = report.build(result);
    const size_t pageCount = document.getCatalog()->getPageCount();
    QVERIFY(pageCount >= 123);
    const size_t summaryPages = pageCount - 120;
    QVERIFY(summaryPages >= 3 && summaryPages <= 5);
    QCOMPARE(document.getCatalog()->getPage(summaryPages)->getMediaBox().size(), QSizeF(1728, 2592));

    QSharedPointer<pdf::PDFOutlineItem> outline = document.getCatalog()->getOutlineRootPtr();
    QCOMPARE(outline->getChildCount(), size_t(121));
    QVERIFY(outline->getChild(6)->getTitle().contains("Broken page"));
}

void VisualCompareTest::pdfToolCompare()
{
    const QString pdfTool = QString::fromUtf8(PDF4QT_PDFTOOL_PATH);
    if (!QFileInfo::exists(pdfTool))
    {
        QSKIP("PdfTool is not built");
    }

    auto run = [&pdfTool](const QStringList& arguments, QString* output) -> int
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(pdfTool, arguments);
        if (!process.waitForFinished(120000))
        {
            process.kill();
            return -1;
        }
        *output = QString::fromUtf8(process.readAll());
        return process.exitCode();
    };

    QString output;
    const QString reportFileName = m_directory.filePath("cli-report.pdf");
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-resolution", "100", "--compare-report", reportFileName }, &output), 0);
    QVERIFY2(output.contains("4 page pairs compared, 2 with changes, 2 changes."), qPrintable(output));
    QVERIFY(output.contains("Page added"));
    QVERIFY(output.contains("Added"));
    QCOMPARE(readDocument(reportFileName).getCatalog()->getPageCount(), size_t(5));

    // Page ranges
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-old-pages", "1,3", "--compare-new-pages", "1,3" }, &output), 0);
    QVERIFY2(output.contains("2 page pairs compared, 0 with changes, 0 changes."), qPrintable(output));

    // XML output
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--console-format", "xml", "--compare-align", "origin" }, &output), 0);
    QVERIFY(output.contains("<"));
    QVERIFY(output.contains("change"));

    // Invalid arguments
    const int invalidArguments = 7;
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-align", "diagonal" }, &output), invalidArguments);
    QVERIFY(output.contains("compare-align"));
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-resolution", "5000" }, &output), invalidArguments);
    QCOMPARE(run({ "compare", m_oldFileName }, &output), invalidArguments);
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-old-pages", "7-9" }, &output), invalidArguments);
    QCOMPARE(run({ "compare", m_oldFileName, m_newFileName, "--compare-report", m_newFileName }, &output), invalidArguments);
    QVERIFY(QFileInfo(m_newFileName).size() > 0);
    QCOMPARE(run({ "compare", m_oldFileName, m_directory.filePath("missing.pdf") }, &output), 4);
}

void VisualCompareTest::dialogDefaults()
{
    QSettings().clear();
    pdfviewer::PDFVisualCompareDialog dialog(&m_newDocument, m_newFileName, nullptr);

    // Open document is the newer revision, the older revision is chosen by the user
    QComboBox* oldSource = child<QComboBox>(&dialog, "oldSourceComboBox");
    QComboBox* newSource = child<QComboBox>(&dialog, "newSourceComboBox");
    QLineEdit* oldFile = child<QLineEdit>(&dialog, "oldFileEdit");
    QLineEdit* newFile = child<QLineEdit>(&dialog, "newFileEdit");
    QLineEdit* reportFile = child<QLineEdit>(&dialog, "reportFileEdit");
    QPushButton* compareButton = child<QPushButton>(&dialog, "compareButton");
    QCOMPARE(oldSource->currentIndex(), 0);
    QCOMPARE(newSource->currentIndex(), 1);
    QCOMPARE(QDir::fromNativeSeparators(newFile->text()), m_newFileName);
    QVERIFY(!newFile->isEnabled());
    QVERIFY(oldFile->isEnabled());
    QVERIFY(!compareButton->isEnabled());
    QCOMPARE(reportFile->text(), pdfviewer::PDFVisualCompareDialog::getDefaultReportFileName(m_newFileName));
    QVERIFY(reportFile->text().endsWith("plan-rev2 - Compare.pdf"));
    QVERIFY(child<QCheckBox>(&dialog, "openReportCheckBox")->isChecked());

    oldFile->setText(m_oldFileName);
    QVERIFY(compareButton->isEnabled());
    QVERIFY(child<QPushButton>(&dialog, "contentDiffButton")->isEnabled());

    // Default settings
    const pdf::PDFVisualCompareSettings defaults;
    const pdf::PDFVisualCompareSettings settings = dialog.getSettings();
    QCOMPARE(settings.resolution, defaults.resolution);
    QCOMPARE(settings.alignment, defaults.alignment);
    QCOMPARE(settings.tolerancePixels, defaults.tolerancePixels);
    QCOMPARE(settings.colorTolerance, defaults.colorTolerance);
    QCOMPARE(settings.minimumChangeSizeMM, defaults.minimumChangeSizeMM);
    QCOMPARE(settings.mergeDistanceMM, defaults.mergeDistanceMM);

    // Swap: the open document becomes the older revision, report follows the newer one
    child<QPushButton>(&dialog, "swapButton")->click();
    QCOMPARE(oldSource->currentIndex(), 1);
    QCOMPARE(newSource->currentIndex(), 0);
    QCOMPARE(newFile->text(), m_oldFileName);
    QVERIFY(reportFile->text().endsWith("plan-rev1 - Compare.pdf"));

    // Report name edited by the user is kept
    QTest::keyClicks(reportFile, "x");
    newFile->setText(m_newFileName);
    QVERIFY(reportFile->text().endsWith("Compare.pdfx"));

    // Without an open document, both revisions are files
    pdfviewer::PDFVisualCompareDialog fileDialog(nullptr, QString(), nullptr);
    QCOMPARE(child<QComboBox>(&fileDialog, "newSourceComboBox")->currentIndex(), 0);
    QVERIFY(!child<QComboBox>(&fileDialog, "newSourceComboBox")->isEnabled());
    QVERIFY(child<QLineEdit>(&fileDialog, "reportFileEdit")->text().isEmpty());
    QVERIFY(!child<QPushButton>(&fileDialog, "compareButton")->isEnabled());
}

void VisualCompareTest::dialogComparison()
{
    QSettings().clear();
    pdfviewer::PDFVisualCompareDialog dialog(&m_newDocument, m_newFileName, nullptr);
    child<QLineEdit>(&dialog, "oldFileEdit")->setText(m_oldFileName);
    child<QSpinBox>(&dialog, "resolutionSpinBox")->setValue(100);
    child<QDoubleSpinBox>(&dialog, "mergeDistanceSpinBox")->setValue(6.0);
    const QString reportFileName = m_directory.filePath("dialog-report.pdf");
    child<QLineEdit>(&dialog, "reportFileEdit")->setText(reportFileName);
    child<QLineEdit>(&dialog, "newPagesEdit")->setText("1-3");

    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFVisualCompareDialog::comparisonFinished);
    QSignalSpy acceptedSpy(&dialog, &QDialog::accepted);
    dialog.show();
    child<QPushButton>(&dialog, "compareButton")->click();
    QVERIFY(dialog.isRunning());
    QVERIFY(!child<QPushButton>(&dialog, "compareButton")->isEnabled());
    QVERIFY(finishedSpy.wait(120000));

    QCOMPARE(acceptedSpy.count(), 1);
    QVERIFY(!dialog.isRunning());
    QVERIFY(dialog.getErrorMessage().isEmpty());
    QCOMPARE(dialog.getReportFileName(), reportFileName);
    QVERIFY(dialog.isOpenReportRequested());

    // Pages 1-3 of the old and of the new document: only page 2 changed
    const pdf::PDFVisualCompareResult& result = dialog.getResult();
    QCOMPARE(result.pages.size(), size_t(3));
    QCOMPARE(result.getChangeCount(), size_t(1));
    QCOMPARE(readDocument(reportFileName).getCatalog()->getPageCount(), size_t(4));

    // Settings are remembered
    pdfviewer::PDFVisualCompareDialog secondDialog(nullptr, QString(), nullptr);
    QCOMPARE(secondDialog.getSettings().resolution, 100.0);
    QCOMPARE(secondDialog.getSettings().mergeDistanceMM, 6.0);
}

void VisualCompareTest::dialogErrors()
{
    QSettings().clear();
    pdfviewer::PDFVisualCompareDialog dialog(&m_newDocument, m_newFileName, nullptr);
    QLineEdit* oldFile = child<QLineEdit>(&dialog, "oldFileEdit");
    QLineEdit* reportFile = child<QLineEdit>(&dialog, "reportFileEdit");
    QLabel* statusLabel = child<QLabel>(&dialog, "statusLabel");

    // Missing file
    oldFile->setText(m_directory.filePath("missing.pdf"));
    dialog.startComparison();
    QVERIFY(!dialog.isRunning());
    QVERIFY(dialog.getErrorMessage().contains("missing.pdf"));
    QCOMPARE(statusLabel->text(), dialog.getErrorMessage());

    // Invalid page range
    oldFile->setText(m_oldFileName);
    child<QLineEdit>(&dialog, "oldPagesEdit")->setText("5-9");
    dialog.startComparison();
    QVERIFY(!dialog.isRunning());
    QVERIFY(dialog.getErrorMessage().startsWith("Invalid page range"));
    child<QLineEdit>(&dialog, "oldPagesEdit")->clear();

    // Report would overwrite a compared document
    reportFile->setText(m_oldFileName);
    dialog.startComparison();
    QVERIFY(!dialog.isRunning());
    QVERIFY(dialog.getErrorMessage().contains("another file"));

    // Report can't be written
    reportFile->setText(m_directory.filePath("missing-directory/report.pdf"));
    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFVisualCompareDialog::comparisonFinished);
    dialog.startComparison();
    QVERIFY(dialog.isRunning());
    QVERIFY(finishedSpy.wait(120000));
    QVERIFY(dialog.getErrorMessage().startsWith("Cannot write the report"));
    QVERIFY(dialog.result() != QDialog::Accepted);
}

void VisualCompareTest::dialogCancel()
{
    QSettings().clear();
    pdfviewer::PDFVisualCompareDialog dialog(&m_newDocument, m_newFileName, nullptr);
    child<QLineEdit>(&dialog, "oldFileEdit")->setText(m_oldFileName);
    child<QSpinBox>(&dialog, "resolutionSpinBox")->setValue(600);
    child<QLineEdit>(&dialog, "reportFileEdit")->setText(m_directory.filePath("cancelled-report.pdf"));

    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFVisualCompareDialog::comparisonFinished);
    QSignalSpy rejectedSpy(&dialog, &QDialog::rejected);
    dialog.show();
    dialog.startComparison();
    QVERIFY(dialog.isRunning());

    // Closing the dialog cancels the comparison, the dialog is closed, when the worker stops
    dialog.reject();
    QVERIFY(finishedSpy.count() > 0 || finishedSpy.wait(120000));
    QTRY_COMPARE(rejectedSpy.count(), 1);
    QVERIFY(!dialog.isRunning());
}

void VisualCompareTest::ribbonAction()
{
    QSettings().clear();
    m_window = std::make_unique<pdfviewer::PDFEditorMainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    // Action is enabled also without a document and placed in the group Compare of the tab Comment
    QAction* action = windowAction("actionCompareDocuments");
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QVERIFY(!action->icon().isNull());

    pdfviewer::PDFRibbonPage* commentPage = m_window->getRibbon()->getPage("comment");
    QVERIFY(commentPage);
    pdfviewer::PDFRibbonGroup* compareGroup = commentPage->findGroup("compare");
    QVERIFY(compareGroup);
    bool isInGroup = false;
    for (pdfviewer::PDFRibbonButton* button : compareGroup->findChildren<pdfviewer::PDFRibbonButton*>())
    {
        isInGroup = isInGroup || button->getAction() == action;
    }
    QVERIFY(isInGroup);

    controller()->openDocument(m_newFileName);
    QTRY_VERIFY_WITH_TIMEOUT(controller()->getDocument() != nullptr, 10000);
    QVERIFY(action->isEnabled());

    // The comparison opens the report in the window
    const QString reportFileName = m_directory.filePath("ribbon-report.pdf");
    bool dialogHandled = false;
    handleNextModalWidget([&](QWidget* widget)
    {
        pdfviewer::PDFVisualCompareDialog* dialog = qobject_cast<pdfviewer::PDFVisualCompareDialog*>(widget);
        if (!dialog)
        {
            widget->close();
            return;
        }

        QCOMPARE(QDir::fromNativeSeparators(child<QLineEdit>(dialog, "newFileEdit")->text()), m_newFileName);
        child<QLineEdit>(dialog, "oldFileEdit")->setText(m_oldFileName);
        child<QSpinBox>(dialog, "resolutionSpinBox")->setValue(100);
        child<QLineEdit>(dialog, "reportFileEdit")->setText(reportFileName);
        dialogHandled = true;
        child<QPushButton>(dialog, "compareButton")->click();
    });
    action->trigger();
    QVERIFY(dialogHandled);

    QTRY_VERIFY_WITH_TIMEOUT(controller()->getDocument() && controller()->getDocument()->getCatalog()->getPageCount() == 5, 30000);
    QVERIFY(QFileInfo::exists(reportFileName));
    QVERIFY(controller()->getDocument()->getCatalog()->getOutlineRootPtr());
}

void VisualCompareTest::realDocument()
{
    const QString fileName = qEnvironmentVariable("PDF4QT_VISUAL_COMPARE_TEST_FILE");
    if (fileName.isEmpty())
    {
        QSKIP("PDF4QT_VISUAL_COMPARE_TEST_FILE is not set");
    }

    const pdf::PDFDocument original = readDocument(fileName);
    QVERIFY(original.getCatalog()->getPageCount() > 0);

    // Revision: the drawing is shifted by 3 mm to the right and 2 mm down (the boxes
    // are moved in the opposite direction), and a filled square is added
    const pdf::PDFReal dx = 3.0 / 25.4 * 72.0;
    const pdf::PDFReal dy = 2.0 / 25.4 * 72.0;
    pdf::PDFDocumentBuilder builder(&original);
    builder.flattenPageTree();
    const pdf::PDFObjectReference pageReference = builder.getPages().front();
    const pdf::PDFPage* page = original.getCatalog()->getPage(0);
    const QRectF mediaBox = page->getMediaBox();
    const QRectF cropBox = page->getCropBox();
    builder.setPageMediaBox(pageReference, mediaBox.translated(-dx, dy));
    builder.setPageCropBox(pageReference, cropBox.translated(-dx, dy));
    const QPointF squarePosition = cropBox.center();
    {
        pdf::PDFPageContentStreamBuilder contentBuilder(&builder, pdf::PDFContentStreamBuilder::CoordinateSystem::PDF, pdf::PDFPageContentStreamBuilder::Mode::PlaceAfter);
        QPainter* painter = contentBuilder.begin(pageReference);
        painter->fillRect(QRectF(squarePosition - mediaBox.topLeft(), QSizeF(20, 20)), QColor(0, 120, 255));
        contentBuilder.end(painter);
    }
    const pdf::PDFDocument revision = builder.build();

    pdf::PDFVisualCompareSettings settings;
    pdf::PDFVisualCompare compare(&original, &revision, settings);

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFVisualComparePageResult result = compare.comparePages(0, 0);
    qInfo() << "Compared in" << timer.elapsed() << "ms, image" << result.overlay.size() << "offset" << result.offset << "changes" << result.changes.size();

    for (const pdf::PDFVisualCompareChange& change : result.changes)
    {
        qInfo() << "Change" << pdf::PDFVisualCompareChange::getTypeName(change.type) << change.pixelRect;
    }

    const QString outputFileName = qEnvironmentVariable("PDF4QT_VISUAL_COMPARE_TEST_OUTPUT");
    if (!outputFileName.isEmpty())
    {
        pdf::PDFVisualCompareReport report(settings, { QFileInfo(fileName).fileName(), QStringLiteral("Shifted revision"), QDateTime::currentDateTime() });
        report.addPage(result);
        pdf::PDFVisualCompareResult documentResult;
        documentResult.pages.push_back(result);
        const pdf::PDFDocument reportDocument = report.build(documentResult);
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(outputFileName, &reportDocument, false));
        QVERIFY(writer.write(outputFileName + ".revision.pdf", &revision, false));
    }

    // Shift is found (the page can be rotated, so the directions can be swapped)
    QVERIFY(result.errorMessage.isEmpty());
    const pdf::PDFReal pixelsPerPoint = settings.resolution / 72.0;
    QVERIFY(qAbs(qAbs(result.offset.x()) + qAbs(result.offset.y()) - (dx + dy) * pixelsPerPoint) <= 2.0);
    QVERIFY(!result.changes.empty());
}

QTEST_MAIN(VisualCompareTest)

#include "tst_visualcomparetest.moc"
