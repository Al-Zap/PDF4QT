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


#include "pdfpagestampdialog.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfribbon.h"
#include "pdfpagestamper.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QTimer>
#include <QAction>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QPushButton>
#include <QMessageBox>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QDoubleSpinBox>

#include <functional>
#include <memory>

/// Tests of the dialog of the headers and footers, of the Bates numbering and of the
/// watermarks (PDFPageStampDialog), and of its actions in the ribbon of the editor
class PageStampDialogTest : public QObject
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

    void headerFooterSettings();
    void batesSettings();
    void watermarkSettings();
    void pageSelection();
    void replaceIsOfferedForExistingStamps();
    void previewShowsStamp();
    void settingsAreRemembered();
    void ribbonActions();
    void watermarkActionWithUndo();
    void removeStampsAction();

private:
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

    /// Calls the function with the next modal widget (dialog or message box), when it is shown
    static void handleNextModalWidget(std::function<void(QWidget*)> function);

    /// Returns true, if the image has a dark pixel in the rectangle
    static bool hasDarkPixel(const QImage& image, const QRect& rect);

    static pdf::PDFDocument createDocument(int pageCount);

    pdfviewer::PDFProgramController* controller() const { return m_window->getProgramController(); }
    QAction* windowAction(const char* name) const { return m_window->findChild<QAction*>(QString::fromLatin1(name)); }

    QTemporaryDir m_directory;
    pdf::PDFDocument m_document;
    QString m_documentFileName;
    std::unique_ptr<pdfviewer::PDFEditorMainWindow> m_window;
};

pdf::PDFDocument PageStampDialogTest::createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, 612, 792));
    }
    return builder.build();
}

void PageStampDialogTest::handleNextModalWidget(std::function<void(QWidget*)> function)
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

bool PageStampDialogTest::hasDarkPixel(const QImage& image, const QRect& rect)
{
    const QRect clipped = rect.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
    {
        for (int x = clipped.left(); x <= clipped.right(); ++x)
        {
            if (qGray(image.pixel(x, y)) < 128)
            {
                return true;
            }
        }
    }
    return false;
}

void PageStampDialogTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Page Stamp Dialog Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.filePath("settings"));
    QSettings().clear();

    m_document = createDocument(5);
    m_documentFileName = m_directory.filePath("pages.pdf");
    pdf::PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(m_documentFileName, &m_document, false));
}

void PageStampDialogTest::cleanupTestCase()
{
    if (m_window)
    {
        // Modified document: the question about saving is answered by No
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

void PageStampDialogTest::headerFooterSettings()
{
    QSettings().clear();
    pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::HeaderFooter, &m_document, 2, "plan.pdf", nullptr);

    child<QPlainTextEdit>(&dialog, "header-center")->setPlainText("CONFIDENTIAL");
    child<QPlainTextEdit>(&dialog, "footer-right")->setPlainText("Page {page} of {pages}");
    child<QSpinBox>(&dialog, "startPageNumberSpinBox")->setValue(3);
    child<QComboBox>(&dialog, "fontComboBox")->setCurrentIndex(child<QComboBox>(&dialog, "fontComboBox")->findData(int(pdf::PDFStampFont::TimesBold)));
    child<QDoubleSpinBox>(&dialog, "fontSizeSpinBox")->setValue(14.0);
    child<QSpinBox>(&dialog, "opacitySpinBox")->setValue(80);
    child<QDoubleSpinBox>(&dialog, "marginLeftSpinBox")->setValue(18.0);
    child<QDoubleSpinBox>(&dialog, "marginBottomSpinBox")->setValue(24.0);
    child<QLineEdit>(&dialog, "batesPrefixEdit")->setText("HF");

    // Variable is inserted into the last edited text
    QPlainTextEdit* headerLeft = child<QPlainTextEdit>(&dialog, "header-left");
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    headerLeft->setFocus();
    QTRY_VERIFY(headerLeft->hasFocus());
    QComboBox* variables = child<QComboBox>(&dialog, "variableComboBox");
    variables->setCurrentIndex(variables->findData(QStringLiteral("{file}")));
    QTest::mouseClick(child<QWidget>(&dialog, "insertVariableButton"), Qt::LeftButton);

    const pdf::PDFHeaderFooterSettings settings = dialog.getHeaderFooterSettings();
    QCOMPARE(settings.texts[pdf::PDFHeaderFooterSettings::HeaderLeft], QString("{file}"));
    QCOMPARE(settings.texts[pdf::PDFHeaderFooterSettings::HeaderCenter], QString("CONFIDENTIAL"));
    QCOMPARE(settings.texts[pdf::PDFHeaderFooterSettings::FooterRight], QString("Page {page} of {pages}"));
    QVERIFY(settings.texts[pdf::PDFHeaderFooterSettings::FooterLeft].isEmpty());
    QCOMPARE(settings.variables.startPageNumber, pdf::PDFInteger(3));
    QCOMPARE(settings.variables.fileName, QString("plan.pdf"));
    QCOMPARE(settings.variables.batesPrefix, QString("HF"));
    QCOMPARE(settings.style.font, pdf::PDFStampFont::TimesBold);
    QCOMPARE(settings.style.fontSize, 14.0);
    QCOMPARE(settings.style.opacity, 0.8);
    QCOMPARE(settings.margins, QMarginsF(18.0, 36.0, 36.0, 24.0));

    const pdf::PDFPageStamper::Result result = dialog.apply();
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QCOMPARE(result.pageCount, pdf::PDFInteger(5));
    QCOMPARE(pdf::PDFPageStamper::getStampedPageCount(result.document.data(), pdf::PDFPageStamper::HeaderFooter), pdf::PDFInteger(5));
}

void PageStampDialogTest::batesSettings()
{
    QSettings().clear();
    pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Bates, &m_document, 0, QString(), nullptr);

    child<QLineEdit>(&dialog, "batesPrefixEdit")->setText("SMITH-");
    child<QLineEdit>(&dialog, "batesSuffixEdit")->setText("-C");
    child<QSpinBox>(&dialog, "batesStartSpinBox")->setValue(120);
    child<QSpinBox>(&dialog, "batesDigitsSpinBox")->setValue(5);
    QComboBox* position = child<QComboBox>(&dialog, "batesPositionComboBox");
    QCOMPARE(position->currentData().toInt(), int(pdf::PDFHeaderFooterSettings::FooterRight));
    position->setCurrentIndex(position->findData(int(pdf::PDFHeaderFooterSettings::HeaderLeft)));

    QCOMPARE(child<QLabel>(&dialog, "batesRangeLabel")->text(), QString("SMITH-00120-C to SMITH-00124-C"));

    const pdf::PDFHeaderFooterSettings settings = dialog.getHeaderFooterSettings();
    QCOMPARE(settings.texts[pdf::PDFHeaderFooterSettings::HeaderLeft], QString("{bates}"));
    for (int i = 1; i < pdf::PDFHeaderFooterSettings::PositionCount; ++i)
    {
        QVERIFY(settings.texts[i].isEmpty());
    }

    const pdf::PDFPageStamper::Result result = dialog.apply();
    QVERIFY(result.isSuccess());
    QCOMPARE(result.firstBatesNumber, QString("SMITH-00120-C"));
    QCOMPARE(result.lastBatesNumber, QString("SMITH-00124-C"));
    QCOMPARE(result.nextBatesNumber, pdf::PDFInteger(125));

    // Range label follows the selected pages
    child<QRadioButton>(&dialog, "pageRangeRadioButton")->setChecked(true);
    child<QLineEdit>(&dialog, "pageRangeEdit")->setText("2-3");
    QCOMPARE(child<QLabel>(&dialog, "batesRangeLabel")->text(), QString("SMITH-00120-C to SMITH-00121-C"));
}

void PageStampDialogTest::watermarkSettings()
{
    QSettings().clear();
    pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, &m_document, 0, QString(), nullptr);

    // Defaults of a watermark: diagonal, semitransparent, fitted to the page
    pdf::PDFWatermarkSettings settings = dialog.getWatermarkSettings();
    QCOMPARE(settings.text, QString("DRAFT"));
    QCOMPARE(settings.rotation, 45.0);
    QCOMPARE(settings.style.fontSize, 0.0);
    QCOMPARE(settings.style.opacity, 0.3);
    QCOMPARE(settings.style.font, pdf::PDFStampFont::HelveticaBold);
    QVERIFY(settings.image.isNull());

    child<QPlainTextEdit>(&dialog, "watermarkTextEdit")->setPlainText("COPY\nDO NOT DISTRIBUTE");
    child<QDoubleSpinBox>(&dialog, "rotationSpinBox")->setValue(-30.0);
    child<QSpinBox>(&dialog, "relativeSizeSpinBox")->setValue(70);
    QComboBox* alignment = child<QComboBox>(&dialog, "alignmentComboBox");
    alignment->setCurrentIndex(alignment->findData(int(Qt::AlignBottom | Qt::AlignRight)));
    child<QCheckBox>(&dialog, "behindContentCheckBox")->setChecked(true);

    settings = dialog.getWatermarkSettings();
    QCOMPARE(settings.text, QString("COPY\nDO NOT DISTRIBUTE"));
    QCOMPARE(settings.rotation, -30.0);
    QCOMPARE(settings.relativeSize, 0.7);
    QCOMPARE(settings.alignment, Qt::Alignment(Qt::AlignBottom | Qt::AlignRight));
    QVERIFY(settings.behindContent);

    // Image watermark: the text is not used
    const QString imageFileName = m_directory.filePath("logo.png");
    QImage image(30, 10, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imageFileName));
    child<QRadioButton>(&dialog, "watermarkImageRadioButton")->setChecked(true);
    QLineEdit* imageEdit = child<QLineEdit>(&dialog, "watermarkImageEdit");
    imageEdit->setText(imageFileName);
    Q_EMIT imageEdit->editingFinished();
    QVERIFY(!child<QPlainTextEdit>(&dialog, "watermarkTextEdit")->isEnabled());
    QVERIFY(!child<QComboBox>(&dialog, "fontComboBox")->isEnabled());

    settings = dialog.getWatermarkSettings();
    QVERIFY(settings.text.isEmpty());
    QCOMPARE(settings.image.size(), QSize(30, 10));

    const pdf::PDFPageStamper::Result result = dialog.apply();
    QVERIFY(result.isSuccess());
    QCOMPARE(pdf::PDFPageStamper::getStampedPageCount(result.document.data(), pdf::PDFPageStamper::Watermark), pdf::PDFInteger(5));
}

void PageStampDialogTest::pageSelection()
{
    pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::HeaderFooter, &m_document, 3, QString(), nullptr);

    QCOMPARE(dialog.getPageIndices(), std::vector<pdf::PDFInteger>({ 0, 1, 2, 3, 4 }));

    child<QRadioButton>(&dialog, "currentPageRadioButton")->setChecked(true);
    QCOMPARE(dialog.getPageIndices(), std::vector<pdf::PDFInteger>({ 3 }));

    QLineEdit* rangeEdit = child<QLineEdit>(&dialog, "pageRangeEdit");
    QVERIFY(!rangeEdit->isEnabled());
    child<QRadioButton>(&dialog, "pageRangeRadioButton")->setChecked(true);
    QVERIFY(rangeEdit->isEnabled());
    rangeEdit->setText("1, 3-4");
    QCOMPARE(dialog.getPageIndices(), std::vector<pdf::PDFInteger>({ 0, 2, 3 }));
    rangeEdit->setText("4-");
    QCOMPARE(dialog.getPageIndices(), std::vector<pdf::PDFInteger>({ 3, 4 }));

    QString errorMessage;
    rangeEdit->setText("2-9");
    QVERIFY(dialog.getPageIndices(&errorMessage).empty());
    QVERIFY(!errorMessage.isEmpty());

    errorMessage.clear();
    rangeEdit->setText("abc");
    QVERIFY(dialog.getPageIndices(&errorMessage).empty());
    QVERIFY(!errorMessage.isEmpty());

    // Nothing is applied with an invalid selection
    child<QPlainTextEdit>(&dialog, "header-center")->setPlainText("X");
    const pdf::PDFPageStamper::Result result = dialog.apply();
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.document);
}

void PageStampDialogTest::replaceIsOfferedForExistingStamps()
{
    pdfviewer::PDFPageStampDialog plainDialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, &m_document, 0, QString(), nullptr);
    QVERIFY(!child<QCheckBox>(&plainDialog, "replaceCheckBox")->isEnabled());
    QVERIFY(!plainDialog.isReplaceExisting());

    pdf::PDFWatermarkSettings settings;
    settings.text = "OLD";
    const pdf::PDFPageStamper::Result watermarked = pdf::PDFPageStamper::addWatermark(&m_document, settings, { 1, 2 }, false);
    QVERIFY(watermarked.isSuccess());

    // Existing watermarks: replacing is offered (and preselected)
    pdfviewer::PDFPageStampDialog watermarkDialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, watermarked.document.data(), 0, QString(), nullptr);
    QCheckBox* replace = child<QCheckBox>(&watermarkDialog, "replaceCheckBox");
    QVERIFY(replace->isEnabled());
    QVERIFY(replace->text().contains("2"));
    QVERIFY(watermarkDialog.isReplaceExisting());

    child<QPlainTextEdit>(&watermarkDialog, "watermarkTextEdit")->setPlainText("NEW");
    pdf::PDFPageStamper::Result result = watermarkDialog.apply();
    QVERIFY(result.isSuccess());
    QCOMPARE(result.removedStampCount, pdf::PDFInteger(2));

    replace->setChecked(false);
    result = watermarkDialog.apply();
    QCOMPARE(result.removedStampCount, pdf::PDFInteger(0));

    // Watermarks are not headers and footers
    pdfviewer::PDFPageStampDialog headerDialog(pdfviewer::PDFPageStampDialog::Mode::Bates, watermarked.document.data(), 0, QString(), nullptr);
    QVERIFY(!headerDialog.isReplaceExisting());
}

void PageStampDialogTest::previewShowsStamp()
{
    QSettings().clear();
    pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::HeaderFooter, &m_document, 0, QString(), nullptr);
    dialog.updatePreview();

    // Nothing is written: blank preview of the page proportions
    QImage preview = dialog.getPreviewImage();
    QVERIFY(!preview.isNull());
    QVERIFY(qAbs(qreal(preview.width()) / preview.height() - 612.0 / 792.0) < 0.02);
    const QRect bottomStrip(4, preview.height() * 9 / 10, preview.width() - 8, preview.height() / 10 - 4);
    QVERIFY(!hasDarkPixel(preview.convertToFormat(QImage::Format_RGB32), bottomStrip));

    // Footer is drawn at the bottom of the preview
    child<QPlainTextEdit>(&dialog, "footer-center")->setPlainText("WWWWWWWW");
    child<QDoubleSpinBox>(&dialog, "fontSizeSpinBox")->setValue(40.0);
    dialog.updatePreview();
    preview = dialog.getPreviewImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(hasDarkPixel(preview, bottomStrip));
    QVERIFY(!hasDarkPixel(preview, QRect(4, 4, preview.width() - 8, preview.height() / 10)));

    // Preview follows the timer after a change
    pdfviewer::PDFPageStampDialog watermarkDialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, &m_document, 0, QString(), nullptr);
    child<QSpinBox>(&watermarkDialog, "opacitySpinBox")->setValue(100);
    child<QToolButton>(&watermarkDialog, "colorButton");
    QTRY_VERIFY(!watermarkDialog.getPreviewImage().isNull());
    const QImage watermarkPreview = watermarkDialog.getPreviewImage().convertToFormat(QImage::Format_RGB32);
    const QColor center = watermarkPreview.pixelColor(watermarkPreview.width() / 2, watermarkPreview.height() / 2);
    QVERIFY(center.red() > 150);
}

void PageStampDialogTest::settingsAreRemembered()
{
    QSettings().clear();

    {
        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Bates, &m_document, 0, QString(), nullptr);
        child<QLineEdit>(&dialog, "batesPrefixEdit")->setText("ACME");
        child<QSpinBox>(&dialog, "batesDigitsSpinBox")->setValue(8);
        child<QSpinBox>(&dialog, "batesStartSpinBox")->setValue(500);
        QComboBox* position = child<QComboBox>(&dialog, "batesPositionComboBox");
        position->setCurrentIndex(position->findData(int(pdf::PDFHeaderFooterSettings::FooterCenter)));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    {
        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Bates, &m_document, 0, QString(), nullptr);
        const pdf::PDFHeaderFooterSettings settings = dialog.getHeaderFooterSettings();
        QCOMPARE(settings.variables.batesPrefix, QString("ACME"));
        QCOMPARE(settings.variables.batesDigits, 8);
        QCOMPARE(settings.texts[pdf::PDFHeaderFooterSettings::FooterCenter], QString("{bates}"));

        // The start number is not remembered (the next document continues elsewhere)
        QCOMPARE(settings.variables.batesStart, pdf::PDFInteger(1));
    }

    {
        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, &m_document, 0, QString(), nullptr);
        child<QPlainTextEdit>(&dialog, "watermarkTextEdit")->setPlainText("SAMPLE");
        child<QDoubleSpinBox>(&dialog, "rotationSpinBox")->setValue(90.0);
        dialog.accept();
    }

    {
        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, &m_document, 0, QString(), nullptr);
        const pdf::PDFWatermarkSettings settings = dialog.getWatermarkSettings();
        QCOMPARE(settings.text, QString("SAMPLE"));
        QCOMPARE(settings.rotation, 90.0);
    }
}

void PageStampDialogTest::ribbonActions()
{
    m_window = std::make_unique<pdfviewer::PDFEditorMainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    QVERIFY(m_window->getRibbon());

    const char* actionNames[] = { "actionHeaderFooter", "actionBatesNumbering", "actionWatermark", "actionRemoveStamps" };

    // Without a document, the actions are disabled
    for (const char* name : actionNames)
    {
        QAction* action = windowAction(name);
        QVERIFY2(action, name);
        QVERIFY2(!action->isEnabled(), name);
        QVERIFY2(!action->icon().isNull(), name);
    }

    controller()->openDocument(m_documentFileName);
    QTRY_VERIFY_WITH_TIMEOUT(controller()->getDocument() != nullptr, 10000);

    // Actions are enabled and placed in the group "Page Design" of the tab "Pages"
    pdfviewer::PDFRibbonPage* pagesPage = m_window->getRibbon()->getPage("pages");
    QVERIFY(pagesPage);
    pdfviewer::PDFRibbonGroup* designGroup = pagesPage->findGroup("design");
    QVERIFY(designGroup);

    for (const char* name : actionNames)
    {
        QAction* action = windowAction(name);
        QVERIFY2(action->isEnabled(), name);

        bool isInGroup = false;
        for (pdfviewer::PDFRibbonButton* button : designGroup->findChildren<pdfviewer::PDFRibbonButton*>())
        {
            isInGroup = isInGroup || button->getAction() == action;
        }
        QVERIFY2(isInGroup, name);
    }
}

void PageStampDialogTest::watermarkActionWithUndo()
{
    QVERIFY(m_window);
    QSettings().clear();
    QCOMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::AllKinds), pdf::PDFInteger(0));

    bool dialogHandled = false;
    handleNextModalWidget([&dialogHandled](QWidget* widget)
    {
        pdfviewer::PDFPageStampDialog* dialog = qobject_cast<pdfviewer::PDFPageStampDialog*>(widget);
        if (dialog)
        {
            dialog->findChild<QPlainTextEdit*>("watermarkTextEdit")->setPlainText("CONFIDENTIAL");
            dialog->findChild<QRadioButton*>("pageRangeRadioButton")->setChecked(true);
            dialog->findChild<QLineEdit*>("pageRangeEdit")->setText("2-4");
            dialog->accept();
            dialogHandled = true;
        }
        else
        {
            widget->close();
        }
    });
    windowAction("actionWatermark")->trigger();
    QVERIFY(dialogHandled);

    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::Watermark), pdf::PDFInteger(3));
    QCOMPARE(pdf::PDFPageStamper::getStampKinds(controller()->getDocument(), 0), pdf::PDFPageStamper::StampKinds(pdf::PDFPageStamper::None));
    QCOMPARE(pdf::PDFPageStamper::getStampKinds(controller()->getDocument(), 1), pdf::PDFPageStamper::StampKinds(pdf::PDFPageStamper::Watermark));

    // Undo restores the document without the watermark, redo adds it again
    QAction* undo = windowAction("actionUndo");
    QAction* redo = windowAction("actionRedo");
    QVERIFY(undo && undo->isEnabled());
    undo->trigger();
    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::AllKinds), pdf::PDFInteger(0));
    QVERIFY(redo && redo->isEnabled());
    redo->trigger();
    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::Watermark), pdf::PDFInteger(3));

    // Cancelled dialog does not change the document
    pdf::PDFDocument* documentBefore = controller()->getDocument();
    handleNextModalWidget([](QWidget* widget) { widget->close(); });
    windowAction("actionHeaderFooter")->trigger();
    QCOMPARE(controller()->getDocument(), documentBefore);
}

void PageStampDialogTest::removeStampsAction()
{
    QVERIFY(m_window);

    // Header and footer on all pages (watermark of the previous test stays on 3 pages)
    handleNextModalWidget([](QWidget* widget)
    {
        if (pdfviewer::PDFPageStampDialog* dialog = qobject_cast<pdfviewer::PDFPageStampDialog*>(widget))
        {
            dialog->findChild<QPlainTextEdit*>("footer-center")->setPlainText("{page}");
            dialog->accept();
        }
    });
    windowAction("actionHeaderFooter")->trigger();
    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::HeaderFooter), pdf::PDFInteger(5));

    // Only the watermarks are removed
    QString question;
    handleNextModalWidget([&question](QWidget* widget)
    {
        QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget);
        QVERIFY(messageBox);
        question = messageBox->text();
        for (QAbstractButton* button : messageBox->buttons())
        {
            if (button->text() == pdfviewer::PDFProgramController::tr("Watermarks"))
            {
                button->click();
                return;
            }
        }
        messageBox->reject();
    });
    windowAction("actionRemoveStamps")->trigger();
    QVERIFY(question.contains("5"));
    QVERIFY(question.contains("3"));
    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::Watermark), pdf::PDFInteger(0));
    QCOMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::HeaderFooter), pdf::PDFInteger(5));

    // All the remaining stamps
    handleNextModalWidget([](QWidget* widget)
    {
        QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget);
        QVERIFY(messageBox);
        for (QAbstractButton* button : messageBox->buttons())
        {
            if (button->text() == pdfviewer::PDFProgramController::tr("All"))
            {
                button->click();
                return;
            }
        }
        messageBox->reject();
    });
    windowAction("actionRemoveStamps")->trigger();
    QTRY_COMPARE(pdf::PDFPageStamper::getStampedPageCount(controller()->getDocument(), pdf::PDFPageStamper::AllKinds), pdf::PDFInteger(0));

    // Nothing to remove: the user is informed
    bool informed = false;
    handleNextModalWidget([&informed](QWidget* widget)
    {
        informed = qobject_cast<QMessageBox*>(widget) != nullptr;
        widget->close();
    });
    windowAction("actionRemoveStamps")->trigger();
    QVERIFY(informed);
}

QTEST_MAIN(PageStampDialogTest)

#include "tst_pagestampdialogtest.moc"
