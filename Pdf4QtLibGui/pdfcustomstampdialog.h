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


#ifndef PDFCUSTOMSTAMPDIALOG_H
#define PDFCUSTOMSTAMPDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfcustomstamp.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;
class QDoubleSpinBox;
class QPlainTextEdit;
class QDialogButtonBox;

namespace pdfviewer
{

/// Editor of the custom stamps ("REVIEWED - company - author - date" with a logo)
class PDF4QTLIBGUILIBSHARED_EXPORT PDFCustomStampDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFCustomStampDialog(const pdf::PDFCustomStamps& stamps, QWidget* parent);
    virtual ~PDFCustomStampDialog() override;

    /// Returns the edited stamps
    const pdf::PDFCustomStamps& getStamps() const { return m_stamps; }

    /// Returns the index of the current stamp (-1, if there is none)
    int getCurrentIndex() const;

    /// Selects the stamp
    void setCurrentIndex(int index);

    /// Adds a new stamp and selects it
    void addStamp();

    /// Adds a copy of the current stamp and selects it
    void duplicateStamp();

    /// Removes the current stamp
    void removeStamp();

    /// Moves the current stamp up (-1) or down (+1) in the list
    void moveStamp(int direction);

    /// Sets the logo of the current stamp from the image file. Returns false,
    /// if the file is not an image.
    bool setLogoFromFile(const QString& fileName);

    /// Removes the logo of the current stamp
    void removeLogo();

    /// Sets the color of the current stamp
    void setColor(const QColor& color);

    /// Adds the stamps of the file (JSON). Returns the number of added stamps, or -1,
    /// if the file is not a file of the stamps.
    int importStamps(const QString& fileName);

    /// Writes the stamps to the file
    bool exportStamps(const QString& fileName) const;

    /// Returns the image of the preview of the current stamp
    QImage getPreviewImage() const;

    QListWidget* getListWidget() const { return m_listWidget; }
    QLineEdit* getNameEdit() const { return m_nameEdit; }
    QLineEdit* getTitleEdit() const { return m_titleEdit; }
    QPlainTextEdit* getTextEdit() const { return m_textEdit; }
    QLineEdit* getCompanyEdit() const { return m_companyEdit; }
    QDoubleSpinBox* getWidthSpinBox() const { return m_widthSpinBox; }
    QLabel* getPreviewLabel() const { return m_previewLabel; }

private:
    void updateList();
    void updateEditors();
    void updatePreview();
    void updateButtons();
    void onCurrentRowChanged(int row);
    void onEditorChanged();
    void onColorButtonClicked();
    void onLogoButtonClicked();
    void onImportClicked();
    void onExportClicked();

    pdf::PDFCustomStamp* getCurrentStamp();
    const pdf::PDFCustomStamp* getCurrentStamp() const;

    pdf::PDFCustomStamps m_stamps;
    bool m_updatingEditors = false;

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_duplicateButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_upButton = nullptr;
    QToolButton* m_downButton = nullptr;
    QPushButton* m_importButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLineEdit* m_companyEdit = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPushButton* m_logoButton = nullptr;
    QPushButton* m_removeLogoButton = nullptr;
    QDoubleSpinBox* m_widthSpinBox = nullptr;
    QLabel* m_previewLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}   // namespace pdfviewer

#endif // PDFCUSTOMSTAMPDIALOG_H
