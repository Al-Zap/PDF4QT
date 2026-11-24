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


#include "searchredactdialog.h"
#include "pdfredact.h"
#include "pdfdocumentbuilder.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QDialogButtonBox>

class RedactDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void test_defaults();
    void test_find_text();
    void test_selection();
    void test_patterns();
    void test_errors();
    void test_settings_round_trip();
    void test_no_text_layouts();

private:
    pdf::PDFDocument m_document;
    pdf::PDFTextLayoutStorage m_textLayouts;

    template<typename T>
    static T* find(QWidget* dialog, const char* name)
    {
        T* widget = dialog->findChild<T*>(name);
        if (!widget)
        {
            qWarning() << "Widget not found:" << name;
        }
        return widget;
    }

    static QPushButton* getOkButton(QWidget* dialog)
    {
        return find<QDialogButtonBox>(dialog, "buttonBox")->button(QDialogButtonBox::Ok);
    }
};

void RedactDialogTest::initTestCase()
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));

    QByteArray content = "BT /F1 12 Tf 72 700 Td (Name: John Smith SSN 123-45-6789) Tj ET\n"
                         "BT /F1 12 Tf 72 650 Td (Contact smith@example.com or 555-123-4567) Tj ET\n";

    pdf::PDFDictionaryBuilder contentDictionary;
    contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
    pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(contentDictionary), std::move(content))));

    pdf::PDFDictionaryBuilder font;
    font.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Type1"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("BaseFont"), pdf::PDFObject::createName("Helvetica"));
    font.addEntry(pdf::PDFInplaceOrMemoryString("Encoding"), pdf::PDFObject::createName("WinAnsiEncoding"));
    pdf::PDFObjectReference fontReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(font)));

    pdf::PDFDictionaryBuilder fonts;
    fonts.addEntry(pdf::PDFInplaceOrMemoryString("F1"), pdf::PDFObject::createReference(fontReference));
    pdf::PDFDictionaryBuilder resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::move(fonts)));
    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
    builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    m_document = builder.build();

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFICCProfileCache iccProfileCache;
    pdf::PDFOptionalContentActivity activity(&m_document, pdf::OCUsage::View, nullptr);
    pdf::PDFModifiedDocument modifiedDocument(&m_document, &activity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);
    m_textLayouts = pdf::PDFRedactSearch::createTextLayouts(&m_document, &fontCache, &iccProfileCache, &cms, &activity, pdf::PDFMeshQualitySettings());
}

void RedactDialogTest::test_defaults()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);

    QComboBox* modeComboBox = find<QComboBox>(&dialog, "modeComboBox");
    QVERIFY(modeComboBox);
    QCOMPARE(modeComboBox->count(), int(pdf::PDFRedactSearch::getPatterns().size()) + 1);
    QCOMPARE(modeComboBox->currentIndex(), 0);
    QVERIFY(find<QLineEdit>(&dialog, "textEdit")->isEnabled());
    QVERIFY(!getOkButton(&dialog)->isEnabled());
    QVERIFY(!find<QPushButton>(&dialog, "selectAllButton")->isEnabled());
    QVERIFY(dialog.getMatches().empty());
    QVERIFY(dialog.getErrorMessage().isEmpty());
}

void RedactDialogTest::test_find_text()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);

    // Find by the button
    QTest::keyClicks(find<QLineEdit>(&dialog, "textEdit"), "smith");
    QTest::mouseClick(find<QPushButton>(&dialog, "findButton"), Qt::LeftButton);
    QCOMPARE(dialog.getMatches().size(), size_t(2));
    QTreeWidget* results = find<QTreeWidget>(&dialog, "resultsWidget");
    QCOMPARE(results->topLevelItemCount(), 2);
    QCOMPARE(results->topLevelItem(0)->text(0), QString("1"));
    QVERIFY(results->topLevelItem(0)->text(1).compare("smith", Qt::CaseInsensitive) == 0);
    QVERIFY(getOkButton(&dialog)->isEnabled());
    QVERIFY(find<QLabel>(&dialog, "statusLabel")->text().contains("2"));

    // Case sensitive, by the Enter key
    find<QCheckBox>(&dialog, "caseSensitiveCheckBox")->setChecked(true);
    QLineEdit* textEdit = find<QLineEdit>(&dialog, "textEdit");
    textEdit->setText("Smith");
    QTest::keyClick(textEdit, Qt::Key_Return);
    QCOMPARE(dialog.getMatches().size(), size_t(1));

    // Whole words
    find<QCheckBox>(&dialog, "caseSensitiveCheckBox")->setChecked(false);
    find<QCheckBox>(&dialog, "wholeWordsCheckBox")->setChecked(true);
    textEdit->setText("smit");
    QVERIFY(dialog.find());
    QCOMPARE(dialog.getMatches().size(), size_t(0));
    QVERIFY(!getOkButton(&dialog)->isEnabled());
    QVERIFY(find<QLabel>(&dialog, "statusLabel")->text().contains("No occurrences"));

    // Regular expression
    find<QCheckBox>(&dialog, "wholeWordsCheckBox")->setChecked(false);
    find<QCheckBox>(&dialog, "regularExpressionCheckBox")->setChecked(true);
    textEdit->setText("J[a-z]+n");
    QVERIFY(dialog.find());
    QCOMPARE(dialog.getMatches().size(), size_t(1));
    QCOMPARE(dialog.getMatches().front().matched, QString("John"));
}

void RedactDialogTest::test_selection()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);
    find<QLineEdit>(&dialog, "textEdit")->setText("smith");
    QVERIFY(dialog.find());
    QCOMPARE(dialog.getSelectedMatches().size(), size_t(2));

    dialog.setChecked(0, false);
    QCOMPARE(dialog.getSelectedMatches().size(), size_t(1));
    QVERIFY(getOkButton(&dialog)->isEnabled());

    QTest::mouseClick(find<QPushButton>(&dialog, "selectNoneButton"), Qt::LeftButton);
    QVERIFY(dialog.getSelectedMatches().empty());
    QVERIFY(!getOkButton(&dialog)->isEnabled());

    QTest::mouseClick(find<QPushButton>(&dialog, "selectAllButton"), Qt::LeftButton);
    QCOMPARE(dialog.getSelectedMatches().size(), size_t(2));

    // Invalid index is ignored
    dialog.setChecked(10, false);
    QCOMPARE(dialog.getSelectedMatches().size(), size_t(2));

    // Selected matches can be marked
    pdf::PDFDocumentBuilder builder(&m_document);
    QCOMPARE(pdf::PDFRedactSearch::createMarks(&builder, &m_document, dialog.getSelectedMatches(), Qt::black), 2);
}

void RedactDialogTest::test_patterns()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);
    QComboBox* modeComboBox = find<QComboBox>(&dialog, "modeComboBox");

    const std::vector<std::pair<pdf::PDFRedactSearch::Pattern, QString>> expected = {
        { pdf::PDFRedactSearch::Pattern::SocialSecurityNumber, "123-45-6789" },
        { pdf::PDFRedactSearch::Pattern::EmailAddress, "smith@example.com" },
        { pdf::PDFRedactSearch::Pattern::PhoneNumber, "555-123-4567" }
    };

    for (const auto& [pattern, text] : expected)
    {
        modeComboBox->setCurrentIndex(modeComboBox->findData(int(pattern)));
        QVERIFY(!find<QLineEdit>(&dialog, "textEdit")->isEnabled());
        QVERIFY(!find<QCheckBox>(&dialog, "regularExpressionCheckBox")->isEnabled());
        QCOMPARE(dialog.getSettings().pattern, pattern);
        QVERIFY(dialog.getSettings().text.isEmpty());

        QVERIFY(dialog.find());
        QCOMPARE(dialog.getMatches().size(), size_t(1));
        QCOMPARE(dialog.getMatches().front().matched, text);
    }

    modeComboBox->setCurrentIndex(modeComboBox->findData(int(pdf::PDFRedactSearch::Pattern::CreditCardNumber)));
    QVERIFY(dialog.find());
    QVERIFY(dialog.getMatches().empty());

    modeComboBox->setCurrentIndex(0);
    QVERIFY(find<QLineEdit>(&dialog, "textEdit")->isEnabled());
}

void RedactDialogTest::test_errors()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);

    // Empty text
    QVERIFY(!dialog.find());
    QVERIFY(!dialog.getErrorMessage().isEmpty());

    // Invalid regular expression
    find<QCheckBox>(&dialog, "regularExpressionCheckBox")->setChecked(true);
    find<QLineEdit>(&dialog, "textEdit")->setText("(abc");
    QVERIFY(!dialog.find());
    QVERIFY(dialog.getErrorMessage().contains("regular expression"));
    QVERIFY(dialog.getMatches().empty());

    // Error is cleared by a successful search
    find<QLineEdit>(&dialog, "textEdit")->setText("abc|John");
    QVERIFY(dialog.find());
    QVERIFY(dialog.getErrorMessage().isEmpty());
}

void RedactDialogTest::test_settings_round_trip()
{
    pdfplugin::SearchRedactDialog dialog([this]() { return &m_textLayouts; }, nullptr);

    pdf::PDFRedactSearch::Settings settings;
    settings.text = "John";
    settings.caseSensitive = true;
    settings.wholeWords = true;
    settings.regularExpression = true;
    dialog.setSettings(settings);

    pdf::PDFRedactSearch::Settings result = dialog.getSettings();
    QCOMPARE(result.text, settings.text);
    QCOMPARE(result.caseSensitive, true);
    QCOMPARE(result.wholeWords, true);
    QCOMPARE(result.regularExpression, true);
    QCOMPARE(result.pattern, pdf::PDFRedactSearch::Pattern::None);

    pdf::PDFRedactSearch::Settings patternSettings;
    patternSettings.pattern = pdf::PDFRedactSearch::Pattern::Date;
    dialog.setSettings(patternSettings);
    QCOMPARE(dialog.getSettings().pattern, pdf::PDFRedactSearch::Pattern::Date);
    QVERIFY(!find<QLineEdit>(&dialog, "textEdit")->isEnabled());
}

void RedactDialogTest::test_no_text_layouts()
{
    pdfplugin::SearchRedactDialog dialog([]() { return static_cast<const pdf::PDFTextLayoutStorage*>(nullptr); }, nullptr);
    find<QLineEdit>(&dialog, "textEdit")->setText("John");
    QVERIFY(dialog.find());
    QVERIFY(dialog.getMatches().empty());

    pdfplugin::SearchRedactDialog dialogWithoutProvider(pdfplugin::SearchRedactDialog::TextLayoutProvider(), nullptr);
    find<QLineEdit>(&dialogWithoutProvider, "textEdit")->setText("John");
    QVERIFY(dialogWithoutProvider.find());
    QVERIFY(dialogWithoutProvider.getMatches().empty());
}

QTEST_MAIN(RedactDialogTest)

#include "tst_redactdialogtest.moc"
