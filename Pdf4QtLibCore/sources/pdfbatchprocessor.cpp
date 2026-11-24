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



#include "pdfbatchprocessor.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocxexporter.h"
#include "pdfpageorganizer.h"
#include "pdfsecurityhandler.h"
#include "pdfocrapplyprocessor.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrsession.h"
#include "pdfocrengine.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfform.h"
#include "pdfexception.h"
#include "pdfutils.h"
#include "pdfconstants.h"

#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QSaveFile>
#include <QDirIterator>
#include <QImageWriter>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QMutex>

#include <algorithm>
#include <numeric>
#include <limits>

namespace pdf
{

/// Serialization of the settings of the steps
class PDFBatchProcessorHelper
{
public:
    PDFBatchProcessorHelper() = delete;

    static constexpr int SEQUENCE_VERSION = 1;

    static QString getFontId(PDFStampFont font);
    static QJsonObject textStyleToJson(const PDFStampTextStyle& style);
    static void textStyleFromJson(const QJsonObject& object, PDFStampTextStyle& style);
    static QJsonArray marginsToJson(const QMarginsF& margins);
    static void marginsFromJson(const QJsonValue& value, QMarginsF& margins);
    static QJsonObject variablesToJson(const PDFStampVariables& variables);
    static void variablesFromJson(const QJsonObject& object, PDFStampVariables& variables);
    static QString imageToBase64(const QImage& image);
    static QImage imageFromBase64(const QString& text);
    static QString getAlignmentId(Qt::Alignment alignment);
    static Qt::Alignment getAlignmentFromId(const QString& id, Qt::Alignment defaultAlignment);

    /// Returns true, if any text of the header and footer contains the variable {bates}
    static bool hasBatesVariable(const PDFHeaderFooterSettings& settings);

    /// Returns the position of the first text with the variable {bates}
    static PDFHeaderFooterSettings::Position getBatesPosition(const PDFHeaderFooterSettings& settings);

    /// Returns true, if the file names point to the same file
    static bool isSameFile(const QString& left, const QString& right);

    /// Compares the file names as a person does: case insensitive, numbers by their value
    /// ("plan2.pdf" is in front of "plan10.pdf")
    static bool isNaturallyLess(const QString& left, const QString& right);

    /// Text written for a page break of the text export
    static QString getPageSeparator() { return QStringLiteral("\n\f"); }
};

bool PDFBatchProcessorHelper::isNaturallyLess(const QString& left, const QString& right)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < left.size() && j < right.size())
    {
        if (left[i].isDigit() && right[j].isDigit())
        {
            qsizetype iEnd = i;
            qsizetype jEnd = j;
            while (iEnd < left.size() && left[iEnd].isDigit())
            {
                ++iEnd;
            }
            while (jEnd < right.size() && right[jEnd].isDigit())
            {
                ++jEnd;
            }

            // Numbers are compared without the leading zeros: first by the length, then by the digits
            QStringView leftNumber = QStringView(left).mid(i, iEnd - i);
            QStringView rightNumber = QStringView(right).mid(j, jEnd - j);
            while (leftNumber.size() > 1 && leftNumber.front() == QChar('0'))
            {
                leftNumber = leftNumber.mid(1);
            }
            while (rightNumber.size() > 1 && rightNumber.front() == QChar('0'))
            {
                rightNumber = rightNumber.mid(1);
            }
            if (leftNumber.size() != rightNumber.size())
            {
                return leftNumber.size() < rightNumber.size();
            }
            const int compare = leftNumber.compare(rightNumber);
            if (compare != 0)
            {
                return compare < 0;
            }

            i = iEnd;
            j = jEnd;
            continue;
        }

        const QChar leftChar = left[i].toCaseFolded();
        const QChar rightChar = right[j].toCaseFolded();
        if (leftChar != rightChar)
        {
            return leftChar < rightChar;
        }
        ++i;
        ++j;
    }

    if ((left.size() - i) != (right.size() - j))
    {
        return (left.size() - i) < (right.size() - j);
    }

    return left < right;
}

QString PDFBatchProcessorHelper::getFontId(PDFStampFont font)
{
    switch (font)
    {
        case PDFStampFont::Helvetica:
            return QStringLiteral("helvetica");
        case PDFStampFont::HelveticaBold:
            return QStringLiteral("helvetica-bold");
        case PDFStampFont::TimesRoman:
            return QStringLiteral("times");
        case PDFStampFont::TimesBold:
            return QStringLiteral("times-bold");
        case PDFStampFont::Courier:
            return QStringLiteral("courier");
        case PDFStampFont::CourierBold:
            return QStringLiteral("courier-bold");
    }

    return QStringLiteral("helvetica");
}

QJsonObject PDFBatchProcessorHelper::textStyleToJson(const PDFStampTextStyle& style)
{
    QJsonObject object;
    object[QStringLiteral("font")] = getFontId(style.font);
    object[QStringLiteral("fontSize")] = style.fontSize;
    object[QStringLiteral("color")] = style.color.name(QColor::HexRgb);
    object[QStringLiteral("opacity")] = style.opacity;
    return object;
}

void PDFBatchProcessorHelper::textStyleFromJson(const QJsonObject& object, PDFStampTextStyle& style)
{
    if (std::optional<PDFStampFont> font = PDFPageStamper::getFontFromName(object.value(QStringLiteral("font")).toString()))
    {
        style.font = *font;
    }

    style.fontSize = object.value(QStringLiteral("fontSize")).toDouble(style.fontSize);

    const QColor color = QColor::fromString(object.value(QStringLiteral("color")).toString());
    if (color.isValid())
    {
        style.color = color;
    }

    style.opacity = qBound(0.0, object.value(QStringLiteral("opacity")).toDouble(style.opacity), 1.0);
}

QJsonArray PDFBatchProcessorHelper::marginsToJson(const QMarginsF& margins)
{
    return QJsonArray({ margins.left(), margins.top(), margins.right(), margins.bottom() });
}

void PDFBatchProcessorHelper::marginsFromJson(const QJsonValue& value, QMarginsF& margins)
{
    const QJsonArray array = value.toArray();
    if (array.size() == 4)
    {
        margins = QMarginsF(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble());
    }
}

QJsonObject PDFBatchProcessorHelper::variablesToJson(const PDFStampVariables& variables)
{
    QJsonObject object;
    object[QStringLiteral("startPageNumber")] = double(variables.startPageNumber);
    object[QStringLiteral("batesPrefix")] = variables.batesPrefix;
    object[QStringLiteral("batesSuffix")] = variables.batesSuffix;
    object[QStringLiteral("batesStart")] = double(variables.batesStart);
    object[QStringLiteral("batesDigits")] = variables.batesDigits;
    object[QStringLiteral("dateFormat")] = variables.dateFormat;
    object[QStringLiteral("timeFormat")] = variables.timeFormat;
    object[QStringLiteral("fileName")] = variables.fileName;
    return object;
}

void PDFBatchProcessorHelper::variablesFromJson(const QJsonObject& object, PDFStampVariables& variables)
{
    variables.startPageNumber = PDFInteger(object.value(QStringLiteral("startPageNumber")).toDouble(double(variables.startPageNumber)));
    variables.batesPrefix = object.value(QStringLiteral("batesPrefix")).toString(variables.batesPrefix);
    variables.batesSuffix = object.value(QStringLiteral("batesSuffix")).toString(variables.batesSuffix);
    variables.batesStart = PDFInteger(object.value(QStringLiteral("batesStart")).toDouble(double(variables.batesStart)));
    variables.batesDigits = object.value(QStringLiteral("batesDigits")).toInt(variables.batesDigits);
    variables.dateFormat = object.value(QStringLiteral("dateFormat")).toString(variables.dateFormat);
    variables.timeFormat = object.value(QStringLiteral("timeFormat")).toString(variables.timeFormat);
    variables.fileName = object.value(QStringLiteral("fileName")).toString(variables.fileName);
}

QString PDFBatchProcessorHelper::imageToBase64(const QImage& image)
{
    if (image.isNull())
    {
        return QString();
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(data.toBase64());
}

QImage PDFBatchProcessorHelper::imageFromBase64(const QString& text)
{
    if (text.isEmpty())
    {
        return QImage();
    }

    QImage image;
    image.loadFromData(QByteArray::fromBase64(text.toLatin1()), "PNG");
    return image;
}

QString PDFBatchProcessorHelper::getAlignmentId(Qt::Alignment alignment)
{
    const bool left = alignment.testFlag(Qt::AlignLeft);
    const bool right = alignment.testFlag(Qt::AlignRight);
    const bool top = alignment.testFlag(Qt::AlignTop);
    const bool bottom = alignment.testFlag(Qt::AlignBottom);

    QString vertical = top ? QStringLiteral("top") : (bottom ? QStringLiteral("bottom") : QString());
    QString horizontal = left ? QStringLiteral("left") : (right ? QStringLiteral("right") : QString());

    if (vertical.isEmpty() && horizontal.isEmpty())
    {
        return QStringLiteral("center");
    }
    if (vertical.isEmpty())
    {
        return horizontal;
    }
    if (horizontal.isEmpty())
    {
        return vertical;
    }
    return vertical + QChar('-') + horizontal;
}

Qt::Alignment PDFBatchProcessorHelper::getAlignmentFromId(const QString& id, Qt::Alignment defaultAlignment)
{
    if (id.isEmpty())
    {
        return defaultAlignment;
    }

    Qt::Alignment alignment;
    const QStringList parts = id.split(QChar('-'), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        if (part == QLatin1String("top"))
        {
            alignment |= Qt::AlignTop;
        }
        else if (part == QLatin1String("bottom"))
        {
            alignment |= Qt::AlignBottom;
        }
        else if (part == QLatin1String("left"))
        {
            alignment |= Qt::AlignLeft;
        }
        else if (part == QLatin1String("right"))
        {
            alignment |= Qt::AlignRight;
        }
        else if (part != QLatin1String("center"))
        {
            return defaultAlignment;
        }
    }

    if (!(alignment & Qt::AlignHorizontal_Mask))
    {
        alignment |= Qt::AlignHCenter;
    }
    if (!(alignment & Qt::AlignVertical_Mask))
    {
        alignment |= Qt::AlignVCenter;
    }
    return alignment;
}

bool PDFBatchProcessorHelper::hasBatesVariable(const PDFHeaderFooterSettings& settings)
{
    return getBatesPosition(settings) != PDFHeaderFooterSettings::PositionCount;
}

PDFHeaderFooterSettings::Position PDFBatchProcessorHelper::getBatesPosition(const PDFHeaderFooterSettings& settings)
{
    for (int i = 0; i < PDFHeaderFooterSettings::PositionCount; ++i)
    {
        if (settings.texts[i].contains(QLatin1String("{bates}")))
        {
            return static_cast<PDFHeaderFooterSettings::Position>(i);
        }
    }

    return PDFHeaderFooterSettings::PositionCount;
}

bool PDFBatchProcessorHelper::isSameFile(const QString& left, const QString& right)
{
    const QFileInfo leftInfo(left);
    const QFileInfo rightInfo(right);

    if (leftInfo.exists() && rightInfo.exists())
    {
        return leftInfo.canonicalFilePath() == rightInfo.canonicalFilePath();
    }

    return QDir::cleanPath(leftInfo.absoluteFilePath()).compare(QDir::cleanPath(rightInfo.absoluteFilePath()), Qt::CaseInsensitive) == 0;
}

// -------------------------------------------------------------------------
// PDFBatchAction
// -------------------------------------------------------------------------

PDFBatchAction PDFBatchAction::create(Type type)
{
    PDFBatchAction action;
    action.type = type;

    switch (type)
    {
        case Type::HeaderFooter:
            action.headerFooter.texts[PDFHeaderFooterSettings::FooterCenter] = PDFTranslationContext::tr("Page {page} of {pages}");
            break;

        case Type::BatesNumbering:
            action.headerFooter.texts[PDFHeaderFooterSettings::FooterRight] = QStringLiteral("{bates}");
            break;

        case Type::Watermark:
            action.watermark.text = PDFTranslationContext::tr("CONFIDENTIAL");
            break;

        case Type::RecognizeText:
            action.ocrConfiguration.languages = QStringList({ QStringLiteral("eng") });
            break;

        default:
            break;
    }

    return action;
}

bool PDFBatchAction::isModifying(Type type)
{
    switch (type)
    {
        case Type::ExportWord:
        case Type::ExportText:
        case Type::ExportImages:
            return false;

        default:
            return true;
    }
}

bool PDFBatchAction::hasPages(Type type)
{
    switch (type)
    {
        case Type::HeaderFooter:
        case Type::BatesNumbering:
        case Type::Watermark:
        case Type::RemoveStamps:
        case Type::RotatePages:
        case Type::RecognizeText:
        case Type::ExportWord:
        case Type::ExportText:
        case Type::ExportImages:
            return true;

        default:
            return false;
    }
}

QString PDFBatchAction::getTypeName(Type type)
{
    switch (type)
    {
        case Type::HeaderFooter:
            return PDFTranslationContext::tr("Add Header and Footer");
        case Type::BatesNumbering:
            return PDFTranslationContext::tr("Add Bates Numbers");
        case Type::Watermark:
            return PDFTranslationContext::tr("Add Watermark");
        case Type::RemoveStamps:
            return PDFTranslationContext::tr("Remove Headers, Footers and Watermarks");
        case Type::FlattenForms:
            return PDFTranslationContext::tr("Flatten Form Fields");
        case Type::RotatePages:
            return PDFTranslationContext::tr("Rotate Pages");
        case Type::RecognizeText:
            return PDFTranslationContext::tr("Make Searchable (OCR)");
        case Type::Optimize:
            return PDFTranslationContext::tr("Reduce File Size");
        case Type::Sanitize:
            return PDFTranslationContext::tr("Remove Hidden Information");
        case Type::SetPassword:
            return PDFTranslationContext::tr("Set Password");
        case Type::ExportWord:
            return PDFTranslationContext::tr("Convert to Word");
        case Type::ExportText:
            return PDFTranslationContext::tr("Convert to Text");
        case Type::ExportImages:
            return PDFTranslationContext::tr("Convert to Images");
        case Type::TypeCount:
            break;
    }

    Q_ASSERT(false);
    return QString();
}

QString PDFBatchAction::getTypeId(Type type)
{
    switch (type)
    {
        case Type::HeaderFooter:
            return QStringLiteral("header-footer");
        case Type::BatesNumbering:
            return QStringLiteral("bates");
        case Type::Watermark:
            return QStringLiteral("watermark");
        case Type::RemoveStamps:
            return QStringLiteral("remove-stamps");
        case Type::FlattenForms:
            return QStringLiteral("flatten-forms");
        case Type::RotatePages:
            return QStringLiteral("rotate");
        case Type::RecognizeText:
            return QStringLiteral("ocr");
        case Type::Optimize:
            return QStringLiteral("optimize");
        case Type::Sanitize:
            return QStringLiteral("sanitize");
        case Type::SetPassword:
            return QStringLiteral("password");
        case Type::ExportWord:
            return QStringLiteral("export-word");
        case Type::ExportText:
            return QStringLiteral("export-text");
        case Type::ExportImages:
            return QStringLiteral("export-images");
        case Type::TypeCount:
            break;
    }

    Q_ASSERT(false);
    return QString();
}

std::optional<PDFBatchAction::Type> PDFBatchAction::getTypeFromId(const QString& id)
{
    for (int i = 0; i < int(Type::TypeCount); ++i)
    {
        const Type type = static_cast<Type>(i);
        if (getTypeId(type) == id.trimmed())
        {
            return type;
        }
    }

    return std::nullopt;
}

QString PDFBatchAction::getSummary() const
{
    QString summary;

    switch (type)
    {
        case Type::HeaderFooter:
        {
            QStringList texts;
            for (const QString& text : headerFooter.texts)
            {
                if (!text.isEmpty())
                {
                    texts << QString(text).replace(QChar('\n'), QChar(' '));
                }
            }
            summary = texts.join(QStringLiteral(" | "));
            break;
        }

        case Type::BatesNumbering:
        {
            const PDFStampVariables& variables = headerFooter.variables;
            summary = PDFPageStamper::formatBatesNumber(variables.batesPrefix, variables.batesStart, variables.batesDigits, variables.batesSuffix);
            const PDFHeaderFooterSettings::Position position = PDFBatchProcessorHelper::getBatesPosition(headerFooter);
            if (position != PDFHeaderFooterSettings::PositionCount)
            {
                summary += QStringLiteral(", ") + PDFHeaderFooterSettings::getPositionName(position);
            }
            summary += continueBatesNumbering ? PDFTranslationContext::tr(", continued over the files") : PDFTranslationContext::tr(", restarted in each file");
            break;
        }

        case Type::Watermark:
            summary = watermark.text.isEmpty() ? PDFTranslationContext::tr("Image") : QString(watermark.text).replace(QChar('\n'), QChar(' '));
            summary += PDFTranslationContext::tr(", opacity %1 %").arg(qRound(watermark.style.opacity * 100.0));
            if (watermark.behindContent)
            {
                summary += PDFTranslationContext::tr(", behind the content");
            }
            break;

        case Type::RemoveStamps:
            if (removedStamps == PDFPageStamper::AllKinds)
            {
                summary = PDFTranslationContext::tr("All stamps added by PDF4QT");
            }
            else if (removedStamps.testFlag(PDFPageStamper::HeaderFooter))
            {
                summary = PDFTranslationContext::tr("Headers, footers and Bates numbers");
            }
            else
            {
                summary = PDFTranslationContext::tr("Watermarks");
            }
            break;

        case Type::FlattenForms:
            summary = PDFTranslationContext::tr("Values of the form fields become a part of the pages");
            break;

        case Type::RotatePages:
            summary = rotation == 180 ? PDFTranslationContext::tr("180°") : PDFTranslationContext::tr("%1° clockwise").arg(rotation);
            break;

        case Type::RecognizeText:
        {
            summary = ocrConfiguration.languages.join(QChar('+'));
            switch (ocrConfiguration.profile)
            {
                case PDFOCRModelProfile::Fast:
                    summary += PDFTranslationContext::tr(", fast");
                    break;
                case PDFOCRModelProfile::Standard:
                    summary += PDFTranslationContext::tr(", standard");
                    break;
                case PDFOCRModelProfile::Best:
                    summary += PDFTranslationContext::tr(", best");
                    break;
            }
            break;
        }

        case Type::Optimize:
        {
            QStringList parts;
            if (optimizationFlags != PDFOptimizer::None)
            {
                parts << PDFTranslationContext::tr("objects and streams");
            }
            if (optimizeImages)
            {
                QString images = imageGoal == PDFImageOptimizer::OptimizationGoal::MinimumSize ? PDFTranslationContext::tr("images (smallest size)") : PDFTranslationContext::tr("images (best quality)");
                if (imageTargetDpi > 0)
                {
                    images += PDFTranslationContext::tr(" at %1 DPI").arg(imageTargetDpi);
                }
                parts << images;
            }
            summary = parts.join(QStringLiteral(", "));
            break;
        }

        case Type::Sanitize:
        {
            QStringList parts;
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::DocumentInfo))
            {
                parts << PDFTranslationContext::tr("document information");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::Metadata))
            {
                parts << PDFTranslationContext::tr("metadata");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::Outline))
            {
                parts << PDFTranslationContext::tr("bookmarks");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::FileAttachments))
            {
                parts << PDFTranslationContext::tr("attachments");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::EmbeddedSearchIndex))
            {
                parts << PDFTranslationContext::tr("search index");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::MarkupAnnotations))
            {
                parts << PDFTranslationContext::tr("comments");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::PageThumbnails))
            {
                parts << PDFTranslationContext::tr("thumbnails");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::PageLabels))
            {
                parts << PDFTranslationContext::tr("page labels");
            }
            if (sanitizationFlags.testFlag(PDFDocumentSanitizer::InvisibleText))
            {
                parts << PDFTranslationContext::tr("invisible text");
            }
            summary = parts.join(QStringLiteral(", "));
            break;
        }

        case Type::SetPassword:
            if (userPassword.isEmpty() && ownerPassword.isEmpty())
            {
                summary = PDFTranslationContext::tr("Enter the password");
            }
            else if (!userPassword.isEmpty())
            {
                summary = PDFTranslationContext::tr("Password to open the document, AES 256");
            }
            else
            {
                summary = PDFTranslationContext::tr("Password to change the document, AES 256");
            }
            break;

        case Type::ExportWord:
            summary = wordIncludeGraphics ? PDFTranslationContext::tr("DOCX, graphics at %1 DPI").arg(wordGraphicsResolution) : PDFTranslationContext::tr("DOCX, text only");
            break;

        case Type::ExportText:
            summary = PDFTranslationContext::tr("Text file (UTF-8)");
            break;

        case Type::ExportImages:
            summary = PDFTranslationContext::tr("%1, %2 DPI, a file for each page").arg(imageFormat.toUpper()).arg(imageResolution);
            break;

        case Type::TypeCount:
            Q_ASSERT(false);
            break;
    }

    if (hasPages(type) && !pages.trimmed().isEmpty())
    {
        summary += PDFTranslationContext::tr(" (pages %1)").arg(pages.trimmed());
    }

    return summary;
}

std::vector<PDFInteger> PDFBatchAction::getPageIndices(PDFInteger pageCount, QString* errorMessage) const
{
    std::vector<PDFInteger> indices;
    const QString selection = pages.trimmed();

    if (!hasPages(type) || selection.isEmpty())
    {
        indices.resize(pageCount);
        std::iota(indices.begin(), indices.end(), PDFInteger(0));
        return indices;
    }

    QString parseError;
    PDFClosedIntervalSet intervals = PDFClosedIntervalSet::parse(1, qMax(pageCount, PDFInteger(1)), selection, &parseError);
    if (!parseError.isEmpty() || intervals.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = parseError.isEmpty() ? PDFTranslationContext::tr("No page is selected by '%1'.").arg(selection) : parseError;
        }
        return indices;
    }

    intervals.translate(-1);
    indices = intervals.unfold();

    auto invalidIt = std::find_if(indices.cbegin(), indices.cend(), [pageCount](PDFInteger index) { return index < 0 || index >= pageCount; });
    if (invalidIt != indices.cend())
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 doesn't exist, the document has %2 pages.").arg(*invalidIt + 1).arg(pageCount);
        }
        indices.clear();
    }

    return indices;
}

QStringList PDFBatchAction::validate() const
{
    QStringList errors;
    const QString name = getName();

    auto addError = [&errors, &name](const QString& text)
    {
        errors << QStringLiteral("%1: %2").arg(name, text);
    };

    if (hasPages(type) && !pages.trimmed().isEmpty())
    {
        QString parseError;
        PDFClosedIntervalSet::parse(1, std::numeric_limits<int>::max(), pages.trimmed(), &parseError);
        if (!parseError.isEmpty())
        {
            addError(PDFTranslationContext::tr("invalid pages '%1'. %2").arg(pages.trimmed(), parseError));
        }
    }

    switch (type)
    {
        case Type::HeaderFooter:
        case Type::BatesNumbering:
            if (headerFooter.isEmpty())
            {
                addError(PDFTranslationContext::tr("enter a text of a header or of a footer."));
            }
            else if (type == Type::BatesNumbering && !PDFBatchProcessorHelper::hasBatesVariable(headerFooter))
            {
                addError(PDFTranslationContext::tr("the text must contain the variable {bates}."));
            }
            if (headerFooter.variables.batesDigits < 1 || headerFooter.variables.batesDigits > 15)
            {
                addError(PDFTranslationContext::tr("the Bates number must have 1 to 15 digits."));
            }
            if (headerFooter.variables.batesStart < 0)
            {
                addError(PDFTranslationContext::tr("the first Bates number must not be negative."));
            }
            break;

        case Type::Watermark:
            if (watermark.text.trimmed().isEmpty() && watermark.image.isNull())
            {
                addError(PDFTranslationContext::tr("enter a text or select an image."));
            }
            if (watermark.relativeSize <= 0.0 || watermark.relativeSize > 1.0)
            {
                addError(PDFTranslationContext::tr("the size must be from 1 % to 100 % of the page."));
            }
            break;

        case Type::RemoveStamps:
            if (!(removedStamps & PDFPageStamper::AllKinds))
            {
                addError(PDFTranslationContext::tr("select the stamps to remove."));
            }
            break;

        case Type::RotatePages:
            if (rotation != 90 && rotation != 180 && rotation != 270)
            {
                addError(PDFTranslationContext::tr("the angle must be 90, 180 or 270 degrees."));
            }
            break;

        case Type::RecognizeText:
            if (ocrConfiguration.languages.isEmpty())
            {
                addError(PDFTranslationContext::tr("select the languages of the text."));
            }
            if (ocrConfiguration.existingTextPolicy == PDFOCRExistingTextPolicy::ReviewOnly)
            {
                addError(PDFTranslationContext::tr("the recognition only for the review writes nothing."));
            }
            for (const QString& error : ocrConfiguration.validate())
            {
                addError(error);
            }
            break;

        case Type::Optimize:
            if (optimizationFlags == PDFOptimizer::None && !optimizeImages)
            {
                addError(PDFTranslationContext::tr("select what to optimize."));
            }
            if (imageTargetDpi < 0 || imageTargetDpi > 2400)
            {
                addError(PDFTranslationContext::tr("the resolution of the images must be from 0 to 2400 DPI."));
            }
            break;

        case Type::Sanitize:
            if (sanitizationFlags == PDFDocumentSanitizer::None)
            {
                addError(PDFTranslationContext::tr("select the information to remove."));
            }
            break;

        case Type::SetPassword:
        {
            if (userPassword.isEmpty() && ownerPassword.isEmpty())
            {
                addError(PDFTranslationContext::tr("enter a password (passwords are not stored in saved sequences)."));
                break;
            }

            PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = PDFSecurityHandlerFactory::AES_256;
            settings.userPassword = userPassword;
            settings.ownerPassword = ownerPassword.isEmpty() ? userPassword : ownerPassword;
            QString errorMessage;
            if (!PDFSecurityHandlerFactory::validate(settings, &errorMessage))
            {
                addError(errorMessage);
            }
            break;
        }

        case Type::ExportWord:
            if (wordGraphicsResolution < 36 || wordGraphicsResolution > 600)
            {
                addError(PDFTranslationContext::tr("the resolution must be from 36 to 600 DPI."));
            }
            break;

        case Type::ExportImages:
            if (!QImageWriter::supportedImageFormats().contains(imageFormat.toLatin1().toLower()))
            {
                addError(PDFTranslationContext::tr("the image format '%1' is not supported.").arg(imageFormat));
            }
            if (imageResolution < 10 || imageResolution > 1200)
            {
                addError(PDFTranslationContext::tr("the resolution must be from 10 to 1200 DPI."));
            }
            break;

        case Type::FlattenForms:
        case Type::ExportText:
            break;

        case Type::TypeCount:
            addError(PDFTranslationContext::tr("unknown action."));
            break;
    }

    return errors;
}

QJsonObject PDFBatchAction::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("type")] = getTypeId(type);

    if (hasPages(type))
    {
        object[QStringLiteral("pages")] = pages;
    }

    switch (type)
    {
        case Type::HeaderFooter:
        case Type::BatesNumbering:
        {
            QJsonObject texts;
            for (int i = 0; i < PDFHeaderFooterSettings::PositionCount; ++i)
            {
                if (!headerFooter.texts[i].isEmpty())
                {
                    texts[PDFHeaderFooterSettings::getPositionName(static_cast<PDFHeaderFooterSettings::Position>(i))] = headerFooter.texts[i];
                }
            }
            object[QStringLiteral("texts")] = texts;
            object[QStringLiteral("style")] = PDFBatchProcessorHelper::textStyleToJson(headerFooter.style);
            object[QStringLiteral("margins")] = PDFBatchProcessorHelper::marginsToJson(headerFooter.margins);
            object[QStringLiteral("variables")] = PDFBatchProcessorHelper::variablesToJson(headerFooter.variables);
            object[QStringLiteral("replaceExisting")] = replaceExisting;
            object[QStringLiteral("continueBatesNumbering")] = continueBatesNumbering;
            break;
        }

        case Type::Watermark:
            object[QStringLiteral("text")] = watermark.text;
            if (watermark.text.isEmpty() && !watermark.image.isNull())
            {
                object[QStringLiteral("image")] = PDFBatchProcessorHelper::imageToBase64(watermark.image);
            }
            object[QStringLiteral("style")] = PDFBatchProcessorHelper::textStyleToJson(watermark.style);
            object[QStringLiteral("rotation")] = watermark.rotation;
            object[QStringLiteral("relativeSize")] = watermark.relativeSize;
            object[QStringLiteral("alignment")] = PDFBatchProcessorHelper::getAlignmentId(watermark.alignment);
            object[QStringLiteral("margins")] = PDFBatchProcessorHelper::marginsToJson(watermark.margins);
            object[QStringLiteral("offset")] = QJsonArray({ watermark.offset.x(), watermark.offset.y() });
            object[QStringLiteral("behindContent")] = watermark.behindContent;
            object[QStringLiteral("variables")] = PDFBatchProcessorHelper::variablesToJson(watermark.variables);
            object[QStringLiteral("replaceExisting")] = replaceExisting;
            break;

        case Type::RemoveStamps:
            object[QStringLiteral("headerFooter")] = removedStamps.testFlag(PDFPageStamper::HeaderFooter);
            object[QStringLiteral("watermark")] = removedStamps.testFlag(PDFPageStamper::Watermark);
            break;

        case Type::RotatePages:
            object[QStringLiteral("rotation")] = rotation;
            break;

        case Type::RecognizeText:
            object[QStringLiteral("configuration")] = ocrConfiguration.toJson();
            object[QStringLiteral("decisionPolicy")] = int(ocrDecisionPolicy);
            object[QStringLiteral("allowPageErrors")] = ocrAllowPageErrors;
            break;

        case Type::Optimize:
            object[QStringLiteral("flags")] = int(optimizationFlags);
            object[QStringLiteral("images")] = optimizeImages;
            object[QStringLiteral("imageGoal")] = imageGoal == PDFImageOptimizer::OptimizationGoal::MinimumSize ? QStringLiteral("size") : QStringLiteral("quality");
            object[QStringLiteral("imageDpi")] = imageTargetDpi;
            break;

        case Type::Sanitize:
            object[QStringLiteral("flags")] = int(sanitizationFlags);
            break;

        case Type::ExportWord:
            object[QStringLiteral("graphics")] = wordIncludeGraphics;
            object[QStringLiteral("resolution")] = wordGraphicsResolution;
            break;

        case Type::ExportImages:
            object[QStringLiteral("format")] = imageFormat;
            object[QStringLiteral("resolution")] = imageResolution;
            break;

        case Type::FlattenForms:
        case Type::SetPassword:
        case Type::ExportText:
        case Type::TypeCount:
            break;
    }

    return object;
}

PDFBatchAction PDFBatchAction::fromJson(const QJsonObject& object, QString* errorMessage)
{
    const QString typeId = object.value(QStringLiteral("type")).toString();
    std::optional<Type> type = getTypeFromId(typeId);
    if (!type)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Unknown action '%1'.").arg(typeId);
        }
        PDFBatchAction action;
        action.type = Type::TypeCount;
        return action;
    }

    PDFBatchAction action = create(*type);
    action.pages = object.value(QStringLiteral("pages")).toString();
    action.replaceExisting = object.value(QStringLiteral("replaceExisting")).toBool(action.replaceExisting);

    switch (action.type)
    {
        case Type::HeaderFooter:
        case Type::BatesNumbering:
        {
            if (object.contains(QStringLiteral("texts")))
            {
                action.headerFooter.texts = { };
                const QJsonObject texts = object.value(QStringLiteral("texts")).toObject();
                for (auto it = texts.constBegin(); it != texts.constEnd(); ++it)
                {
                    const PDFHeaderFooterSettings::Position position = PDFHeaderFooterSettings::getPositionFromName(it.key());
                    if (position != PDFHeaderFooterSettings::PositionCount)
                    {
                        action.headerFooter.texts[position] = it.value().toString();
                    }
                }
            }
            PDFBatchProcessorHelper::textStyleFromJson(object.value(QStringLiteral("style")).toObject(), action.headerFooter.style);
            PDFBatchProcessorHelper::marginsFromJson(object.value(QStringLiteral("margins")), action.headerFooter.margins);
            PDFBatchProcessorHelper::variablesFromJson(object.value(QStringLiteral("variables")).toObject(), action.headerFooter.variables);
            action.continueBatesNumbering = object.value(QStringLiteral("continueBatesNumbering")).toBool(action.continueBatesNumbering);
            break;
        }

        case Type::Watermark:
        {
            action.watermark.text = object.value(QStringLiteral("text")).toString(action.watermark.text);
            action.watermark.image = PDFBatchProcessorHelper::imageFromBase64(object.value(QStringLiteral("image")).toString());
            PDFBatchProcessorHelper::textStyleFromJson(object.value(QStringLiteral("style")).toObject(), action.watermark.style);
            action.watermark.rotation = object.value(QStringLiteral("rotation")).toDouble(action.watermark.rotation);
            action.watermark.relativeSize = object.value(QStringLiteral("relativeSize")).toDouble(action.watermark.relativeSize);
            action.watermark.alignment = PDFBatchProcessorHelper::getAlignmentFromId(object.value(QStringLiteral("alignment")).toString(), action.watermark.alignment);
            PDFBatchProcessorHelper::marginsFromJson(object.value(QStringLiteral("margins")), action.watermark.margins);
            const QJsonArray offset = object.value(QStringLiteral("offset")).toArray();
            if (offset.size() == 2)
            {
                action.watermark.offset = QPointF(offset[0].toDouble(), offset[1].toDouble());
            }
            action.watermark.behindContent = object.value(QStringLiteral("behindContent")).toBool(action.watermark.behindContent);
            PDFBatchProcessorHelper::variablesFromJson(object.value(QStringLiteral("variables")).toObject(), action.watermark.variables);
            break;
        }

        case Type::RemoveStamps:
        {
            PDFPageStamper::StampKinds kinds = PDFPageStamper::None;
            if (object.value(QStringLiteral("headerFooter")).toBool(true))
            {
                kinds |= PDFPageStamper::HeaderFooter;
            }
            if (object.value(QStringLiteral("watermark")).toBool(true))
            {
                kinds |= PDFPageStamper::Watermark;
            }
            action.removedStamps = kinds;
            break;
        }

        case Type::RotatePages:
            action.rotation = object.value(QStringLiteral("rotation")).toInt(action.rotation);
            break;

        case Type::RecognizeText:
            if (object.contains(QStringLiteral("configuration")))
            {
                action.ocrConfiguration = PDFOCRConfiguration::fromJson(object.value(QStringLiteral("configuration")).toObject());
            }
            action.ocrDecisionPolicy = PDFOCRDocumentRunner::DecisionPolicy(qBound(0, object.value(QStringLiteral("decisionPolicy")).toInt(int(action.ocrDecisionPolicy)), int(PDFOCRDocumentRunner::DecisionPolicy::ReviewOnly)));
            action.ocrAllowPageErrors = object.value(QStringLiteral("allowPageErrors")).toBool(action.ocrAllowPageErrors);
            break;

        case Type::Optimize:
            action.optimizationFlags = PDFOptimizer::OptimizationFlags(object.value(QStringLiteral("flags")).toInt(int(action.optimizationFlags)));
            action.optimizeImages = object.value(QStringLiteral("images")).toBool(action.optimizeImages);
            action.imageGoal = object.value(QStringLiteral("imageGoal")).toString() == QLatin1String("size") ? PDFImageOptimizer::OptimizationGoal::MinimumSize : PDFImageOptimizer::OptimizationGoal::PreferQuality;
            action.imageTargetDpi = object.value(QStringLiteral("imageDpi")).toInt(action.imageTargetDpi);
            break;

        case Type::Sanitize:
            action.sanitizationFlags = PDFDocumentSanitizer::SanitizationFlags(object.value(QStringLiteral("flags")).toInt(int(action.sanitizationFlags)));
            break;

        case Type::ExportWord:
            action.wordIncludeGraphics = object.value(QStringLiteral("graphics")).toBool(action.wordIncludeGraphics);
            action.wordGraphicsResolution = object.value(QStringLiteral("resolution")).toInt(action.wordGraphicsResolution);
            break;

        case Type::ExportImages:
            action.imageFormat = object.value(QStringLiteral("format")).toString(action.imageFormat).toLower();
            action.imageResolution = object.value(QStringLiteral("resolution")).toInt(action.imageResolution);
            break;

        case Type::FlattenForms:
        case Type::SetPassword:
        case Type::ExportText:
        case Type::TypeCount:
            break;
    }

    return action;
}

// -------------------------------------------------------------------------
// PDFBatchSettings
// -------------------------------------------------------------------------

bool PDFBatchSettings::writesDocument() const
{
    return std::any_of(actions.cbegin(), actions.cend(), [](const PDFBatchAction& action) { return action.isModifying(); });
}

QStringList PDFBatchSettings::validate() const
{
    QStringList errors;

    if (actions.empty())
    {
        errors << PDFTranslationContext::tr("Add at least one action.");
    }

    bool passwordSet = false;
    std::vector<PDFBatchAction::Type> exports;
    for (const PDFBatchAction& action : actions)
    {
        errors << action.validate();

        if (passwordSet && action.isModifying())
        {
            errors << PDFTranslationContext::tr("'%1' must be the last action, which changes the document.").arg(PDFBatchAction::getTypeName(PDFBatchAction::Type::SetPassword));
            passwordSet = false;
        }
        if (action.type == PDFBatchAction::Type::SetPassword)
        {
            passwordSet = true;
        }

        if (!action.isModifying())
        {
            if (std::find(exports.cbegin(), exports.cend(), action.type) != exports.cend())
            {
                errors << PDFTranslationContext::tr("'%1' is in the sequence more than once, it would write the same files.").arg(action.getName());
            }
            exports.push_back(action.type);
        }
    }

    switch (outputLocation)
    {
        case OutputLocation::Folder:
            if (outputDirectory.trimmed().isEmpty())
            {
                errors << PDFTranslationContext::tr("Select the output folder.");
            }
            break;

        case OutputLocation::NextToOriginal:
            if (suffix.isEmpty())
            {
                errors << PDFTranslationContext::tr("Enter a suffix of the file names, the original files are never overwritten.");
            }
            break;
    }

    static const QString invalidCharacters = QStringLiteral("/\\:*?\"<>|");
    if (std::any_of(suffix.cbegin(), suffix.cend(), [](QChar character) { return invalidCharacters.contains(character) || character.unicode() < 32; }))
    {
        errors << PDFTranslationContext::tr("The suffix of the file names must not contain any of the characters %1.").arg(invalidCharacters);
    }

    return errors;
}

QJsonObject PDFBatchSettings::toJson() const
{
    QJsonArray actionArray;
    for (const PDFBatchAction& action : actions)
    {
        actionArray.append(action.toJson());
    }

    QJsonObject output;
    output[QStringLiteral("location")] = outputLocation == OutputLocation::Folder ? QStringLiteral("folder") : QStringLiteral("original");
    output[QStringLiteral("folder")] = outputDirectory;
    output[QStringLiteral("suffix")] = suffix;
    switch (existingFilePolicy)
    {
        case ExistingFilePolicy::Rename:
            output[QStringLiteral("existing")] = QStringLiteral("rename");
            break;
        case ExistingFilePolicy::Overwrite:
            output[QStringLiteral("existing")] = QStringLiteral("overwrite");
            break;
        case ExistingFilePolicy::Skip:
            output[QStringLiteral("existing")] = QStringLiteral("skip");
            break;
    }

    QJsonObject object;
    object[QStringLiteral("version")] = PDFBatchProcessorHelper::SEQUENCE_VERSION;
    object[QStringLiteral("actions")] = actionArray;
    object[QStringLiteral("output")] = output;
    return object;
}

PDFBatchSettings PDFBatchSettings::fromJson(const QJsonObject& object, QString* errorMessage)
{
    PDFBatchSettings settings;
    QStringList errors;

    const QJsonArray actionArray = object.value(QStringLiteral("actions")).toArray();
    for (const QJsonValue& value : actionArray)
    {
        QString actionError;
        PDFBatchAction action = PDFBatchAction::fromJson(value.toObject(), &actionError);
        if (!actionError.isEmpty())
        {
            errors << actionError;
            continue;
        }
        settings.actions.push_back(std::move(action));
    }

    if (object.contains(QStringLiteral("output")))
    {
        const QJsonObject output = object.value(QStringLiteral("output")).toObject();
        settings.outputLocation = output.value(QStringLiteral("location")).toString() == QLatin1String("original") ? OutputLocation::NextToOriginal : OutputLocation::Folder;
        settings.outputDirectory = output.value(QStringLiteral("folder")).toString();
        settings.suffix = output.value(QStringLiteral("suffix")).toString(settings.suffix);

        const QString existing = output.value(QStringLiteral("existing")).toString();
        if (existing == QLatin1String("overwrite"))
        {
            settings.existingFilePolicy = ExistingFilePolicy::Overwrite;
        }
        else if (existing == QLatin1String("skip"))
        {
            settings.existingFilePolicy = ExistingFilePolicy::Skip;
        }
        else
        {
            settings.existingFilePolicy = ExistingFilePolicy::Rename;
        }
    }

    if (errorMessage)
    {
        *errorMessage = errors.join(QChar('\n'));
    }

    return settings;
}

// -------------------------------------------------------------------------
// PDFBatchProcessor
// -------------------------------------------------------------------------

struct PDFBatchProcessor::Context
{
    QString inputFile;
    PDFDocument document;

    /// Output base name (without the extension) after the policy of the existing files
    QString baseName;

    /// Files written for the current original (removed, when the original fails)
    QStringList writtenFiles;

    QStringList details;
};

PDFBatchProcessor::PDFBatchProcessor(PDFBatchSettings settings) :
    m_settings(std::move(settings))
{

}

PDFBatchProcessor::~PDFBatchProcessor()
{

}

QStringList PDFBatchProcessor::validate() const
{
    QStringList errors = m_settings.validate();

    for (const PDFBatchAction& action : m_settings.actions)
    {
        if (action.type != PDFBatchAction::Type::RecognizeText)
        {
            continue;
        }

        const PDFOCRConfiguration& configuration = action.ocrConfiguration;
        std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
        QString reason;
        if (!factory || !factory->isAvailable(&reason))
        {
            errors << PDFTranslationContext::tr("The OCR engine '%1' is not available. %2").arg(configuration.engineId, reason).trimmed();
        }
        else if (factory->usesManagedModels() && !configuration.languages.isEmpty())
        {
            PDFOCRModelManager* modelManager = const_cast<PDFBatchProcessor*>(this)->getModelManager();
            const QStringList missing = modelManager->getMissingModels(configuration.engineId, configuration.languages, configuration.profile);
            if (!missing.isEmpty())
            {
                errors << PDFTranslationContext::tr("The language models are not installed: %1.").arg(missing.join(QStringLiteral(", ")));
            }
        }
    }

    return errors;
}

PDFOCRModelManager* PDFBatchProcessor::getModelManager()
{
    if (m_modelManager)
    {
        return m_modelManager;
    }

    if (!m_ownModelManager)
    {
        m_ownModelManager = std::make_unique<PDFOCRModelManager>(nullptr);
        m_ownModelManager->loadBundledCatalog();
        m_ownModelManager->refresh();
    }

    return m_ownModelManager.get();
}

QString PDFBatchProcessor::getOutputBaseName(const QString& inputFile) const
{
    const QFileInfo inputInfo(inputFile);
    const QDir directory = m_settings.outputLocation == PDFBatchSettings::OutputLocation::NextToOriginal ? inputInfo.absoluteDir() : QDir(m_settings.outputDirectory);
    return directory.absoluteFilePath(inputInfo.completeBaseName() + m_settings.suffix);
}

QString PDFBatchProcessor::getExportExtension(const PDFBatchAction& action)
{
    switch (action.type)
    {
        case PDFBatchAction::Type::ExportWord:
            return QStringLiteral(".docx");
        case PDFBatchAction::Type::ExportText:
            return QStringLiteral(".txt");
        case PDFBatchAction::Type::ExportImages:
            return QStringLiteral("-001.") + action.imageFormat.toLower();
        default:
            return QStringLiteral(".pdf");
    }
}

QStringList PDFBatchProcessor::getOutputFiles(const QString& inputFile) const
{
    QStringList files;
    const QString baseName = getOutputBaseName(inputFile);

    if (m_settings.writesDocument())
    {
        files << baseName + QStringLiteral(".pdf");
    }

    for (const PDFBatchAction& action : m_settings.actions)
    {
        if (!action.isModifying())
        {
            files << baseName + getExportExtension(action);
        }
    }

    return files;
}

QString PDFBatchProcessor::getUniqueFileName(const QString& fileName, const QStringList& reserved)
{
    auto isFree = [&reserved](const QString& name)
    {
        return !QFileInfo::exists(name) && !reserved.contains(name, Qt::CaseInsensitive);
    };

    if (isFree(fileName))
    {
        return fileName;
    }

    const QFileInfo info(fileName);
    const QString suffix = info.suffix();
    const QString base = info.absoluteDir().absoluteFilePath(info.completeBaseName());

    for (int i = 2; ; ++i)
    {
        QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(i);
        if (!suffix.isEmpty())
        {
            candidate += QChar('.') + suffix;
        }

        if (isFree(candidate))
        {
            return candidate;
        }
    }
}

QStringList PDFBatchProcessor::findPdfFiles(const QString& directory, bool recursive)
{
    QStringList files;
    QDirIterator iterator(directory, QStringList({ QStringLiteral("*.pdf"), QStringLiteral("*.PDF"), QStringLiteral("*.Pdf") }), QDir::Files | QDir::Readable,
                          recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (iterator.hasNext())
    {
        const QString file = QDir::cleanPath(iterator.next());
        if (!files.contains(file, Qt::CaseInsensitive))
        {
            files << file;
        }
    }

    std::sort(files.begin(), files.end(), &PDFBatchProcessorHelper::isNaturallyLess);
    return files;
}

QString PDFBatchProcessor::getStatusName(FileResult::Status status)
{
    switch (status)
    {
        case FileResult::Status::Succeeded:
            return PDFTranslationContext::tr("Done");
        case FileResult::Status::Skipped:
            return PDFTranslationContext::tr("Skipped");
        case FileResult::Status::Failed:
            return PDFTranslationContext::tr("Failed");
        case FileResult::Status::Cancelled:
            return PDFTranslationContext::tr("Stopped");
    }

    return QString();
}

PDFBatchProcessor::FileResult PDFBatchProcessor::processFile(const QString& inputFile, const ProgressCallback& progress, const PDFOperationControl* operationControl)
{
    QElapsedTimer timer;
    timer.start();

    Context context;
    context.inputFile = inputFile;

    FileResult result;
    try
    {
        result = processFileImpl(inputFile, progress, operationControl, context);
    }
    catch (const PDFException& exception)
    {
        result = FileResult();
        result.status = FileResult::Status::Failed;
        result.message = exception.getMessage();
    }
    catch (const std::exception& exception)
    {
        result = FileResult();
        result.status = FileResult::Status::Failed;
        result.message = QString::fromLocal8Bit(exception.what());
    }
    catch (...)
    {
        result = FileResult();
        result.status = FileResult::Status::Failed;
        result.message = PDFTranslationContext::tr("Unknown error.");
    }

    // A file, which was not processed completely, leaves no output
    if (result.status != FileResult::Status::Succeeded)
    {
        for (const QString& file : std::as_const(context.writtenFiles))
        {
            QFile::remove(file);
        }
        result.outputFiles.clear();
    }

    result.inputFile = inputFile;
    result.details = context.details + result.details;
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}

PDFBatchProcessor::FileResult PDFBatchProcessor::processFileImpl(const QString& inputFile, const ProgressCallback& progress, const PDFOperationControl* operationControl, Context& context)
{
    FileResult result;
    result.status = FileResult::Status::Failed;

    auto isCancelled = [operationControl]()
    {
        return operationControl && operationControl->isOperationCancelled();
    };

    auto cancelled = [&result]()
    {
        result.status = FileResult::Status::Cancelled;
        result.message = PDFTranslationContext::tr("Stopped by the user, nothing was written.");
        return result;
    };

    if (isCancelled())
    {
        return cancelled();
    }

    const QStringList settingsErrors = m_settings.validate();
    if (!settingsErrors.isEmpty())
    {
        result.message = settingsErrors.join(QChar('\n'));
        return result;
    }

    if (!QFileInfo::exists(inputFile))
    {
        result.message = PDFTranslationContext::tr("The file '%1' does not exist.").arg(QDir::toNativeSeparators(inputFile));
        return result;
    }

    // Output files: the originals are never overwritten, the policy of the existing files is applied
    if (m_settings.outputLocation == PDFBatchSettings::OutputLocation::Folder)
    {
        if (!QDir().mkpath(m_settings.outputDirectory))
        {
            result.message = PDFTranslationContext::tr("The output folder '%1' cannot be created.").arg(QDir::toNativeSeparators(m_settings.outputDirectory));
            return result;
        }
    }

    const QStringList plannedFiles = getOutputFiles(inputFile);
    for (const QString& file : plannedFiles)
    {
        if (PDFBatchProcessorHelper::isSameFile(file, inputFile))
        {
            result.message = PDFTranslationContext::tr("The output would overwrite the original file '%1'.").arg(QDir::toNativeSeparators(inputFile));
            return result;
        }
    }

    context.baseName = getOutputBaseName(inputFile);
    const bool anyExists = std::any_of(plannedFiles.cbegin(), plannedFiles.cend(), [](const QString& file) { return QFileInfo::exists(file); });
    if (anyExists)
    {
        switch (m_settings.existingFilePolicy)
        {
            case PDFBatchSettings::ExistingFilePolicy::Skip:
                result.status = FileResult::Status::Skipped;
                result.message = PDFTranslationContext::tr("The output files already exist.");
                return result;

            case PDFBatchSettings::ExistingFilePolicy::Overwrite:
                break;

            case PDFBatchSettings::ExistingFilePolicy::Rename:
            {
                // All outputs of the original get the same new name
                for (int i = 2; ; ++i)
                {
                    const QString candidate = QStringLiteral("%1 (%2)").arg(getOutputBaseName(inputFile)).arg(i);
                    const QString originalBase = getOutputBaseName(inputFile);
                    const bool free = std::none_of(plannedFiles.cbegin(), plannedFiles.cend(), [&](const QString& file) { return QFileInfo::exists(candidate + file.mid(originalBase.size())); });
                    if (free)
                    {
                        context.baseName = candidate;
                        break;
                    }
                }
                break;
            }
        }
    }

    // Reading of the document (the password is tried only once)
    bool isFirstPasswordAttempt = true;
    const QString password = m_settings.password;
    auto passwordCallback = [&password, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt && !password.isEmpty();
        isFirstPasswordAttempt = false;
        return password;
    };

    PDFDocumentReader reader(nullptr, passwordCallback, true, false);
    context.document = reader.readFromFile(inputFile);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        const QString reason = reader.getReadingResult() == PDFDocumentReader::Result::Cancelled ? PDFTranslationContext::tr("The document is protected by a password.") : reader.getErrorMessage();
        result.message = PDFTranslationContext::tr("The document cannot be read. %1").arg(reason);
        return result;
    }

    result.pageCount = int(context.document.getCatalog()->getPageCount());

    const PDFSecurityHandler* securityHandler = context.document.getStorage().getSecurityHandler();
    if (m_settings.writesDocument() && securityHandler && !securityHandler->isAllowed(PDFSecurityHandler::Permission::Modify))
    {
        result.status = FileResult::Status::Skipped;
        result.message = PDFTranslationContext::tr("The document does not allow changes. Enter the owner password of the document.");
        return result;
    }

    if (m_settings.writesDocument() && PDFOCRApplyProcessor::hasSignatureFields(&context.document))
    {
        context.details << PDFTranslationContext::tr("The signatures of the document are not valid in the processed copy.");
    }

    const int stepCount = int(m_settings.actions.size());
    for (size_t i = 0; i < m_settings.actions.size(); ++i)
    {
        const PDFBatchAction& action = m_settings.actions[i];

        if (isCancelled())
        {
            return cancelled();
        }

        if (progress)
        {
            progress(int(i), stepCount, action.getName());
        }

        const QString error = action.isModifying() ? applyStep(i, action, context, operationControl) : exportStep(action, context, operationControl);

        if (isCancelled())
        {
            return cancelled();
        }

        if (!error.isEmpty())
        {
            result.message = QStringLiteral("%1: %2").arg(action.getName(), error);
            return result;
        }
    }

    if (m_settings.writesDocument())
    {
        if (progress)
        {
            progress(stepCount, stepCount, PDFTranslationContext::tr("Writing"));
        }

        const QString fileName = context.baseName + QStringLiteral(".pdf");
        PDFDocumentWriter writer(nullptr);
        PDFOperationResult writeResult = writer.write(fileName, &context.document, true);
        if (!writeResult)
        {
            result.message = PDFTranslationContext::tr("The file '%1' cannot be written. %2").arg(QDir::toNativeSeparators(fileName), writeResult.getErrorMessage());
            return result;
        }

        // The document is listed in front of the exports
        context.writtenFiles.prepend(fileName);
    }

    result.status = FileResult::Status::Succeeded;
    result.outputFiles = context.writtenFiles;
    result.message = PDFTranslationContext::tr("%n file(s) written.", nullptr, int(context.writtenFiles.size()));
    return result;
}

QString PDFBatchProcessor::applyStep(size_t index, const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl)
{
    PDFDocument& document = context.document;
    const PDFInteger pageCount = PDFInteger(document.getCatalog()->getPageCount());

    QString pageError;
    const std::vector<PDFInteger> pageIndices = action.getPageIndices(pageCount, &pageError);
    if (!pageError.isEmpty())
    {
        return pageError;
    }

    const QString fileName = QFileInfo(context.inputFile).fileName();

    switch (action.type)
    {
        case PDFBatchAction::Type::HeaderFooter:
        case PDFBatchAction::Type::BatesNumbering:
        {
            PDFHeaderFooterSettings settings = action.headerFooter;
            if (settings.variables.fileName.isEmpty())
            {
                settings.variables.fileName = fileName;
            }
            settings.variables.dateTime = QDateTime::currentDateTime();

            auto it = m_nextBatesNumbers.find(index);
            if (action.continueBatesNumbering && it != m_nextBatesNumbers.end())
            {
                settings.variables.batesStart = it->second;
            }

            const PDFPageStamper::Result stampResult = PDFPageStamper::addHeaderFooter(&document, settings, pageIndices, action.replaceExisting);
            if (!stampResult.isSuccess())
            {
                return stampResult.errorMessage;
            }

            if (stampResult.document)
            {
                document = *stampResult.document;
            }

            if (!stampResult.firstBatesNumber.isEmpty())
            {
                context.details << PDFTranslationContext::tr("Bates numbers %1 to %2.").arg(stampResult.firstBatesNumber, stampResult.lastBatesNumber);
                if (action.continueBatesNumbering)
                {
                    m_nextBatesNumbers[index] = stampResult.nextBatesNumber;
                }
            }
            else
            {
                context.details << PDFTranslationContext::tr("Header and footer added to %n page(s).", nullptr, int(stampResult.pageCount));
            }
            return QString();
        }

        case PDFBatchAction::Type::Watermark:
        {
            PDFWatermarkSettings settings = action.watermark;
            if (settings.variables.fileName.isEmpty())
            {
                settings.variables.fileName = fileName;
            }
            settings.variables.dateTime = QDateTime::currentDateTime();

            const PDFPageStamper::Result stampResult = PDFPageStamper::addWatermark(&document, settings, pageIndices, action.replaceExisting);
            if (!stampResult.isSuccess())
            {
                return stampResult.errorMessage;
            }

            if (stampResult.document)
            {
                document = *stampResult.document;
            }
            context.details << PDFTranslationContext::tr("Watermark added to %n page(s).", nullptr, int(stampResult.pageCount));
            return QString();
        }

        case PDFBatchAction::Type::RemoveStamps:
        {
            const PDFPageStamper::Result stampResult = PDFPageStamper::removeStamps(&document, action.removedStamps, pageIndices);
            if (!stampResult.isSuccess())
            {
                return stampResult.errorMessage;
            }

            if (stampResult.document)
            {
                document = *stampResult.document;
            }
            context.details << PDFTranslationContext::tr("%n stamp(s) removed.", nullptr, int(stampResult.removedStampCount));
            return QString();
        }

        case PDFBatchAction::Type::FlattenForms:
        {
            PDFFormManager formManager(nullptr);
            formManager.setAppearanceFlags(PDFFormManager::FormAppearanceFlags());
            formManager.setDocument(PDFModifiedDocument(&document, nullptr));
            if (!formManager.hasAcroForm())
            {
                context.details << PDFTranslationContext::tr("The document has no form fields.");
                return QString();
            }

            const size_t widgetCount = formManager.getWidgets().size();
            PDFDocument flattened = formManager.createFlattenedDocument();
            formManager.setDocument(PDFModifiedDocument());
            document = std::move(flattened);
            context.details << PDFTranslationContext::tr("%n form field widget(s) flattened.", nullptr, int(widgetCount));
            return QString();
        }

        case PDFBatchAction::Type::RotatePages:
        {
            const int steps = action.rotation == 180 ? 2 : 1;
            const bool clockwise = action.rotation != 270;
            for (int i = 0; i < steps; ++i)
            {
                PDFOperationResult rotateResult = PDFPageOrganizer::rotatePages(&document, pageIndices, clockwise);
                if (!rotateResult)
                {
                    return rotateResult.getErrorMessage();
                }
            }
            context.details << PDFTranslationContext::tr("%n page(s) rotated.", nullptr, int(pageIndices.size()));
            return QString();
        }

        case PDFBatchAction::Type::RecognizeText:
            return recognizeText(action, context, operationControl);

        case PDFBatchAction::Type::Optimize:
        {
            if (action.optimizeImages)
            {
                PDFImageOptimizer::Settings settings = PDFImageOptimizer::Settings::createDefault();
                settings.enabled = true;
                settings.goal = action.imageGoal;
                if (action.imageTargetDpi > 0)
                {
                    settings.colorProfile.targetDpi = action.imageTargetDpi;
                    settings.grayProfile.targetDpi = action.imageTargetDpi;
                }

                PDFImageOptimizer imageOptimizer;
                document = imageOptimizer.optimize(&document, settings);
            }

            if (action.optimizationFlags != PDFOptimizer::None)
            {
                PDFOptimizer optimizer(action.optimizationFlags, nullptr);
                optimizer.setDocument(&document);
                optimizer.optimize();
                document = optimizer.takeOptimizedDocument();
            }
            return QString();
        }

        case PDFBatchAction::Type::Sanitize:
        {
            PDFDocumentSanitizer sanitizer(PDFDocumentSanitizer::None, nullptr);
            sanitizer.setFlags(action.sanitizationFlags);
            sanitizer.setDocument(&document);
            sanitizer.sanitize();
            document = sanitizer.takeSanitizedDocument();
            return QString();
        }

        case PDFBatchAction::Type::SetPassword:
        {
            PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = PDFSecurityHandlerFactory::AES_256;
            settings.encryptContents = PDFSecurityHandlerFactory::All;
            settings.userPassword = action.userPassword;
            settings.ownerPassword = action.ownerPassword.isEmpty() ? action.userPassword : action.ownerPassword;
            settings.permissions = uint32_t(PDFSecurityHandler::Permission::PrintLowResolution) |
                                   uint32_t(PDFSecurityHandler::Permission::Modify) |
                                   uint32_t(PDFSecurityHandler::Permission::CopyContent) |
                                   uint32_t(PDFSecurityHandler::Permission::ModifyInteractiveItems) |
                                   uint32_t(PDFSecurityHandler::Permission::ModifyFormFields) |
                                   uint32_t(PDFSecurityHandler::Permission::Accessibility) |
                                   uint32_t(PDFSecurityHandler::Permission::Assemble) |
                                   uint32_t(PDFSecurityHandler::Permission::PrintHighResolution);

            QString errorMessage;
            if (!PDFSecurityHandlerFactory::validate(settings, &errorMessage))
            {
                return errorMessage;
            }

            PDFSecurityHandlerPointer securityHandler = PDFSecurityHandlerFactory::createSecurityHandler(settings);
            if (!securityHandler)
            {
                return PDFTranslationContext::tr("The encryption cannot be created.");
            }

            PDFDocumentBuilder builder(&document);
            builder.setSecurityHandler(std::move(securityHandler));
            document = builder.build();
            return QString();
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return PDFTranslationContext::tr("Unknown action.");
}

QString PDFBatchProcessor::recognizeText(const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl)
{
    const PDFDocument& document = context.document;
    const PDFInteger pageCount = PDFInteger(document.getCatalog()->getPageCount());

    QString pageError;
    const std::vector<PDFInteger> pageIndices = action.getPageIndices(pageCount, &pageError);
    if (!pageError.isEmpty())
    {
        return pageError;
    }

    const PDFOCRApplyProcessor::Context applyContext = PDFOCRApplyProcessor::createContext(&document, context.inputFile);
    const QString permissionError = PDFOCRApplyProcessor::checkPermissions(applyContext, PDFOCRApplyProcessor::OutputMode::ModifyDocument);
    if (!permissionError.isEmpty())
    {
        return permissionError;
    }

    PDFOCRDocumentRunner::Settings settings;
    settings.configuration = action.ocrConfiguration;
    settings.pages = pageIndices;
    settings.decisionPolicy = action.ocrDecisionPolicy;
    settings.modelManager = getModelManager();
    settings.fileName = context.inputFile;

    const PDFOCRDocumentRunner::Result result = PDFOCRDocumentRunner::run(&document, settings, PDFOCRDocumentRunner::ProgressCallback(), operationControl);
    if (result.cancelled)
    {
        return PDFTranslationContext::tr("Stopped.");
    }
    if (!result.errorMessage.isEmpty())
    {
        return result.errorMessage;
    }

    const int failedPages = result.getFailedPageCount();
    if (failedPages > 0 && !action.ocrAllowPageErrors)
    {
        QStringList messages;
        for (const PDFOCRDocumentRunner::PageRecord& record : result.records)
        {
            if (record.state == PDFOCRPageState::Error)
            {
                messages << PDFTranslationContext::tr("page %1: %2").arg(record.pageIndex + 1).arg(record.message);
            }
        }
        return PDFTranslationContext::tr("%n page(s) failed to be recognized. %1", nullptr, failedPages).arg(messages.join(QStringLiteral("; ")));
    }

    PDFOCRApplyProcessor::Request request;
    request.outputMode = PDFOCRApplyProcessor::OutputMode::ModifyDocument;
    if (result.session)
    {
        request.reviewCriteria = result.session->getReviewCriteria();
        for (PDFInteger page : result.pages)
        {
            const PDFOCRPageResult* pageResult = result.session->getPage(page);
            if (!pageResult || !pageResult->hasResult())
            {
                continue;
            }

            request.results.push_back(*pageResult);

            auto analysisIt = result.analysis.find(page);
            if (analysisIt != result.analysis.end())
            {
                request.analysis[page] = analysisIt->second;
            }

            auto fingerprintIt = result.fingerprints.find(page);
            if (fingerprintIt != result.fingerprints.end() && !fingerprintIt->second.isEmpty())
            {
                request.fingerprints[page] = fingerprintIt->second;
            }
        }
    }

    const PDFOCRApplyProcessor::Plan plan = PDFOCRApplyProcessor::createPlan(applyContext, request);
    if (!plan.hasRequests())
    {
        if (failedPages > 0)
        {
            context.details << PDFTranslationContext::tr("%n page(s) failed to be recognized.", nullptr, failedPages);
        }
        else
        {
            context.details << PDFTranslationContext::tr("No page needed the recognition of the text.");
        }
        return QString();
    }

    const PDFOCRApplyProcessor::Result applyResult = PDFOCRApplyProcessor::execute(applyContext, plan, operationControl);
    if (!applyResult.isSuccess())
    {
        return applyResult.errorMessage;
    }

    if (applyResult.document)
    {
        context.document = *applyResult.document;
    }

    context.details << PDFTranslationContext::tr("Text recognized on %n page(s).", nullptr, int(plan.getPages().size()));
    if (failedPages > 0)
    {
        context.details << PDFTranslationContext::tr("%n page(s) failed to be recognized.", nullptr, failedPages);
    }
    return QString();
}

QString PDFBatchProcessor::exportStep(const PDFBatchAction& action, Context& context, const PDFOperationControl* operationControl)
{
    const PDFDocument& document = context.document;
    const PDFInteger pageCount = PDFInteger(document.getCatalog()->getPageCount());

    QString pageError;
    const std::vector<PDFInteger> pageIndices = action.getPageIndices(pageCount, &pageError);
    if (!pageError.isEmpty())
    {
        return pageError;
    }

    const PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
    if (action.type != PDFBatchAction::Type::ExportImages && securityHandler && !securityHandler->isAllowed(PDFSecurityHandler::Permission::CopyContent))
    {
        return PDFTranslationContext::tr("The document does not allow to copy its content.");
    }

    const QString fileName = context.baseName + getExportExtension(action);

    switch (action.type)
    {
        case PDFBatchAction::Type::ExportWord:
        {
            PDFDocxExportSettings settings;
            settings.includeGraphics = action.wordIncludeGraphics;
            settings.graphicsResolution = action.wordGraphicsResolution;

            PDFDocxExporter exporter(settings);
            exporter.setOperationControl(operationControl);

            QByteArray data;
            PDFOperationResult exportResult = exporter.exportDocument(&document, pageIndices, data);
            if (!exportResult)
            {
                return exportResult.getErrorMessage();
            }

            QSaveFile file(fileName);
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            {
                return PDFTranslationContext::tr("The file '%1' cannot be written.").arg(QDir::toNativeSeparators(fileName));
            }
            context.writtenFiles << fileName;
            return QString();
        }

        case PDFBatchAction::Type::ExportText:
        {
            PDFDocumentTextFlowFactory factory;
            const PDFDocumentTextFlow textFlow = factory.create(&document, pageIndices, PDFDocumentTextFlowFactory::Algorithm::Auto);

            QString text;
            bool firstPage = true;
            for (const PDFDocumentTextFlow::Item& item : textFlow.getItems())
            {
                if (item.flags.testFlag(PDFDocumentTextFlow::PageStart))
                {
                    if (!firstPage)
                    {
                        text += PDFBatchProcessorHelper::getPageSeparator();
                    }
                    firstPage = false;
                }

                if (item.flags.testFlag(PDFDocumentTextFlow::Text) && !item.text.isEmpty())
                {
                    text += item.text;
                    text += QChar('\n');
                }
            }

            QSaveFile file(fileName);
            const QByteArray data = text.toUtf8();
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            {
                return PDFTranslationContext::tr("The file '%1' cannot be written.").arg(QDir::toNativeSeparators(fileName));
            }
            context.writtenFiles << fileName;
            return QString();
        }

        case PDFBatchAction::Type::ExportImages:
            return exportImages(action, context, fileName, operationControl);

        default:
            Q_ASSERT(false);
            break;
    }

    return PDFTranslationContext::tr("Unknown action.");
}

QString PDFBatchProcessor::exportImages(const PDFBatchAction& action, Context& context, const QString& firstFileName, const PDFOperationControl* operationControl)
{
    Q_UNUSED(firstFileName);

    const PDFDocument& document = context.document;
    const PDFInteger pageCount = PDFInteger(document.getCatalog()->getPageCount());

    QString pageError;
    const std::vector<PDFInteger> pageIndices = action.getPageIndices(pageCount, &pageError);
    if (!pageError.isEmpty())
    {
        return pageError;
    }

    const int digits = qMax(3, int(QString::number(pageCount).size()));
    const QByteArray format = action.imageFormat.toLatin1().toLower();
    auto getFileName = [&](PDFInteger pageIndex)
    {
        return QStringLiteral("%1-%2.%3").arg(context.baseName).arg(pageIndex + 1, digits, 10, QChar('0')).arg(QString::fromLatin1(format));
    };

    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::Export, nullptr);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    PDFRasterizerPool rasterizerPool(&document, &fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity,
                                     PDFRenderer::getDefaultFeatures(), meshQualitySettings,
                                     PDFRasterizerPool::getDefaultRasterizerCount(), RendererEngine::QPainter, nullptr);

    QMutex mutex;
    QStringList errors;
    std::map<PDFInteger, QString> writtenFiles;
    const double resolution = action.imageResolution;

    auto imageSizeGetter = [resolution](const PDFPage* page) -> QSize
    {
        const QSizeF size = page->getRotatedMediaBox().size() * PDF_POINT_TO_INCH * resolution;
        return QSize(qMax(1, qRound(size.width())), qMax(1, qRound(size.height())));
    };

    auto processImage = [&](PDFRenderedPageImage& renderedImage)
    {
        const QString fileName = getFileName(renderedImage.pageIndex);
        QImageWriter writer(fileName, format);
        writer.setText(QStringLiteral("Software"), QStringLiteral("PDF4QT"));
        if (renderedImage.pageImage.isNull() || !writer.write(renderedImage.pageImage))
        {
            QMutexLocker lock(&mutex);
            errors << PDFTranslationContext::tr("The image of the page %1 cannot be written. %2").arg(renderedImage.pageIndex + 1).arg(writer.errorString());
            return;
        }

        QMutexLocker lock(&mutex);
        writtenFiles[renderedImage.pageIndex] = fileName;
    };

    rasterizerPool.render(pageIndices, imageSizeGetter, processImage, nullptr, operationControl);
    fontCache.setCacheShrinkEnabled(nullptr, true);

    for (const auto& item : writtenFiles)
    {
        context.writtenFiles << item.second;
    }

    if (!errors.isEmpty())
    {
        return errors.join(QChar('\n'));
    }

    if (!(operationControl && operationControl->isOperationCancelled()) && writtenFiles.size() != pageIndices.size())
    {
        return PDFTranslationContext::tr("Not all pages were rendered.");
    }

    return QString();
}

}   // namespace pdf
