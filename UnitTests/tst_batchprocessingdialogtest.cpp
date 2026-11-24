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



#include "pdfbatchprocessingdialog.h"
#include "pdfpagestampdialog.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfribbon.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"

#include <QtTest>
#include <QMenu>
#include <QTimer>
#include <QAction>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QRadioButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QDoubleSpinBox>

#include <functional>
#include <memory>

/// Dialog of the batch processing (PDFBatchProcessingDialog), editor of the steps
/// (PDFBatchActionDialog), batch mode of PDFPageStampDialog and the ribbon action
class BatchProcessingDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void filesAndFolders();
    void sequenceEditing();
    void sequenceFiles();
    void rememberedSettings();
    void actionDialogs();
    void actionDialogs_data();
    void actionDialogWidgets();
    void stampDialogBatchMode();
    void editStepsByDialogs();
    void runBatch();
    void startErrors();
    void stopBatch();
    void openResult();
    void ribbonAction();

private:
    template<typename T>
    static T* child(QWidget* widget, const char* name)
    {
        T* result = widget->findChild<T*>(QString::fromLatin1(name));
        if (!result)
        {
            qWarning("Child widget '%s' not found", name);
        }
        return result;
    }

    static pdf::PDFDocument createDocument(int pageCount);
    static void handleNextModalWidget(std::function<void(QWidget*)> function);
    QString writeDocument(const QString& name, int pageCount);

    QTemporaryDir m_directory;
    QDir m_input;
    QDir m_output;
};

pdf::PDFDocument BatchProcessingDialogTest::createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, 612, 792));
    }
    return builder.build();
}

QString BatchProcessingDialogTest::writeDocument(const QString& name, int pageCount)
{
    const QString fileName = m_input.filePath(name);
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    const pdf::PDFDocument document = createDocument(pageCount);
    pdf::PDFDocumentWriter writer(nullptr);
    if (!writer.write(fileName, &document, false))
    {
        return QString();
    }
    return QDir::cleanPath(fileName);
}

void BatchProcessingDialogTest::handleNextModalWidget(std::function<void(QWidget*)> function)
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

void BatchProcessingDialogTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    // Settings of the application are not touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("PDF4QT-UnitTests");
    QCoreApplication::setApplicationName("PDF4QT Batch Processing Dialog Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.filePath("settings"));
    QSettings().clear();
}

void BatchProcessingDialogTest::cleanupTestCase()
{
    QSettings().clear();
}

void BatchProcessingDialogTest::init()
{
    static int counter = 0;
    ++counter;
    QDir root(m_directory.path());
    const QString inputName = QStringLiteral("input%1").arg(counter);
    const QString outputName = QStringLiteral("output%1").arg(counter);
    QVERIFY(root.mkpath(inputName));
    m_input = QDir(root.filePath(inputName));
    m_output = QDir(root.filePath(outputName));
    QSettings().clear();
}

void BatchProcessingDialogTest::filesAndFolders()
{
    const QString a = writeDocument("a.pdf", 1);
    const QString b = writeDocument("folder/b.pdf", 1);
    const QString c = writeDocument("folder/sub/c.pdf", 1);
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    QTableWidget* table = child<QTableWidget>(&dialog, "filesTable");
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(!child<QPushButton>(&dialog, "startButton")->isEnabled());

    dialog.addFiles({ a });
    QCOMPARE(dialog.getFiles(), QStringList({ a }));
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("a.pdf"));

    // Folder without the subfolders, files already listed are ignored
    dialog.addFiles({ m_input.filePath("folder"), a });
    QCOMPARE(dialog.getFiles(), QStringList({ a, b }));

    // With the subfolders
    child<QCheckBox>(&dialog, "includeSubfoldersCheckBox")->setChecked(true);
    dialog.addFiles({ m_input.filePath("folder") });
    QCOMPARE(dialog.getFiles(), QStringList({ a, b, c }));
    QVERIFY(child<QPushButton>(&dialog, "clearFilesButton")->isEnabled());

    // Removal of the selected rows
    QVERIFY(!child<QPushButton>(&dialog, "removeFilesButton")->isEnabled());
    table->selectRow(1);
    QVERIFY(child<QPushButton>(&dialog, "removeFilesButton")->isEnabled());
    child<QPushButton>(&dialog, "removeFilesButton")->click();
    QCOMPARE(dialog.getFiles(), QStringList({ a, c }));

    child<QPushButton>(&dialog, "clearFilesButton")->click();
    QVERIFY(dialog.getFiles().isEmpty());

    // Drop of files and folders
    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(a), QUrl::fromLocalFile(m_input.filePath("folder")) });
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    QDragEnterEvent enterEvent(QPointF(10, 10), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
#else
    QDragEnterEvent enterEvent(QPoint(10, 10), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
#endif
    QApplication::sendEvent(&dialog, &enterEvent);
    QVERIFY(enterEvent.isAccepted());
    QDropEvent dropEvent(QPointF(10, 10), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &dropEvent);
    QCOMPARE(dialog.getFiles(), QStringList({ a, b, c }));
}

void BatchProcessingDialogTest::sequenceEditing()
{
    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    QListWidget* list = child<QListWidget>(&dialog, "actionsList");
    QVERIFY(list);
    QCOMPARE(list->count(), 0);
    QVERIFY(!child<QPushButton>(&dialog, "saveSequenceButton")->isEnabled());

    dialog.addAction(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::Watermark));
    dialog.addAction(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::FlattenForms));
    dialog.addAction(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportWord));
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->currentRow(), 2);
    QVERIFY(list->item(0)->text().startsWith("1. " + pdf::PDFBatchAction::getTypeName(pdf::PDFBatchAction::Type::Watermark)));
    QVERIFY(list->item(0)->text().contains("CONFIDENTIAL"));
    QVERIFY(child<QPushButton>(&dialog, "saveSequenceButton")->isEnabled());

    // Buttons follow the current row
    QVERIFY(child<QPushButton>(&dialog, "moveUpButton")->isEnabled());
    QVERIFY(!child<QPushButton>(&dialog, "moveDownButton")->isEnabled());
    list->setCurrentRow(1);
    QVERIFY(!child<QPushButton>(&dialog, "editActionButton")->isEnabled()); // flattening has no settings

    // Move the flattening up
    child<QPushButton>(&dialog, "moveUpButton")->click();
    QCOMPARE(dialog.getActions()[0].type, pdf::PDFBatchAction::Type::FlattenForms);
    QCOMPARE(dialog.getActions()[1].type, pdf::PDFBatchAction::Type::Watermark);
    QCOMPARE(list->currentRow(), 0);
    QVERIFY(!child<QPushButton>(&dialog, "moveUpButton")->isEnabled());

    // Move it back down
    child<QPushButton>(&dialog, "moveDownButton")->click();
    QCOMPARE(dialog.getActions()[1].type, pdf::PDFBatchAction::Type::FlattenForms);
    QCOMPARE(list->currentRow(), 1);

    // Remove it
    child<QPushButton>(&dialog, "removeActionButton")->click();
    QCOMPARE(dialog.getActions().size(), size_t(2));
    QCOMPARE(dialog.getActions()[1].type, pdf::PDFBatchAction::Type::ExportWord);

    // An invalid step is shown in red with its errors
    pdf::PDFBatchAction invalid = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::SetPassword);
    dialog.addAction(invalid);
    QCOMPARE(list->item(2)->foreground().color(), QColor(Qt::red));
    QVERIFY(list->item(2)->toolTip().contains("password"));

    pdf::PDFBatchAction replaced = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RotatePages);
    dialog.setAction(2, replaced);
    QCOMPARE(dialog.getActions()[2].type, pdf::PDFBatchAction::Type::RotatePages);
    dialog.setAction(7, replaced);
    QCOMPARE(dialog.getActions().size(), size_t(3));

    // The menu offers all types of the steps
    QMenu* menu = child<QToolButton>(&dialog, "addActionButton")->menu();
    QVERIFY(menu);
    int actionCount = 0;
    for (QAction* action : menu->actions())
    {
        actionCount += action->isSeparator() ? 0 : 1;
    }
    QCOMPARE(actionCount, int(pdf::PDFBatchAction::Type::TypeCount));

    // Adding by the menu: the flattening has no settings, so it is added at once
    menu->findChild<QAction*>("addAction_flatten-forms")->trigger();
    QCOMPARE(dialog.getActions().size(), size_t(4));
    QCOMPARE(dialog.getActions().back().type, pdf::PDFBatchAction::Type::FlattenForms);
}

void BatchProcessingDialogTest::sequenceFiles()
{
    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);

    pdf::PDFBatchSettings settings;
    pdf::PDFBatchAction bates = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::BatesNumbering);
    bates.headerFooter.variables.batesPrefix = QStringLiteral("ISL-");
    pdf::PDFBatchAction password = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::SetPassword);
    password.userPassword = QStringLiteral("hidden");
    settings.actions = { bates, password };
    settings.outputLocation = pdf::PDFBatchSettings::OutputLocation::NextToOriginal;
    settings.suffix = QStringLiteral("_bates");
    settings.existingFilePolicy = pdf::PDFBatchSettings::ExistingFilePolicy::Overwrite;
    settings.password = QStringLiteral("filepassword");
    dialog.setSettings(settings);

    QVERIFY(child<QRadioButton>(&dialog, "nextToOriginalRadioButton")->isChecked());
    QVERIFY(!child<QLineEdit>(&dialog, "outputDirectoryEdit")->isEnabled());
    QCOMPARE(child<QLineEdit>(&dialog, "suffixEdit")->text(), QStringLiteral("_bates"));

    const QString fileName = m_input.filePath("sequence.json");
    QVERIFY(dialog.saveSequence(fileName).isEmpty());
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.contains("ISL-"));
    QVERIFY(!data.contains("hidden"));
    QVERIFY(!data.contains("filepassword"));
    file.close();

    pdfviewer::PDFBatchProcessingDialog other(nullptr);
    child<QLineEdit>(&other, "passwordEdit")->setText("kept");
    QVERIFY(other.loadSequence(fileName).isEmpty());
    const pdf::PDFBatchSettings loaded = other.getSettings();
    QCOMPARE(loaded.actions.size(), size_t(2));
    QCOMPARE(loaded.actions[0].headerFooter.variables.batesPrefix, QStringLiteral("ISL-"));
    QVERIFY(loaded.actions[1].userPassword.isEmpty());
    QCOMPARE(loaded.outputLocation, pdf::PDFBatchSettings::OutputLocation::NextToOriginal);
    QCOMPARE(loaded.existingFilePolicy, pdf::PDFBatchSettings::ExistingFilePolicy::Overwrite);
    QCOMPARE(loaded.password, QStringLiteral("kept"));

    // Errors
    QVERIFY(!other.loadSequence(m_input.filePath("missing.json")).isEmpty());
    QFile broken(m_input.filePath("broken.json"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{ not json");
    broken.close();
    QVERIFY(other.loadSequence(broken.fileName()).contains("not a sequence"));
    QVERIFY(!other.saveSequence(m_input.filePath("no/such/folder/x.json")).isEmpty());
}

void BatchProcessingDialogTest::rememberedSettings()
{
    {
        pdfviewer::PDFBatchProcessingDialog dialog(nullptr);

        // Default output folder
        QVERIFY(dialog.getSettings().outputDirectory.endsWith("PDF4QT Batch"));
        QVERIFY(child<QRadioButton>(&dialog, "outputFolderRadioButton")->isChecked());

        dialog.addAction(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportText));
        child<QLineEdit>(&dialog, "outputDirectoryEdit")->setText(m_output.path());
        child<QLineEdit>(&dialog, "passwordEdit")->setText("not-remembered");
        child<QCheckBox>(&dialog, "includeSubfoldersCheckBox")->setChecked(true);
        dialog.reject();
    }

    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    QCOMPARE(dialog.getActions().size(), size_t(1));
    QCOMPARE(dialog.getActions()[0].type, pdf::PDFBatchAction::Type::ExportText);
    QCOMPARE(dialog.getSettings().outputDirectory, m_output.path());
    QVERIFY(dialog.getSettings().password.isEmpty());
    QVERIFY(child<QCheckBox>(&dialog, "includeSubfoldersCheckBox")->isChecked());
}

void BatchProcessingDialogTest::actionDialogs_data()
{
    QTest::addColumn<int>("type");
    for (int i = 0; i < int(pdf::PDFBatchAction::Type::TypeCount); ++i)
    {
        const pdf::PDFBatchAction::Type type = static_cast<pdf::PDFBatchAction::Type>(i);
        switch (type)
        {
            case pdf::PDFBatchAction::Type::HeaderFooter:
            case pdf::PDFBatchAction::Type::BatesNumbering:
            case pdf::PDFBatchAction::Type::Watermark:
                break; // edited by the stamp dialog
            default:
                QTest::newRow(qPrintable(pdf::PDFBatchAction::getTypeId(type))) << i;
                break;
        }
    }
}

void BatchProcessingDialogTest::actionDialogs()
{
    QFETCH(int, type);

    // The dialog keeps the settings it does not change
    pdf::PDFBatchAction action = pdf::PDFBatchAction::create(static_cast<pdf::PDFBatchAction::Type>(type));
    action.pages = pdf::PDFBatchAction::hasPages(action.type) ? QStringLiteral("1-2") : QString();
    action.userPassword = QStringLiteral("abc");
    action.rotation = 180;
    action.imageTargetDpi = 150;
    action.optimizeImages = true;
    action.wordGraphicsResolution = 200;
    action.imageResolution = 300;

    pdfviewer::PDFBatchActionDialog dialog(action, nullptr);
    QCOMPARE(dialog.windowTitle(), action.getName());
    const pdf::PDFBatchAction result = dialog.getAction();
    QCOMPARE(result.toJson(), action.toJson());
    QCOMPARE(result.userPassword, action.userPassword);
    QCOMPARE(pdfviewer::PDFBatchActionDialog::hasSettings(action.type), action.type != pdf::PDFBatchAction::Type::FlattenForms);
    QCOMPARE(dialog.findChild<QLineEdit*>("pagesEdit") != nullptr, pdf::PDFBatchAction::hasPages(action.type));
}

void BatchProcessingDialogTest::actionDialogWidgets()
{
    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RotatePages), nullptr);
        QComboBox* rotation = child<QComboBox>(&dialog, "rotationComboBox");
        rotation->setCurrentIndex(rotation->findData(270));
        child<QLineEdit>(&dialog, "pagesEdit")->setText(" 2-4 ");
        QCOMPARE(dialog.getAction().rotation, 270);
        QCOMPARE(dialog.getAction().pages, QStringLiteral("2-4"));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RecognizeText), nullptr);
        child<QLineEdit>(&dialog, "languagesEdit")->setText("deu + eng, deu");
        QComboBox* profile = child<QComboBox>(&dialog, "profileComboBox");
        profile->setCurrentIndex(profile->findData(int(pdf::PDFOCRModelProfile::Standard)));
        QComboBox* existing = child<QComboBox>(&dialog, "existingTextComboBox");
        existing->setCurrentIndex(existing->findData(int(pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer)));
        QComboBox* decision = child<QComboBox>(&dialog, "decisionComboBox");
        decision->setCurrentIndex(decision->findData(int(pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText)));
        child<QCheckBox>(&dialog, "allowPageErrorsCheckBox")->setChecked(true);
        const pdf::PDFBatchAction action = dialog.getAction();
        QCOMPARE(action.ocrConfiguration.languages, QStringList({ "deu", "eng" }));
        QCOMPARE(action.ocrConfiguration.profile, pdf::PDFOCRModelProfile::Standard);
        QCOMPARE(action.ocrConfiguration.existingTextPolicy, pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer);
        QCOMPARE(action.ocrDecisionPolicy, pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText);
        QVERIFY(action.ocrAllowPageErrors);
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::Optimize), nullptr);
        QVERIFY(!child<QComboBox>(&dialog, "imageGoalComboBox")->isEnabled());
        child<QCheckBox>(&dialog, "optimizeImagesCheckBox")->setChecked(true);
        QVERIFY(child<QComboBox>(&dialog, "imageGoalComboBox")->isEnabled());
        QComboBox* goal = child<QComboBox>(&dialog, "imageGoalComboBox");
        goal->setCurrentIndex(goal->findData(int(pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize)));
        child<QSpinBox>(&dialog, "imageDpiSpinBox")->setValue(150);
        child<QCheckBox>(&dialog, "optimizeObjectsCheckBox")->setChecked(false);
        const pdf::PDFBatchAction action = dialog.getAction();
        QVERIFY(action.optimizeImages);
        QCOMPARE(action.imageGoal, pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize);
        QCOMPARE(action.imageTargetDpi, 150);
        QCOMPARE(action.optimizationFlags, pdf::PDFOptimizer::OptimizationFlags(pdf::PDFOptimizer::None));
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::Sanitize), nullptr);
        child<QCheckBox>(&dialog, QStringLiteral("sanitizeCheckBox%1").arg(int(pdf::PDFDocumentSanitizer::Metadata)).toLatin1().constData())->setChecked(false);
        child<QCheckBox>(&dialog, QStringLiteral("sanitizeCheckBox%1").arg(int(pdf::PDFDocumentSanitizer::MarkupAnnotations)).toLatin1().constData())->setChecked(true);
        QCOMPARE(dialog.getAction().sanitizationFlags, pdf::PDFDocumentSanitizer::SanitizationFlags(pdf::PDFDocumentSanitizer::DocumentInfo | pdf::PDFDocumentSanitizer::MarkupAnnotations));
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::SetPassword), nullptr);
        QCOMPARE(child<QLineEdit>(&dialog, "userPasswordEdit")->echoMode(), QLineEdit::Password);
        child<QLineEdit>(&dialog, "userPasswordEdit")->setText("open");
        child<QLineEdit>(&dialog, "ownerPasswordEdit")->setText("owner");
        QCOMPARE(dialog.getAction().userPassword, QStringLiteral("open"));
        QCOMPARE(dialog.getAction().ownerPassword, QStringLiteral("owner"));
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportWord), nullptr);
        child<QCheckBox>(&dialog, "wordGraphicsCheckBox")->setChecked(false);
        QVERIFY(!child<QSpinBox>(&dialog, "wordResolutionSpinBox")->isEnabled());
        QVERIFY(!dialog.getAction().wordIncludeGraphics);
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportImages), nullptr);
        QComboBox* format = child<QComboBox>(&dialog, "imageFormatComboBox");
        QVERIFY(format->findData("png") >= 0);
        format->setCurrentIndex(format->findData("jpg") >= 0 ? format->findData("jpg") : format->findData("png"));
        child<QSpinBox>(&dialog, "imageResolutionSpinBox")->setValue(72);
        QCOMPARE(dialog.getAction().imageFormat, format->currentData().toString());
        QCOMPARE(dialog.getAction().imageResolution, 72);
    }

    {
        pdfviewer::PDFBatchActionDialog dialog(pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RemoveStamps), nullptr);
        child<QCheckBox>(&dialog, "removeHeaderFooterCheckBox")->setChecked(false);
        QCOMPARE(dialog.getAction().removedStamps, pdf::PDFPageStamper::StampKinds(pdf::PDFPageStamper::Watermark));

        // An invalid step is not accepted (the warning is closed)
        child<QCheckBox>(&dialog, "removeWatermarkCheckBox")->setChecked(false);
        bool warningShown = false;
        handleNextModalWidget([&warningShown](QWidget* widget)
        {
            warningShown = qobject_cast<QMessageBox*>(widget) != nullptr;
            widget->close();
        });
        dialog.accept();
        QVERIFY(warningShown);
        QVERIFY(dialog.result() != int(QDialog::Accepted));
    }
}

void BatchProcessingDialogTest::stampDialogBatchMode()
{
    // Header and footer
    {
        pdf::PDFBatchAction action = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::HeaderFooter);
        action.headerFooter.texts[pdf::PDFHeaderFooterSettings::HeaderRight] = QStringLiteral("{file}\n{date}");
        action.headerFooter.style.font = pdf::PDFStampFont::TimesBold;
        action.headerFooter.style.fontSize = 12.0;
        action.headerFooter.style.color = QColor(0, 0, 255);
        action.headerFooter.style.opacity = 0.5;
        action.headerFooter.margins = QMarginsF(10, 20, 30, 40);
        action.headerFooter.variables.startPageNumber = 4;
        action.headerFooter.variables.dateFormat = QStringLiteral("yyyy-MM-dd");
        action.headerFooter.variables.batesPrefix = QStringLiteral("P");
        action.headerFooter.variables.batesStart = 77;
        action.headerFooter.variables.batesDigits = 3;

        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::HeaderFooter, nullptr, -1, QString(), nullptr);
        dialog.setBatchMode();
        QVERIFY(dialog.isBatchMode());
        dialog.setHeaderFooterSettings(action.headerFooter);
        dialog.setPageSelection("2-");
        dialog.setReplaceExisting(false);
        dialog.setContinueBatesNumbering(false);

        QVERIFY(child<QRadioButton>(&dialog, "currentPageRadioButton")->isHidden());
        QVERIFY(child<QCheckBox>(&dialog, "replaceCheckBox")->isEnabled());
        QVERIFY(!child<QCheckBox>(&dialog, "continueBatesCheckBox")->isHidden());

        const pdf::PDFHeaderFooterSettings settings = dialog.getHeaderFooterSettings();
        QCOMPARE(settings.texts, action.headerFooter.texts);
        QCOMPARE(settings.style, action.headerFooter.style);
        QCOMPARE(settings.margins, action.headerFooter.margins);
        QCOMPARE(settings.variables.startPageNumber, pdf::PDFInteger(4));
        QCOMPARE(settings.variables.dateFormat, QStringLiteral("yyyy-MM-dd"));
        QCOMPARE(settings.variables.batesStart, pdf::PDFInteger(77));
        QCOMPARE(settings.variables.batesDigits, 3);
        QVERIFY(settings.variables.fileName.isEmpty());
        QCOMPARE(dialog.getPageSelection(), QStringLiteral("2-"));
        QVERIFY(!dialog.isReplaceExisting());
        QVERIFY(!dialog.isContinueBatesNumbering());

        // Accepted without a document, the settings of the interactive dialog are not changed
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!QSettings().childGroups().contains("PageStamp_HeaderFooter"));

        dialog.setPageSelection(QString());
        QVERIFY(dialog.getPageSelection().isEmpty());
        QVERIFY(child<QRadioButton>(&dialog, "allPagesRadioButton")->isChecked());
    }

    // Bates numbering
    {
        pdf::PDFBatchAction action = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::BatesNumbering);
        action.headerFooter.texts = { };
        action.headerFooter.texts[pdf::PDFHeaderFooterSettings::HeaderCenter] = QStringLiteral("{bates}");
        action.headerFooter.variables.batesPrefix = QStringLiteral("ISL");
        action.headerFooter.variables.batesStart = 5;

        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Bates, nullptr, -1, QString(), nullptr);
        dialog.setBatchMode();
        dialog.setHeaderFooterSettings(action.headerFooter);
        QCOMPARE(dialog.getHeaderFooterSettings().texts, action.headerFooter.texts);
        QVERIFY(child<QLabel>(&dialog, "batesRangeLabel")->text().contains("ISL000005"));
        QVERIFY(dialog.isContinueBatesNumbering());
        QVERIFY(dialog.isReplaceExisting());
    }

    // Watermark (text and image)
    {
        pdf::PDFBatchAction action = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::Watermark);
        action.watermark.text = QStringLiteral("FOR REVIEW");
        action.watermark.rotation = 15.0;
        action.watermark.relativeSize = 0.4;
        action.watermark.alignment = Qt::AlignBottom | Qt::AlignRight;
        action.watermark.behindContent = true;

        pdfviewer::PDFPageStampDialog dialog(pdfviewer::PDFPageStampDialog::Mode::Watermark, nullptr, -1, QString(), nullptr);
        dialog.setBatchMode();
        dialog.setWatermarkSettings(action.watermark);
        pdf::PDFWatermarkSettings settings = dialog.getWatermarkSettings();
        QCOMPARE(settings.text, action.watermark.text);
        QCOMPARE(settings.rotation, 15.0);
        QCOMPARE(settings.relativeSize, 0.4);
        QCOMPARE(settings.alignment, action.watermark.alignment);
        QVERIFY(settings.behindContent);
        QCOMPARE(settings.style.opacity, action.watermark.style.opacity);

        action.watermark.text.clear();
        action.watermark.image = QImage(8, 8, QImage::Format_ARGB32);
        action.watermark.image.fill(Qt::blue);
        dialog.setWatermarkSettings(action.watermark);
        settings = dialog.getWatermarkSettings();
        QVERIFY(settings.text.isEmpty());
        QCOMPARE(settings.image.size(), QSize(8, 8));
        QVERIFY(child<QRadioButton>(&dialog, "watermarkImageRadioButton")->isChecked());
    }
}

void BatchProcessingDialogTest::editStepsByDialogs()
{
    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);

    // A step is added by the stamp dialog in the batch mode
    bool stampDialogShown = false;
    handleNextModalWidget([&stampDialogShown](QWidget* widget)
    {
        pdfviewer::PDFPageStampDialog* stampDialog = qobject_cast<pdfviewer::PDFPageStampDialog*>(widget);
        if (stampDialog)
        {
            stampDialogShown = stampDialog->isBatchMode();
            stampDialog->findChild<QPlainTextEdit*>("watermarkTextEdit")->setPlainText("BATCH COPY");
            stampDialog->accept();
        }
        else
        {
            widget->close();
        }
    });
    child<QToolButton>(&dialog, "addActionButton")->menu()->findChild<QAction*>("addAction_watermark")->trigger();
    QVERIFY(stampDialogShown);
    QCOMPARE(dialog.getActions().size(), size_t(1));
    QCOMPARE(dialog.getActions()[0].watermark.text, QStringLiteral("BATCH COPY"));

    // A cancelled dialog adds nothing
    handleNextModalWidget([](QWidget* widget) { qobject_cast<QDialog*>(widget)->reject(); });
    child<QToolButton>(&dialog, "addActionButton")->menu()->findChild<QAction*>("addAction_rotate")->trigger();
    QCOMPARE(dialog.getActions().size(), size_t(1));

    // A step is added by its own dialog
    handleNextModalWidget([](QWidget* widget)
    {
        if (pdfviewer::PDFBatchActionDialog* actionDialog = qobject_cast<pdfviewer::PDFBatchActionDialog*>(widget))
        {
            QComboBox* rotation = actionDialog->findChild<QComboBox*>("rotationComboBox");
            rotation->setCurrentIndex(rotation->findData(180));
            actionDialog->accept();
        }
        else
        {
            widget->close();
        }
    });
    child<QToolButton>(&dialog, "addActionButton")->menu()->findChild<QAction*>("addAction_rotate")->trigger();
    QCOMPARE(dialog.getActions().size(), size_t(2));
    QCOMPARE(dialog.getActions()[1].rotation, 180);

    // Edit of the watermark by the button
    child<QListWidget>(&dialog, "actionsList")->setCurrentRow(0);
    QVERIFY(child<QPushButton>(&dialog, "editActionButton")->isEnabled());
    handleNextModalWidget([](QWidget* widget)
    {
        if (pdfviewer::PDFPageStampDialog* stampDialog = qobject_cast<pdfviewer::PDFPageStampDialog*>(widget))
        {
            QCOMPARE(stampDialog->findChild<QPlainTextEdit*>("watermarkTextEdit")->toPlainText(), QStringLiteral("BATCH COPY"));
            stampDialog->setPageSelection("1");
            stampDialog->accept();
        }
        else
        {
            widget->close();
        }
    });
    child<QPushButton>(&dialog, "editActionButton")->click();
    QCOMPARE(dialog.getActions()[0].pages, QStringLiteral("1"));
    QCOMPARE(dialog.getActions()[0].watermark.text, QStringLiteral("BATCH COPY"));
}

void BatchProcessingDialogTest::runBatch()
{
    const QString a = writeDocument("a.pdf", 2);
    const QString b = writeDocument("b.pdf", 3);
    QFile broken(m_input.filePath("c.pdf"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("broken");
    broken.close();

    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    dialog.addFiles({ a, b, broken.fileName() });

    pdf::PDFBatchSettings settings;
    pdf::PDFBatchAction bates = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::BatesNumbering);
    bates.headerFooter.variables.batesPrefix = QStringLiteral("B");
    settings.actions = { bates, pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportText) };
    settings.outputDirectory = m_output.path();
    settings.suffix = QStringLiteral("_out");
    dialog.setSettings(settings);
    QVERIFY(child<QPushButton>(&dialog, "startButton")->isEnabled());

    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFBatchProcessingDialog::batchFinished);
    QVERIFY(dialog.startBatch().isEmpty());
    QVERIFY(dialog.isRunning());
    QVERIFY(!child<QPushButton>(&dialog, "startButton")->isEnabled());
    QVERIFY(!child<QListWidget>(&dialog, "actionsList")->isEnabled());
    QVERIFY(!dialog.startBatch().isEmpty()); // already running
    QVERIFY(finishedSpy.wait(30000));
    QVERIFY(!dialog.isRunning());

    const auto& results = dialog.getResults();
    QCOMPARE(results.size(), size_t(3));
    QCOMPARE(results[0].status, pdf::PDFBatchProcessor::FileResult::Status::Succeeded);
    QCOMPARE(results[1].status, pdf::PDFBatchProcessor::FileResult::Status::Succeeded);
    QCOMPARE(results[2].status, pdf::PDFBatchProcessor::FileResult::Status::Failed);
    QCOMPARE(results[1].outputFiles.front(), m_output.filePath("b_out.pdf"));

    // Bates numbers continued from the first file
    QVERIFY(results[1].details.join(' ').contains("B000003 to B000005"));

    QTableWidget* table = child<QTableWidget>(&dialog, "filesTable");
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Done"));
    QCOMPARE(table->item(1, 2)->text(), QStringLiteral("3"));
    QCOMPARE(table->item(2, 1)->text(), QStringLiteral("Failed"));
    QVERIFY(table->item(2, 4)->toolTip().contains("cannot be read"));
    QVERIFY(child<QLabel>(&dialog, "progressLabel")->text().contains("2 done, 1 failed"));
    QVERIFY(QFileInfo::exists(m_output.filePath("a_out.txt")));

    // A second run renames the outputs and restarts the Bates numbers
    QVERIFY(dialog.startBatch().isEmpty());
    QVERIFY(finishedSpy.wait(30000));
    QCOMPARE(dialog.getResults()[0].outputFiles.front(), m_output.filePath("a_out (2).pdf"));
    QVERIFY(dialog.getResults()[0].details.join(' ').contains("B000001 to B000002"));
}

void BatchProcessingDialogTest::startErrors()
{
    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    QStringList errors = dialog.startBatch();
    QVERIFY(errors.join('\n').contains("Add the files"));
    QVERIFY(errors.join('\n').contains("Add at least one action"));
    QVERIFY(!dialog.isRunning());

    dialog.addFiles({ writeDocument("a.pdf", 1) });
    pdf::PDFBatchAction ocr = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RecognizeText);
    ocr.ocrConfiguration.engineId = QStringLiteral("missing-engine");
    dialog.addAction(ocr);
    errors = dialog.startBatch();
    QVERIFY(errors.join('\n').contains("missing-engine"));

    // The start button shows the errors in a message box
    bool warningShown = false;
    handleNextModalWidget([&warningShown](QWidget* widget)
    {
        warningShown = qobject_cast<QMessageBox*>(widget) != nullptr;
        widget->close();
    });
    child<QPushButton>(&dialog, "startButton")->click();
    QVERIFY(warningShown);
    QVERIFY(!dialog.isRunning());
}

void BatchProcessingDialogTest::stopBatch()
{
    QStringList files;
    for (int i = 0; i < 40; ++i)
    {
        files << writeDocument(QStringLiteral("file%1.pdf").arg(i), 20);
    }

    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    dialog.addFiles(files);
    pdf::PDFBatchAction images = pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::ExportImages);
    images.imageResolution = 50;
    pdf::PDFBatchSettings settings;
    settings.actions = { images };
    settings.outputDirectory = m_output.path();
    dialog.setSettings(settings);

    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFBatchProcessingDialog::batchFinished);
    QVERIFY(dialog.startBatch().isEmpty());
    QVERIFY(child<QPushButton>(&dialog, "stopButton")->isEnabled());
    child<QPushButton>(&dialog, "stopButton")->click();
    QVERIFY(!child<QPushButton>(&dialog, "stopButton")->isEnabled());
    QVERIFY(finishedSpy.wait(60000));

    // Not all files were processed, the rest is marked as stopped
    QVERIFY(dialog.getResults().size() < size_t(files.size()));
    QTableWidget* table = child<QTableWidget>(&dialog, "filesTable");
    QCOMPARE(table->item(files.size() - 1, 1)->text(), QStringLiteral("Stopped"));
    QVERIFY(child<QLabel>(&dialog, "progressLabel")->text().startsWith("Stopped"));

    // Closing the dialog during a batch stops it first, then closes the dialog
    QVERIFY(dialog.startBatch().isEmpty());
    dialog.reject();
    QVERIFY(dialog.isRunning() || finishedSpy.count() == 2);
    QVERIFY(finishedSpy.wait(60000) || finishedSpy.count() == 2);
    QVERIFY(!dialog.isRunning());
}

void BatchProcessingDialogTest::openResult()
{
    const QString a = writeDocument("a.pdf", 1);
    pdfviewer::PDFBatchProcessingDialog dialog(nullptr);
    dialog.addFiles({ a });
    pdf::PDFBatchSettings settings;
    settings.actions = { pdf::PDFBatchAction::create(pdf::PDFBatchAction::Type::RotatePages) };
    settings.outputDirectory = m_output.path();
    dialog.setSettings(settings);

    QSignalSpy finishedSpy(&dialog, &pdfviewer::PDFBatchProcessingDialog::batchFinished);
    QVERIFY(dialog.startBatch().isEmpty());
    QVERIFY(finishedSpy.wait(30000));

    QPushButton* openButton = child<QPushButton>(&dialog, "openResultButton");
    QVERIFY(!openButton->isEnabled());
    child<QTableWidget>(&dialog, "filesTable")->selectRow(0);
    QVERIFY(openButton->isEnabled());
    openButton->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.getDocumentToOpen(), m_output.filePath("a_processed.pdf"));
}

void BatchProcessingDialogTest::ribbonAction()
{
    auto window = std::make_unique<pdfviewer::PDFEditorMainWindow>();
    window->resize(1400, 900);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    QVERIFY(window->getRibbon());

    // Available without a document, in the group "Batch" of the tab "Convert"
    QAction* action = window->findChild<QAction*>("actionBatchProcessing");
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QVERIFY(!action->icon().isNull());

    pdfviewer::PDFRibbonPage* convertPage = window->getRibbon()->getPage("convert");
    QVERIFY(convertPage);
    pdfviewer::PDFRibbonGroup* batchGroup = convertPage->findGroup("batch");
    QVERIFY(batchGroup);
    bool isInGroup = false;
    for (pdfviewer::PDFRibbonButton* button : batchGroup->findChildren<pdfviewer::PDFRibbonButton*>())
    {
        isInGroup = isInGroup || button->getAction() == action;
    }
    QVERIFY(isInGroup);

    // The action opens the dialog
    bool dialogShown = false;
    handleNextModalWidget([&dialogShown](QWidget* widget)
    {
        dialogShown = qobject_cast<pdfviewer::PDFBatchProcessingDialog*>(widget) != nullptr;
        qobject_cast<QDialog*>(widget)->reject();
    });
    action->trigger();
    QVERIFY(dialogShown);

    window->close();
}

QTEST_MAIN(BatchProcessingDialogTest)

#include "tst_batchprocessingdialogtest.moc"
