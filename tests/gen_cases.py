# Generate the regression test files for tests/run-tests.sh.
#
# Every file is built from synthetic images (Pillow gradients and solid
# colours) plus hand-made segments, so no third-party images are needed.
# Finding IDs (C1, H3, U1, ...) refer to docs/code-review-macos-qt6.md.
#
# usage: gen_cases.py <out_dir>
# Writes <out_dir>/files/* and <out_dir>/cases.tsv with the columns
#   id, finding, mode, files (comma-separated), flags, timeout_s, expect, forbid
# where expect/forbid are extended regexes matched against the driver's
# stdout ("-" for none).

import io
import struct
import sys
from pathlib import Path

from PIL import Image

out_dir = Path(sys.argv[1])
files_dir = out_dir / "files"
files_dir.mkdir(parents=True, exist_ok=True)
cases = []


# --- helpers ------------------------------------------------------------------
def jpeg(img, **kw):
    buf = io.BytesIO()
    img.save(buf, "JPEG", **kw)
    return buf.getvalue()


def seg(marker, payload):
    return bytes([0xFF, marker]) + struct.pack(">H", len(payload) + 2) + payload


def marker_pos(data, code):
    """Offset of the first header segment with this marker (searching up to SOS)."""
    i = 2
    while i + 4 <= len(data):
        m = data[i + 1]
        if m == code:
            return i
        if m == 0xDA:
            break
        i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    raise ValueError(f"marker {code:02X} not found")


def after_app0(data):
    """Split a Pillow JPEG into (SOI + APP0, rest)."""
    i = marker_pos(data, 0xE0)
    end = i + 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    return data[:end], data[end:]


def insert_after_app0(data, extra):
    head, rest = after_app0(data)
    return head + extra + rest


def write(name, data):
    (files_dir / name).write_bytes(data)
    return name


def case(cid, finding, mode, names, flags="-", timeout=20, expect="-", forbid="-"):
    if isinstance(names, str):
        names = [names]
    cases.append((cid, finding, mode, ",".join(names), flags, str(timeout), expect, forbid))


DECODED = "harness: decoded=1"


# --- synthetic images ---------------------------------------------------------
def gradient(w, h):
    r = Image.linear_gradient("L").resize((w, h))
    g = Image.linear_gradient("L").rotate(90).resize((w, h))
    b = Image.radial_gradient("L").resize((w, h))
    return Image.merge("RGB", (r, g, b))


color = gradient(64, 48)
c444 = jpeg(color, quality=90, subsampling=0)
c422 = jpeg(color, quality=90, subsampling=1)
c420 = jpeg(color, quality=90, subsampling=2)
gray = jpeg(color.convert("L"), quality=90)
gray8 = jpeg(color.convert("L").resize((8, 8)), quality=90)
progressive = jpeg(color, quality=85, progressive=True)
cmyk = jpeg(color.convert("CMYK"), quality=85)
small = jpeg(gradient(16, 16), quality=90, subsampling=0)
white = jpeg(Image.new("RGB", (32, 32), (255, 255, 255)), quality=95, subsampling=0)
black = jpeg(Image.new("RGB", (32, 32), (0, 0, 0)), quality=95, subsampling=0)

exif = Image.Exif()
exif[0x010F] = "TestMake"
exif[0x0110] = "TestModel"
exif[0x8825] = {1: "N", 2: (37.0, 48.0, 30.12), 3: "W", 4: (122.0, 25.0, 9.5), 6: 123.4}
exif_gps = jpeg(color, quality=85, exif=exif.tobytes())

# --- valid files: must decode cleanly -----------------------------------------
for name, data in (("ok_444.jpg", c444), ("ok_422.jpg", c422), ("ok_420.jpg", c420),
                   ("ok_gray.jpg", gray), ("ok_small.jpg", small), ("ok_black.jpg", black),
                   ("ok_exif_gps.jpg", exif_gps)):
    write(name, data)
    case(name[:-4], "valid", "decode", name, "--ac --histo", expect=DECODED)
write("ok_progressive.jpg", progressive)
case("ok_progressive", "valid", "decode", "ok_progressive.jpg", "--ac --histo")
write("ok_cmyk.jpg", cmyk)
case("ok_cmyk", "valid", "decode", "ok_cmyk.jpg", "--ac --histo")

# --- C: memory safety -----------------------------------------------------------
def patch_sof_dims(data, w, h):
    d = bytearray(data)
    i = marker_pos(d, 0xC0)
    d[i + 5:i + 7] = struct.pack(">H", h)
    d[i + 7:i + 9] = struct.pack(">H", w)
    return bytes(d)


case("C1_huge_65535", "C1", "decode", write("c1_huge_65535.jpg", patch_sof_dims(gray8, 65535, 65535)), "--ac")
case("C1_huge_46345", "C1", "decode", write("c1_huge_46345.jpg", patch_sof_dims(gray8, 46345, 46345)), "--ac")

d = bytearray(gray)
i = marker_pos(d, 0xDA)
assert d[i + 4] == 1
d[i + 6] = 0x40                       # DC table selector 4 (valid: 0-3)
case("C2_sos_td4", "C2", "decode", write("c2_sos_td4.jpg", bytes(d)), "--ac")
d = bytearray(c420)
i = marker_pos(d, 0xDA)
d[i + 6] = 0xFF                       # DC and AC selectors 15
case("C2_sos_sel15", "C2", "decode", write("c2_sos_sel15.jpg", bytes(d)), "--ac")

counts_258 = bytes([0] * 14 + [3, 255])
case("C3_dht_258", "C3", "decode",
     write("c3_dht_258.jpg", insert_after_app0(c444, seg(0xC4, bytes([0x13]) + counts_258 + bytes(258)))))
case("C3_dht_4080", "C3", "decode",
     write("c3_dht_4080.jpg", insert_after_app0(c444, seg(0xC4, bytes([0x13]) + bytes([255] * 16)
                                                       + (bytes(range(256)) * 16)[:4080]))))

case("C4_white_histo", "C4", "decode", write("c4_white.jpg", white), "--ac --histo", expect=DECODED)

i = marker_pos(c444, 0xC0)
end = i + 2 + struct.unpack(">H", c444[i + 2:i + 4])[0]
sof_h0 = seg(0xC0, bytes([8]) + struct.pack(">HH", 48, 64) + bytes([3, 1, 0x11, 0, 2, 0x01, 1, 3, 0x11, 1]))
case("C5_second_sof_h0", "C5", "decode", write("c5_second_sof_h0.jpg", c444[:end] + sof_h0 + c444[end:]),
     "--ac", forbid="Chroma subsampling: +0x0")
i = marker_pos(gray, 0xDA)
end = i + 2 + struct.unpack(">H", gray[i + 2:i + 4])[0]
sos3 = b"\xff\xda" + struct.pack(">H", 12) + bytes([3, 1, 0x00, 2, 0x00, 3, 0x00, 0, 63, 0])
case("C5_sof1_sos3", "C5", "decode", write("c5_sof1_sos3.jpg", gray[:i] + sos3 + gray[end:]), "--ac")


# --- H: hangs -----------------------------------------------------------------
def ifd_be(entries, next_off):
    b = struct.pack(">H", len(entries))
    for tag, fmt, cnt, val in entries:
        b += struct.pack(">HHI", tag, fmt, cnt) + val
    return b + struct.pack(">I", next_off)


tiff = b"MM\x00\x2a" + struct.pack(">I", 8) + ifd_be([(0x0112, 3, 1, struct.pack(">HH", 1, 0))], 8)
case("H1_ifd_loop", "H1", "decode",
     write("h1_ifd_loop.jpg", insert_after_app0(c444, seg(0xE1, b"Exif\x00\x00" + tiff))), timeout=10)


def bim(rid, data, length=None):
    n = len(data) if length is None else length
    return b"8BIM" + struct.pack(">H", rid) + b"\x00\x00" + struct.pack(">I", n) + data + (b"\x00" if len(data) % 2 else b"")


def app13_only(blocks):
    payload = b"Photoshop 3.0\x00" + blocks + b"\x00" * 4
    return b"\xff\xd8" + seg(0xED, payload) + b"\xff\xd9"


case("H2_8bim_wrap", "H2", "decode", write("h2_8bim_wrap.jpg", app13_only(bim(0x7777, b"", 0xFFFFFFF4))), timeout=10)
case("H2_8bim_wrap_hex", "H2", "decode", write("h2_8bim_wrap_hex.jpg", app13_only(bim(0x03E9, b"", 0xFFFFFFF4))),
     timeout=10)

xmp_short = b"\xff\xe1" + struct.pack(">H", 30) + b"http://ns.adobe.com/xap/1.0/\x00" + b"<x/>"
case("H3_xmp_short", "H3", "decode", write("h3_xmp_short.jpg", insert_after_app0(c444, xmp_short)), timeout=10)

avi = b"RIFF" + struct.pack("<I", 100) + b"AVI " + b"JUNK" + struct.pack("<I", 0xFFFFFFF8) + bytes(64)
case("H4_avi_loop", "H4", "decode", write("h4_avi_loop.avi", avi), timeout=10)

# H5: EXIF IFD1 thumbnail (compression 6) pointing at an SOS with no end marker
head, rest = after_app0(c444)
tiff = bytearray(b"II*\x00" + struct.pack("<I", 8) + struct.pack("<HI", 0, 14))
ifd1 = len(tiff)
thumb_entries = [(0x0103, 3, 1, 6), (0x0201, 4, 1, 0), (0x0202, 4, 1, 1000)]
tiff += struct.pack("<H", len(thumb_entries))
for tag, fmt, cnt, val in thumb_entries:
    tiff += struct.pack("<HHII", tag, fmt, cnt, val)
tiff += struct.pack("<I", 0)
app1_len = 4 + 6 + len(tiff)
tiff_start = len(head) + 4 + 6
trailer_pos = len(head) + app1_len + len(rest)
struct.pack_into("<I", tiff, ifd1 + 2 + 12 + 8, trailer_pos - tiff_start)
trailer = b"\xff\xd8\xff\xda" + bytes(64)
case("H5_thumb_sos", "H5", "decode",
     write("h5_thumb_sos.jpg", head + seg(0xE1, b"Exif\x00\x00" + bytes(tiff)) + rest + trailer), timeout=10)

# H6: JFIF APP0 declaring a 255x255 thumbnail with no thumbnail data
app0 = seg(0xE0, b"JFIF\x00" + bytes([1, 1, 0]) + struct.pack(">HH", 1, 1) + bytes([255, 255]))
_, rest = after_app0(c444)
case("H6_app0_thumb", "H6", "decode", write("h6_app0_thumb.jpg", b"\xff\xd8" + app0 + rest), timeout=10)

case("H7_psd_guides", "H7", "decode",
     write("h7_psd_guides.jpg", app13_only(bim(0x0408, struct.pack(">IIII", 1, 576, 576, 0xFFFFFFFF)))), timeout=10)
case("H7_psd_layer_groups", "H7", "decode",
     write("h7_psd_layer_groups.jpg", app13_only(bim(0x0402, b"", 0x7FFFFFFE))), timeout=10)


def iptc(rec, ds, data):
    return bytes([0x1C, rec, ds]) + struct.pack(">H", len(data)) + data


case("H8_iptc_num3", "H8", "decode",
     write("h8_iptc_num3.jpg", app13_only(bim(0x0404, iptc(2, 0, b"\x00\x00\x04")))), timeout=10)

# --- U: GUI -------------------------------------------------------------------
case("U1_second_file", "U1", "gui", ["ok_444.jpg", "ok_420.jpg"], timeout=30,
     expect="file=ok_420.jpg scan=1")
case("U1_gray_then_color", "U1", "gui", ["ok_gray.jpg", "ok_444.jpg"], "--mouse", timeout=30,
     expect="harness: done")
case("U2_undecodable_after_decoded", "U2", "gui", ["ok_444.jpg", "ok_progressive.jpg"], "--mouse", timeout=30,
     expect="harness: done")
case("U3_mouse_small_image", "U3", "gui", ["ok_small.jpg"], "--mouse", timeout=30, expect="harness: done")

with open(out_dir / "cases.tsv", "w") as f:
    for row in cases:
        f.write("\t".join(row) + "\n")
print(f"{len(cases)} cases, {len(list(files_dir.iterdir()))} files")
