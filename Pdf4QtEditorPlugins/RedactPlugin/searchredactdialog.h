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


#ifndef SEARCHREDACTDIALOG_H
#define SEARCHREDACTDIALOG_H

#include "pdfredact.h"

#include <QDialog>

#include <functional>

class QLabel;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QDialogButtonBox;

namespace pdfplugin
{

/// Dialog, which finds text (plain text, regular expression, or a predefined
/// pattern) in the document and lets the user choose the occurrences, which
/// are marked for redaction.
class SearchRedactDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// Provides text layouts of all pages of the document
    using TextLayoutProvider = std::function<const pdf::PDFTextLayoutStorage*()>;

    explicit SearchRedactDialog(TextLayoutProvider textLayoutProvider, QWidget* parent);
    virtual ~SearchRedactDialog() override;

    /// Returns current search settings
    pdf::PDFRedactSearch::Settings getSettings() const;

    /// Sets search settings to the widgets
    void setSettings(const pdf::PDFRedactSearch::Settings& settings);

    /// Finds the occurrences using the current settings. Returns false,
    /// if the settings are invalid (the error is displayed in the dialog).
    bool find();

    /// Returns all found occurrences
    const pdf::PDFRedactSearch::Matches& getMatches() const { return m_matches; }

    /// Returns the occurrences checked in the result list
    pdf::PDFRedactSearch::Matches getSelectedMatches() const;

    /// Checks or unchecks all occurrences
    void setAllChecked(bool checked);

    /// Checks or unchecks a single occurrence
    void setChecked(int index, bool checked);

    /// Returns error message of the last search (empty, if there was no error)
    QString getErrorMessage() const;

private:
    void updateUi();
    void onFindClicked();

    TextLayoutProvider m_textLayoutProvider;
    pdf::PDFRedactSearch::Matches m_matches;

    QComboBox* m_modeComboBox = nullptr;
    QLineEdit* m_textEdit = nullptr;
    QCheckBox* m_caseSensitiveCheckBox = nullptr;
    QCheckBox* m_wholeWordsCheckBox = nullptr;
    QCheckBox* m_regularExpressionCheckBox = nullptr;
    QPushButton* m_findButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTreeWidget* m_resultsWidget = nullptr;
    QPushButton* m_selectAllButton = nullptr;
    QPushButton* m_selectNoneButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}   // namespace pdfplugin

#endif // SEARCHREDACTDIALOG_H
