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

#include "pdfdocxexporter.h"
#include "pdfdocument.h"
#include "pdfdocumentreader.h"

#include <QtTest>
#include <QImage>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QMutex>

#include <zlib.h>

#include <map>

using namespace pdf;

/// Exported paragraph of document.xml, read back by the test
struct DocxTestParagraph
{
    QString text;           ///< Text, tabs are '\t'
    bool bold = false;      ///< Some run of the paragraph is bold
    QStringList colors;     ///< Colors of the runs
    QStringList fonts;      ///< Font families of the runs
    int tabStops = 0;       ///< Number of the tab stops
    bool hasDrawing = false;
};

struct DocxTestSection
{
    int width = 0;
    int height = 0;
    bool landscape = false;
};

struct DocxTestDocument
{
    std::vector<DocxTestParagraph> paragraphs;
    std::vector<DocxTestSection> sections;

    const DocxTestParagraph* find(const QString& text) const
    {
        for (const DocxTestParagraph& paragraph : paragraphs)
        {
            if (paragraph.text.contains(text))
            {
                return &paragraph;
            }
        }
        return nullptr;
    }
};

class DocxExporterTest : public QObject
{
    Q_OBJECT

private slots:
    void zipArchiveRoundTrip();
    void packageStructure();
    void storedPackage();
    void textLayout();
    void graphicsBackground();
    void graphicsTurnedOff();
    void invalidPages();
    void exportToFile();

private:
    /// Reads all files of a ZIP archive (stored or deflated), checks the CRC
    static std::vector<std::pair<QString, QByteArray>> readZipArchive(const QByteArray& archive);
    static std::map<QString, QByteArray> readZipArchiveMap(const QByteArray& archive);

    /// Creates a PDF document from the objects (object number = index + 1)
    static QByteArray createPdf(const std::vector<QByteArray>& objects);

    /// Spec sheet: page 1 (text, graphics, form field), page 2 (landscape, text only),
    /// page 3 (scan: gray picture with an invisible text)
    static PDFDocument createSpecSheet();

    static DocxTestDocument parseDocument(const QByteArray& documentXml);

    static QByteArray exportDocx(const PDFDocument& document, PDFDocxExportSettings settings, const std::vector<PDFInteger>& pages);
};

std::vector<std::pair<QString, QByteArray>> DocxExporterTest::readZipArchive(const QByteArray& archive)
{
    std::vector<std::pair<QString, QByteArray>> files;

    auto readUInt16 = [&archive](qsizetype offset) -> quint32
    {
        return quint32(quint8(archive[offset])) | (quint32(quint8(archive[offset + 1])) << 8);
    };
    auto readUInt32 = [&archive](qsizetype offset) -> quint32
    {
        return quint32(quint8(archive[offset])) | (quint32(quint8(archive[offset + 1])) << 8) |
               (quint32(quint8(archive[offset + 2])) << 16) | (quint32(quint8(archive[offset + 3])) << 24);
    };

    // End of central directory is the last 22 bytes (no comment)
    if (archive.size() < 22 || readUInt32(archive.size() - 22) != 0x06054b50)
    {
        return files;
    }

    const qsizetype endOffset = archive.size() - 22;
    const quint32 count = readUInt16(endOffset + 10);
    qsizetype offset = qsizetype(readUInt32(endOffset + 16));

    for (quint32 i = 0; i < count; ++i)
    {
        if (readUInt32(offset) != 0x02014b50)
        {
            return { };
        }

        const quint32 method = readUInt16(offset + 10);
        const quint32 crc = readUInt32(offset + 16);
        const quint32 compressedSize = readUInt32(offset + 20);
        const quint32 size = readUInt32(offset + 24);
        const quint32 nameLength = readUInt16(offset + 28);
        const quint32 extraLength = readUInt16(offset + 30);
        const quint32 commentLength = readUInt16(offset + 32);
        const qsizetype localOffset = qsizetype(readUInt32(offset + 42));
        const QString name = QString::fromUtf8(archive.mid(offset + 46, nameLength));
        offset += 46 + nameLength + extraLength + commentLength;

        if (readUInt32(localOffset) != 0x04034b50 || readUInt16(localOffset + 8) != method)
        {
            return { };
        }

        const qsizetype dataOffset = localOffset + 30 + readUInt16(localOffset + 26) + readUInt16(localOffset + 28);
        const QByteArray storedData = archive.mid(dataOffset, compressedSize);

        QByteArray data;
        if (method == 0)
        {
            data = storedData;
        }
        else if (method == 8)
        {
            data.resize(size);
            z_stream stream = { };
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            {
                return { };
            }
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(storedData.constData()));
            stream.avail_in = uInt(storedData.size());
            stream.next_out = reinterpret_cast<Bytef*>(data.data());
            stream.avail_out = uInt(data.size());
            const int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != size)
            {
                return { };
            }
        }
        else
        {
            return { };
        }

        if (quint32(data.size()) != size || quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()))) != crc)
        {
            return { };
        }

        files.emplace_back(name, data);
    }

    return files;
}

std::map<QString, QByteArray> DocxExporterTest::readZipArchiveMap(const QByteArray& archive)
{
    std::map<QString, QByteArray> result;
    for (const auto& file : readZipArchive(archive))
    {
        result[file.first] = file.second;
    }
    return result;
}

QByteArray DocxExporterTest::createPdf(const std::vector<QByteArray>& objects)
{
    QByteArray data("%PDF-1.7\n");
    std::vector<qsizetype> offsets;

    for (size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(data.size());
        data += QByteArray::number(qulonglong(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const qsizetype xrefOffset = data.size();
    data += "xref\n0 " + QByteArray::number(qulonglong(objects.size() + 1)) + "\n";
    data += "0000000000 65535 f \n";
    for (qsizetype offset : offsets)
    {
        data += QByteArray::number(qlonglong(offset)).rightJustified(10, '0') + " 00000 n \n";
    }
    data += "trailer\n<< /Size " + QByteArray::number(qulonglong(objects.size() + 1)) + " /Root 1 0 R /Info 12 0 R >>\n";
    data += "startxref\n" + QByteArray::number(qlonglong(xrefOffset)) + "\n%%EOF\n";
    return data;
}

PDFDocument DocxExporterTest::createSpecSheet()
{
    auto stream = [](const QByteArray& dictionary, const QByteArray& content)
    {
        return "<< " + dictionary + " /Length " + QByteArray::number(qlonglong(content.size())) + " >>\nstream\n" + content + "\nendstream";
    };

    // Page 1: a title, label and value columns, a paragraph of three lines,
    // a red warning, a Times line and a gray box (graphics)
    const QByteArray content1 =
        "0.8 g 60 560 492 40 re f 0 g\n"
        "BT /F2 18 Tf 72 720 Td (Product Spec Sheet) Tj ET\n"
        "BT /F1 11 Tf 72 690 Td (Model:) Tj ET\n"
        "BT /F1 11 Tf 300 690 Td (LX-200) Tj ET\n"
        "BT /F1 10 Tf 12 TL 72 660 Td\n"
        "(The luminaire housing is made of die cast aluminum with a powder coated) Tj T*\n"
        "(finish, the lens is made of impact resistant acrylic and the driver is) Tj T*\n"
        "(dimmable.) Tj ET\n"
        "BT 1 0 0 rg /F2 12 Tf 72 600 Td (WARNING) Tj ET\n"
        "BT 0 g /F3 12 Tf 72 540 Td (Times text line) Tj ET\n";

    // Appearance stream of a filled text field
    const QByteArray fieldAppearance = "/Tx BMC q BT 0 g /F1 10 Tf 2 4 Td (Filled Value) Tj ET Q EMC";

    // Page 2: landscape, text only
    const QByteArray content2 = "BT /F1 14 Tf 72 500 Td (Second page) Tj ET";

    // Page 3: scan (gray picture) with an invisible text layer
    const QByteArray content3 = "0.5 g 0 0 612 792 re f\nBT 3 Tr /F1 12 Tf 72 700 Td (Scanned text) Tj ET";

    std::vector<QByteArray> objects = {
        /* 1 */ "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [ 9 0 R ] >> >>",
        /* 2 */ "<< /Type /Pages /Kids [ 3 0 R 6 0 R 10 0 R ] /Count 3 >>",
        /* 3 */ "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Resources 5 0 R /Contents 4 0 R /Annots [ 9 0 R ] >>",
        /* 4 */ stream("", content1),
        /* 5 */ "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> /F2 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >> /F3 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman >> >> >>",
        /* 6 */ "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 792 612 ] /Resources 5 0 R /Contents 7 0 R >>",
        /* 7 */ stream("", content2),
        /* 8 */ stream("/Type /XObject /Subtype /Form /BBox [ 0 0 150 20 ] /Resources 5 0 R", fieldAppearance),
        /* 9 */ "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Model) /V (Filled Value) /Rect [ 300 450 450 470 ] /F 4 /P 3 0 R /AP << /N 8 0 R >> >>",
        /* 10 */ "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Resources 5 0 R /Contents 11 0 R >>",
        /* 11 */ stream("", content3),
        /* 12 */ "<< /Title (Spec sheet) /Author (Tester) >>",
    };

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(createPdf(objects));
    return document;
}

DocxTestDocument DocxExporterTest::parseDocument(const QByteArray& documentXml)
{
    DocxTestDocument document;
    const QString w = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");

    QXmlStreamReader reader(documentXml);
    bool inRun = false;
    bool inTabs = false;
    DocxTestParagraph* paragraph = nullptr;

    while (!reader.atEnd())
    {
        reader.readNext();

        if (reader.isStartElement())
        {
            const QStringView name = reader.name();
            if (reader.namespaceUri() == w)
            {
                if (name == QLatin1String("p"))
                {
                    document.paragraphs.emplace_back();
                    paragraph = &document.paragraphs.back();
                }
                else if (name == QLatin1String("r"))
                {
                    inRun = true;
                }
                else if (name == QLatin1String("tabs"))
                {
                    inTabs = true;
                }
                else if (name == QLatin1String("tab") && paragraph)
                {
                    if (inTabs)
                    {
                        ++paragraph->tabStops;
                    }
                    else if (inRun)
                    {
                        paragraph->text.append(QChar('\t'));
                    }
                }
                else if (name == QLatin1String("t") && paragraph)
                {
                    paragraph->text.append(reader.readElementText());
                }
                else if (name == QLatin1String("b") && paragraph && inRun)
                {
                    paragraph->bold = true;
                }
                else if (name == QLatin1String("color") && paragraph && inRun)
                {
                    paragraph->colors << reader.attributes().value(w, QStringLiteral("val")).toString();
                }
                else if (name == QLatin1String("rFonts") && paragraph && inRun)
                {
                    paragraph->fonts << reader.attributes().value(w, QStringLiteral("ascii")).toString();
                }
                else if (name == QLatin1String("drawing") && paragraph)
                {
                    paragraph->hasDrawing = true;
                }
                else if (name == QLatin1String("pgSz"))
                {
                    DocxTestSection section;
                    section.width = reader.attributes().value(w, QStringLiteral("w")).toInt();
                    section.height = reader.attributes().value(w, QStringLiteral("h")).toInt();
                    section.landscape = reader.attributes().value(w, QStringLiteral("orient")) == QLatin1String("landscape");
                    document.sections.push_back(section);
                }
            }
        }
        else if (reader.isEndElement() && reader.namespaceUri() == w)
        {
            if (reader.name() == QLatin1String("r"))
            {
                inRun = false;
            }
            else if (reader.name() == QLatin1String("tabs"))
            {
                inTabs = false;
            }
        }
    }

    if (reader.hasError())
    {
        qWarning() << reader.errorString();
        return DocxTestDocument();
    }

    return document;
}

QByteArray DocxExporterTest::exportDocx(const PDFDocument& document, PDFDocxExportSettings settings, const std::vector<PDFInteger>& pages)
{
    PDFDocxExporter exporter(settings);
    QByteArray data;
    PDFOperationResult result = exporter.exportDocument(&document, pages, data);
    if (!result)
    {
        qWarning() << result.getErrorMessage();
    }
    return data;
}

void DocxExporterTest::zipArchiveRoundTrip()
{
    QByteArray large;
    for (int i = 0; i < 5000; ++i)
    {
        large += "Repeated line of text " + QByteArray::number(i % 7) + "\n";
    }

    const std::vector<std::pair<QString, QByteArray>> files = {
        { QStringLiteral("[Content_Types].xml"), QByteArray("<Types/>") },
        { QStringLiteral("empty.txt"), QByteArray() },
        { QStringLiteral("folder/large.txt"), large },
        { QStringLiteral("binary.bin"), QByteArray("\x00\x01\x02\xFF", 4) },
    };

    for (bool compress : { false, true })
    {
        const QByteArray archive = PDFDocxExporter::createZipArchive(files, compress);
        const auto readFiles = readZipArchive(archive);
        QCOMPARE(readFiles.size(), files.size());
        for (size_t i = 0; i < files.size(); ++i)
        {
            QCOMPARE(readFiles[i].first, files[i].first);
            QCOMPARE(readFiles[i].second, files[i].second);
        }

        if (compress)
        {
            QVERIFY(archive.size() < large.size() / 4);
        }
    }
}

void DocxExporterTest::packageStructure()
{
    PDFDocument document = createSpecSheet();
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(3));

    const QByteArray docx = exportDocx(document, PDFDocxExportSettings(), { 0, 1, 2 });
    QVERIFY(!docx.isEmpty());

    const auto files = readZipArchive(docx);
    QVERIFY(!files.empty());
    QCOMPARE(files.front().first, QStringLiteral("[Content_Types].xml"));

    const auto map = readZipArchiveMap(docx);
    for (const char* part : { "[Content_Types].xml", "_rels/.rels", "docProps/core.xml", "docProps/app.xml", "word/document.xml",
                              "word/styles.xml", "word/settings.xml", "word/_rels/document.xml.rels" })
    {
        QVERIFY2(map.count(QString::fromLatin1(part)), part);

        // Each XML part must be well formed
        QXmlStreamReader reader(map.at(QString::fromLatin1(part)));
        while (!reader.atEnd())
        {
            reader.readNext();
        }
        QVERIFY2(!reader.hasError(), qPrintable(QString::fromLatin1(part) + ": " + reader.errorString()));
    }

    QVERIFY(map.at(QStringLiteral("[Content_Types].xml")).contains("image/png"));
    QVERIFY(map.at(QStringLiteral("word/_rels/document.xml.rels")).contains("media/page1.png"));
    QVERIFY(map.at(QStringLiteral("docProps/core.xml")).contains("Spec sheet"));
    QVERIFY(map.at(QStringLiteral("docProps/core.xml")).contains("Tester"));
}

void DocxExporterTest::storedPackage()
{
    PDFDocument document = createSpecSheet();

    PDFDocxExportSettings settings;
    settings.compress = false;
    const QByteArray stored = exportDocx(document, settings, { 0 });
    const QByteArray compressed = exportDocx(document, PDFDocxExportSettings(), { 0 });

    const auto storedMap = readZipArchiveMap(stored);
    const auto compressedMap = readZipArchiveMap(compressed);
    QVERIFY(!storedMap.empty());
    QVERIFY(compressed.size() < stored.size());
    QCOMPARE(storedMap.at(QStringLiteral("word/document.xml")), compressedMap.at(QStringLiteral("word/document.xml")));
}

void DocxExporterTest::textLayout()
{
    PDFDocument document = createSpecSheet();
    const auto map = readZipArchiveMap(exportDocx(document, PDFDocxExportSettings(), { 0, 1, 2 }));
    const DocxTestDocument docx = parseDocument(map.at(QStringLiteral("word/document.xml")));
    QVERIFY(!docx.paragraphs.empty());

    // Title is bold Arial (Helvetica-Bold)
    const DocxTestParagraph* title = docx.find(QStringLiteral("Product Spec Sheet"));
    QVERIFY(title);
    QVERIFY(title->bold);
    QVERIFY(title->fonts.contains(QStringLiteral("Arial")));

    // Columns of a line are one paragraph separated by a tab stop
    const DocxTestParagraph* model = docx.find(QStringLiteral("Model:"));
    QVERIFY(model);
    QCOMPARE(model->text, QStringLiteral("Model:\tLX-200"));
    QCOMPARE(model->tabStops, 1);
    QVERIFY(!model->bold);

    // Lines of the body text are joined into one paragraph
    const DocxTestParagraph* body = docx.find(QStringLiteral("The luminaire housing"));
    QVERIFY(body);
    QCOMPARE(body->text, QStringLiteral("The luminaire housing is made of die cast aluminum with a powder coated "
                                        "finish, the lens is made of impact resistant acrylic and the driver is dimmable."));

    // Color of the text
    const DocxTestParagraph* warning = docx.find(QStringLiteral("WARNING"));
    QVERIFY(warning);
    QVERIFY(warning->colors.contains(QStringLiteral("FF0000")));

    // Times is mapped to Times New Roman
    const DocxTestParagraph* times = docx.find(QStringLiteral("Times text line"));
    QVERIFY(times);
    QVERIFY(times->fonts.contains(QStringLiteral("Times New Roman")));

    // Value of the form field (appearance stream of the widget)
    QVERIFY(docx.find(QStringLiteral("Filled Value")));

    // Order of the paragraphs follows the page from top to bottom
    auto indexOf = [&docx](const QString& text)
    {
        for (size_t i = 0; i < docx.paragraphs.size(); ++i)
        {
            if (docx.paragraphs[i].text.contains(text))
            {
                return int(i);
            }
        }
        return -1;
    };
    QVERIFY(indexOf(QStringLiteral("Product Spec Sheet")) < indexOf(QStringLiteral("Model:")));
    QVERIFY(indexOf(QStringLiteral("Model:")) < indexOf(QStringLiteral("The luminaire")));
    QVERIFY(indexOf(QStringLiteral("WARNING")) < indexOf(QStringLiteral("Times text line")));
    QVERIFY(indexOf(QStringLiteral("Times text line")) < indexOf(QStringLiteral("Filled Value")));
    QVERIFY(indexOf(QStringLiteral("Filled Value")) < indexOf(QStringLiteral("Second page")));
    QVERIFY(indexOf(QStringLiteral("Second page")) < indexOf(QStringLiteral("Scanned text")));

    // Each page is a section with the page size
    QCOMPARE(docx.sections.size(), size_t(3));
    QCOMPARE(docx.sections[0].width, 12240);
    QCOMPARE(docx.sections[0].height, 15840);
    QVERIFY(!docx.sections[0].landscape);
    QCOMPARE(docx.sections[1].width, 15840);
    QCOMPARE(docx.sections[1].height, 12240);
    QVERIFY(docx.sections[1].landscape);

    // Without reflow, each line is a paragraph
    PDFDocxExportSettings settings;
    settings.reflowParagraphs = false;
    const auto mapNoReflow = readZipArchiveMap(exportDocx(document, settings, { 0 }));
    const DocxTestDocument docxNoReflow = parseDocument(mapNoReflow.at(QStringLiteral("word/document.xml")));
    const DocxTestParagraph* firstLine = docxNoReflow.find(QStringLiteral("The luminaire housing"));
    QVERIFY(firstLine);
    QVERIFY(!firstLine->text.contains(QStringLiteral("finish")));
    QVERIFY(docxNoReflow.find(QStringLiteral("dimmable.")));
}

void DocxExporterTest::graphicsBackground()
{
    PDFDocument document = createSpecSheet();
    const auto map = readZipArchiveMap(exportDocx(document, PDFDocxExportSettings(), { 0, 1, 2 }));

    // Page 1 has graphics, page 2 has no graphics, page 3 is a scan (the picture
    // contains the text, so it is not exported, the text is exported instead)
    QVERIFY(map.count(QStringLiteral("word/media/page1.png")));
    QVERIFY(!map.count(QStringLiteral("word/media/page2.png")));
    QVERIFY(!map.count(QStringLiteral("word/media/page3.png")));

    QImage image;
    QVERIFY(image.loadFromData(map.at(QStringLiteral("word/media/page1.png")), "PNG"));
    QCOMPARE(image.width(), 1275);
    QCOMPARE(image.height(), 1650);

    // The gray box is in the picture, the text is not (the title lies at y = 720 pt)
    const double scale = 150.0 / 72.0;
    const QColor boxColor = image.pixelColor(int(300 * scale), int((792 - 580) * scale));
    QVERIFY(qAbs(boxColor.red() - 204) <= 2);

    bool titleIsPainted = false;
    for (int x = int(72 * scale); x < int(250 * scale); ++x)
    {
        for (int y = int((792 - 725) * scale); y < int((792 - 715) * scale); ++y)
        {
            if (image.pixelColor(x, y) != QColor(Qt::white))
            {
                titleIsPainted = true;
            }
        }
    }
    QVERIFY(!titleIsPainted);

    const DocxTestDocument docx = parseDocument(map.at(QStringLiteral("word/document.xml")));
    QVERIFY(docx.paragraphs.front().hasDrawing);
    QVERIFY(docx.find(QStringLiteral("Scanned text")));
}

void DocxExporterTest::graphicsTurnedOff()
{
    PDFDocument document = createSpecSheet();
    PDFDocxExportSettings settings;
    settings.includeGraphics = false;
    settings.includeAnnotations = false;
    const auto map = readZipArchiveMap(exportDocx(document, settings, { 0 }));
    QVERIFY(!map.count(QStringLiteral("word/media/page1.png")));
    QVERIFY(!map.at(QStringLiteral("[Content_Types].xml")).contains("image/png"));

    const DocxTestDocument docx = parseDocument(map.at(QStringLiteral("word/document.xml")));
    QVERIFY(docx.find(QStringLiteral("Model:")));
    QVERIFY(!docx.find(QStringLiteral("Filled Value")));
}

void DocxExporterTest::invalidPages()
{
    PDFDocument document = createSpecSheet();
    PDFDocxExporter exporter;
    QByteArray data;
    QVERIFY(!exporter.exportDocument(&document, { 5 }, data));
    QVERIFY(!exporter.exportDocument(&document, { -1 }, data));
    QVERIFY(!exporter.exportDocument(&document, std::vector<PDFInteger>(), data));
    QVERIFY(!exporter.exportDocument(nullptr, { 0 }, data));
    QVERIFY(data.isEmpty());
}

void DocxExporterTest::exportToFile()
{
    PDFDocument document = createSpecSheet();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString fileName = directory.filePath(QStringLiteral("spec sheet.docx"));

    std::vector<std::pair<size_t, size_t>> progress;
    QMutex mutex;
    PDFDocxExporter exporter;
    exporter.setProgressCallback([&](size_t done, size_t total) { QMutexLocker lock(&mutex); progress.emplace_back(done, total); });
    QVERIFY(exporter.exportDocument(&document, { 0, 1, 2 }, fileName));

    QFile file(fileName);
    QVERIFY(file.open(QFile::ReadOnly));
    QVERIFY(!readZipArchive(file.readAll()).empty());

    QCOMPARE(progress.size(), size_t(3));
    std::sort(progress.begin(), progress.end());
    QCOMPARE(progress.back(), std::make_pair(size_t(3), size_t(3)));

    // Directory, which doesn't exist
    QVERIFY(!exporter.exportDocument(&document, { 0 }, directory.filePath(QStringLiteral("missing/file.docx"))));
}

QTEST_MAIN(DocxExporterTest)

#include "tst_docxexportertest.moc"
