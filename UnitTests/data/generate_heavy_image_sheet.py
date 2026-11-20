# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Generates heavy-image-sheet.pdf, a synthetic plan sheet with the traits of a
sheet plotted by AutoCAD LT (pdfplot17.hdi), whose page is slow to compile:

 - PDF 1.7, classic cross-reference table, not linearized, Architect D page
   (36 x 24 in = 2592 x 1728 pt) with a page level DeviceRGB transparency group,
 - 48 DeviceRGB images 5061 x 551 / 5061 x 496 placed at 600 ppi, filtered by
   [/FlateDecode /DCTDecode], each with a DeviceGray soft mask filtered by
   [/FlateDecode /ASCII85Decode], Interpolate unset,
 - one 830 x 2063 logo image (600 ppi) without a soft mask,
 - a small vector luminaire schedule (lines and text).

About 520 MB of uncompressed image and soft mask data. The output is
deterministic (fixed random seed). Usage:

    python3 generate_heavy_image_sheet.py [output.pdf]

Needs numpy and Pillow.
"""

import base64
import io
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw

PAGE_WIDTH = 2592
PAGE_HEIGHT = 1728
PPI = 600.0


def make_strip(rng, index, width, height):
    """Raster of a schedule strip: rows of glyph-like marks, ruled lines and
    colored symbols on a white background, with photographic noise, so the
    JPEG has a realistic size. Returns (rgb, alpha)."""
    rgb = np.full((height, width, 3), 255, dtype=np.uint8)
    alpha = np.zeros((height, width), dtype=np.uint8)

    image = Image.fromarray(rgb)
    mask = Image.fromarray(alpha)
    draw = ImageDraw.Draw(image)
    draw_mask = ImageDraw.Draw(mask)

    # Frame and ruled lines of the table
    line_width = 6
    for target, color in ((draw, (0, 0, 0)), (draw_mask, 255)):
        target.rectangle([0, 0, width - 1, height - 1], outline=color, width=line_width)
        for x in range(0, width, 640):
            target.line([x, 0, x, height], fill=color, width=line_width)
        for y in range(0, height, 120):
            target.line([0, y, width, y], fill=color, width=3)

    # Glyph-like marks (text of the cells)
    for row_top in range(30, height - 60, 120):
        x = 40
        while x < width - 80:
            if rng.random() < 0.15:
                x += 60
                continue
            glyph_width = int(rng.integers(24, 48))
            glyph_height = int(rng.integers(40, 70))
            gray = int(rng.integers(0, 60))
            box = [x, row_top, x + glyph_width, row_top + glyph_height]
            draw.rectangle(box, outline=(gray, gray, gray), width=5)
            draw_mask.rectangle(box, outline=255, width=5)
            x += glyph_width + 14

    # Colored luminaire symbols with a gradient fill
    for _ in range(6):
        cx = int(rng.integers(100, width - 100))
        cy = int(rng.integers(60, height - 60))
        radius = int(rng.integers(30, 50))
        color = tuple(int(c) for c in rng.integers(0, 255, 3))
        box = [cx - radius, cy - radius, cx + radius, cy + radius]
        draw.ellipse(box, fill=color, outline=(0, 0, 0), width=4)
        draw_mask.ellipse(box, fill=255)

    rgb = np.asarray(image).astype(np.int16)
    alpha = np.asarray(mask).astype(np.int16)

    # Scanner like noise and a light hue shift per strip
    noise = rng.normal(0.0, 2.0, size=rgb.shape)
    rgb = rgb + noise.astype(np.int16)
    rgb[:, :, index % 3] -= 4
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)

    # Antialiased edges of the soft mask
    soft = alpha.copy()
    soft[1:, :] = np.maximum(soft[1:, :], alpha[:-1, :] // 2)
    soft[:, 1:] = np.maximum(soft[:, 1:], alpha[:, :-1] // 2)
    return rgb, soft.astype(np.uint8)


def make_logo(rng, width, height):
    y, x = np.mgrid[0:height, 0:width]
    rgb = np.zeros((height, width, 3), dtype=np.float32)
    rgb[:, :, 0] = 40 + 180 * (x / width)
    rgb[:, :, 1] = 90 + 120 * (y / height)
    rgb[:, :, 2] = 200 - 100 * np.sin(x / 70.0) * np.cos(y / 90.0)
    rgb += rng.normal(0.0, 4.0, size=rgb.shape)
    image = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    draw = ImageDraw.Draw(image)
    draw.rectangle([60, 60, width - 60, height - 60], outline=(255, 255, 255), width=24)
    draw.ellipse([150, 300, width - 150, 300 + width - 300], outline=(255, 255, 255), width=30)
    return np.asarray(image)


def jpeg_bytes(rgb, quality):
    buffer = io.BytesIO()
    Image.fromarray(rgb, "RGB").save(buffer, format="JPEG", quality=quality, subsampling=2)
    return buffer.getvalue()


class PDFWriter:
    def __init__(self):
        self.objects = []

    def reserve(self):
        self.objects.append(None)
        return len(self.objects)

    def set(self, number, data):
        self.objects[number - 1] = data

    def add(self, data):
        number = self.reserve()
        self.set(number, data)
        return number

    def add_stream(self, dictionary, data):
        header = ("<< %s /Length %d >>\nstream\n" % (dictionary, len(data))).encode("latin-1")
        return self.add(header + data + b"\nendstream")

    def write(self, file_name, root):
        output = io.BytesIO()
        output.write(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for number, data in enumerate(self.objects, start=1):
            if isinstance(data, str):
                data = data.encode("latin-1")
            offsets.append(output.tell())
            output.write(b"%d 0 obj\n" % number)
            output.write(data)
            output.write(b"\nendobj\n")
        xref = output.tell()
        output.write(b"xref\n0 %d\n" % (len(self.objects) + 1))
        output.write(b"0000000000 65535 f \n")
        for offset in offsets:
            output.write(b"%010d 00000 n \n" % offset)
        output.write(b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(self.objects) + 1, root, xref))
        with open(file_name, "wb") as file:
            file.write(output.getvalue())


def schedule_content():
    """Vector luminaire schedule: a ruled table with text."""
    commands = ["q 0 0 0 RG 0 0 0 rg 1.2 w"]
    left, top = 1700.0, 640.0
    columns = [0, 60, 260, 360, 480, 600, 700]
    rows = 16
    row_height = 18.0
    for column in columns:
        commands.append("%.2f %.2f m %.2f %.2f l S" % (left + column, top, left + column, top - rows * row_height))
    for row in range(rows + 1):
        commands.append("%.2f %.2f m %.2f %.2f l S" % (left, top - row * row_height, left + columns[-1], top - row * row_height))
    commands.append("BT /F1 8 Tf")
    headers = ["TYPE", "DESCRIPTION", "LAMP", "WATTS", "VOLTS", "MOUNT"]
    for row in range(rows):
        for index, column in enumerate(columns[:-1]):
            text = headers[index] if row == 0 else "%s%d-%d" % (headers[index][0], row, index)
            commands.append("1 0 0 1 %.2f %.2f Tm (%s) Tj" % (left + column + 4, top - (row + 1) * row_height + 5, text))
    commands.append("ET Q")
    return "\n".join(commands).encode("latin-1")


def main():
    output_file = sys.argv[1] if len(sys.argv) > 1 else "heavy-image-sheet.pdf"
    rng = np.random.default_rng(20261120)
    writer = PDFWriter()

    catalog = writer.reserve()
    pages = writer.reserve()
    page = writer.reserve()
    font = writer.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")

    xobjects = []
    content = [b"q"]

    # 48 strips in 4 columns of 12 rows
    column_width = 5061 / PPI * 72.0
    for index in range(48):
        width = 5061
        height = 551 if index % 3 != 2 else 496
        rgb, alpha = make_strip(rng, index, width, height)

        image_data = zlib.compress(jpeg_bytes(rgb, 70), 9)
        mask_data = zlib.compress(base64.a85encode(alpha.tobytes(), wrapcol=80) + b"~>", 9)

        mask = writer.add_stream("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter [/FlateDecode /ASCII85Decode]" % (width, height), mask_data)
        image = writer.add_stream("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask %d 0 R /Filter [/FlateDecode /DCTDecode]" % (width, height, mask), image_data)
        name = "Im%d" % index
        xobjects.append("/%s %d 0 R" % (name, image))

        column = index % 4
        row = index // 4
        x = 36.0 + column * (column_width + 8.0)
        y = PAGE_HEIGHT - 120.0 - row * 72.0
        image_width = width / PPI * 72.0
        image_height = height / PPI * 72.0
        content.append(("q %.4f 0 0 %.4f %.4f %.4f cm /%s Do Q" % (image_width, image_height, x, y, name)).encode("latin-1"))

    # Logo without a soft mask
    logo = make_logo(rng, 830, 2063)
    logo_object = writer.add_stream("/Type /XObject /Subtype /Image /Width 830 /Height 2063 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter [/FlateDecode /DCTDecode]", zlib.compress(jpeg_bytes(logo, 85), 9))
    xobjects.append("/Logo %d 0 R" % logo_object)
    content.append(("q %.4f 0 0 %.4f %.4f %.4f cm /Logo Do Q" % (830 / PPI * 72.0, 2063 / PPI * 72.0, 2460.0, 40.0)).encode("latin-1"))
    content.append(b"Q")
    content.append(schedule_content())

    content_stream = writer.add_stream("/Filter /FlateDecode", zlib.compress(b"\n".join(content), 9))

    writer.set(catalog, "<< /Type /Catalog /Pages %d 0 R >>" % pages)
    writer.set(pages, "<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page)
    writer.set(page, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Group << /Type /Group /S /Transparency /CS /DeviceRGB /I false /K false >> "
                     "/Resources << /Font << /F1 %d 0 R >> /XObject << %s >> >> /Contents %d 0 R >>"
                     % (pages, PAGE_WIDTH, PAGE_HEIGHT, font, " ".join(xobjects), content_stream))
    writer.write(output_file, catalog)


if __name__ == "__main__":
    main()
