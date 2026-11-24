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


#ifndef PDFPRINTDIALOG_H
#define PDFPRINTDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfprintlayout.h"

#include <QImage>
#include <QDialog>
#include <QPrinter>
#include <QPageSize>

#include <memory>
#include <optional>
#include <functional>

class QLabel;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QPushButton;
class QToolButton;
class QRadioButton;
class QDoubleSpinBox;

namespace pdf
{
class PDFDocument;
class PDFDrawWidgetProxy;
}

namespace pdfviewer
{

/// Draws the sheets of the print job (pages with their annotations, tile marks)
class PDF4QTLIBGUILIBSHARED_EXPORT PDFPrintRenderer
{
public:
    PDFPrintRenderer() = delete;

    /// Draws a sheet of paper: the page (or its part, when the sheet is a tile) as it is
    /// printed, in the colors of the document (color adjustments of the screen are not
    /// applied), with the annotations marked as printed (comments, measurements, stamps,
    /// form fields), and the trim lines and the label of a tile. The paper is not filled.
    /// \param painter Painter
    /// \param sheet Sheet
    /// \param paperToDevice Maps paper points (origin in the top left corner of the paper) to the device of the painter
    /// \param document Document
    /// \param proxy Proxy of the draw widget (caches, color management, settings of the rendering)
    /// \param tileMarks Draw the trim lines and the label of a tile
    static void drawSheet(QPainter* painter,
                          const pdf::PDFPrintSheet& sheet,
                          const QTransform& paperToDevice,
                          const pdf::PDFDocument* document,
                          const pdf::PDFDrawWidgetProxy* proxy,
                          bool tileMarks);

    /// Prints the sheets. The printer gets the full page mode, the sheets are
    /// laid out for its paper (see PDFPrintDialog::getPrintableRect).
    /// \param printer Printer
    /// \param sheets Sheets
    /// \param document Document
    /// \param proxy Proxy of the draw widget
    /// \param tileMarks Draw the trim lines and the labels of the tiles
    /// \param progress Called after each sheet, printing stops, when it returns false (can be empty)
    /// \returns true, if all sheets were printed
    static bool print(QPrinter* printer,
                      const std::vector<pdf::PDFPrintSheet>& sheets,
                      const pdf::PDFDocument* document,
                      const pdf::PDFDrawWidgetProxy* proxy,
                      bool tileMarks,
                      const std::function<bool(void)>& progress);

    /// Returns a short description of the sheet (page, tile, scale), for example
    /// "Page 2, tile A3 (2 x 3 tiles), 100 %"
    static QString getSheetDescription(const pdf::PDFPrintSheet& sheet);
};

/// Preview of one sheet of paper of the print job
class PDF4QTLIBGUILIBSHARED_EXPORT PDFPrintPreviewWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFPrintPreviewWidget(QWidget* parent);

    /// Sets the sheet shown in the preview
    void setSheet(const pdf::PDFPrintSheet& sheet, const pdf::PDFDocument* document, const pdf::PDFDrawWidgetProxy* proxy, bool tileMarks);

    /// Clears the preview (no sheet is shown)
    void clear();

    /// Returns the image of the paper shown in the preview (null image, if no sheet is shown)
    QImage getPaperImage();

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QRectF getPaperRect() const;
    void updateImage();

    std::optional<pdf::PDFPrintSheet> m_sheet;
    const pdf::PDFDocument* m_document = nullptr;
    const pdf::PDFDrawWidgetProxy* m_proxy = nullptr;
    bool m_tileMarks = true;
    QImage m_image;
};

/// Print dialog: printer, paper, pages, scale (fit to paper, actual size for 1:1 drawings,
/// half size or any other scale), tiling of large sheets on smaller paper and a preview
/// of each sheet of paper. The settings are remembered.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFPrintDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// Creates the dialog
    /// \param document Document
    /// \param currentPageIndex Index of the current page (zero based, -1 if there is none)
    /// \param proxy Proxy of the draw widget (caches, color management, settings of the rendering)
    /// \param printerMode Mode of the printer (screen resolution, when the document doesn't allow printing in high resolution)
    /// \param parent Parent widget
    explicit PDFPrintDialog(const pdf::PDFDocument* document,
                            pdf::PDFInteger currentPageIndex,
                            const pdf::PDFDrawWidgetProxy* proxy,
                            QPrinter::PrinterMode printerMode,
                            QWidget* parent);
    virtual ~PDFPrintDialog() override;

    /// Returns the printer with the selected printer, paper and number of copies
    QPrinter* getPrinter() const { return m_printer.get(); }

    /// Returns the settings of the layout (scale, rotation, tiles)
    pdf::PDFPrintSettings getPrintSettings() const;

    /// Returns the selected pages (zero based indices). Empty vector, if the page range is invalid.
    std::vector<pdf::PDFInteger> getPageIndices(QString* errorMessage = nullptr) const;

    /// Returns the sheets of paper of the print job
    std::vector<pdf::PDFPrintSheet> getSheets() const;

    /// Returns the size of the paper of the printer in points
    static QSizeF getPaperSize(const QPrinter* printer);

    /// Returns the printable area of the paper of the printer in points
    /// (the paper minus the minimum margins of the printer)
    static QRectF getPrintableRect(const QPrinter* printer);

    /// Prints into a PDF file instead of a printer (the output file is not asked for)
    void setOutputFileName(const QString& fileName);

    /// Shows the sheet of the given index in the preview
    void setCurrentSheet(int index);
    int getCurrentSheet() const { return m_currentSheet; }

    virtual void accept() override;

private:
    void onPrinterChanged();
    void onPaperChanged();
    void onPrinterProperties();
    void updatePaperSizes();
    void updateUi();
    void updatePreview();
    void loadSettings();
    void saveSettings() const;
    bool isPdfOutput() const;
    bool isImperial() const;
    QString formatLength(pdf::PDFReal points) const;

    const pdf::PDFDocument* m_document;
    pdf::PDFInteger m_currentPageIndex;
    const pdf::PDFDrawWidgetProxy* m_proxy;
    std::unique_ptr<QPrinter> m_printer;
    QString m_outputFileName;
    bool m_updatingUi = false;

    QComboBox* m_printerComboBox = nullptr;
    QPushButton* m_propertiesButton = nullptr;
    QComboBox* m_paperComboBox = nullptr;
    QComboBox* m_orientationComboBox = nullptr;
    QSpinBox* m_copiesSpinBox = nullptr;
    QRadioButton* m_allPagesRadioButton = nullptr;
    QRadioButton* m_currentPageRadioButton = nullptr;
    QRadioButton* m_pageRangeRadioButton = nullptr;
    QLineEdit* m_pageRangeEdit = nullptr;
    QComboBox* m_scaleComboBox = nullptr;
    QDoubleSpinBox* m_customScaleSpinBox = nullptr;
    QCheckBox* m_autoRotateCheckBox = nullptr;
    QCheckBox* m_tilingCheckBox = nullptr;
    QDoubleSpinBox* m_overlapSpinBox = nullptr;
    QCheckBox* m_tileMarksCheckBox = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    PDFPrintPreviewWidget* m_previewWidget = nullptr;
    QToolButton* m_previousSheetButton = nullptr;
    QToolButton* m_nextSheetButton = nullptr;
    QLabel* m_sheetLabel = nullptr;
    QPushButton* m_printButton = nullptr;

    QList<QPageSize> m_pageSizes;
    std::vector<pdf::PDFPrintSheet> m_sheets;
    int m_currentSheet = 0;
};

}   // namespace pdfviewer

#endif // PDFPRINTDIALOG_H
