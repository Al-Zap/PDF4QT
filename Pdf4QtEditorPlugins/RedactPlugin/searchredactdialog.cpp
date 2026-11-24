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
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QCursor>

namespace pdfplugin
{

SearchRedactDialog::SearchRedactDialog(TextLayoutProvider textLayoutProvider, QWidget* parent) :
    BaseClass(parent),
    m_textLayoutProvider(std::move(textLayoutProvider))
{
    setWindowTitle(tr("Search & Redact"));

    m_modeComboBox = new QComboBox(this);
    m_modeComboBox->setObjectName("modeComboBox");
    m_modeComboBox->addItem(tr("Words or phrases"), int(pdf::PDFRedactSearch::Pattern::None));
    for (pdf::PDFRedactSearch::Pattern pattern : pdf::PDFRedactSearch::getPatterns())
    {
        m_modeComboBox->addItem(pdf::PDFRedactSearch::getPatternName(pattern), int(pattern));
    }

    m_textEdit = new QLineEdit(this);
    m_textEdit->setObjectName("textEdit");
    m_textEdit->setPlaceholderText(tr("Text to redact"));
    m_textEdit->setClearButtonEnabled(true);

    m_caseSensitiveCheckBox = new QCheckBox(tr("&Case sensitive"), this);
    m_caseSensitiveCheckBox->setObjectName("caseSensitiveCheckBox");
    m_wholeWordsCheckBox = new QCheckBox(tr("&Whole words only"), this);
    m_wholeWordsCheckBox->setObjectName("wholeWordsCheckBox");
    m_regularExpressionCheckBox = new QCheckBox(tr("Regular e&xpression"), this);
    m_regularExpressionCheckBox->setObjectName("regularExpressionCheckBox");

    m_findButton = new QPushButton(tr("&Find"), this);
    m_findButton->setObjectName("findButton");
    m_findButton->setDefault(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);

    m_resultsWidget = new QTreeWidget(this);
    m_resultsWidget->setObjectName("resultsWidget");
    m_resultsWidget->setColumnCount(3);
    m_resultsWidget->setHeaderLabels({ tr("Page"), tr("Found"), tr("Context") });
    m_resultsWidget->setRootIsDecorated(false);
    m_resultsWidget->setUniformRowHeights(true);
    m_resultsWidget->header()->setStretchLastSection(true);

    m_selectAllButton = new QPushButton(tr("Select &All"), this);
    m_selectAllButton->setObjectName("selectAllButton");
    m_selectNoneButton = new QPushButton(tr("Select &None"), this);
    m_selectNoneButton->setObjectName("selectNoneButton");

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName("buttonBox");
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("&Mark for Redaction"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(false);
    m_buttonBox->button(QDialogButtonBox::Ok)->setAutoDefault(false);

    QGridLayout* searchLayout = new QGridLayout();
    searchLayout->addWidget(new QLabel(tr("Search for:"), this), 0, 0);
    searchLayout->addWidget(m_modeComboBox, 0, 1);
    searchLayout->addWidget(new QLabel(tr("Text:"), this), 1, 0);
    searchLayout->addWidget(m_textEdit, 1, 1);
    searchLayout->addWidget(m_findButton, 1, 2);

    QHBoxLayout* optionsLayout = new QHBoxLayout();
    optionsLayout->addWidget(m_caseSensitiveCheckBox);
    optionsLayout->addWidget(m_wholeWordsCheckBox);
    optionsLayout->addWidget(m_regularExpressionCheckBox);
    optionsLayout->addStretch(1);
    searchLayout->addLayout(optionsLayout, 2, 1, 1, 2);

    QHBoxLayout* selectionLayout = new QHBoxLayout();
    selectionLayout->addWidget(m_selectAllButton);
    selectionLayout->addWidget(m_selectNoneButton);
    selectionLayout->addStretch(1);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(searchLayout);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_resultsWidget, 1);
    mainLayout->addLayout(selectionLayout);
    mainLayout->addWidget(m_buttonBox);

    connect(m_modeComboBox, &QComboBox::currentIndexChanged, this, &SearchRedactDialog::updateUi);
    connect(m_textEdit, &QLineEdit::returnPressed, this, &SearchRedactDialog::onFindClicked);
    connect(m_findButton, &QPushButton::clicked, this, &SearchRedactDialog::onFindClicked);
    connect(m_selectAllButton, &QPushButton::clicked, this, [this]() { setAllChecked(true); });
    connect(m_selectNoneButton, &QPushButton::clicked, this, [this]() { setAllChecked(false); });
    connect(m_resultsWidget, &QTreeWidget::itemChanged, this, &SearchRedactDialog::updateUi);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SearchRedactDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &SearchRedactDialog::reject);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(560, 420)));
    updateUi();
    pdf::PDFWidgetUtils::style(this);
}

SearchRedactDialog::~SearchRedactDialog()
{

}

pdf::PDFRedactSearch::Settings SearchRedactDialog::getSettings() const
{
    pdf::PDFRedactSearch::Settings settings;
    settings.pattern = static_cast<pdf::PDFRedactSearch::Pattern>(m_modeComboBox->currentData().toInt());

    if (settings.pattern == pdf::PDFRedactSearch::Pattern::None)
    {
        settings.text = m_textEdit->text();
        settings.caseSensitive = m_caseSensitiveCheckBox->isChecked();
        settings.wholeWords = m_wholeWordsCheckBox->isChecked();
        settings.regularExpression = m_regularExpressionCheckBox->isChecked();
    }

    return settings;
}

void SearchRedactDialog::setSettings(const pdf::PDFRedactSearch::Settings& settings)
{
    const int index = m_modeComboBox->findData(int(settings.text.isEmpty() ? settings.pattern : pdf::PDFRedactSearch::Pattern::None));
    m_modeComboBox->setCurrentIndex(qMax(index, 0));
    m_textEdit->setText(settings.text);
    m_caseSensitiveCheckBox->setChecked(settings.caseSensitive);
    m_wholeWordsCheckBox->setChecked(settings.wholeWords);
    m_regularExpressionCheckBox->setChecked(settings.regularExpression);
    updateUi();
}

bool SearchRedactDialog::find()
{
    m_matches.clear();
    m_resultsWidget->clear();

    QString errorMessage;
    QRegularExpression expression = pdf::PDFRedactSearch::createExpression(getSettings(), &errorMessage);

    if (!pdf::PDFRedactSearch::isValidExpression(expression))
    {
        m_statusLabel->setText(errorMessage);
        m_statusLabel->setProperty("isError", true);
        updateUi();
        return false;
    }

    const pdf::PDFTextLayoutStorage* storage = m_textLayoutProvider ? m_textLayoutProvider() : nullptr;
    if (storage)
    {
        m_matches = pdf::PDFRedactSearch::find(*storage, expression);
    }

    QSignalBlocker blocker(m_resultsWidget);
    for (const pdf::PDFRedactSearch::Match& match : m_matches)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_resultsWidget);
        item->setText(0, QString::number(match.pageIndex + 1));
        item->setText(1, match.matched);
        item->setText(2, match.context.simplified());
        item->setToolTip(2, match.context);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }
    m_resultsWidget->resizeColumnToContents(0);
    m_resultsWidget->resizeColumnToContents(1);

    m_statusLabel->setProperty("isError", false);
    m_statusLabel->setText(m_matches.empty() ? tr("No occurrences were found.") : tr("Found %1 occurrence(s). Checked ones will be marked for redaction.").arg(m_matches.size()));
    updateUi();
    return true;
}

pdf::PDFRedactSearch::Matches SearchRedactDialog::getSelectedMatches() const
{
    pdf::PDFRedactSearch::Matches result;

    for (int i = 0; i < m_resultsWidget->topLevelItemCount() && i < int(m_matches.size()); ++i)
    {
        if (m_resultsWidget->topLevelItem(i)->checkState(0) == Qt::Checked)
        {
            result.push_back(m_matches[i]);
        }
    }

    return result;
}

void SearchRedactDialog::setAllChecked(bool checked)
{
    {
        QSignalBlocker blocker(m_resultsWidget);
        for (int i = 0; i < m_resultsWidget->topLevelItemCount(); ++i)
        {
            m_resultsWidget->topLevelItem(i)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }
    }

    updateUi();
}

void SearchRedactDialog::setChecked(int index, bool checked)
{
    if (QTreeWidgetItem* item = m_resultsWidget->topLevelItem(index))
    {
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
}

QString SearchRedactDialog::getErrorMessage() const
{
    return m_statusLabel->property("isError").toBool() ? m_statusLabel->text() : QString();
}

void SearchRedactDialog::updateUi()
{
    const bool isTextMode = m_modeComboBox->currentData().toInt() == int(pdf::PDFRedactSearch::Pattern::None);
    m_textEdit->setEnabled(isTextMode);
    m_caseSensitiveCheckBox->setEnabled(isTextMode);
    m_wholeWordsCheckBox->setEnabled(isTextMode);
    m_regularExpressionCheckBox->setEnabled(isTextMode);

    const bool hasResults = m_resultsWidget->topLevelItemCount() > 0;
    m_selectAllButton->setEnabled(hasResults);
    m_selectNoneButton->setEnabled(hasResults);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!getSelectedMatches().empty());
}

void SearchRedactDialog::onFindClicked()
{
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    find();
    QGuiApplication::restoreOverrideCursor();
}

}   // namespace pdfplugin
