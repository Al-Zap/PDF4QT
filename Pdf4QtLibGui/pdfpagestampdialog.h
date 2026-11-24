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


#ifndef PDFPAGESTAMPDIALOG_H
#define PDFPAGESTAMPDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfpagestamper.h"

#include <QDialog>

class QLabel;
class QTimer;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QToolButton;
class QRadioButton;
class QPlainTextEdit;
class QDoubleSpinBox;

namespace pdfviewer
{

/// Dialog of the headers and footers, of the Bates numbering and of the watermarks
/// (the settings of PDFPageStamper). A preview shows the stamp on a blank sheet of
/// the size and rotation of the current page. The settings are remembered.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFPageStampDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode
    {
        HeaderFooter,
        Bates,
        Watermark
    };

    /// Creates the dialog
    /// \param mode Mode of the dialog
    /// \param document Document (used for the page count, the page geometry of the preview and the existing stamps)
    /// \param currentPageIndex Index of the current page (zero based)
    /// \param fileName File name of the document (variable {file})
    /// \param parent Parent widget
    explicit PDFPageStampDialog(Mode mode, const pdf::PDFDocument* document, pdf::PDFInteger currentPageIndex, const QString& fileName, QWidget* parent);
    virtual ~PDFPageStampDialog() override;

    Mode getMode() const { return m_mode; }

    /// Settings of the headers and footers (modes HeaderFooter and Bates)
    pdf::PDFHeaderFooterSettings getHeaderFooterSettings() const;

    /// Settings of the watermark (mode Watermark)
    pdf::PDFWatermarkSettings getWatermarkSettings() const;

    /// Returns the selected pages (zero based indices). Empty vector, if the page range is invalid.
    std::vector<pdf::PDFInteger> getPageIndices(QString* errorMessage = nullptr) const;

    /// Returns true, if the stamps of the same kind added before are replaced
    bool isReplaceExisting() const;

    /// Applies the settings to the document (the result of PDFPageStamper)
    pdf::PDFPageStamper::Result apply() const;

    /// Returns the image of the current preview
    QImage getPreviewImage() const { return m_previewImage; }

    /// Returns the image of the headers and footers at a readable size, which is
    /// shown below the preview, when the text is too small in the preview of the
    /// whole page (large drawings). Null, when it is not shown.
    QImage getDetailImage() const { return m_detailImage; }

    /// Updates the preview immediately (it is updated after a short delay, when the settings change)
    /// The dialog edits a step of a batch (PDFBatchProcessingDialog): the settings are
    /// set by the setters below, the pages are entered as a text (there is no current
    /// page), the replacement of the existing stamps is always offered, Bates numbers can
    /// continue over the files, the settings are not remembered and nothing is applied.
    /// Call it before the setters.
    void setBatchMode();
    bool isBatchMode() const { return m_batchMode; }

    void setHeaderFooterSettings(const pdf::PDFHeaderFooterSettings& settings);
    void setWatermarkSettings(const pdf::PDFWatermarkSettings& settings);
    void setReplaceExisting(bool replaceExisting);

    /// Pages of the batch mode in the form "1-3, 5, 8-" (empty = all pages)
    void setPageSelection(const QString& pages);
    QString getPageSelection() const;

    /// Bates numbers continue in the next file of the batch
    void setContinueBatesNumbering(bool continueNumbering);
    bool isContinueBatesNumbering() const;

    void updatePreview();

    virtual void accept() override;

private:
    QWidget* createHeaderFooterPage();
    QWidget* createBatesWidgets(bool withPosition);
    QWidget* createWatermarkPage();
    QWidget* createStyleWidgets();
    QWidget* createMarginsWidgets();
    QWidget* createPagesWidgets();

    void connectChanges();
    void schedulePreview();
    void insertVariable(const QString& variable);
    void selectColor();
    void selectImage();
    void updateColorButton();
    void updateEnabledState();
    void loadSettings();
    void saveSettings() const;
    pdf::PDFStampTextStyle getTextStyle() const;
    pdf::PDFStampVariables getVariables() const;
    QMarginsF getMargins() const;
    void setTextStyle(const pdf::PDFStampTextStyle& style);
    void setVariables(const pdf::PDFStampVariables& variables);
    void setMargins(const QMarginsF& margins);
    QString getSettingsGroup() const;

    virtual bool eventFilter(QObject* watched, QEvent* event) override;

    /// Font size in pixels of the preview of the whole page, below which the
    /// headers and footers are shown once more at a readable size
    static constexpr qreal MINIMAL_READABLE_PREVIEW_FONT_SIZE = 7.0;

    Mode m_mode;
    const pdf::PDFDocument* m_document;
    pdf::PDFInteger m_currentPageIndex;
    QString m_fileName;
    QColor m_color;
    QImage m_watermarkImage;
    QImage m_previewImage;
    QImage m_detailImage;
    QTimer* m_previewTimer = nullptr;
    bool m_batchMode = false;

    // Headers and footers
    std::array<QPlainTextEdit*, pdf::PDFHeaderFooterSettings::PositionCount> m_positionEdits = { };
    QPlainTextEdit* m_lastTextEdit = nullptr;
    QComboBox* m_variableComboBox = nullptr;
    QSpinBox* m_startPageNumberSpinBox = nullptr;
    QLineEdit* m_dateFormatEdit = nullptr;

    // Bates numbering
    QLineEdit* m_batesPrefixEdit = nullptr;
    QLineEdit* m_batesSuffixEdit = nullptr;
    QSpinBox* m_batesStartSpinBox = nullptr;
    QSpinBox* m_batesDigitsSpinBox = nullptr;
    QComboBox* m_batesPositionComboBox = nullptr;
    QLabel* m_batesRangeLabel = nullptr;
    QCheckBox* m_continueBatesCheckBox = nullptr;

    // Watermark
    QRadioButton* m_watermarkTextRadioButton = nullptr;
    QRadioButton* m_watermarkImageRadioButton = nullptr;
    QPlainTextEdit* m_watermarkTextEdit = nullptr;
    QLineEdit* m_watermarkImageEdit = nullptr;
    QToolButton* m_watermarkImageButton = nullptr;
    QDoubleSpinBox* m_rotationSpinBox = nullptr;
    QSpinBox* m_relativeSizeSpinBox = nullptr;
    QComboBox* m_alignmentComboBox = nullptr;
    QCheckBox* m_behindContentCheckBox = nullptr;

    // Style
    QComboBox* m_fontComboBox = nullptr;
    QDoubleSpinBox* m_fontSizeSpinBox = nullptr;
    QToolButton* m_colorButton = nullptr;
    QSpinBox* m_opacitySpinBox = nullptr;
    std::array<QDoubleSpinBox*, 4> m_marginSpinBoxes = { };

    // Pages
    QRadioButton* m_allPagesRadioButton = nullptr;
    QRadioButton* m_currentPageRadioButton = nullptr;
    QRadioButton* m_pageRangeRadioButton = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;
    QCheckBox* m_replaceCheckBox = nullptr;

    QLabel* m_previewLabel = nullptr;
    QLabel* m_detailCaptionLabel = nullptr;
    QLabel* m_detailLabel = nullptr;
};

}   // namespace pdfviewer

#endif // PDFPAGESTAMPDIALOG_H
