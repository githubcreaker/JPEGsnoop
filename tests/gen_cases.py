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

import hashlib
import io
import os
import random
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
# Valid files whose scan isn't decoded (progressive, CMYK): the report says
# so, and there is no image or histogram
OK_NO_IMAGE = "^harness: decoded=0 preview=0x0 "
write("ok_progressive.jpg", progressive)
case("ok_progressive", "valid", "decode", "ok_progressive.jpg", "--ac --histo", expect=OK_NO_IMAGE, forbid="ERROR")
case("ok_progressive_note", "valid", "decode", "ok_progressive.jpg", "--ac --histo",
     expect="^  NOTE: Scan parsing doesn't support this SOF mode\\.$")
case("ok_progressive_histogram", "valid", "decode", "ok_progressive.jpg", "--ac --histo",
     expect="^harness: histogram bars rgb=-1 y=-1$")
write("ok_cmyk.jpg", cmyk)
case("ok_cmyk", "valid", "decode", "ok_cmyk.jpg", "--ac --histo", expect=OK_NO_IMAGE, forbid="ERROR")
case("ok_cmyk_note", "valid", "decode", "ok_cmyk.jpg", "--ac --histo",
     expect="^  NOTE: Scan parsing doesn't support CMYK files yet\\.$")

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


# C7: a sharp edge coded at quality 100, then decoded with every quantiser
# set to 64, gives IDCT results beyond int16_t. They now saturate at its
# limits (before: an undefined conversion, which wrapped to -25928..25416).
def with_dqt_values(data, q):
    d = bytearray(data)
    i = marker_pos(d, 0xDB)
    end = i + 2 + struct.unpack(">H", d[i + 2:i + 4])[0]
    j = i + 4
    while j < end:
        assert d[j] >> 4 == 0, "8-bit tables only"
        d[j + 1:j + 65] = bytes([q]) * 64
        j += 65
    return bytes(d)


edge = Image.new("L", (16, 16))
edge.putdata([255 if x >= 4 else 0 for y in range(16) for x in range(16)])
case("C7_idct_out_of_range", "C7", "decode",
     write("c7_idct_range.jpg", with_dqt_values(jpeg(edge, quality=100), 64)), "--ac --histo",
     expect="Y  component histo: \\[min=-32768 max=32767")

# C7: the histogram sums are over every pixel. In 32 bits the pre-ranged Y sum
# (1016 per white pixel) overflowed at 2.1 million pixels, and the average
# luminance sum wrapped at 16.8 million. Both are valid images.
case("C7_histo_sum_2mp", "C7", "decode",
     write("c7_white_1600x1400.jpg", jpeg(Image.new("L", (1600, 1400), 255), quality=95)), "--histo",
     timeout=90, expect="Y  component histo: \\[min= +[0-9]+ max= 1016 avg= 1016\\.0\\]")
case("C7_avg_luma_17mp", "C7", "decode",
     write("c7_white_4200x4100.jpg", jpeg(Image.new("L", (4200, 4100), 255), quality=95)),
     timeout=90, expect="Y=\\[255\\] \\(range")
# The average divided by (width + 1) x (height + 1): 239 for a white 32x32 image
case("C7_avg_luma_count", "C7", "decode", "c4_white.jpg", expect="Y=\\[255\\] \\(range")


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


# --- R: report output (port regressions) --------------------------------------
# Minimal TIFF/EXIF builder: each IFD sits at a fixed offset with its
# out-of-line values right after it.
def tiff_ifd(bo, base, entries, next_off=0):
    """entries: (tag, type, count, payload bytes); payloads over 4 bytes are stored after the IFD."""
    data_off = base + 2 + 12 * len(entries) + 4
    out, data = struct.pack(bo + "H", len(entries)), b""
    for tag, typ, count, payload in sorted(entries):
        if len(payload) <= 4:
            field = payload.ljust(4, b"\x00")
        else:
            field = struct.pack(bo + "I", data_off + len(data))
            data += payload + (b"\x00" if len(payload) % 2 else b"")
        out += struct.pack(bo + "HHI", tag, typ, count) + field
    return out + struct.pack(bo + "I", next_off) + data


def tiff(bo, ifds):
    """ifds: {offset: bytes}. Returns the TIFF block (header + IFDs)."""
    hdr = (b"II*\x00" if bo == "<" else b"MM\x00*") + struct.pack(bo + "I", 8)
    size = max(off + len(b) for off, b in ifds.items())
    t = bytearray(size)
    t[0:8] = hdr
    for off, b in ifds.items():
        assert all(x == 0 for x in t[off:off + len(b)]), "IFD overlap"
        t[off:off + len(b)] = b
    return bytes(t)


def ascii_(s):
    return (2, len(s) + 1, s.encode("latin-1") + b"\x00")


def shorts(bo, *v):
    return (3, len(v), struct.pack(bo + "H" * len(v), *v))


def longs(bo, *v):
    return (4, len(v), struct.pack(bo + "I" * len(v), *v))


def rationals(bo, *v):
    return (5, len(v), b"".join(struct.pack(bo + "II", n, d) for n, d in v))


def srationals(bo, *v):
    return (10, len(v), b"".join(struct.pack(bo + "ii", n, d) for n, d in v))


def entry(tag, value):
    return (tag,) + value


def app1_exif(t):
    return seg(0xE1, b"Exif\x00\x00" + t)


# EXIF with GPS, an unknown GPS tag, a 2-value ISO, a light source and an XP tag
bo = "<"
SUB, GPS = 0x100, 0x200
xp = "Hello XP comment".encode("utf-16-le") + b"\x00\x00"
ifd0 = tiff_ifd(bo, 8, [entry(0x010F, ascii_("TestMake")), entry(0x0110, ascii_("TestModel")),
                        (0x9C9C, 1, len(xp), xp), entry(0x8769, longs(bo, SUB)), entry(0x8825, longs(bo, GPS))])
sub = tiff_ifd(bo, SUB, [entry(0x8827, shorts(bo, 100, 200)), entry(0x9208, shorts(bo, 12))])
gps = tiff_ifd(bo, GPS, [(0x0000, 1, 4, bytes([2, 2, 0, 0])), entry(0x0001, ascii_("N")),
                         entry(0x0002, rationals(bo, (37, 1), (48, 1), (3012, 100))),
                         entry(0x0006, rationals(bo, (1234, 10))),
                         entry(0x0007, rationals(bo, (12, 1), (34, 1), (5678, 100))),
                         entry(0x000D, rationals(bo, (555, 10))),
                         entry(0x0050, shorts(bo, 7))])
name = write("r_exif.jpg", insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0, SUB: sub, GPS: gps}))))
for cid, finding, pattern in (
        ("R1_gps_latitude", "R1", "37 deg 48' 30\\.120\""),
        ("R1_gps_altitude", "R1", "GPSAltitude +\\] = 123\\.400 m"),
        ("R1_gps_timestamp", "R1", "GPSTimeStamp +\\] = 12:34:56\\.78"),
        ("R1_gps_speed", "R1", "GPSSpeed +\\] = 55\\.500"),
        ("R11_two_shorts", "R11", "ISOSpeedRatings +\\] = 100, 200"),
        ("R12_unknown_gps_tag", "R12", "GPS\\.0x0050"),
        ("R19_light_source", "R19", "Daylight fluorescent \\(D 5700 - 7100K\\)"),
        ("XP_unicode_tag", "XP", "Hello XP comment")):
    case(cid, finding, "decode", name, "--unknown-exif", expect=pattern)

# C7: a negative GPS degree was converted to an unsigned int (undefined; 0 on
# arm64, 4294967295 on x86). GPSLatitude is RATIONAL, so declare it SRATIONAL:
# since R21 a RATIONAL can't be negative.
ifd0 = tiff_ifd(bo, 8, [entry(0x8825, longs(bo, GPS))])
gps = tiff_ifd(bo, GPS, [entry(0x0001, ascii_("N")),
                         entry(0x0002, srationals(bo, (-1, 1), (30, 1), (0, 1)))])
case("C7_gps_negative_degree", "C7", "decode",
     write("c7_gps_negative.jpg", insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0, GPS: gps})))),
     expect="GPSLatitude +\\] = -1 deg 30' 0\\.000\"")

# JPEG comment from known editing software
com = seg(0xFE, b"CREATOR: gd-jpeg v1.0 (using IJG JPEG v62), quality = 80")
name = write("r_comment.jpg", insert_after_app0(c444, com))
case("R2_comment_text", "R2", "decode", name, expect="Comment=CREATOR: gd-jpeg v1\\.0")
case("R8_comment_known_software", "R8", "decode", name, expect="JFIF COMMENT field is known software")

# Photoshop IRB: IPTC, Unicode version info, a slices v7 descriptor, a URL
ip = iptc(2, 5, b"Hello World") + iptc(2, 120, b"A caption")
case("R3_iptc_value", "R3", "decode", write("r_iptc.jpg", app13_only(bim(0x0404, ip))),
     expect="IPTC \\[002:005\\] Object Name +=\\s+\"Hello World\"")


def uni(s):
    return struct.pack(">I", len(s)) + s.encode("utf-16-be")


vinfo = struct.pack(">I", 1) + b"\x01" + uni("Adobe Photoshop") + uni("Adobe Photoshop 2024") + struct.pack(">I", 1)
case("R5_photoshop_unicode", "R5", "decode", write("r_ps_version.jpg", app13_only(bim(0x0421, vinfo))),
     expect="Adobe Photoshop 2024")


def lstr4(s):
    return struct.pack(">I", 0) + s


desc = (uni("") + lstr4(b"null") + struct.pack(">I", 2) + lstr4(b"Vrsn") + b"long" + struct.pack(">I", 1)
        + lstr4(b"Nm  ") + b"TEXT" + uni("Hi"))
name = write("r_ps_slices7.jpg", app13_only(bim(0x041A, struct.pack(">II", 7, 16) + desc)))
case("R4_photoshop_ostype", "R4", "decode", name, expect="OSType key += \"?TEXT", forbid="Parsing offset length mismatch")

url = b"http://example.com/abc"
case("R19_hexdump_header", "R19", "decode", write("r_ps_url.jpg", app13_only(bim(0x040B, url))),
     forbid="s =$")

# Canon makernote saying the file came from a RAW converter (edit assessment)
bo = ">"
SUB, MK, D = 0x40, 0x80, 0xC0
itype = b"CRW:EOS 300D DIGITAL CMOS RAW\x00"
ifd0 = tiff_ifd(bo, 8, [entry(0x010F, ascii_("Canon")), entry(0x8769, longs(bo, SUB))])
sub = tiff_ifd(bo, SUB, [(0x927C, 7, 64, struct.pack(bo + "I", MK))])
mk = tiff_ifd(bo, MK, [(0x0006, 2, len(itype), struct.pack(bo + "I", D))])
t = bytearray(tiff(bo, {8: ifd0, SUB: sub, MK: mk}))
t += bytes(D + len(itype) - len(t)) if len(t) < D + len(itype) else b""
t[D:D + len(itype)] = itype
case("R6_raw_converter", "R6", "decode", write("r_canon_crw.jpg", insert_after_app0(c444, app1_exif(bytes(t)))),
     expect="Additional EXIF fields indicate software processing")

# Portrait 4:2:2 reports 1x2
case("R7_portrait_subsampling", "R7", "decode",
     write("r_portrait_422.jpg", jpeg(gradient(48, 64), quality=90, subsampling=1)),
     expect="Chroma subsampling: +1x2")

# Camera exception list: a SONY model that isn't an exception, without makernotes
bo = "<"
ifd0 = tiff_ifd(bo, 8, [entry(0x010F, ascii_("SONY")), entry(0x0110, ascii_("ILCE-7M3"))])
case("R9_exception_list", "R9", "decode",
     write("r_sony_no_makernote.jpg", insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0})))),
     expect="ASSESSMENT: Class 2")

# DAC numbering, AVI timestamp/sample rate, ICC four-character codes
case("R19_dac_index", "R19", "decode",
     write("r_dac.jpg", insert_after_app0(c444, seg(0xCC, bytes([0x10, 0x01])))),
     expect="#01: Table destination identifier")


def chunk(fourcc, data):
    return fourcc + struct.pack("<I", len(data)) + data + (b"\x00" if len(data) % 2 else b"")


strh = b"vidsMJPG" + struct.pack("<11I", 0, 0, 0, 1, 30, 0, 10, 0, 0, 0, 0) + bytes(4)
strl = b"strl" + chunk(b"strh", strh) + chunk(b"strf", bytes(40)) + chunk(b"strn", b"Video\x00")
avi_body = (b"AVI " + chunk(b"LIST", b"hdrl" + chunk(b"avih", bytes(56)))
            + chunk(b"LIST", strl) + chunk(b"IDIT", b"THU OCT 13 12:00:00 2016\n\x00"))
name = write("r_avi.avi", b"RIFF" + struct.pack("<I", len(avi_body)) + avi_body)
case("R19_avi_sample_rate", "R19", "decode", name, expect="Sample Rate\\]  = \\[30\\.00\\]")
case("R19_avi_timestamp", "R19", "decode", name, expect="Timestamp\\] = \\[THU OCT 13 12:00:00 2016\\]")

icc = bytearray(128)
struct.pack_into(">I", icc, 0, 128)
icc[4:8], icc[12:16], icc[16:20], icc[20:24], icc[36:40] = b"appl", b"mntr", b"RGB ", b"XYZ ", b"acsp"
icc[8:12] = bytes([4, 0, 0, 0])
app2 = seg(0xE2, b"ICC_PROFILE\x00" + bytes([1, 1]) + bytes(icc))
case("R19_icc_fourcc", "R19", "decode", write("r_icc.jpg", insert_after_app0(c444, app2)),
     expect="'RGB ' \\(0x52474220\\)")


# --- R13-R18: inherited decode errors -------------------------------------------
def huff_codes(counts, values):
    """Canonical Huffman codes (T.81 Annex C): symbol -> (code, length)."""
    codes, code, k = {}, 0, 0
    for length in range(1, 17):
        for _ in range(counts[length - 1]):
            codes[values[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


DC_COUNTS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]  # T.81 Table K.3
DC_VALUES = list(range(12))


class Bits:
    """Entropy-coded data writer with 0xFF byte stuffing."""

    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, value, length):
        for i in range(length - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out += bytes([self.acc, 0]) if self.acc == 0xFF else bytes([self.acc])
                self.acc, self.n = 0, 0

    def pos(self):
        """(byte, bit) of the next bit."""
        return len(self.out), self.n

    def flush(self):
        if self.n:
            self.put((1 << (8 - self.n)) - 1, 8 - self.n)
        return bytes(self.out)

    def restart(self, k, fill=0):
        """Pad to a byte with 1 bits, then fill bytes and RSTk (mod 8)."""
        self.flush()
        self.out += b"\xff" * fill + bytes([0xFF, 0xD0 + k % 8])


def dc_only_jpeg(width, height, comps, level, restart=0, fill=0):
    """Baseline JPEG made of flat 8x8 blocks (DC only, quantiser 1), with any
    sampling factors and any level, even outside 0..255, which Pillow can't
    write. comps: [(id, h, v)]; level(comp_index, x, y) is the component's
    level for the block whose top-left image pixel is (x, y). restart: a DRI
    interval in MCUs, with an RST marker after each interval but the last,
    and fill 0xFF fill bytes before each (T.81 B.1.1.2).
    Returns (file, offset of the scan data, [(byte, bit) where each MCU starts
    in the scan data])."""
    hmax, vmax = max(c[1] for c in comps), max(c[2] for c in comps)
    mcus_x = -(-width // (8 * hmax))
    dc = huff_codes(DC_COUNTS, DC_VALUES)
    bits, pred, mcu_pos = Bits(), [0] * len(comps), []
    for my in range(-(-height // (8 * vmax))):
        for mx in range(mcus_x):
            mcu = my * mcus_x + mx
            if restart and mcu and mcu % restart == 0:
                bits.restart(mcu // restart - 1, fill)
                pred = [0] * len(comps)
            mcu_pos.append(bits.pos())
            for ci, (_, h, v) in enumerate(comps):
                for by in range(v):
                    for bx in range(h):
                        x = (mx * hmax + bx * (hmax // h)) * 8
                        y = (my * vmax + by * (vmax // v)) * 8
                        coef = round((level(ci, x, y) - 128) * 8)
                        diff, pred[ci] = coef - pred[ci], coef
                        cat = abs(diff).bit_length()
                        bits.put(*dc[cat])
                        if cat:
                            bits.put(diff if diff > 0 else diff + (1 << cat) - 1, cat)
                        bits.put(0, 1)      # EOB: the AC table's only code
    head = (b"\xff\xd8" + seg(0xDB, bytes([0]) + bytes([1]) * 64)
            + seg(0xC0, bytes([8]) + struct.pack(">HHB", height, width, len(comps))
                  + b"".join(bytes([cid, (h << 4) | v, 0]) for cid, h, v in comps))
            + seg(0xC4, bytes([0x00] + DC_COUNTS + DC_VALUES))
            + seg(0xC4, bytes([0x10, 1] + [0] * 15 + [0x00]))
            + (seg(0xDD, struct.pack(">H", restart)) if restart else b"")
            + seg(0xDA, bytes([len(comps)]) + b"".join(bytes([cid, 0x00]) for cid, _, _ in comps)
                  + bytes([0, 63, 0])))
    return head + bits.flush() + b"\xff\xd9", len(head), mcu_pos


# R13: a big-endian EXIF thumbnail (Compression is a SHORT, read as 4 bytes)
thumb = jpeg(gradient(16, 16), quality=80)
bo = ">"
IFD1, T = 0x40, 0x80
ifd0 = tiff_ifd(bo, 8, [entry(0x010F, ascii_("TestMake"))], next_off=IFD1)
ifd1 = tiff_ifd(bo, IFD1, [entry(0x0103, shorts(bo, 6)), entry(0x0201, longs(bo, T)),
                           entry(0x0202, longs(bo, len(thumb)))])
t = bytearray(tiff(bo, {8: ifd0, IFD1: ifd1}))
t += bytes(T - len(t)) + thumb
case("R13_big_endian_thumbnail", "R13", "decode",
     write("r13_be_thumb.jpg", insert_after_app0(c444, app1_exif(bytes(t)))),
     expect="\\*\\*\\* Embedded JPEG Thumbnail \\*\\*\\*")

# R14: a DHT redefining a table before the scan (allowed by T.81). The first
# definition (all 8-bit codes) left fast-lookup entries for the prefixes of the
# real table's longer codes, which a noisy image uses.
rnd = random.Random(1)
noise = Image.new("RGB", (64, 64))
noise.putdata([(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(64 * 64)])
noise444 = jpeg(noise, quality=90, subsampling=0)
dummy_ac = seg(0xC4, bytes([0x10]) + bytes([0] * 7 + [255] + [0] * 8) + bytes(range(255)))
case("R14_dht_redefined", "R14", "decode", write("r14_dht_redefined.jpg", insert_after_app0(noise444, dummy_ac)),
     "--ac", expect=DECODED, forbid="ERROR")

# R15: Y 4x1, Cb/Cr 2x1. Cb is high in the left half of each 32-pixel MCU
# (blue) and low in the right half (yellow). The second chroma block was drawn
# at x 8-23 instead of 16-31, so x 12 was yellow and x 24-31 had no chroma.
data, _, _ = dc_only_jpeg(64, 8, [(1, 4, 1), (2, 2, 1), (3, 2, 1)],
                          lambda c, x, y: 128 if c != 1 else (188 if x % 32 < 16 else 68))
name = write("r15_css_4x1_2x1.jpg", data)
case("R15_chroma_4x1_2x1", "R15", "decode", name, "--ac --pixels 12,4/28,4",
     expect="pixel 12,4 = 128,10[6-8],23[3-5]$")
case("R15_chroma_4x1_2x1_right", "R15", "decode", name, "--ac --pixels 12,4/28,4",
     expect="pixel 28,4 = 128,1(4[7-9]|50),2[1-3]$")

# R17: Y above 255 in blocks 1 and 2 of a 20x8 grey image (block 2 is half
# padding). 12 of 20 columns x 8 rows = 96 real pixels clip; the first note
# is for MCU 1 and should give that MCU's position, not the end of the scan.
data, scan_off, mcu_pos = dc_only_jpeg(20, 8, [(1, 1, 1)], lambda c, x, y: 128 if x < 8 else 300)
name = write("r17_ycc_clip.jpg", data)
byte, bit = mcu_pos[1]
case("R17_clip_note_offset", "R17", "decode", name, "--histo",
     expect=f"MCU=\\(   1,   0\\).* Y Overflow @ Offset 0x{scan_off + byte:08x}\\.{bit}$")
case("R17_clip_totals_ycc", "R17", "decode", name, "--histo", expect="Y  component: \\[<0=    0\\] \\[>255=   96\\]")
# Without the histograms nothing is counted: the totals said 0
case("R17_clip_not_counted", "R17", "decode", name, expect="clipping in DC: not counted",
     forbid="Y  component: \\[<0=    0\\] \\[>255=    0\\]")
# RGB clipping comes from in-range YCC: Y 200 with Cr 250 gives red 371
data, _, _ = dc_only_jpeg(20, 8, [(1, 1, 1), (2, 1, 1), (3, 1, 1)],
                          lambda c, x, y: 128 if x < 8 else (200, 128, 250)[c])
case("R17_clip_totals_rgb", "R17", "decode", write("r17_rgb_clip.jpg", data), "--histo",
     expect="R  component: \\[<0=    0\\] \\[>255=   96\\]")

# R18: UserComment with fewer than the 8 bytes of its character code
bo = "<"
SUB = 0x40
ifd0 = tiff_ifd(bo, 8, [entry(0x8769, longs(bo, SUB))])
sub = tiff_ifd(bo, SUB, [(0x9286, 7, 6, b"ASCII\x00" + b"\x00\x00Overrun text\x00")])
case("R18_user_comment_short", "R18", "decode",
     write("r18_user_comment.jpg", insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0, SUB: sub})))),
     expect="UserComment +\\] = \"\"", forbid="Overrun text")

# R18: ICC version 4.2, profile flag bit 1 and device attribute bits 2 and 3
# set (ICC.1:2022 7.2.4, Tables 21 and 22)
icc = bytearray(128)
struct.pack_into(">I", icc, 0, 128)
icc[4:8], icc[12:16], icc[16:20], icc[20:24], icc[36:40] = b"appl", b"mntr", b"RGB ", b"XYZ ", b"acsp"
icc[8:12] = bytes([4, 0x20, 0, 0])
struct.pack_into(">I", icc, 44, 0x00000003)           # embedded, not usable on its own
struct.pack_into(">II", icc, 56, 0, 0x0000000C)       # negative, black & white media
name = write("r18_icc_header.jpg",
             insert_after_app0(c444, seg(0xE2, b"ICC_PROFILE\x00" + bytes([1, 1]) + bytes(icc))))
for cid, pattern in (("R18_icc_version", "Profile Version +: 4\\.2\\.0\\.0 \\(0x04200000\\)"),
                     ("R18_icc_flag_independent", "Profile flags +> Profile can't be used independently"),
                     ("R18_icc_polarity", "Device attributes +> Media polarity = negative"),
                     ("R18_icc_bw_media", "Device attributes +> Black & white media")):
    case(cid, "R18", "decode", name, expect=pattern)

# R18: EXIF TIFF header with neither "II" nor "MM"
case("R18_exif_byte_order", "R18", "decode",
     write("r18_exif_byte_order.jpg", insert_after_app0(c444, app1_exif(b"XX\x00\x2a" + bytes(12)))),
     expect="ERROR: Unknown EXIF byte order")

# R18: chroma DQT with a zero entry (zig-zag index 1). The estimate used the
# previous entry's percentage for it; a zero now counts as 999.99%, as for luma.
d = bytearray(c444)
i = 2
while d[i + 1] != 0xDA:                    # every DQT segment before the scan
    end = i + 2 + struct.unpack(">H", d[i + 2:i + 4])[0]
    j = i + 4
    while d[i + 1] == 0xDB and j < end:
        if d[j] & 0x0F == 1:
            d[j + 1:j + 65] = bytes([99, 0]) + bytes([99]) * 62
        j += 65
    i = end
case("R18_dqt_zero_entry", "R18", "decode", write("r18_dqt_zero.jpg", bytes(d)),
     expect="Approx quality factor = 31\\.41 \\(scaling=159\\.19 ", forbid="scaling=152\\.67")

# R18: APP12 "Ducky" Save for Web quality followed by an APP13 without one
ducky = seg(0xEC, b"Ducky" + struct.pack(">HHI", 1, 4, 60) + bytes(2))
app13 = seg(0xED, b"Photoshop 3.0\x00" + bim(0x0404, iptc(2, 5, b"Title")))
case("R18_ducky_then_app13", "R18", "decode",
     write("r18_ducky_app13.jpg", insert_after_app0(c444, ducky + app13)),
     expect="NOTE: Photoshop IRB detected")

# R18: truncated scan data (no EOI): Buf() returns 0 past the end of the file
i = marker_pos(c444, 0xDA)
scan = i + 2 + struct.unpack(">H", c444[i + 2:i + 4])[0]
case("R18_truncated_scan", "R18", "decode", write("r18_truncated.jpg", c444[:scan + (len(c444) - scan) // 3]),
     "--ac", expect="ERROR: Scan data ends at the end of the file")

# R18: IPTC 2-byte field ending on the last byte of the file (BufX returned 0).
# The block is even-length, so bim() adds no pad byte after it.
block = bim(0x0404, iptc(2, 5, b"AB") + iptc(2, 0, b"\x00\x04"))
assert block.endswith(b"\x00\x04")
case("R18_bufx_last_bytes", "R18", "decode",
     write("r18_bufx_end.jpg", b"\xff\xd8" + seg(0xED, b"Photoshop 3.0\x00" + block)),
     expect="Record Version += 4$")

# R18: FlashPix contents list (the reader advanced m_nPos twice in one expression)
fpxr = b"FPXR\x00" + bytes([0, 1]) + struct.pack(">H", 1) + struct.pack(">I", 0x80000010) + b"\x00" \
    + "Contents".encode("utf-16-le") + b"\x00\x00"
case("R18_flashpix_contents", "R18", "decode", write("r18_fpxr.jpg", insert_after_app0(c444, seg(0xE2, fpxr))),
     expect="Entity Size = 2147483664$")

# R20: histogram minimums and maximums started at 0, so a white image showed
# min=0 and a black one max=0
case("R20_histo_min_white", "R20", "decode", "c4_white.jpg", "--ac --histo",
     expect="Y  component histo: \\[min= 1016 max= 1016 avg= 1016\\.0\\]")
case("R20_histo_max_black", "R20", "decode", "ok_black.jpg", "--ac --histo",
     expect="Y  component histo: \\[min=-1024 max=-1024 avg=-1024\\.0\\]")

# R21: RATIONAL parts are unsigned (TIFF 6.0, EXIF), but were read as signed
bo = "<"
ifd0 = tiff_ifd(bo, 8, [entry(0x011A, rationals(bo, (3000000000, 1))), entry(0x8825, longs(bo, GPS))])
gps = tiff_ifd(bo, GPS, [entry(0x0001, ascii_("N")),
                         entry(0x0002, rationals(bo, (0xFFFFFFFF, 1), (30, 1), (0, 1)))])
name = write("r21_rational_unsigned.jpg", insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0, GPS: gps}))))
case("R21_rational_unsigned", "R21", "decode", name, expect="XResolution +\\] = 3000000000/1")
case("R21_rational_unsigned_gps", "R21", "decode", name, expect="GPSLatitude +\\] = 4294967295 deg 30' 0\\.000\"")


# R22: UserComment in Unicode (UCS-2, in the EXIF byte order) was read one byte
# at a time and stopped at the first zero byte
def user_comment_file(bo, text):
    sub_off = 0x40
    comment = b"UNICODE\x00" + text.encode("utf-16-le" if bo == "<" else "utf-16-be")
    ifd0 = tiff_ifd(bo, 8, [entry(0x8769, longs(bo, sub_off))])
    sub = tiff_ifd(bo, sub_off, [(0x9286, 7, len(comment), comment)])
    return insert_after_app0(c444, app1_exif(tiff(bo, {8: ifd0, sub_off: sub})))


case("R22_unicode_comment_le", "R22", "decode", write("r22_comment_le.jpg", user_comment_file("<", "Hello World")),
     expect="UserComment +\\] = \"Hello World\"")
case("R22_unicode_comment_be", "R22", "decode", write("r22_comment_be.jpg", user_comment_file(">", "Hello World")),
     expect="UserComment +\\] = \"Hello World\"")


# --- F: follow-up review (see docs/code-review-macos-qt6.md) ------------------
def dc_only_scans(width, height, levels, scans, ids=(1, 2, 3)):
    """Like dc_only_jpeg, all components 1x1, but split into the given scans:
    lists of component indexes. levels: one flat level per component."""
    dc = huff_codes(DC_COUNTS, DC_VALUES)
    nmcu = (-(-width // 8)) * (-(-height // 8))
    out = (b"\xff\xd8" + seg(0xDB, bytes([0]) + bytes([1]) * 64)
           + seg(0xC0, bytes([8]) + struct.pack(">HHB", height, width, len(levels))
                 + b"".join(bytes([cid, 0x11, 0]) for cid in ids[:len(levels)]))
           + seg(0xC4, bytes([0x00] + DC_COUNTS + DC_VALUES))
           + seg(0xC4, bytes([0x10, 1] + [0] * 15 + [0x00])))
    for comps in scans:
        bits, pred = Bits(), {}
        for _ in range(nmcu):
            for ci in comps:
                coef = round((levels[ci] - 128) * 8)
                diff, pred[ci] = coef - pred.get(ci, 0), coef
                cat = abs(diff).bit_length()
                bits.put(*dc[cat])
                if cat:
                    bits.put(diff if diff > 0 else diff + (1 << cat) - 1, cat)
                bits.put(0, 1)
        out += (seg(0xDA, bytes([len(comps)]) + b"".join(bytes([ids[ci], 0x00]) for ci in comps)
                    + bytes([0, 63, 0])) + bits.flush())
    return out + b"\xff\xd9"


# F1: a Y-only scan, then a scan of all three components (invalid in sequential
# mode). The second scan header switched the decoder to 3 components while only
# the Y map existed, so choosing an image channel read a null chroma map.
case("F1_later_scan_state", "F1", "gui",
     write("f1_scan1_then_scan3.jpg", dc_only_scans(16, 8, (150, 100, 200), [[0], [0, 1, 2]])),
     "--cycle-channels", timeout=60, expect="harness: done")

# F2: Photoshop descriptor whose key length (0xFFFFFFF8) wrapped the cursor back
# 8 bytes, so the same item repeated for its 2^28 declared items
desc = uni("") + lstr4(b"null") + struct.pack(">I", 0x10000000) + struct.pack(">I", 0xFFFFFFF8) + bytes(16)
case("F2_ps_string_wrap", "F2", "decode",
     write("f2_desc_wrap.jpg", app13_only(bim(0x041A, struct.pack(">II", 7, 16) + desc))), timeout=10,
     expect="harness: decoded=")


# F2 (second review): the limit stopped at the resource's end. A descriptor cut
# off right after its name read its classID from the data after the resource,
# here a 40,000-character string. Also the 4-character form of the classID,
# and a name whose length is inside the resource but whose text isn't.
def ps_after_resource(name, inside, after):
    blocks = bim(0x041A, struct.pack(">II", 7, 16) + inside) + after
    return write(name, app13_only(blocks))


case("F2_ps_classid_after_end", "F2", "decode",
     ps_after_resource("f2_classid_after.jpg", uni(""), struct.pack(">I", 40000) + b"A" * 40000), timeout=10,
     expect="harness: decoded=", forbid="AAAAAAAA")
case("F2_ps_fixed_after_end", "F2", "decode",
     ps_after_resource("f2_fixed_after.jpg", uni("") + struct.pack(">I", 0), b"ZZZZ" + bytes(8)), timeout=10,
     expect="harness: decoded=", forbid="ZZZZ")
case("F2_ps_name_after_end", "F2", "decode",
     ps_after_resource("f2_name_after.jpg", struct.pack(">I", 200), ("B" * 200).encode("utf-16-be")), timeout=10,
     expect="harness: decoded=", forbid="BBBBBBBB")


# F3: RGB JPEGs (Adobe APP14 transform 0, or no JFIF/APP14 and component IDs
# 'R','G','B', as libjpeg decides). Red decoded as (75,255,28).
def adobe(transform):
    return seg(0xEE, b"Adobe" + struct.pack(">HHHB", 100, 0, 0, transform))


red_rgb, _, _ = dc_only_jpeg(32, 16, [(ord("R"), 1, 1), (ord("G"), 1, 1), (ord("B"), 1, 1)],
                             lambda c, x, y: (255, 0, 0)[c])
case("F3_rgb_adobe", "F3", "decode", write("f3_rgb_adobe.jpg", red_rgb[:2] + adobe(0) + red_rgb[2:]),
     "--ac --pixels 4,4", expect="pixel 4,4 = 25[3-5],[0-2],[0-2]$")
case("F3_rgb_component_ids", "F3", "decode", write("f3_rgb_ids.jpg", red_rgb), "--ac --histo --pixels 4,4",
     expect="pixel 4,4 = 25[3-5],[0-2],[0-2]$")
# Transform 1 says YCbCr, whatever the component IDs: Y 128, Cb 128, Cr 255 is red-ish
ycc_ids_rgb, _, _ = dc_only_jpeg(32, 16, [(ord("R"), 1, 1), (ord("G"), 1, 1), (ord("B"), 1, 1)],
                                 lambda c, x, y: (128, 128, 255)[c])
case("F3_adobe_ycc", "F3", "decode", write("f3_adobe_ycc.jpg", ycc_ids_rgb[:2] + adobe(1) + ycc_ids_rgb[2:]),
     "--ac --pixels 4,4", expect="pixel 4,4 = 2(4[5-9]|5[0-5]),[0-9]+,[0-9]+$")

# F4: an undecodable (progressive) file after a decoded one left the first
# file's preview and histograms on screen
case("F4_stale_preview", "F4", "gui", ["ok_444.jpg", "ok_progressive.jpg"], "--check-preview", timeout=30,
     expect="harness: preview missing")
case("F4_stale_histograms", "F4", "gui", ["ok_444.jpg", "ok_progressive.jpg"], "--histo --check-histo",
     timeout=30, expect="harness: histogram [A-Za-z]+ missing")
# (second review) An empty file or a failed open cleared the screen, but the
# decoder still had the first file's image, so a zoom or channel change drew it
# again
f4_empty = write("f4_empty.jpg", b"")
case("F4_zoom_after_empty", "F4", "gui", ["ok_444.jpg", f4_empty], "--zoom 100 --check-preview", timeout=30,
     expect="harness: preview missing")
case("F4_zoom_after_failed_open", "F4", "gui", ["ok_444.jpg", "f4_no_such_file.jpg"], "--zoom 100 --check-preview",
     timeout=30, expect="harness: preview missing")
case("F4_failed_open_message", "F4", "gui", ["f4_no_such_file.jpg"], timeout=30,
     expect="harness: message box: ERROR: Couldn't open file")
case("F4_channel_after_empty", "F4", "gui", ["ok_444.jpg", f4_empty], "--cycle-channels --check-preview",
     timeout=60, expect="harness: preview after channels missing")
# A failed open added its error to the previous file's report, which stayed
# (an empty file's report is cleared). scan=1 means the report has a scan decode.
case("F4_report_after_failed_open", "F4", "gui", ["ok_444.jpg", "f4_no_such_file.jpg"], timeout=30,
     expect="harness: file=f4_no_such_file.jpg scan=0")
case("F4_failed_open_report", "F4", "gui", ["f4_no_such_file.jpg"], "--log", timeout=30,
     expect="^ERROR: Couldn't open file: \\[.*f4_no_such_file\\.jpg\\]$")

# F5: a histogram bin over 35,791,394 pixels overflowed 60 * count in 32 bits,
# so the white 6000x6000 image's only bar wasn't drawn
case("F5_histo_bar_overflow", "F5", "decode",
     write("f5_white_6000.jpg", jpeg(Image.new("L", (6000, 6000), 255), quality=90)), "--histo", timeout=240,
     expect="histogram bars rgb=[1-9][0-9]* ")

# F6: 9x8 grey image, 8 black columns and 1 white one: 28 on average, but the
# statistics included the 7 padding columns (white, copied by the encoder)
im9 = Image.new("L", (9, 8), 0)
for y in range(8):
    im9.putpixel((8, y), 255)
name = write("f6_9x8.jpg", jpeg(im9, quality=100))
case("F6_padding_avg_luma", "F6", "decode", name, "--ac", expect="Y=\\[ 28\\] \\(range")
case("F6_padding_histogram", "F6", "decode", name, "--ac --histo",
     expect="R  component histo: \\[min=    0 max=  255 avg=   28\\.3\\]", forbid="avg=  127\\.5\\]")

# F7: a valid sequential JPEG with one component per scan: only the first scan
# (Y) was decoded, with no word that the preview was incomplete
case("F7_multiscan_note", "F7", "decode",
     write("f7_multiscan.jpg", dc_only_scans(16, 8, (150, 100, 200), [[0], [1], [2]])), "--ac",
     expect="only the first scan is decoded")

# F8: XMP with no final newline printed nothing, and the last byte was dropped
xmp = b'<x:xmpmeta xmlns:x="adobe:ns:meta/">SINGLE-LINE-XMP</x:xmpmeta>'
case("F8_xmp_single_line", "F8", "decode",
     write("f8_xmp.jpg", insert_after_app0(c444, seg(0xE1, b"http://ns.adobe.com/xap/1.0/\x00" + xmp))),
     expect="\\|<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">SINGLE-LINE-XMP</x:xmpmeta>$")
xmp = "<x:xmpmeta>\n  <dc:title>Café</dc:title>\n</x:xmpmeta>".encode()
case("F8_xmp_utf8", "F8", "decode",
     write("f8_xmp_utf8.jpg", insert_after_app0(c444, seg(0xE1, b"http://ns.adobe.com/xap/1.0/\x00" + xmp))),
     expect="<dc:title>Café</dc:title>")


# F9: SSHORT values were not sign-extended (-1 printed as 65535)
def sshort_file(values):
    count = len(values)
    payload = struct.pack("<" + "h" * count, *values)
    ifd0 = tiff_ifd("<", 8, [(0xC0DE, 8, count, payload)])
    return insert_after_app0(c444, app1_exif(tiff("<", {8: ifd0})))


case("F9_sshort_two", "F9", "decode", write("f9_sshort2.jpg", sshort_file([-1, -2])), "--unknown-exif",
     expect="IFD0\\.0xc0de +\\] = -1, -2$")
case("F9_sshort_array", "F9", "decode", write("f9_sshort3.jpg", sshort_file([-1, -2, -300])), "--unknown-exif",
     expect="IFD0\\.0xc0de +\\] = -1, -2, -300$")

# F10: Y selects quantization table 3, which isn't defined; it was used as all zeros
d = bytearray(c444)
i = marker_pos(d, 0xC0)
assert d[i + 10] == 1
d[i + 12] = 3
case("F10_undefined_dqt", "F10", "decode", write("f10_dqt3.jpg", bytes(d)), "--ac",
     expect="ERROR: Component 1 uses quantization table 3, which isn't defined", forbid="decoded=1")

# --- S: settings and GUI behaviour -------------------------------------------
# "settings" mode needs no file ("-"); see tests/harness/main.cpp
case("S1_fresh_install_defaults", "S1", "settings", "-", expect="fresh decodeImage=1 coachIdct=1")
case("S1_fresh_install_decodes", "S1", "gui", ["ok_444.jpg"], timeout=30, expect="file=ok_444.jpg scan=1")
case("S2_makernotes_toggle", "S2", "settings", "-", expect="makernotes 1 0 1$")
case("S3_max_errors_persist", "S3", "settings", "-", expect="maxErrors after restart=5$")
case("S4_config_dialog_save", "S4", "settings", "-",
     expect="dialog result=1 maxErrors=7 autoReprocess=1 coachIdct=1 stored=7")
case("S5_empty_file", "S5", "gui", ["ok_444.jpg", write("s_empty.jpg", b"")], "--log", timeout=30,
     expect="File length is zero")
case("S5_reprocess_no_file", "S5", "gui", ["ok_444.jpg"], "--reprocess-first", timeout=30,
     expect="harness: done")
case("S5_status_messages", "S5", "decode", "ok_444.jpg", "--ac", expect="status=[1-9]")

# --- L: leaks -------------------------------------------------------------------
# Re-open a 1024x768 file and re-render its preview: memory measured after the
# first round must level off (it plateaus around 15 MB from allocator caching;
# each leaked preview was ~3 MB, so 20 re-opens leaked 70+ MB). Not measured
# under ASan, which keeps freed memory in quarantine.
name = write("l_1024x768.jpg", jpeg(gradient(1024, 768), quality=85, subsampling=2))
case("L1_reopen", "L1", "gui", name, "--repeat 21 --max-growth-mb 30", timeout=60,
     expect="memory growth.*: ok$")
case("L1_histograms_and_channels", "L1", "gui", name, "--histo --cycle-channels --repeat 6 --max-growth-mb 30",
     timeout=90, expect="memory growth.*: ok$")

# --- U4: zoom memory ------------------------------------------------------------
# 800% used to build an 8x-size copy of the whole preview (200+ MB for this
# file); the preview now enlarges only what is on screen.
case("U4_zoom_800_memory", "U4", "gui", name, "--zoom 800 --max-zoom-mb 40", timeout=60,
     expect="zoom .*: ok$")

# --- M: macOS -------------------------------------------------------------------
# M1: report text keeps at least 4.5:1 contrast (WCAG AA) on light and dark
# backgrounds, written in either and after switching while open. The files give
# ordinary, header, description, warning and error lines.
theme_files = ["ok_444.jpg", "c3_dht_258.jpg", "s_empty.jpg"]
case("M1_dark_report", "M1", "gui", theme_files, "--dark --check-theme", timeout=60,
     expect="theme dark as written: .*: ok$", forbid=": low$")
case("M1_switch_to_dark", "M1", "gui", theme_files, "--check-theme", timeout=60,
     expect="theme dark after switching: .*: ok$", forbid=": low$")
# M4: on a Retina (2x) or 1.5x screen the preview is drawn at screen resolution:
# its size, mouse mapping and pixels match the decoded image at each zoom
for zoom, scale in (("12.5", "2"), ("50", "2"), ("100", "2"), ("150", "1.5"), ("800", "2"), ("200", "1")):
    case(f"M4_preview_{zoom.replace('.', '_')}_at_{scale.replace('.', '_')}x", "M4", "gui", name,
         f"--scale {scale} --zoom {zoom} --check-preview --mouse", timeout=60, expect="harness: preview .*: ok$")
case("M4_histograms_retina", "M4", "gui", name, "--scale 2 --histo --check-histo", timeout=60,
     expect="histogram yHistogram .*scale=2: ok$", forbid="histogram .*(missing|: wrong)")


# --- PSD: Photoshop files ---------------------------------------------------------
# The PSD decode call was commented out, so a .psd file was parsed as a JPEG
# ("File did not start with JPEG marker"), and the PSD parser had never run in
# this port. These check its image, and the memory and loop bugs it had.
def packbits(row):
    """PackBits-encode one row: runs of 3+ equal bytes repeat, the rest is literal."""
    out = bytearray()
    i = 0
    while i < len(row):
        j = i
        while j < len(row) and j - i < 128 and row[j] == row[i]:
            j += 1
        if j - i >= 3:
            out += bytes([257 - (j - i), row[i]])  # count 1-n as a signed byte
            i = j
            continue
        j = i
        while j < len(row) and j - i < 128 and not (j + 2 < len(row) and row[j] == row[j + 1] == row[j + 2]):
            j += 1
        out += bytes([j - i - 1]) + row[i:j]
        i = j
    return bytes(out)


def psd_rows(plane, row_bytes, rle):
    """A channel's rows: raw, or (row length table, PackBits rows)."""
    rows = [plane[i:i + row_bytes] for i in range(0, len(plane), row_bytes)]
    if not rle:
        return b"", b"".join(rows)
    enc = [packbits(r) for r in rows]
    return b"".join(struct.pack(">H", len(e)) for e in enc), b"".join(enc)


def psd(w, h, planes, depth=8, mode=3, rle=True, layers=b""):
    """Header, no colour mode data or resources, the layer section, then the image data."""
    head = b"8BPS" + struct.pack(">H", 1) + b"\0" * 6
    head += struct.pack(">HIIHH", len(planes), h, w, depth, mode)
    head += struct.pack(">I", 0) + struct.pack(">I", 0) + struct.pack(">I", len(layers)) + layers
    enc = [psd_rows(p, len(p) // h, rle) for p in planes]
    return head + struct.pack(">H", 1 if rle else 0) + b"".join(t for t, _ in enc) + b"".join(d for _, d in enc)


def psd_rle_channel(plane, w):
    """A layer channel's data: compression 1, the row length table, the rows."""
    table, data = psd_rows(plane, w, True)
    return struct.pack(">H", 1) + table + data


def psd_block(key, data):
    """A tagged block in a layer record (its length is even, as the specification says)."""
    return b"8BIM" + key + struct.pack(">I", len(data)) + data + b"\0" * (len(data) % 2)


def psd_layer_record(rect, chans, name, mask=b"", blocks=b""):
    """A layer record: rect is (top, left, bottom, right), chans [(id, channel data)]."""
    rec = struct.pack(">iiiiH", *rect, len(chans))
    rec += b"".join(struct.pack(">hI", cid, len(d)) for cid, d in chans)
    rec += b"8BIMnorm" + bytes([255, 0, 0, 0])
    luni = psd_block(b"luni", struct.pack(">I", len(name)) + name.encode("utf-16-be"))
    extra = struct.pack(">I", len(mask)) + mask + struct.pack(">I", 0) + b"\x07Layer 1" + luni + blocks
    return rec + struct.pack(">I", len(extra)) + extra


def psd_layer_section(layers, global_mask=True):
    """Layer and mask section from [(record, [channel data])], then an empty global mask."""
    info = struct.pack(">h", len(layers)) + b"".join(r for r, _ in layers)
    info += b"".join(d for _, chans in layers for d in chans)
    info += b"\0" * (len(info) % 2)
    return struct.pack(">I", len(info)) + info + (struct.pack(">I", 0) if global_mask else b"")


def psd_layers(w, h, planes, name, bad_row=False, blocks=b"", global_mask=True):
    """Layer and mask section: one RLE layer (channels 0-2) named in a "luni" block."""
    chans = []
    for cid, p in enumerate(planes):
        data = psd_rle_channel(p, w)
        if bad_row and cid == 1:
            data = data[:2 + 2 * h] + bytes([257 - (w - 1)]) + data[3 + 2 * h:]  # row 0 repeats one byte too few
        chans.append((cid, data))
    rec = psd_layer_record((0, 0, h, w), chans, name, blocks=blocks)
    return psd_layer_section([(rec, [d for _, d in chans])], global_mask)


# 37x23 RGB: R = 7x, G = 11y, B = 200 (B's rows are one repeat run; R's are literal)
pw, ph = 37, 23
p_r = bytes((x * 7) % 256 for y in range(ph) for x in range(pw))
p_g = bytes((y * 11) % 256 for y in range(ph) for x in range(pw))
p_b = bytes([200]) * (pw * ph)
P_PIX = "--pixels 10,5/36,22"
P_WANT = "pixel 10,5 = 70,55,200$"
psd_rle = write("psd_rgb_rle.psd", psd(pw, ph, [p_r, p_g, p_b]))
case("PSD_rgb_rle", "PSD", "decode", psd_rle, P_PIX, expect=P_WANT)
case("PSD_rgb_rle_corner", "PSD", "decode", psd_rle, P_PIX, expect="pixel 36,22 = 252,242,200$")
case("PSD_rgb_rle_preview", "PSD", "decode", psd_rle, expect="harness: decoded=0 preview=37x23 ")
case("PSD_rgb_raw", "PSD", "decode", write("psd_rgb_raw.psd", psd(pw, ph, [p_r, p_g, p_b], rle=False)), P_PIX,
     expect=P_WANT)
case("PSD_gray", "PSD", "decode", write("psd_gray.psd", psd(pw, ph, [p_r], mode=1)), P_PIX,
     expect="pixel 10,5 = 70,70,70$")
# An alpha channel after R, G and B isn't drawn
case("PSD_rgb_alpha", "PSD", "decode", write("psd_rgba.psd", psd(pw, ph, [p_r, p_g, p_b, p_g])), P_PIX,
     expect=P_WANT)
# 16-bit samples: rows are counted in bytes (a byte a pixel failed on row 0),
# and the image isn't drawn
p_16 = b"".join(struct.pack(">H", (x * 1000) % 65536) for y in range(ph) for x in range(pw))
case("PSD_16bit", "PSD", "decode", write("psd_gray16.psd", psd(pw, ph, [p_16], depth=16, mode=1)),
     expect="Image not shown: only 8-bit RGB and grayscale images are drawn", forbid="ERROR")
# A layer is reported (with its Unicode name) and the composite still drawn
psd_lay = write("psd_layer.psd", psd(pw, ph, [p_r, p_g, p_b], layers=psd_layers(pw, ph, [p_r, p_g, p_b], "Café")))
case("PSD_layer_name", "PSD", "decode", psd_lay, expect='Layer Name \\(Unicode\\) += "Café"')
case("PSD_layer_composite", "PSD", "decode", psd_lay, P_PIX, expect=P_WANT, forbid="ERROR")
# A bad layer channel skips the rest of the layers instead of the whole file
psd_badlay = write("psd_bad_layer.psd",
                   psd(pw, ph, [p_r, p_g, p_b], layers=psd_layers(pw, ph, [p_r, p_g, p_b], "Bad", bad_row=True)))
case("PSD_bad_layer_error", "PSD", "decode", psd_badlay, expect="ERROR: RLE row 0 at .* doesn't decode to 37 bytes")
case("PSD_bad_layer_composite", "PSD", "decode", psd_badlay, P_PIX, expect=P_WANT)
# B's last row (the file's last 2 bytes, one repeat run) becomes a 128-byte
# literal run: it was written past the end of the image
psd_over = write("psd_rle_overrun.psd", psd(pw, ph, [p_r, p_g, p_b])[:-2] + bytes([127]) + bytes(128))
case("PSD_rle_overrun", "PSD", "decode", psd_over, expect="ERROR: RLE row 22 at .* doesn't decode to 37 bytes")
case("PSD_rle_overrun_no_image", "PSD", "decode", psd_over, expect="harness: decoded=0 preview=0x0 ")
# Truncated raw image data: reported, not read as zeros
case("PSD_truncated", "PSD", "decode", write("psd_truncated.psd", psd(pw, ph, [p_r, p_g, p_b], rle=False)[:-100]),
     expect="ERROR: Image data at \\[0x00000028\\] runs past the end of the file")
# Two channels of 2^31 rows: 2 * 2^31 wrapped to 0, so the row table was empty
# and the decode read past it (heap overflow). It is now outside the PSD limits.
wrap = psd(1, 1, [b"\0", b"\0"])
wrap = wrap[:12] + struct.pack(">HII", 2, 0x80000000, 1) + wrap[22:]  # channels, height, width
case("PSD_rows_wrap", "PSD", "decode", write("psd_rows_wrap.psd", wrap),
     expect="ERROR: An image of 1x2147483648 pixels, 2 channels and 8 bits a sample is outside the PSD limits")
# 32768 raw channels of 2^20 rows of 2^29 bytes: the size, 2^64, wrapped to 0
# in 64 bits, so the check passed and the decode stepped through 2^35 rows
huge = psd(1, 1, [b"\0"], rle=False)
huge = huge[:12] + struct.pack(">HII", 32768, 1 << 20, 1 << 29) + huge[22:]  # channels, height, width
case("PSD_size_overflow", "PSD", "decode", write("psd_size_overflow.psd", huge), timeout=20,
     expect="ERROR: An image of 536870912x1048576 pixels, 32768 channels and 8 bits a sample is outside")
# An additional layer info length of 0xFFFFFFF4 wrapped the position back to
# the same block: the decode never ended
addtl = b"8BIMabcd" + struct.pack(">I", 0xFFFFFFF4)
lm = struct.pack(">II", 0, 0) + addtl + b"\0" * 8
case("PSD_addtl_wrap", "PSD", "decode",
     write("psd_addtl_wrap.psd", psd(1, 1, [b"\0"] * 3, layers=lm)), timeout=20,
     expect="ERROR: Additional layer info \\[abcd\\] at .* runs past the end of the file")
# A layer whose bottom is above its top: a height near 4 billion, and a 16 GB
# row length table
tall = struct.pack(">iiiiH", 0, 0, -16, 1, 1) + struct.pack(">HI", 0, 2) + b"8BIMnorm" + bytes([255, 0, 0, 0])
tall += struct.pack(">I", 12) + struct.pack(">II", 0, 0) + b"\0" * 4
tall = struct.pack(">h", 1) + tall + struct.pack(">H", 1) + b"\0" * 32
tall = struct.pack(">I", len(tall)) + tall + struct.pack(">I", 0)
case("PSD_layer_tall", "PSD", "decode", write("psd_layer_tall.psd", psd(1, 1, [b"\0"] * 3, layers=tall)),
     expect="ERROR: Channel row length table at .* runs past the end of the channel's data")
# A layer named "Cat" has a 10-byte "luni" block. Padding it to 4 bytes read the
# next block ("lyid") 2 bytes late: "signature unknown", and the ID was lost.
lyid = psd_block(b"lyid", struct.pack(">I", 7))
case("PSD_luni_even_padding", "PSD", "decode",
     write("psd_layer_cat.psd", psd(pw, ph, [p_r, p_g, p_b], layers=psd_layers(pw, ph, [p_r, p_g, p_b], "Cat",
                                                                                blocks=lyid))),
     expect="Layer ID += 7 ", forbid="ERROR|signature unknown")
# The global layer mask info is optional: here the layer info fills the section
# (as macOS's sips writes it). It was read from the image data after it.
case("PSD_no_global_mask", "PSD", "decode",
     write("psd_no_global_mask.psd",
           psd(pw, ph, [p_r, p_g, p_b], layers=psd_layers(pw, ph, [p_r, p_g, p_b], "Plain", global_mask=False))),
     P_PIX, expect=P_WANT, forbid="ERROR|NOTE: Skipping")
# A 4x4 layer with a 2x2 user mask (channel -2) and a 1x3 real user mask (-3),
# then a second layer: each mask channel was decoded as 4x4, which gave an RLE
# error and lost the rest of the layers
q = 4
quad = [bytes((x * 60 + c * 20) % 256 for y in range(q) for x in range(q)) for c in range(3)]
mask = struct.pack(">iiii", 1, 1, 3, 3) + bytes([0, 0])  # user mask: rectangle, colour, flags
mask += bytes([0, 255]) + struct.pack(">iiii", 0, 0, 3, 1)  # real user mask: flags, background, rectangle
masked = [(c, psd_rle_channel(quad[c], q)) for c in range(3)]
masked += [(-2, psd_rle_channel(bytes([255, 0, 0, 255]), 2)), (-3, psd_rle_channel(bytes([9, 9, 9]), 1))]
second = [(c, psd_rle_channel(quad[c], q)) for c in range(3)]
masks = psd_layer_section([(psd_layer_record((0, 0, q, q), masked, "Masked", mask=mask), [d for _, d in masked]),
                           (psd_layer_record((0, 0, q, q), second, "Second", blocks=lyid), [d for _, d in second])])
case("PSD_mask_channels", "PSD", "decode", write("psd_masks.psd", psd(q, q, quad, layers=masks)),
     expect="Layer   2/  2, Channel  3/ 3", forbid="ERROR|NOTE: Skipping")
# A layer 0 pixels wide and 2^31 - 1 tall: its raw rows of 0 bytes fit in any
# length, so they would be stepped through one by one
empty_chan = [(0, struct.pack(">H", 0))]
empty = psd_layer_section([(psd_layer_record((0, 0, 0x7FFFFFFF, 0), empty_chan, "Empty"), [empty_chan[0][1]])])
case("PSD_empty_rows", "PSD", "decode", write("psd_empty_rows.psd", psd(pw, ph, [p_r, p_g, p_b], layers=empty)),
     P_PIX, timeout=20, expect=P_WANT, forbid="ERROR")
# The preview window shows a PSD (it was only drawn for a JPEG scan), at the
# zoom picked, and mouse moves and channel changes keep it. It has no MCUs.
case("PSD_gui_preview", "PSD", "gui", ["ok_444.jpg", psd_rle], "--zoom 800 --check-preview --mouse --cycle-channels",
     timeout=60, expect="harness: preview after channels zoom=8 .* image=37x23 .*: ok$", forbid="missing|differ|gave")

# --- E: Tools > Export JPEG and Export TIFF -----------------------------------
def md5(data):
    return hashlib.md5(data).hexdigest()


def exported(*datas):
    """The export-jpeg line for these exported files, in order."""
    return "harness: exported %d md5s=%s$" % (len(datas), ",".join(md5(d) for d in datas))


def com_padding(n):
    """COM segments totalling exactly n bytes (n >= 4)."""
    out = b""
    while n > 0:
        size = min(n, 4 + 65533)
        if 0 < n - size < 4:
            size -= 4
        out += seg(0xFE, b"\x00" * (size - 4))
        n -= size
    return out


def sos_pos(data):
    return marker_pos(data, 0xDA)


case("E_jpeg_whole", "E", "export-jpeg", "ok_444.jpg", expect=exported(c444), forbid="source unchanged=0")
case("E_jpeg_trailing", "E", "export-jpeg", write("e_trailing.jpg", c444 + b"TRAILING" * 100), expect=exported(c444))
# [SOI, SOS) one byte over a whole number of 128 KB copy blocks: the copy
# loop dropped its last byte
block_edge = insert_after_app0(c444, com_padding(131073 - sos_pos(c444)))
assert sos_pos(block_edge) == 131073
case("E_jpeg_block_edge", "E", "export-jpeg", write("e_block_edge.jpg", block_edge), expect=exported(block_edge))
no_eoi = write("e_no_eoi.jpg", c444[:-2])
case("E_jpeg_no_eoi", "E", "export-jpeg", no_eoi, expect="harness: exported 0 md5s=$")
case("E_jpeg_no_eoi_message", "E", "export-jpeg", no_eoi,
     expect="message box: Nothing was exported\\. The image has no end-of-image marker")
case("E_jpeg_force_eoi", "E", "export-jpeg", no_eoi, "--force-eoi", expect=exported(c444))
case("E_jpeg_ignore_eoi", "E", "export-jpeg", no_eoi, "--ignore-eoi", expect=exported(c444[:-2]))
# An image that has its EOI got a second one
case("E_jpeg_force_eoi_has_eoi", "E", "export-jpeg", "ok_444.jpg", "--force-eoi", expect=exported(c444))
case("E_jpeg_force_soi", "E", "export-jpeg", write("e_no_soi.jpg", c444[2:]), "--force-soi", expect=exported(c444))
# Motion JPEG DHT: the standard tables (the option wrote nothing) go in before
# SOS, but only those the image doesn't define: replacing a frame's own
# (optimized) tables made its scan decode wrongly. Pillow's tables are the
# standard ones, so its segments give the expected bytes.
def dht_tables(data):
    """{Tc/Th: table bytes} from the DHT segments before SOS."""
    tables, i = {}, 2
    while data[i + 1] != 0xDA:
        n = struct.unpack(">H", data[i + 2:i + 4])[0]
        if data[i + 1] == 0xC4:
            j = i + 4
            while j < i + 2 + n:
                size = 17 + sum(data[j + 1:j + 17])
                tables[data[j]] = data[j:j + size]
                j += size
        i += 2 + n
    return tables


STD_DHT = dht_tables(c444)
assert sorted(STD_DHT) == [0x00, 0x01, 0x10, 0x11]


def with_std_dht(frame, ids):
    """frame with the standard tables ids (in the standard segment's order) inserted before SOS."""
    body = b"".join(STD_DHT[t] for t in (0x00, 0x01, 0x10, 0x11) if t in ids)
    p = sos_pos(frame)
    return frame[:p] + b"\xff\xc4" + struct.pack(">H", len(body) + 2) + body + frame[p:]


def without_dht(data, ids=(0x00, 0x01, 0x10, 0x11)):
    """data without the DHT segments of these tables (one table a segment, as Pillow writes)."""
    out, i = data[:2], 2
    while True:
        m, n = data[i + 1], struct.unpack(">H", data[i + 2:i + 4])[0]
        if m == 0xDA:
            return out + data[i:]
        if not (m == 0xC4 and data[i + 4] in ids):
            out += data[i:i + 2 + n]
        i += 2 + n


case("E_jpeg_dht_has_tables", "E", "export-jpeg", "ok_444.jpg", "--dht-avi", expect=exported(c444))
case("E_jpeg_dht_has_tables_report", "E", "export-jpeg", "ok_444.jpg", "--dht-avi",
     expect="Not inserting the standard AVI DHT huffman table: the image defines its own tables")
optimized = jpeg(color, quality=90, subsampling=0, optimize=True)
assert dht_tables(optimized) != STD_DHT
case("E_jpeg_dht_optimized", "E", "export-jpeg", write("e_optimized.jpg", optimized), "--dht-avi",
     expect=exported(optimized))
no_chroma = without_dht(c444, (0x01, 0x11))
case("E_jpeg_dht_missing_chroma", "E", "export-jpeg", write("e_no_chroma_dht.jpg", no_chroma), "--dht-avi",
     expect=exported(with_std_dht(no_chroma, (0x01, 0x11))))
case("E_jpeg_dht_missing_chroma_report", "E", "export-jpeg", "e_no_chroma_dht.jpg", "--dht-avi",
     expect="Inserting standard AVI DHT huffman table \\(DC 1, AC 1\\)$")
case("E_jpeg_same_file", "E", "export-jpeg", "ok_444.jpg", "--out-same",
     expect="ERROR: Can't overwrite source file", forbid="source unchanged=0")
# The same file by a name in another case (macOS disks are usually
# case-insensitive): the names differed, so it was truncated while being read
case("E_jpeg_same_file_case", "E", "export-jpeg", write("e_case.jpg", c444), "--out-same-case",
     expect="ERROR: Can't overwrite source file", forbid="source unchanged=0")
case("E_jpeg_unwritable", "E", "export-jpeg", "ok_444.jpg", "--out /nonexistent-jpegsnoop-dir/out.jpg",
     expect="message box: ERROR: Couldn't open file for write")
case("E_jpeg_psd", "E", "export-jpeg", psd_rle,
     expect="message box: Nothing was exported\\. No JPEG image data \\(SOS marker\\) was found")

# Extract All: each JPEG after junk, then the file's own report from offset 0
junk = bytes(range(100))
two = write("e_two.jpg", junk + c444 + junk + c420 + junk)
case("E_jpeg_all", "E", "export-jpeg", two, "--all", expect=exported(c444, c420))
case("E_jpeg_all_list", "E", "export-jpeg", two, "--all",
     expect="\\[0x%08x\\] Exported to \\[.*out\\.000002\\.jpg\\]" % (len(junk) * 2 + len(c444)))
case("E_jpeg_all_count", "E", "export-jpeg", two, "--all", expect="^  Exported 2 JPEGs$")
case("E_jpeg_all_unwritable", "E", "export-jpeg", two, "--all --out /nonexistent-jpegsnoop-dir/out.jpg",
     expect="message box: ERROR: Couldn't open file for write")
# The file opened through a link with the first export's name: the link was
# removed (as one leading elsewhere is), and then the export took its place
case("E_jpeg_all_src_link", "E", "export-jpeg", two, "--all --src-link", expect="^harness: source link kept=1$")
case("E_jpeg_all_src_link_error", "E", "export-jpeg", two, "--all --src-link",
     expect="^harness: message box: ERROR: Couldn't write \\[.*/out\\.000001\\.jpg\\]: it would replace the file analyzed$")
# Motion JPEG frames leave out the DHT: Extract All with the standard one
# inserted before each SOS (all four tables, 420 bytes)
mjpeg_frame = without_dht(c444)
mjpeg_std = with_std_dht(mjpeg_frame, (0x00, 0x01, 0x10, 0x11))
assert len(mjpeg_std) == len(mjpeg_frame) + 420
mjpeg = write("e_mjpeg_frames.bin", junk + mjpeg_frame + junk + mjpeg_frame + junk)
case("E_jpeg_all_mjpeg", "E", "export-jpeg", mjpeg, "--all --dht-avi", expect=exported(mjpeg_std, mjpeg_std))
# Fill bytes (0xFF) may come before any marker, SOS included. The last one,
# right before SOS, stopped the check for the frame's tables, so none were
# inserted.
STD_DHT_SEG = b"\xff\xc4" + struct.pack(">H", 2 + sum(len(v) for v in STD_DHT.values())) + b"".join(
    STD_DHT[t] for t in (0x00, 0x01, 0x10, 0x11))
for n_fill in (1, 3):
    p_sos = sos_pos(mjpeg_frame) + n_fill
    padded = mjpeg_frame[:p_sos - n_fill] + b"\xff" * n_fill + mjpeg_frame[p_sos - n_fill:]
    case("E_jpeg_dht_fill_%d" % n_fill, "E", "export-jpeg", write("e_fill_%d.jpg" % n_fill, padded), "--dht-avi",
         expect=exported(padded[:p_sos] + STD_DHT_SEG + padded[p_sos:]))
# An EXIF thumbnail is a JPEG of its own
SUB = 0x100
thumb_at = 0x200
ifd0 = tiff_ifd("<", 8, [entry(0x010F, ascii_("TestMake"))], next_off=SUB)
ifd1 = tiff_ifd("<", SUB, [entry(0x0201, longs("<", thumb_at)), entry(0x0202, longs("<", len(small)))])
with_thumb = insert_after_app0(c444, app1_exif(tiff("<", {8: ifd0, SUB: ifd1, thumb_at: small})))
case("E_jpeg_all_thumbnail", "E", "export-jpeg", write("e_thumb.jpg", with_thumb), "--all",
     expect=exported(with_thumb, small))

# TIFF: tags and every sample checked against a separate decode
TIFF_OK = "check=ok$"
case("E_tiff_rgb8", "E", "export-tiff", "ok_444.jpg", "--format rgb8", expect="tiff 64x48 bits=8 photometric=2 .*" + TIFF_OK)
case("E_tiff_rgb16", "E", "export-tiff", "ok_420.jpg", "--format rgb16", expect="bits=16 photometric=2 .*" + TIFF_OK)
case("E_tiff_ycc8", "E", "export-tiff", "ok_420.jpg", "--format ycc8", expect="bits=8 photometric=6 .*" + TIFF_OK)
case("E_tiff_ycc8_gray", "E", "export-tiff", "ok_gray.jpg", "--format ycc8", expect=TIFF_OK)
case("E_tiff_ycc8_rgb_jpeg", "E", "export-tiff", "f3_rgb_adobe.jpg", "--format ycc8", expect=TIFF_OK)
# The image's own size: the preview is padded to 16x8
case("E_tiff_frame_size", "E", "export-tiff", "f6_9x8.jpg", "--format rgb8", expect="tiff 9x8 .*" + TIFF_OK)
case("E_tiff_psd", "E", "export-tiff", psd_rle, "--format rgb16", expect="tiff 37x23 .*" + TIFF_OK)
case("E_tiff_psd_ycc", "E", "export-tiff", psd_rle, "--format ycc8",
     expect="error=Only a decoded JPEG image can be exported as YCC")
case("E_tiff_no_image", "E", "export-tiff", "ok_progressive.jpg", "--format rgb8",
     expect="error=There is no decoded image to export")
case("E_tiff_same_file", "E", "export-tiff", "ok_444.jpg", "--format rgb8 --out-same",
     expect="error=Can't overwrite source file", forbid="source unchanged=0")

# The menus, with their dialogs
case("E_menus_jpeg", "E", "gui", ["ok_444.jpg"], "--export-menus", timeout=30,
     expect="menu Export JPEG\\.\\.\\. wrote=1 size=%d$" % len(c444))
case("E_menus_tiff", "E", "gui", ["ok_444.jpg"], "--export-menus", timeout=30,
     expect="menu Export TIFF\\.\\.\\. wrote=1 size=[1-9]")
case("E_menus_names", "E", "gui", ["ok_444.jpg"], "--export-menus", timeout=30,
     expect="default=ok_444\\.jpg\\.export\\.jpg$")
case("E_menus_offset", "E", "gui", ["ok_444.jpg"], "--export-menus", timeout=30,
     expect="Export JPEG Settings \\(Source file start offset: 0x00000000, MJPEG DHT off\\)")
# Cancel in the settings or format dialog writes nothing (Export TIFF wrote
# the file anyway)
case("E_menus_cancel", "E", "gui", ["ok_444.jpg"], "--export-menus-cancel", timeout=30,
     expect="menu Export TIFF\\.\\.\\. wrote=0", forbid="wrote=1")
case("E_menus_psd", "E", "gui", [psd_rle], "--export-menus", timeout=30,
     expect="Export as TIFF \\(YCC unavailable\\)")
case("E_menus_no_image", "E", "gui", ["ok_progressive.jpg"], "--export-menus", timeout=30,
     expect="message box: There is no decoded image to export")
# A Motion JPEG AVI through the menus, with Extract All: the DHT option is on,
# a frame with its own (optimized) tables keeps them, and one without gets
# the standard tables
movi = b"movi" + chunk(b"00dc", optimized) + chunk(b"00dc", mjpeg_frame)
avi_mjpeg = (b"AVI " + chunk(b"LIST", b"hdrl" + chunk(b"avih", bytes(56))) + chunk(b"LIST", strl)
             + chunk(b"LIST", movi))
avi_mjpeg = write("e_mjpeg.avi", b"RIFF" + struct.pack("<I", len(avi_mjpeg)) + avi_mjpeg)
case("E_menus_mjpeg_dht_default", "E", "gui", [avi_mjpeg], "--export-menus-all", timeout=30,
     expect="Export JPEG Settings \\(.*, MJPEG DHT on\\)")
case("E_menus_mjpeg_frames", "E", "gui", [avi_mjpeg], "--export-menus-all", timeout=30,
     expect="all files=2 md5s=%s,%s " % (md5(optimized), md5(mjpeg_std)))
# As reported: a frame with a fill byte before SOS, in an AVI
p_sos = sos_pos(mjpeg_frame)
padded = mjpeg_frame[:p_sos] + b"\xff" + mjpeg_frame[p_sos:]
movi = b"movi" + chunk(b"00dc", padded)
avi_fill = (b"AVI " + chunk(b"LIST", b"hdrl" + chunk(b"avih", bytes(56))) + chunk(b"LIST", strl)
            + chunk(b"LIST", movi))
avi_fill = write("e_mjpeg_fill.avi", b"RIFF" + struct.pack("<I", len(avi_fill)) + avi_fill)
case("E_menus_mjpeg_fill", "E", "gui", [avi_fill], "--export-menus-all", timeout=30,
     expect="all files=1 md5s=%s " % md5(padded[:p_sos + 1] + STD_DHT_SEG + padded[p_sos + 1:]))
# Extract All asks before replacing numbered files that exist (they were
# replaced without asking, even when the name chosen was new), and doesn't ask
# about the name chosen, which it doesn't write
case("E_menus_all_asks", "E", "gui", ["ok_444.jpg"], "--export-menus-all --old-numbered", timeout=30,
     expect="message box: A file with a name Extract All uses already exists: \"menu-export-1\\.000001\\.jpg\"",
     forbid="replace it\\?")
case("E_menus_all_cancel_keeps", "E", "gui", ["ok_444.jpg"], "--export-menus-all --old-numbered", timeout=30,
     expect="all files=1 md5s=%s old kept=1$" % md5(b"old"))
case("E_menus_all_replace", "E", "gui", ["ok_444.jpg"], "--export-menus-all --old-numbered --replace", timeout=30,
     expect="all files=1 md5s=%s old kept=0$" % md5(c444), forbid="replace it\\?")
# The menus stay enabled after a file that can't be opened
case("E_menus_after_failed_open", "E", "gui", ["ok_444.jpg", "e_no_such_file.jpg"], "--export-menus", timeout=30,
     expect="message box: The file shown couldn't be analyzed, so there is nothing to export")

# --- D: Options > Scan Segment > Detailed Decode --------------------------------
# decode --detail X,Y,LEN prints "harness: detail blocks=N first=X,Y last=X,Y
# labels=... rgb dumps=N rows=N header=..." and decodes the file again without
# the detail: "changes report=0 image=0" means only detail lines were added.
def detail(blocks, first, last, labels, rows, header):
    return ("harness: detail blocks=%d first=%s last=%s labels=%s rgb dumps=%d rows=%d header=%s"
            % (blocks, first, last, labels.replace("(", "\\(").replace(")", "\\)"), 1 if rows else 0, rows,
               header.replace("[", "\\[").replace("]", "\\]").replace("(", "\\(").replace(")", "\\)")))


SAME = "harness: detail changes report=0 image=0$"
ALL = "0,0,4294967295"
YCC = "Lum/Chr(0)/Chr(1)"

# ok_420.jpg is 64x48 in 16x16 MCUs: 4 x 3 MCUs of 4 Lum blocks, Cb and Cr.
# Cr was labelled "Chr(0)" like Cb (both use quantization table 1)
case("D_detail_one_mcu", "D", "decode", "ok_420.jpg", "--ac --detail 1,1,1",
     expect=detail(6, "1,1", "1,1", YCC, 16, "Detailed Decode: 1 MCU from MCU [1,1]"))
case("D_detail_one_mcu_same", "D", "decode", "ok_420.jpg", "--ac --detail 1,1,1", expect=SAME)
# An MCU in the last column lost the last row of its RGB dump
case("D_detail_last_column", "D", "decode", "ok_420.jpg", "--ac --detail 3,0,1",
     expect=detail(6, "3,0", "3,0", YCC, 16, "Detailed Decode: 1 MCU from MCU [3,0]"))
case("D_detail_last_mcu", "D", "decode", "ok_420.jpg", "--ac --detail 3,2,1",
     expect=detail(6, "3,2", "3,2", YCC, 16, "Detailed Decode: 1 MCU from MCU [3,2]"))
case("D_detail_444_last", "D", "decode", "ok_444.jpg", "--ac --detail 7,5,1",
     expect=detail(3, "7,5", "7,5", YCC, 8, "Detailed Decode: 1 MCU from MCU [7,5]"))
case("D_detail_gray", "D", "decode", "ok_gray.jpg", "--ac --detail 6,5,2",
     expect=detail(2, "6,5", "7,5", "Lum", 8, "Detailed Decode: 2 MCUs from MCU [6,5]"))
# Every MCU: the report gains only the detail, in both decode modes. In No
# IDCT mode the MCUs reported were drawn at full resolution (the others from
# their DC), which also changed the average and brightest pixel
case("D_detail_all_ac", "D", "decode", "ok_420.jpg", "--ac --detail " + ALL, expect=SAME)
case("D_detail_all_dc", "D", "decode", "ok_420.jpg", "--detail " + ALL, expect=SAME)
case("D_detail_all_dc_444", "D", "decode", "ok_444.jpg", "--detail " + ALL, expect=SAME)
case("D_detail_dc_note", "D", "decode", "ok_420.jpg", "--detail 1,1,1",
     expect="^    \\(No IDCT mode: each block shows its DC value only\\)$")
# A start beyond the right edge picked an MCU in the next row; one far below
# the image wrapped around to MCU [0,0]; a length past 2^32 - start reported
# nothing
case("D_detail_outside_x", "D", "decode", "ok_420.jpg", "--ac --detail 4,0,1",
     expect=detail(0, "-", "-", "-", 0, "NOTE: Detailed Decode starts at MCU [4,0], outside this image (4 x 3 MCUs), "
                   "so nothing is reported"))
case("D_detail_y_wraps", "D", "decode", "ok_420.jpg", "--ac --detail 0,1073741824,1",
     expect="harness: detail blocks=0 first=- ")
case("D_detail_len_wraps", "D", "decode", "ok_420.jpg", "--ac --detail 2,0,4294967295",
     expect=detail(60, "2,0", "3,2", YCC, 16, "Detailed Decode: 4294967295 MCUs from MCU [2,0]"))
# Restart markers between every MCU, a corrupt scan, a truncated one, an RGB
# JPEG, and files with no decoded scan
restart = write("d_restart.jpg", jpeg(color, quality=90, subsampling=2, restart_marker_blocks=1))
case("D_detail_restart", "D", "decode", restart, "--ac --detail " + ALL,
     expect=detail(72, "0,0", "3,2", YCC, 16, "Detailed Decode: 4294967295 MCUs from MCU [0,0]"))
case("D_detail_restart_same", "D", "decode", restart, "--ac --detail " + ALL, expect=SAME)
p_scan = sos_pos(c420) + 2 + struct.unpack(">H", c420[sos_pos(c420) + 2:sos_pos(c420) + 4])[0]
rng = random.Random(7)
corrupt = bytearray(c420)
for i in range(p_scan + 40, p_scan + 60):
    corrupt[i] = rng.randrange(0, 0xFF)
corrupt = write("d_corrupt.jpg", bytes(corrupt))
case("D_detail_corrupt", "D", "decode", corrupt, "--ac --detail " + ALL, expect=SAME)
case("D_detail_corrupt_dc", "D", "decode", corrupt, "--detail " + ALL, expect=SAME)
truncated = write("d_truncated.jpg", c420[:p_scan + 200])
case("D_detail_truncated", "D", "decode", truncated, "--ac --detail " + ALL, expect=SAME)
case("D_detail_rgb_jpeg", "D", "decode", "f3_rgb_ids.jpg", "--ac --detail " + ALL, expect=SAME)
case("D_detail_multiscan", "D", "decode", "f7_multiscan.jpg", "--ac --detail " + ALL, expect=SAME)
case("D_detail_progressive", "D", "decode", "ok_progressive.jpg", "--ac --detail " + ALL,
     expect="harness: detail blocks=0 first=- ")

# Second check: what the detail lines say.
# A 16-bit AC code (run 15, size 10, value 1000) from bit 7 of its byte takes
# 26 bits over 5 bytes. Only 4 bytes were shown, cutting off its last bit.
dc_tbl = huff_codes([1] + [0] * 15, [3])
ac_tbl = huff_codes([1, 1] + [0] * 13 + [1], [0x00, 0x01, 0xFA])
codes = [dc_tbl[3], (4, 3), ac_tbl[0x01], (1, 1), ac_tbl[0xFA], (1000, 10), ac_tbl[0x00]]
bits, raw = Bits(), ""
for value, length in codes:
    bits.put(value, length)
    raw += format(value, "0%db" % length)
head = (b"\xff\xd8" + seg(0xDB, bytes([0]) + bytes([1]) * 64)
        + seg(0xC0, bytes([8]) + struct.pack(">HHB", 8, 8, 1) + bytes([1, 0x11, 0]))
        + seg(0xC4, bytes([0x00, 1] + [0] * 15 + [3]))
        + seg(0xC4, bytes([0x10, 1, 1] + [0] * 13 + [1, 0x00, 0x01, 0xFA]))
        + seg(0xDA, bytes([1, 1, 0x00, 0, 63, 0])))
long_vlc = write("d_long_vlc.jpg", head + bits.flush() + b"\xff\xd9")
raw += "1" * (-len(raw) % 8)                       # padding, as flush() writes it
shown = [int(raw[i:i + 8], 2) for i in range(0, 40, 8)]
marked = ("-" * 7 + raw[7:33]).ljust(40, "-")
case("D_detail_long_vlc", "D", "decode", long_vlc, "--ac --detail 0,0,1",
     expect="\\[0x%08x\\.7\\]: ZRL=\\[15\\] Val=\\[ 1000\\] Coef=\\[02\\.\\.17\\] Data=\\[0x %s = 0b \\(%s\\)\\]"
            % (len(head), " ".join("%02x" % b for b in shown), " ".join(marked[i:i + 8] for i in range(0, 40, 8))))
# Each matrix showed the block's DC difference, so a flat image's blocks after
# the first had DC 0. A difference of 0 was also marked "EOB".
flat, _, _ = dc_only_jpeg(24, 8, [(1, 1, 1)], lambda c, x, y: 180)
flat = write("d_flat.jpg", flat)
case("D_detail_dc_absolute", "D", "decode", flat, "--ac --detail 0,0,3",
     expect="harness: detail blocks=3 .* dc=416/416/416$")
case("D_detail_dc_zero_not_eob", "D", "decode", flat, "--ac --detail 0,0,3",
     expect="Val=\\[    0\\] Coef=\\[00= DC\\]", forbid="Coef=\\[00= DC\\].* EOB$")
# The predictor is per component and restarts at each restart marker
flat_rst = write("d_flat_restart.jpg", jpeg(Image.new("L", (24, 8), 180), quality=100, restart_marker_blocks=1))
case("D_detail_dc_restart", "D", "decode", flat_rst, "--ac --detail 0,0,3",
     expect="harness: detail blocks=3 .* dc=416/416/416$")
# (200, 100, 50) is Y 124, Cb 86, Cr 182: DC -32, -336 and 432
flat_color = Image.new("RGB", (32, 16), (200, 100, 50))
FLAT_DC = "harness: detail blocks=12 .* dc=-32/-32/-32/-32/-336/432/-32/-32$"
case("D_detail_dc_color", "D", "decode",
     write("d_flat_color.jpg", jpeg(flat_color, quality=100, subsampling=2)), "--ac --detail 0,0,2", expect=FLAT_DC)
case("D_detail_dc_color_restart", "D", "decode",
     write("d_flat_color_rst.jpg", jpeg(flat_color, quality=100, subsampling=2, restart_marker_blocks=1)),
     "--ac --detail 0,0,2", expect=FLAT_DC)
# An RGB JPEG's blocks were labelled Lum, Chr(0) and Chr(1)
case("D_detail_rgb_labels", "D", "decode", "f3_rgb_ids.jpg", "--ac --detail 0,0,1",
     expect="harness: detail blocks=3 first=0,0 last=0,0 labels=R/G/B ")
# The first code after a restart marker was reported at the padding before the
# marker, and so was an error there in the normal decode
restart_bytes = (files_dir / restart).read_bytes()
p_sos = sos_pos(restart_bytes)
p_rst = restart_bytes.find(b"\xff\xd0", p_sos + 2 + struct.unpack(">H", restart_bytes[p_sos + 2:p_sos + 4])[0])
case("D_detail_after_restart", "D", "decode", restart, "--ac --detail 0,0,2",
     expect="^      \\[0x%08x\\.0\\]: ZRL=\\[ 0\\] Val=\\[ *-?[0-9]+\\] Coef=\\[00= DC\\]" % (p_rst + 2))
bad_rst = write("d_restart_bad.jpg", restart_bytes[:p_rst + 2] + b"\xff\x00\xff\x00" + restart_bytes[p_rst + 2:])
case("D_error_after_restart", "D", "decode", bad_rst, "--ac",
     expect="Bad huffman code @ 0x%08x\\.0$" % (p_rst + 2))
case("D_error_after_restart_detail", "D", "decode", bad_rst, "--ac --detail 0,0,2",
     expect="Bad huffman code @ 0x%08x\\.0$" % (p_rst + 2))

# The menu item opens the dialog (it did nothing; the dialog's OK and Cancel
# did nothing either, so it never closed). gui --detail-dlg answers it; see
# tests/harness/main.cpp. Pixels (20,4) and (40,20) are MCUs [1,0] and [2,1],
# 6 MCUs apart in decode order.
STILL_OPEN = "still open|widgets not found|unavailable"
case("D_menu_two_clicks", "D", "gui", "ok_420.jpg", "--mark 20,4/40,20 --detail-dlg load,ok --auto-reprocess",
     timeout=30, forbid=STILL_OPEN,
     expect="detail report now blocks=36 first=1,0 last=2,1 labels=Lum/Chr\\(0\\)/Chr\\(1\\) rgb dumps=1 rows=16 "
            "header=Detailed Decode: 6 MCUs from MCU \\[1,0\\]")
# Clicked last to first: the range began at the later MCU
case("D_menu_clicks_reversed", "D", "gui", "ok_420.jpg", "--mark 40,20/20,4 --detail-dlg load,ok --auto-reprocess",
     timeout=30, forbid=STILL_OPEN, expect="detail set en=1 x=1 y=0 len=6$")
# One click: that MCU (the length was 0, so nothing was reported)
case("D_menu_one_click", "D", "gui", "ok_420.jpg", "--mark 63,47 --detail-dlg load,ok --auto-reprocess",
     timeout=30, forbid=STILL_OPEN, expect="detail set en=1 x=3 y=2 len=1$")
case("D_menu_no_clicks", "D", "gui", "ok_420.jpg", "--detail-dlg load,cancel", timeout=30, forbid=STILL_OPEN,
     expect="detail dialog: Detailed Scan Decode Options \\(X=0 Y=0 len=1 enabled=0 load=off note=-\\)")
case("D_menu_cancel", "D", "gui", "ok_420.jpg", "--mark 20,4 --detail-dlg load,cancel", timeout=30,
     forbid=STILL_OPEN, expect="detail set en=0 x=0 y=0 len=1$")
case("D_menu_typed", "D", "gui", "ok_420.jpg", "--detail-dlg x=2,y=1,len=2,on,ok --auto-reprocess", timeout=30,
     forbid=STILL_OPEN, expect="detail report now blocks=12 first=2,1 last=3,1 ")
# Fields are checked on OK
BAD_STEPS = "--detail-dlg x=,ok,x=99999999999,ok,x=2,on,len=0,ok,len=3,ok"
case("D_menu_bad_empty", "D", "gui", "ok_420.jpg", BAD_STEPS, timeout=30,
     expect="message box: Enter a whole number from 0 to 4294967295 for MCU X\\.$")
case("D_menu_bad_len0", "D", "gui", "ok_420.jpg", BAD_STEPS, timeout=30,
     expect="message box: Enter at least 1 for # MCUs: no MCUs would be reported\\.$")
case("D_menu_bad_then_ok", "D", "gui", "ok_420.jpg", BAD_STEPS, timeout=30, forbid=STILL_OPEN,
     expect="detail set en=1 x=2 y=0 len=3$")
# Without Automatic Reprocessing the report changes at the next decode
case("D_menu_no_auto_now", "D", "gui", "ok_420.jpg", "--mark 20,4/40,20 --detail-dlg load,ok", timeout=30,
     expect="detail report now blocks=0 first=- .* marks=2$")
case("D_menu_no_auto_reprocess", "D", "gui", "ok_420.jpg", "--mark 20,4/40,20 --detail-dlg load,ok", timeout=30,
     expect="detail report after reprocess blocks=36 first=1,0 last=2,1 ")
# OK with nothing changed doesn't reprocess (the marks stay in the report)
case("D_menu_unchanged", "D", "gui", "ok_420.jpg", "--mark 20,4 --detail-dlg ok --auto-reprocess", timeout=30,
     expect="detail report now blocks=0 .* marks=1$")
# Opened again: it shows the setting; then turned off
case("D_menu_reopen", "D", "gui", "ok_420.jpg", "--detail-dlg x=1,on,ok+off,ok --auto-reprocess", timeout=30,
     expect="detail dialog: Detailed Scan Decode Options \\(X=1 Y=0 len=1 enabled=1 ")
case("D_menu_off", "D", "gui", "ok_420.jpg", "--detail-dlg x=1,on,ok+off,ok --auto-reprocess", timeout=30,
     expect="detail set en=0 x=1 y=0 len=1$", forbid="detail report (now|after reprocess) blocks=[1-9]")
# View > Image Chan added another RGB dump to the report for each channel shown
case("D_menu_channels", "D", "gui", "ok_420.jpg", "--mark 63,47 --detail-dlg load,ok --auto-reprocess --cycle-channels",
     timeout=60, expect="detail report after channels blocks=6 first=3,2 last=3,2 .* rgb dumps=1 rows=16 ")
# The setting stays for the next file
case("D_menu_next_file", "D", "gui", ["ok_420.jpg", "ok_444.jpg"],
     "--mark 63,47 --detail-dlg load,ok --auto-reprocess", timeout=60,
     expect="detail report blocks=3 first=3,2 last=3,2 .* rows=8 header=Detailed Decode: 1 MCU from MCU \\[3,2\\]")
# Positions clicked on the previous file aren't loaded after a failed open
case("D_menu_failed_open", "D", "gui", ["ok_420.jpg", "e_no_such_file.jpg"],
     "--mark 20,4 --detail-dlg-at 2 --detail-dlg load,cancel", timeout=30,
     expect="detail dialog: .* load=off ")
case("D_menu_decode_off", "D", "gui", "ok_420.jpg", "--no-decode-image --detail-dlg cancel", timeout=30,
     expect="note=Options > Scan Segment > Decode Image is off, so the scan isn't decoded and there is nothing "
            "to report\\.\\)$")
case("D_menu_psd", "D", "gui", "psd_rgb_rle.psd", "--detail-dlg x=0,on,ok --auto-reprocess", timeout=30,
     forbid=STILL_OPEN, expect="detail report after reprocess blocks=0 ")

# --- A: Help > About JPEGsnoop ---------------------------------------------------
# The menu item did nothing. gui --about opens the dialog through the menu and
# prints what it shows; on macOS the item belongs in the application menu.
snoop_h = (Path(__file__).resolve().parent.parent / "source" / "snoop.h").read_text()
version = snoop_h.split('#define VERSION_STR "', 1)[1].split('"', 1)[0]
ver_re = version.replace(".", "\\.")
case("A_about_role", "A", "gui", "ok_444.jpg", "--about", timeout=30, expect="harness: about role=about$")
case("A_about_text", "A", "gui", "ok_444.jpg", "--about", timeout=30,
     expect="harness: about: About JPEGsnoop \\(JPEGsnoop / Version %s / Copyright [^ ]+ 2017 Calvin Hass / "
            "Qt 6\\.[0-9]+\\.[0-9]+\\)$" % ver_re)
case("A_about_links", "A", "gui", "ok_444.jpg", "--about", timeout=30,
     expect="harness: about links: Documentation and Updates for JPEGsnoop -> "
            "https://www\\.impulseadventure\\.com/photo/jpeg-snoop\\.html\\?ver=%s \\| "
            "www\\.impulseadventure\\.com/photo/ -> https://www\\.impulseadventure\\.com/photo/ external=1$" % ver_re)
# The icon is the 48-pixel frame with the most colours (the 32-bit one; the
# 16-colour frame has about a dozen), doubled pixel for pixel on a Retina screen
ICON_COLOURS = "colours=([6-9][0-9]{2}|[0-9]{4,})$"
case("A_about_icon", "A", "gui", "ok_444.jpg", "--about", timeout=30,
     expect="harness: about icon=48x48 dpr=1 " + ICON_COLOURS)
case("A_about_icon_retina", "A", "gui", "ok_444.jpg", "--about --scale 2", timeout=30,
     expect="harness: about icon=96x96 dpr=2 " + ICON_COLOURS)
case("A_about_closes", "A", "gui", "ok_444.jpg", "--about", timeout=30, expect="harness: about closed=1$")

# --- T: Edit > Select All and Copy (the report) ----------------------------------
# Neither item had a handler, and the report could be typed into; on Windows it
# is read-only. gui --edit runs steps on the report and prints, after each, how
# much is selected, the Edit items enabled, the undo history, where the focus is,
# whether the report changed and what the clipboard holds (first a marker).
def edit_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "T", "gui", list(names), ("--edit " + steps + " " + extra).strip(), timeout=30,
         expect=expect, forbid=forbid)


edit_case("T_selectall_menu", "selectall",
          "harness: edit selectall: selected=all chars=[1-9][0-9]* copy=1 selectall=1 ")
edit_case("T_selectall_focuses", "unfocus,selectall", "harness: edit selectall: selected=all .* focus=report ")
edit_case("T_selectall_off_before_file", "state",
          "harness: edit state: selected=none chars=[0-9]+ copy=0 selectall=0 ", extra="--edit-before")
edit_case("T_copy_menu", "selectall,copy", "harness: edit copy: selected=all .* clipboard=report$")
# Select All is on while the report has any text, as on Windows: also when the
# first file is empty or can't be opened and the report only holds the error
# (it came on only once a file opened)
for cid, name in (("T_selectall_empty_file", "s_empty.jpg"), ("T_selectall_failed_open", "f4_no_such_file.jpg")):
    edit_case(cid, "state,selectall,copy",
              "harness: edit copy: selected=all chars=[1-9][0-9]* copy=1 selectall=1 .* clipboard=report$",
              names=(name,))
edit_case("T_copy_off_without_selection", "state", "harness: edit state: selected=none chars=[0-9]+ copy=0 selectall=1 ")
# Shift+Up after Select All leaves all but the last line selected: keyboard
# selection still works in the read-only report, and Copy takes only that
edit_case("T_copy_part", "selectall,key:shift-up,copy",
          "harness: edit copy: selected=part chars=[0-9]+ copy=1 .* clipboard=selection$")
edit_case("T_click_deselects", "selectall,click", "harness: edit click: selected=none chars=[0-9]+ copy=0 ")
# Cmd+A and Cmd+C with the focus outside the report reach the menu items
edit_case("T_key_selectall_unfocused", "unfocus,key:selectall",
          "harness: edit key:selectall: selected=all .* focus=report ")
edit_case("T_key_copy_unfocused", "selectall,unfocus,key:copy",
          "harness: edit key:copy: selected=all .* focus=none .* clipboard=report$")
edit_case("T_key_copy_focused", "key:selectall,key:copy", "harness: edit key:copy: selected=all .* clipboard=report$")
# The report can't be changed: typing after Select All replaced all of it
edit_case("T_type_after_selectall", "selectall,type:x", "harness: edit type:x: selected=all .* report=same ",
          forbid="report=changed")
edit_case("T_paste_ignored", "key:paste", "harness: edit key:paste: .* report=same ", forbid="report=changed")
edit_case("T_cut_ignored", "key:selectall,key:cut", "harness: edit key:cut: .* report=same ", forbid="report=changed")
edit_case("T_backspace_ignored", "key:selectall,key:backspace", "harness: edit key:backspace: .* report=same ",
          forbid="report=changed")
edit_case("T_undo_ignored", "key:undo", "harness: edit key:undo: selected=none .* undo-steps=0 .* report=same ",
          forbid="report=changed")
# No undo history is kept for the decoder's lines (one step per line before)
edit_case("T_no_undo_history", "state", "harness: edit state: .* undo-steps=0 readonly=1 ")
# The clipboard has plain text and HTML. The HTML keeps the report's font, and
# its coloured lines have their light-background colours even in Dark Mode
# (the dark ones are pale on a white page); the report keeps its own colours.
LIGHT_COLOURS = "#0101ff,#2020ff,#800101"
edit_case("T_copy_html_light", "selectall,copy",
          f"harness: edit clipboard formats=text/plain,text/html html colours={LIGHT_COLOURS} fonts=Courier "
          f"report colours={LIGHT_COLOURS}$")
edit_case("T_copy_html_dark", "selectall,copy",
          f"harness: edit clipboard formats=text/plain,text/html html colours={LIGHT_COLOURS} fonts=Courier "
          "report colours=#6ca0ff,#8ab4ff,#ff8a80$", extra="--dark")
# A selection doesn't outlive the report: the next file starts with none
edit_case("T_next_file_clears", "state,selectall",
          "harness: edit state: selected=none chars=[0-9]+ copy=0 ", names=("ok_444.jpg", "ok_420.jpg"),
          forbid="harness: edit state: selected=(all|part)|harness: edit state: selected=none chars=[0-9]+ copy=1")

# --- FN: Edit > Find and Find Next (the report) ----------------------------------
# Neither item did anything. As on Windows (MFC's rich edit view), Find shows a
# modeless dialog and Find Next repeats its last search. gui --edit's Find steps
# also print a "harness: find" line after each step: the Find items enabled, the
# dialog and its fields, the selection and which match it is, where the dialog
# is and the status bar's message. ok_444.jpg's report has "Precision" on lines
# 25, 42 and 57, "SOF" four times (once as "SOF0") and "Calvin" once, on line 1;
# loading a file leaves the caret at the end of the report.
def find_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "FN", "gui", list(names), ("--edit " + steps + " " + extra).strip(), timeout=30,
         expect=expect, forbid=forbid)


FIND_MSG = 'status="(Cannot find|Reached)'
find_case("FN_find_off_before_file", "find", "harness: find find: find=0 findnext=0 dialog=hidden ",
          extra="--edit-before")
find_case("FN_dialog_opens", "find",
          'harness: find find: find=1 findnext=0 dialog=active what="" button=0 case=0 word=0 dir=down sel=none ')
# Find is on while the report has text, as Select All: also for an error
for cid, name in (("FN_find_empty_file", "s_empty.jpg"), ("FN_find_failed_open", "f4_no_such_file.jpg")):
    find_case(cid, "find", "harness: find find: find=1 findnext=0 dialog=active ", names=(name,))
# The dialog's Find Next is off while its text is empty; Edit > Find Next is
# off until a search has been made
find_case("FN_button_off_when_empty", "find,what:x,what:", 'harness: find what:: .* what="" button=0 ')
find_case("FN_next_off_until_search", "find,what:Precision",
          'harness: find what:Precision: find=1 findnext=0 dialog=active what="Precision" button=1 ')
# The first search goes on from the top when nothing follows the caret (at the
# report's end after loading), without a message
find_case("FN_first_search_wraps", "find,what:Precision,next",
          'harness: find next: find=1 findnext=1 dialog=active .* sel=L25\\+9 "Precision" match=1/3 ',
          forbid=FIND_MSG)
find_case("FN_return_finds", "find,what:Precision,return", 'harness: find return: .* match=1/3 ')
find_case("FN_next_goes_on", "line:1,find,what:Precision,next,next,next",
          'harness: find next: .* sel=L57\\+9 "Precision" match=3/3 ', forbid=FIND_MSG)
# After the last match the search stops once, with a beep and a message, and
# the next one goes on from the top (Windows only beeps)
find_case("FN_end_stops", "line:1,find,what:Precision,next,next,next,next",
          'harness: find next: .* match=3/3 dialog-at=[a-z]+ '
          'status="Reached the end of the report: Find Next goes on from the top"$')
find_case("FN_end_then_top", "line:1,find,what:Precision,next,next,next,next,next",
          'harness: find next: .* match=1/3 dialog-at=[a-z]+ status=""$')
find_case("FN_up", "line:end,find,what:Precision,up,next,next", 'harness: find next: .* dir=up .* match=2/3 ',
          forbid=FIND_MSG)
find_case("FN_up_start_stops", "line:end,find,what:Precision,up,next,next,next,next",
          'harness: find next: .* match=1/3 dialog-at=[a-z]+ '
          'status="Reached the start of the report: Find Next goes on from the bottom"$')
find_case("FN_up_then_bottom", "line:end,find,what:Precision,up,next,next,next,next,next",
          'harness: find next: .* match=3/3 dialog-at=[a-z]+ status=""$')
find_case("FN_not_found", "find,what:nosuchtext,next",
          'harness: find next: .* sel=none match=0/0 dialog-at=- status="Cannot find "nosuchtext""$')
find_case("FN_ignores_case", "find,what:precision,next", 'harness: find next: .* sel=L25\\+9 "Precision" match=1/3 ')
find_case("FN_match_case", "find,what:precision,case,next", 'harness: find next: .* status="Cannot find "precision""$')
find_case("FN_part_of_word", "find,what:SOF,next", 'harness: find next: .* sel=L54\\+3 "SOF" match=1/4 ')
find_case("FN_whole_word", "find,what:SOF,word,next", 'harness: find next: .* status="Cannot find "SOF""$')
# A search starts after the selection's start, or at the caret
find_case("FN_from_selection", "select:Precision,find,next", 'harness: find next: .* match=2/3 ', forbid=FIND_MSG)
find_case("FN_from_caret", "line:40,find,what:Precision,next", 'harness: find next: .* match=2/3 ', forbid=FIND_MSG)
# With one match, selected, a new search finds it again rather than nothing
find_case("FN_only_match_again", "select:SOF0,find,next", 'harness: find next: .* sel=L54\\+4 "SOF0" match=1/1 ',
          forbid=FIND_MSG)
# Edit > Find Next (and Cmd+G) repeats the last search, from the selection
find_case("FN_menu_find_next", "find,what:Precision,next,cancel,activate,findnext",
          'harness: find findnext: find=1 findnext=1 dialog=hidden .* match=2/3 ')
find_case("FN_find_next_from_caret", "find,what:Precision,next,cancel,activate,line:50,findnext",
          'harness: find findnext: .* match=3/3 ')
find_case("FN_key_find", "key:find", 'harness: find key:find: find=1 findnext=0 dialog=active ')
find_case("FN_key_find_next", "key:find,what:Precision,return,escape,activate,key:findnext",
          'harness: find key:findnext: find=1 findnext=1 dialog=hidden .* match=2/3 ')
find_case("FN_cancel_closes", "find,cancel", 'harness: find cancel: find=1 findnext=0 dialog=hidden ')
find_case("FN_escape_closes", "find,escape", 'harness: find escape: find=1 findnext=0 dialog=hidden ')
# The dialog holds the selected text when it is on one line, otherwise the
# last text searched for, and the last search's options; while it is open,
# Find only brings it to the front
find_case("FN_text_from_selection", "select:Huffman,find", 'harness: find find: .* what="Huffman" button=1 ')
find_case("FN_text_kept_for_lines", "find,what:Precision,next,cancel,selectall,find",
          'harness: find find: find=1 findnext=1 dialog=active what="Precision" ')
find_case("FN_options_kept", "find,what:precision,case,up,next,cancel,line:1,find",
          'harness: find find: find=1 findnext=1 dialog=active what="precision" button=1 case=1 word=0 dir=up ')
find_case("FN_open_dialog_kept", "find,what:abc,find", 'harness: find find: .* what="abc" ')
# The dialog moves off the text found when it covers it: above it, or below it
# when there's no room above (as on Windows)
find_case("FN_dialog_moves_up", "select:SOF0,find,cover,next", 'harness: find next: .* match=1/1 dialog-at=above ')
find_case("FN_dialog_moves_down", "top,select:Calvin,find,cover,next",
          'harness: find next: .* match=1/1 dialog-at=below ')
# Searching doesn't change the report
find_case("FN_report_unchanged", "find,what:Precision,next,next,return,escape",
          "harness: edit escape: .* report=same ", forbid="report=changed")
# The last search lasts into the next file, where it starts afresh at the
# caret (at the end, so it goes on from the top). File 1's Find Next is off.
find_case("FN_next_file", "findnext,find,what:Precision,next,next,next,cancel,activate",
          'harness: find findnext: find=1 findnext=1 dialog=hidden .* match=1/3 ', names=("ok_444.jpg", "ok_444.jpg"))

# --- OV: Tools > File Overlay ------------------------------------------------------
# It did nothing. As on Windows, its dialog sets bytes to decode in place of the
# file's own at an offset, and OK or Apply reprocesses the file with them. gui
# --overlay answers the dialog and prints its fields and a summary of the
# report: APP0's version, the overlays the scan section lists (the first's
# length and offset) and the header's overlay line. ok_444.jpg's APP0 version
# bytes, 01 01, are at 0x0B; its density at 0x0E; the file is 1380 bytes.
def overlay_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "OV", "gui", list(names), ("--overlay " + steps + " " + extra).strip(), timeout=30,
         expect=expect, forbid=forbid)


OVL_NONE = "version=\\[1\\.1\\] overlays=0 len=- offset=- header=-$"
OVL_VERSION = "version=\\[1\\.2\\] overlays=1 len=2 offset=0x0000000B header=2@0x0000000B$"
overlay_case("OV_off_failed_open", "on,ok", "harness: menu File Overlay unavailable$", names=("f4_no_such_file.jpg",))
# The menu stays on after a file that couldn't be opened
overlay_case("OV_not_analyzed", "on,ok",
             "harness: message box: The file shown couldn't be analyzed, so there is nothing to overlay\\.$",
             names=("ok_444.jpg", "f4_no_such_file.jpg"), extra="--overlay-at 2")
overlay_case("OV_dialog_opens", "cancel",
             'harness: overlay dialog: File Overlay Test \\(en=0 offset=0x00000000 original="0x FF D8 FF E0 00 10 4A 46 '
             '\\.\\.\\." bin="0b 11111111 11011000 11111111 11100000 00000000 00010000 01001010 01000110 \\.\\.\\." '
             'value= len=0\\)$')
# The file's bytes follow the offset typed (on Windows only Load Original
# updated them); near the end there are fewer, with no "..."; past it, none
overlay_case("OV_original_follows", "offset=0B,fields,cancel",
             'harness: overlay fields \\(en=0 offset=0B original="0x 01 01 00 00 01 00 01 00 \\.\\.\\." '
             'bin="0b 00000001 00000001 ')
overlay_case("OV_original_load", "offset=0B,load,fields,cancel", 'harness: overlay fields \\(.* original="0x 01 01 ')
overlay_case("OV_original_at_end", "offset=0x55E,fields,cancel",
             'harness: overlay fields \\(.* original="0x AE 27 08 7F FF D9" bin="0b 10101110 00100111 00001000 '
             '01111111 11111111 11011001" ')
overlay_case("OV_original_past_end", "offset=564,fields,cancel",
             'harness: overlay fields \\(.* original="\\(past the end of the file\\)" bin="" ')
# Len follows the bytes typed
overlay_case("OV_len_follows", "value=01020304,fields,cancel", "harness: overlay fields \\(.* value=01020304 len=4\\)$")
# OK puts the overlay in place and reprocesses: the report shows the new
# version, lists the overlay with its offset, and says so in its header
overlay_case("OV_ok_installs", "on,offset=0B,value=0102,ok", "harness: overlay after: " + OVL_VERSION)
overlay_case("OV_reprocess_keeps", "on,offset=0B,value=0102,ok", "harness: overlay after reprocess: " + OVL_VERSION)
# With Decode Image off there is no scan section, but the header still says so
overlay_case("OV_header_without_scan", "on,offset=0B,value=0102,ok",
             "harness: overlay after: version=\\[1\\.2\\] overlays=0 len=- offset=- header=2@0x0000000B$",
             extra="--no-decode-image")
# A shorter Len uses only the first bytes
overlay_case("OV_len_shorter", "on,offset=0B,value=0203,len=1,ok",
             "harness: overlay after: version=\\[2\\.1\\] overlays=1 len=1 offset=0x0000000B ")
overlay_case("OV_apply_reprocesses", "on,offset=0B,value=0102,apply,cancel",
             "harness: overlay applied \\(en=1 offset=0x0000000B .* value=0102 len=2\\) report " + OVL_VERSION)
overlay_case("OV_cancel_keeps_applied", "on,offset=0B,value=0102,apply,cancel", "harness: overlay after: " + OVL_VERSION)
overlay_case("OV_cancel_changes_nothing", "on,offset=0B,value=0102,cancel", "harness: overlay after: " + OVL_NONE)
# A second Apply replaces the overlay (on Windows it added another, and the
# first stayed in place)
overlay_case("OV_apply_replaces", "on,offset=0B,value=0102,apply,offset=0x0E,value=0002,ok",
             "harness: overlay after: version=\\[1\\.1\\] overlays=1 len=2 offset=0x0000000E header=2@0x0000000E$")
overlay_case("OV_reopen_shows", "on,offset=0B,value=0102,ok+cancel",
             'harness: overlay dialog: File Overlay Test \\(en=1 offset=0x0000000B original="0x 01 01 .* '
             'value=0102 len=2\\)$')
overlay_case("OV_off_removes", "on,offset=0B,value=0102,ok+off,ok", "harness: overlay after: " + OVL_NONE)
# An overlay is for the file it was made for: the next file is decoded as it is
overlay_case("OV_next_file_clears", "on,offset=0B,value=0102,ok",
             "harness: overlay file 2: " + OVL_NONE, names=("ok_444.jpg", "ok_420.jpg"))
# Fields that can't be used are refused with a message, and the dialog stays
# open (on Windows they became offset 0, or bytes read from past the value)
for cid, steps, msg in (
        ("OV_bad_offset", "on,offset=zz,value=01,ok",
         "File Offset must be a hexadecimal number, such as 0x0000ABCD\\."),
        ("OV_offset_past_end", "on,offset=564,value=01,ok",
         "File Offset 0x00000564 is past the end of the file \\(1380 bytes\\)\\."),
        ("OV_bad_value", "on,offset=0B,value=XYZ,ok",
         "The new value must be hexadecimal bytes, two digits each, such as FFD8\\."),
        ("OV_odd_digits", "on,offset=0B,value=010,ok",
         "The new value must be hexadecimal bytes, two digits each, such as FFD8\\."),
        ("OV_empty_value", "on,offset=0B,ok",
         "The new value must be hexadecimal bytes, two digits each, such as FFD8\\."),
        ("OV_len_too_long", "on,offset=0B,value=0102,len=3,ok",
         "Len must be from 1 to the number of bytes in the new value \\(2\\)\\."),
        ("OV_len_zero", "on,offset=0B,value=0102,len=0,ok",
         "Len must be from 1 to the number of bytes in the new value \\(2\\)\\."),
        ("OV_runs_past_end", "on,offset=0x562,value=FFD9FF,ok",
         "The overlay runs past the end of the file \\(1380 bytes\\)\\."),
        ("OV_value_too_long", "on,offset=10,value=" + "00" * 500 + ",ok",
         "The new value can be at most 499 bytes\\.")):
    overlay_case(cid, steps + ",cancel", "harness: message box: " + msg + "$", forbid="overlays=1|header=[0-9]")
# 499 bytes is allowed (zeros over the tables and frame header: decoding must
# cope)
overlay_case("OV_max_length", "on,offset=10,value=" + "00" * 499 + ",ok",
             "harness: overlay after: .* header=499@0x00000010$")
# Off, the fields don't matter
overlay_case("OV_off_ignores_fields", "off,offset=zz,ok", "harness: overlay after: " + OVL_NONE, forbid="message box")
# Export JPEG's Enable Overlays (disabled until now) writes the overlaid bytes;
# without it the export is the file's own. The file itself is never changed.
c444_ovl = bytearray(c444)
c444_ovl[0x0B:0x0D] = b"\x01\x02"
overlay_case("OV_export_box_on", "cancel", "harness: dialog: Enable Overlays available$", extra="--export-menus")
case("OV_export_with", "OV", "export-jpeg", "ok_444.jpg", "--overlays --overlay on,offset=0B,value=0102,ok", timeout=30,
     expect=f"harness: exported 1 md5s={hashlib.md5(c444_ovl).hexdigest()}$")
case("OV_export_without", "OV", "export-jpeg", "ok_444.jpg", "--overlay on,offset=0B,value=0102,ok", timeout=30,
     expect=f"harness: exported 1 md5s={hashlib.md5(c444).hexdigest()}$")
case("OV_export_source_kept", "OV", "export-jpeg", "ok_444.jpg", "--overlays --overlay on,offset=0B,value=0102,ok",
     timeout=30, expect="harness: source unchanged=1$")
# The export finds the JPEG in the bytes it copies. With an EOI (FFD9) overlaid
# in the scan, an export without Enable Overlays analyzed the overlaid bytes,
# stopped at that EOI and wrote the first 675 bytes of the file itself, with no
# EOI; Extract All did the same. Now it is the whole file, with a note in the
# report, and the overlay is still in place afterwards. With Enable Overlays,
# the export ends at the overlaid EOI.
EOI_STEPS = "--overlay on,offset=2A1,value=FFD9,ok"
c444_eoi = bytearray(c444)
c444_eoi[0x2A1:0x2A3] = b"\xff\xd9"
EOI_KEPT = ("harness: overlay after export and reprocess: version=\\[1\\.1\\] overlays=1 len=2 offset=0x000002A1 "
            "header=2@0x000002A1$")
for cid, flags, data in (("OV_eoi_export_without", "", c444), ("OV_eoi_export_with", "--overlays ", c444_eoi[:0x2A3]),
                         ("OV_eoi_all_without", "--all ", c444),
                         ("OV_eoi_all_with", "--all --overlays ", c444_eoi[:0x2A3])):
    case(cid, "OV", "export-jpeg", "ok_444.jpg", flags + EOI_STEPS, timeout=30,
         expect=f"harness: exported 1 md5s={hashlib.md5(data).hexdigest()}$",
         forbid="File Overlay left out" if "--overlays" in flags else "-")
case("OV_eoi_export_note", "OV", "export-jpeg", "ok_444.jpg", EOI_STEPS, timeout=30,
     expect="  NOTE: File Overlay left out \\(Enable Overlays is off\\): the file was analyzed and exported as it is "
            "on disk$")
case("OV_eoi_export_kept", "OV", "export-jpeg", "ok_444.jpg", EOI_STEPS, timeout=30, expect=EOI_KEPT)
case("OV_eoi_all_kept", "OV", "export-jpeg", "ok_444.jpg", "--all " + EOI_STEPS, timeout=30, expect=EOI_KEPT)
# A failed export (a folder that doesn't exist) writes nothing, so the note
# mustn't say the file was exported; the overlay is still kept
UNWRITABLE = "--out /nonexistent-jpegsnoop-dir/out.jpg "
for cid, flags in (("OV_eoi_unwritable_note", ""), ("OV_eoi_all_unwritable_note", "--all ")):
    case(cid, "OV", "export-jpeg", "ok_444.jpg", flags + UNWRITABLE + EOI_STEPS, timeout=30,
         expect="  NOTE: File Overlay left out \\(Enable Overlays is off\\): the file was analyzed as it is on disk$",
         forbid="exported as it is on disk")
case("OV_eoi_unwritable_kept", "OV", "export-jpeg", "ok_444.jpg", UNWRITABLE + EOI_STEPS, timeout=30,
     expect=EOI_KEPT)

# --- Batch Process (BP) -------------------------------------------------------
# File > Batch Process. The driver copies a folder made here to <tmp>/batch/in,
# so a batch can write beside its files, answers the dialog, and prints each
# file the batch writes below <tmp>/batch ("<batch>" in its output) and a
# "files:" line listing them.


def tree(root, entries):
    """A folder of files below files_dir: {path below it: bytes}."""
    for rel, data in entries.items():
        path = files_dir / root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return root


def batch_case(cid, steps, expect="-", names=("bp_tree",), extra="", forbid="-", timeout=60):
    case(cid, "BP", "batch", list(names), ("--batch " + steps + " " + extra).strip(), timeout=timeout, expect=expect,
         forbid=forbid)


junk_bytes = bytes(range(100))
bp_two = junk_bytes + c444 + junk_bytes + c420 + junk_bytes
tree("bp_tree", {
    "a.jpg": c444,
    "B.JPEG": c420,                 # the extension in capitals
    "empty.jpg": b"",
    "junk.jpg": bytes(range(256)) * 4,
    "notes.txt": b"not an image\n",
    "two.bin": bp_two,              # two JPEGs inside, for Extract All
    ".hidden.jpg": c444,            # hidden files and folders are left out
    ".hiddendir/f.jpg": c444,
    "photos.jpg/e.jpg": c444,       # a folder named like an image
    "sub/c.jpg": c444,
    "sub/deeper/d.jpeg": c420,
})
# A link to its parent folder: followed (as master did), the batch would list
# the same files again at each level
try:
    os.symlink("..", files_dir / "bp_tree/sub/loop")
except FileExistsError:
    pass
tree("bp_none", {"notes.txt": b"not an image\n", "sub/readme.md": b"# none\n"})
(files_dir / "bp_empty").mkdir(exist_ok=True)
tree("bp_old", {"a.jpg": c444, "a.jpg.txt": b"an old log\n", "b.jpg": c420})
tree("bp_old_all", {"a.jpg": c444, "a.jpg.export.000001.jpg": b"an old export\n"})
tree("bp_logdir", {"a.jpg": c444, "a.jpg.txt/keep": b"", "b.jpg": c420})
tree("bp_utf8", {"café.jpg": c444})
tree("bp_alias", {"a.jpg": c444, "b.jpg": c420, "notes.txt": b"not an image\n"})
tree("bp_self", {"x.jpg.txt": c444})
# A Motion JPEG AVI: its frames leave out the Huffman tables
mjpeg_movi = b"movi" + chunk(b"00dc", mjpeg_frame) + chunk(b"00dc", mjpeg_frame)
mjpeg_avi_body = (b"AVI " + chunk(b"LIST", b"hdrl" + chunk(b"avih", bytes(56))) + chunk(b"LIST", strl)
                  + chunk(b"LIST", mjpeg_movi))
tree("bp_mjpeg", {"clip.avi": b"RIFF" + struct.pack("<I", len(mjpeg_avi_body)) + mjpeg_avi_body})

BP_RUN = "in=in,out=out,ok"
BP_FILES = "out/B.JPEG.txt,out/a.jpg.txt,out/empty.jpg.txt,out/junk.jpg.txt"
BP_SUB_FILES = BP_FILES + ",out/photos.jpg/e.jpg.txt,out/sub/c.jpg.txt,out/sub/deeper/d.jpeg.txt"

# The menu item, in the File menu as on Windows, on with no file open
batch_case("BP_menu", "cancel",
           'harness: batch menu=&File after="Reprocess File" enabled=1 tip="Start batch processing"$')
batch_case("BP_dialog_defaults", "fields,cancel",
           "harness: batch dialog: Batch Processing \\(in=<home> out=<home> subfolders=0 all=0\\)$")
batch_case("BP_dialog_cancel", "in=in,out=out,cancel", "harness: batch 1 files: -$")
batch_case("BP_dialog_cancel_report", "in=in,out=out,cancel", "harness: batch 1 report unchanged$")

# The logs: .jpg and .jpeg files (any case), not hidden ones, each log the
# report File > Open shows for the file
batch_case("BP_logs", BP_RUN, f"harness: batch 1 files: {BP_FILES}$")
batch_case("BP_subfolders", "in=in,out=out,subfolders,ok", f"harness: batch 1 files: {BP_SUB_FILES}$")
batch_case("BP_order", "in=in,out=out,subfolders,ok",
           "^  Batch processing file      5 of      7 \\( 71\\.43 %\\): \\[<batch>/in/photos\\.jpg/e\\.jpg\\]$")
batch_case("BP_hidden_left_out", "in=in,out=out,subfolders,all,ok", "harness: batch 1 wrote new=", forbid="hidden")
batch_case("BP_link_not_followed", "in=in,out=out,subfolders,ok", "harness: batch 1 wrote new=7 ", forbid="/loop")
batch_case("BP_logs_are_reports", "in=in,out=out,subfolders,ok", "harness: batch compared 7 same=7$",
           extra="--batch-compare")
batch_case("BP_log_header", BP_RUN, "^harness: cat:   Filename: \\[<batch>/in/a\\.jpg\\]$",
           extra="--batch-cat out/a.jpg.txt")
batch_case("BP_log_decoded", BP_RUN, "^harness: cat: \\*\\*\\* Decoding SCAN Data \\*\\*\\*$",
           extra="--batch-cat out/a.jpg.txt")
batch_case("BP_log_empty_file", BP_RUN,
           "^harness: cat: ERROR: File length is zero, no decoding done\\. \\[<batch>/in/empty\\.jpg\\]$",
           extra="--batch-cat out/empty.jpg.txt")
# UTF-8, as Save Log writes (master used the Windows code page)
batch_case("BP_log_utf8", BP_RUN, "^harness: cat:   Filename: \\[<batch>/in/caf(\u00e9|e\u0301)\\.jpg\\]$",
           names=("bp_utf8",), extra="--batch-cat out/caf\u00e9.jpg.txt")
batch_case("BP_sources_kept", "in=in,out=in,subfolders,all,ok", "harness: batch sources unchanged=1$")
batch_case("BP_beside_images", "in=in,out=in,ok",
           "harness: batch 1 files: in/B.JPEG.txt,in/a.jpg.txt,in/empty.jpg.txt,in/junk.jpg.txt$")

# The report: the settings, master's line for each file, what it wrote
batch_case("BP_report_header", BP_RUN, "^\\*\\*\\* Batch Processing \\*\\*\\*$")
batch_case("BP_report_folders", BP_RUN, "^  Output folder:      \\[<batch>/out\\]$")
batch_case("BP_report_log", BP_RUN, "^    Log written to \\[<batch>/out/B\\.JPEG\\.txt\\]$")
batch_case("BP_report_complete", BP_RUN, "^  Batch processing complete: 4 files$")
batch_case("BP_report_counts", BP_RUN, "^  Logs written:      4$")
batch_case("BP_message", BP_RUN,
           "harness: message box: Batch processing complete\\. 4 of 4 files processed: 4 logs written\\.$")

# Extract All: every file, and every JPEG in each
bp_all_files = sorted(
    [f"out/{n}.txt" for n in ("a.jpg", "B.JPEG", "empty.jpg", "junk.jpg", "notes.txt", "two.bin", "photos.jpg/e.jpg",
                              "sub/c.jpg", "sub/deeper/d.jpeg")]
    + [f"out/{n}.export.000001.jpg" for n in ("a.jpg", "B.JPEG", "photos.jpg/e.jpg", "sub/c.jpg", "sub/deeper/d.jpeg",
                                              "two.bin")]
    + ["out/two.bin.export.000002.jpg"])
BP_ALL = "in=in,out=out,subfolders,all,ok"
batch_case("BP_all_files", BP_ALL, f"harness: batch 1 files: {','.join(bp_all_files)}$")
batch_case("BP_all_second", BP_ALL,
           f"harness: batch 1 new out/two\\.bin\\.export\\.000002\\.jpg size={len(c420)} md5={md5(c420)}$")
batch_case("BP_all_report", BP_ALL,
           f"^    \\[0x{len(junk_bytes) * 2 + len(c444):08x}\\] Exported to \\[<batch>/out/two\\.bin\\.export\\.000002\\.jpg\\]$")
batch_case("BP_all_message", BP_ALL,
           "harness: message box: Batch processing complete\\. 9 of 9 files processed: 9 logs written, 7 JPEGs "
           "exported\\.$")
# Motion JPEG frames get the standard tables, as Export JPEG's default
# (master exported them without)
batch_case("BP_all_mjpeg", "in=in,out=out,all,ok",
           f"harness: batch 1 new out/clip\\.avi\\.export\\.000002\\.jpg size={len(mjpeg_std)} md5={md5(mjpeg_std)}$",
           names=("bp_mjpeg",))
# The output folder inside the input folder isn't searched: a second batch
# writes the same files again (master processed the first one's logs)
BP_INSIDE = "in=in,out=in/logs,subfolders,all,ok"
batch_case("BP_output_inside", BP_INSIDE + "+" + BP_INSIDE, "harness: batch 2 wrote new=0 changed=0$",
           extra="--replace", forbid="logs/logs")
batch_case("BP_output_inside_first", BP_INSIDE + "+" + BP_INSIDE, "harness: batch 1 wrote new=16 changed=0$",
           extra="--replace")

# Files that exist: asked first (master replaced them)
batch_case("BP_existing_asked", "in=in,out=in,ok",
           'harness: message box: A file the batch writes already exists: "a\\.jpg\\.txt"\\. Each one the batch '
           'writes again is replaced\\. Replace them\\?$', names=("bp_old",))
batch_case("BP_existing_cancel", "in=in,out=in,ok", "harness: batch 1 files: -$", names=("bp_old",))
batch_case("BP_existing_replace", "in=in,out=in,ok", "harness: batch 1 files: in/a.jpg.txt,in/b.jpg.txt$",
           names=("bp_old",), extra="--replace")
batch_case("BP_existing_replaced", "in=in,out=in,ok", "harness: batch 1 wrote new=1 changed=1$", names=("bp_old",),
           extra="--replace")
batch_case("BP_existing_export", "in=in,out=in,all,ok",
           'harness: message box: A file the batch writes already exists: "a\\.jpg\\.export\\.000001\\.jpg"\\.',
           names=("bp_old_all",))
batch_case("BP_existing_two", BP_RUN + "+" + BP_RUN,
           'harness: message box: 4 files the batch writes already exist, such as "a\\.jpg\\.txt"\\.')

# Errors: listed, and the other files still processed
batch_case("BP_unreadable", BP_RUN, "^    ERROR: Couldn't open file: .+$", extra="--batch-unreadable a.jpg")
batch_case("BP_unreadable_others", BP_RUN, "harness: batch 1 files: out/B.JPEG.txt,out/empty.jpg.txt,out/junk.jpg.txt$",
           extra="--batch-unreadable a.jpg")
batch_case("BP_unreadable_count", BP_RUN, "^  Files with errors: 1$", extra="--batch-unreadable a.jpg")
batch_case("BP_unreadable_message", BP_RUN, "1 file had errors; the report lists them\\.$",
           extra="--batch-unreadable a.jpg")
batch_case("BP_log_unwritable", "in=in,out=in,ok", "^    ERROR: Couldn't write the log \\[<batch>/in/a\\.jpg\\.txt\\]: .+$",
           names=("bp_logdir",), extra="--replace")
batch_case("BP_log_unwritable_others", "in=in,out=in,ok", "harness: batch 1 files: in/b.jpg.txt$",
           names=("bp_logdir",), extra="--replace")
batch_case("BP_output_unwritable", "in=in,out=/nonexistent-jpegsnoop-dir/logs,ok",
           'harness: message box: Couldn\'t create the output folder "/nonexistent-jpegsnoop-dir/logs"\\.$')
batch_case("BP_no_files", BP_RUN, 'harness: message box: There are no \\.jpg or \\.jpeg files in ".*/batch/in"\\.$',
           names=("bp_none",))
batch_case("BP_no_files_sub", "in=in,out=out,subfolders,ok", "files in \".*/batch/in\" or its subfolders\\.$",
           names=("bp_none",))
batch_case("BP_no_files_all", "in=in,out=out,all,ok", 'There are no files to process in ".*/batch/in"\\.$',
           names=("bp_empty",))

# A name the batch writes that is a link: the link is replaced, and the file
# it led to (an image the batch processes, or any other) is left as it was.
# Opened to write, a log named like a link to b.jpg wrote over b.jpg, whose
# own log then had no image in it.
BP_ALIAS = "in=in,out=in,ok"
BP_LINK_REPLACED = "now exists=1 symlink=0 names=1 target exists=1 unchanged=1$"
batch_case("BP_alias_symlink", BP_ALIAS, "harness: batch link sym:in/a\\.jpg\\.txt=in/b\\.jpg " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link sym:in/a.jpg.txt=in/b.jpg --replace")
batch_case("BP_alias_symlink_next", BP_ALIAS, "^harness: cat: \\*\\*\\* Decoding SCAN Data \\*\\*\\*$",
           names=("bp_alias",), extra="--batch-link sym:in/a.jpg.txt=in/b.jpg --replace --batch-cat in/b.jpg.txt")
batch_case("BP_alias_hardlink", BP_ALIAS, "harness: batch link hard:in/a\\.jpg\\.txt=in/b\\.jpg " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link hard:in/a.jpg.txt=in/b.jpg --replace")
batch_case("BP_alias_other_folder", BP_RUN, "harness: batch link sym:out/a\\.jpg\\.txt=in/b\\.jpg " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link sym:out/a.jpg.txt=in/b.jpg --replace")
batch_case("BP_alias_not_input", BP_RUN,
           "harness: batch link sym:out/a\\.jpg\\.txt=in/notes\\.txt " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link sym:out/a.jpg.txt=in/notes.txt --replace")
# A link that leads nowhere is asked about too (opened to write, it made the
# file it named)
batch_case("BP_alias_dangling_asked", BP_RUN,
           'harness: message box: A file the batch writes already exists: "a\\.jpg\\.txt"\\.',
           names=("bp_alias",), extra="--batch-link sym:out/a.jpg.txt=nowhere")
batch_case("BP_alias_dangling", BP_RUN,
           "harness: batch link sym:out/a\\.jpg\\.txt=nowhere now exists=1 symlink=0 names=1 target exists=0 "
           "unchanged=1$", names=("bp_alias",), extra="--batch-link sym:out/a.jpg.txt=nowhere --replace")
# Extract All's exports (a QSaveFile wrote through a symbolic link; it
# already replaced a hard one)
batch_case("BP_alias_export", "in=in,out=out,all,ok",
           "harness: batch link sym:out/a\\.jpg\\.export\\.000001\\.jpg=in/b\\.jpg " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link sym:out/a.jpg.export.000001.jpg=in/b.jpg --replace")
batch_case("BP_alias_export_hardlink", "in=in,out=out,all,ok",
           "harness: batch link hard:out/a\\.jpg\\.export\\.000001\\.jpg=in/b\\.jpg " + BP_LINK_REPLACED,
           names=("bp_alias",), extra="--batch-link hard:out/a.jpg.export.000001.jpg=in/b.jpg --replace")
batch_case("BP_alias_export_dangling_asked", "in=in,out=out,all,ok",
           'harness: message box: A file the batch writes already exists: "a\\.jpg\\.export\\.000001\\.jpg"\\.',
           names=("bp_alias",), extra="--batch-link sym:out/a.jpg.export.000001.jpg=nowhere")
# Not the file analyzed, though: an image x.jpg that is a link to x.jpg.txt
# (its log's name) is kept, even if replacing was chosen. The log replaced
# it, and the link led to the log.
BP_SELF = "--batch-link sym:in/x.jpg=in/x.jpg.txt --replace"
batch_case("BP_self_link_kept", BP_ALIAS, "harness: batch link sym:in/x\\.jpg=in/x\\.jpg\\.txt now exists=1 symlink=1 names=1 "
           "target exists=1 unchanged=1$", names=("bp_self",), extra=BP_SELF)
batch_case("BP_self_link_error", BP_ALIAS, "^    ERROR: Couldn't write the log \\[<batch>/in/x\\.jpg\\.txt\\]: it would replace "
           "the file analyzed$", names=("bp_self",), extra=BP_SELF)

# Folders that can't be read: listed, with the files in the others processed
# (master took them for empty ones)
BP_SUB = "in=in,out=out,subfolders,ok"
batch_case("BP_folder_unreadable", BP_SUB, "^  ERROR: Couldn't read the folder \\[<batch>/in/sub\\]: Permission denied$",
           extra="--batch-unreadable sub")
batch_case("BP_folder_unreadable_others", BP_SUB,
           f"harness: batch 1 files: {BP_FILES},out/photos.jpg/e.jpg.txt$", extra="--batch-unreadable sub")
batch_case("BP_folder_unreadable_count", BP_SUB, "^  Folders not read:  1$", extra="--batch-unreadable sub")
batch_case("BP_folder_unreadable_message", BP_SUB,
           "harness: message box: Batch processing complete\\. 5 of 5 files processed: 5 logs written\\. 1 folder "
           "couldn't be read; the report lists it\\.$", extra="--batch-unreadable sub")
batch_case("BP_folder_and_file_message", BP_SUB,
           "1 file had errors and 1 folder couldn't be read; the report lists them\\.$",
           extra="--batch-unreadable sub --batch-unreadable a.jpg")
batch_case("BP_folder_deeper", BP_SUB,
           "^  ERROR: Couldn't read the folder \\[<batch>/in/sub/deeper\\]: Permission denied$",
           extra="--batch-unreadable sub/deeper")
batch_case("BP_folder_not_searched", BP_RUN, f"harness: batch 1 files: {BP_FILES}$", forbid="Couldn't read",
           extra="--batch-unreadable sub")
batch_case("BP_folder_hidden", BP_SUB, f"harness: batch 1 files: {BP_SUB_FILES}$", forbid="Couldn't read",
           extra="--batch-unreadable .hiddendir")
# Read but not searched: its files are listed but can't be opened
batch_case("BP_folder_unsearchable", BP_SUB,
           "^  ERROR: Couldn't read the folder \\[<batch>/in/sub/deeper\\]: Permission denied$",
           extra="--batch-mode 444:sub")
batch_case("BP_folder_unsearchable_file", BP_SUB, "^    ERROR: Couldn't open file: Permission denied$",
           extra="--batch-mode 444:sub")
batch_case("BP_folder_no_files", BP_SUB,
           "harness: message box: There are no \\.jpg or \\.jpeg files in the folders that could be read\\. "
           "Couldn't read \".*/batch/in/sub\": Permission denied\\.$", names=("bp_none",),
           extra="--batch-unreadable sub")
# The input folder: refused in the dialog
batch_case("BP_input_unreadable", "in=in,out=out,ok,cancel",
           "harness: message box: Couldn't read the input folder \".*/batch/in\": Permission denied\\.$",
           extra="--batch-unreadable .")

# The dialog's fields: refused with a message, and the dialog stays open
for cid, steps, message in (
        ("BP_input_missing", "in=nope", 'The input folder doesn\'t exist: ".*/batch/nope"\\.$'),
        ("BP_input_file", "in=in/notes.txt", 'The input folder is a file: ".*/batch/in/notes\\.txt"\\.$'),
        ("BP_input_empty", "in=-", "Choose the folder of images to process\\.$"),
        ("BP_input_relative", "in=raw:in", "The input folder must be a full path, such as ~/Pictures\\.$"),
        ("BP_input_home", "in=raw:~/no-such-jpegsnoop-folder",
         'The input folder doesn\'t exist: "/[^~"]*/no-such-jpegsnoop-folder"\\.$'),
        ("BP_output_empty", "in=in,out=-", "Choose a folder for the logs\\.$"),
        ("BP_output_relative", "in=in,out=raw:logs",
         "The output folder must be a full path, such as ~/Pictures\\.$"),
        ("BP_output_file", "in=in,out=in/notes.txt", 'The output folder is a file: ".*/batch/in/notes\\.txt"\\.$')):
    batch_case(cid, steps + ",ok,cancel", "harness: message box: " + message)
batch_case("BP_refused_then_ok", "in=nope,ok,in=in,out=out,ok", f"harness: batch 1 files: {BP_FILES}$")
# Choose...: as on Windows, the input folder chosen is also the output folder
batch_case("BP_choose_title", "browse-in=in/sub,cancel",
           "harness: batch chooser: Select input image folder \\(start <home>\\)$")
batch_case("BP_choose_input", "browse-in=in/sub,fields,cancel",
           "harness: batch dialog: Batch Processing \\(in=<batch>/in/sub out=<batch>/in/sub subfolders=0 all=0\\)$")
batch_case("BP_choose_output", "in=in,out=out,browse-out=in/sub/deeper,fields,cancel",
           "harness: batch dialog: Batch Processing \\(in=<batch>/in out=<batch>/in/sub/deeper subfolders=0 all=0\\)$")
batch_case("BP_choose_cancel", "in=in,out=out,browse-in,fields,cancel",
           "harness: batch dialog: Batch Processing \\(in=<batch>/in out=<batch>/out subfolders=0 all=0\\)$")
# The folders are remembered, and saved with the settings
batch_case("BP_remembered", BP_RUN + "+fields,cancel",
           "harness: batch dialog: Batch Processing \\(in=<batch>/in out=<batch>/out subfolders=0 all=0\\)$")
batch_case("BP_saved", BP_RUN, "harness: batch saved in=<batch>/in out=<batch>/out$")

# The progress, which can stop the batch (master's couldn't)
batch_case("BP_progress", BP_RUN, 'harness: batch progress shown=1 modal=app title="Batch Processing"$')
batch_case("BP_progress_label", BP_RUN, 'harness: batch progress 0/4 "Batch processing file 1 of 4: / a\\.jpg"$')
batch_case("BP_cancel", BP_RUN, "harness: batch 1 files: out/B.JPEG.txt,out/a.jpg.txt$",
           extra="--batch-cancel-after 2")
batch_case("BP_cancel_report", BP_RUN, "^  Batch processing stopped after 2 of 4 files$", extra="--batch-cancel-after 2")
batch_case("BP_cancel_message", BP_RUN,
           "harness: message box: Batch processing stopped\\. 2 of 4 files processed: 2 logs written\\.$",
           extra="--batch-cancel-after 2")
batch_case("BP_close_window", BP_RUN, "harness: batch window closed=0$", extra="--batch-close-after 1")
batch_case("BP_close_stops", BP_RUN, "^  Batch processing stopped after 1 of 4 files$", extra="--batch-close-after 1")
batch_case("BP_messages_after", BP_RUN, "harness: message box: ERROR: Couldn't open file: \\[.*/no-such-file\\.jpg\\]$",
           extra="--batch-open-missing")

# The file shown is left as it was: its own decoders, start offset and File
# Overlay (which isn't applied to the batch's files)
BP_OPEN = ("bp_tree", "ok_444.jpg")
batch_case("BP_file_kept", BP_RUN, "harness: batch after reprocess file=ok_444\\.jpg scan=1$", names=BP_OPEN)
batch_case("BP_file_note", BP_RUN, "^  File > Reprocess shows the report of \\[.*/ok_444\\.jpg\\] again$", names=BP_OPEN)
batch_case("BP_no_file_no_note", BP_RUN, "harness: batch 1 wrote new=4 ", forbid="File > Reprocess shows")
batch_case("BP_overlay_not_used", BP_RUN, "^harness: cat:   version    = \\[1\\.1\\]$", names=BP_OPEN,
           extra="--overlay on,offset=0B,value=0102,ok --batch-cat out/a.jpg.txt")
batch_case("BP_overlay_kept", BP_RUN, "harness: batch after reprocess overlay: " + OVL_VERSION, names=BP_OPEN,
           extra="--overlay on,offset=0B,value=0102,ok")

# --- FO: File > Offset ----------------------------------------------------------------
# It did nothing. As on Windows, its dialog takes the offset to decode the file
# from, in hex or decimal, and OK decodes the file again from there. gui
# --offset answers the dialog and prints its fields, then a summary of the
# report and preview: the start offset, whether an SOI was found there and the
# scan decoded, the signature section's offset, the assessment, and the
# preview's size and MD5, with ref=same if it is the image a separate decode
# from that offset gives. e_two.jpg is 100 junk bytes, a 4:4:4 JPEG at 0x64, 100
# more, a 4:2:0 JPEG at 0x62C (1067 bytes) and 100 more: 2747 bytes (0xABB).
def offset_case(cid, steps, expect, names=("e_two.jpg",), extra="", forbid="-"):
    case(cid, "FO", "gui", list(names), ("--offset " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


FO_SECOND = "start=0x0000062C soi=1 scan=1 sig=1580 assessment=skipped preview=64x48 md5=[0-9a-f]{8} ref=same$"
FO_START = "start=0x00000000 soi=0 scan=0 sig=- assessment=- preview=none md5=- ref=same$"
FO_DIALOG = "harness: offset dialog: Starting File Offset \\("

# The menu item, where Windows has it; off until a file is open
offset_case("FO_menu", "cancel",
            'harness: offset menu=&File after="Batch Process\\.\\.\\." separator=1 enabled=1 '
            'tip="Start processing at an offset"$')
offset_case("FO_off_no_file", "cancel", "harness: offset menu=&File .* enabled=0 ", extra="--offset-at 0")
offset_case("FO_off_failed_open", "cancel", "harness: offset menu=&File .* enabled=0 ", names=("f4_no_such_file.jpg",))
# The menu stays on after a file that couldn't be opened, or was empty
for cid, name in (("FO_not_analyzed", "f4_no_such_file.jpg"), ("FO_not_analyzed_empty", "f4_empty.jpg")):
    offset_case(cid, "cancel",
                "harness: message box: The file shown couldn't be analyzed, so it can't be decoded from an offset\\.$",
                names=("e_two.jpg", name), extra="--offset-at 2")

# The dialog: as on Windows it opens in hex with the offset in use, which is
# selected so typing replaces it
offset_case("FO_dialog_opens", "cancel", FO_DIALOG + 'base=hex value="0x00000000" focus=value selected=all\\)$')
offset_case("FO_dialog_shows_offset", "value=62C,ok+cancel", FO_DIALOG + 'base=hex value="0x0000062C" ')
offset_case("FO_typing_replaces", "type=62C,ok", "harness: offset now: " + FO_SECOND)

# OK decodes the file from the offset, in hex (with or without 0x) or decimal
offset_case("FO_hex", "value=62C,ok", "harness: offset now: " + FO_SECOND)
offset_case("FO_hex_prefix", "value=0x62c,ok", "harness: offset now: " + FO_SECOND)
offset_case("FO_decimal", "dec,value=1580,ok", "harness: offset now: " + FO_SECOND)
offset_case("FO_spaces", "value=%20%2062C%20,ok", "harness: offset now: " + FO_SECOND)
offset_case("FO_return", "value=62C,return", "harness: offset now: " + FO_SECOND)
offset_case("FO_cancel", "value=62C,cancel", "harness: offset now: " + FO_START)
offset_case("FO_escape", "value=62C,escape", "harness: offset now: " + FO_START)
# Master decoded the file again without first marking the image source
# changed (only after), so once a scan had been decoded, as when the file
# starts with a JPEG, the scan at the new offset wasn't: the report had no scan
# decode and the preview kept the old image. fo_start_two.jpg is a 4:4:4 JPEG
# (1380 bytes), 100 junk bytes, then a 4:2:0 JPEG at 0x5C8.
write("fo_start_two.jpg", c444 + junk + c420)
offset_case("FO_after_scan_decoded", "value=5C8,ok",
            "harness: offset now: start=0x000005C8 soi=1 scan=1 sig=1480 assessment=skipped preview=64x48 "
            "md5=[0-9a-f]{8} ref=same$", names=("fo_start_two.jpg",))
offset_case("FO_new_offset_decoded", "value=64,ok+value=62C,ok", "harness: offset now: " + FO_SECOND)
offset_case("FO_first_offset_decoded", "value=64,ok",
            "harness: offset now: start=0x00000064 soi=1 scan=1 sig=100 assessment=skipped preview=64x48 "
            "md5=[0-9a-f]{8} ref=same$")
offset_case("FO_back_to_start", "value=64,ok+value=0,ok", "harness: offset now: " + FO_START)
# The last byte is the last offset
offset_case("FO_last_byte", "value=ABA,ok",
            "harness: offset now: start=0x00000ABA soi=0 scan=0 sig=- assessment=- preview=none md5=- ref=same$")

# Hex and Decimal change the offset typed to that base, as on Windows
offset_case("FO_to_decimal", "value=62C,dec,cancel", 'harness: offset clicked dec \\(base=dec value="1580" ')
offset_case("FO_to_hex", "dec,value=1580,hex,cancel", 'harness: offset clicked hex \\(base=hex value="0x0000062C" ')
# A number in the base clicked but not the one it was typed in is kept
offset_case("FO_typed_in_other_base", "dec,value=0x62C,hex,ok", "harness: offset now: " + FO_SECOND)
# Windows said "Invalid hex string" and changed the offset to 0; the choice
# now goes back, keeping the text
offset_case("FO_switch_refused", "value=zz,dec,cancel",
            'harness: message box: "zz" isn\'t a hex or decimal number\\.$')
offset_case("FO_switch_refused_keeps", "value=zz,dec,cancel", 'harness: offset clicked dec \\(base=hex value="zz" ')
offset_case("FO_switch_too_large", "value=FFFFFFFFFFFFFFFFFFFF,dec,cancel",
            'harness: message box: "FFFFFFFFFFFFFFFFFFFF" is too large for an offset\\.$')

# OK refuses an offset it can't use, saying why, and the dialog stays open.
# Windows took any 32-bit number, and decoded past the end of the file as if
# 0x00 bytes were there.
for cid, steps, message in (
        ("FO_empty", "value=,ok", "Type the offset to decode the file from\\."),
        ("FO_bad_hex", "value=0x1G,ok", '"0x1G" isn\'t a hex number\\. Type digits 0-9 and A-F, with or without 0x\\.'),
        ("FO_bad_decimal", "dec,value=1.5,ok", '"1\\.5" isn\'t a decimal number\\. Type digits 0-9 only\\.'),
        ("FO_past_end", "value=ABB,ok", "The offset must be less than the file's size, 2747 bytes \\(0x00000ABB\\)\\."),
        ("FO_past_32_bits", "value=100000000,ok",
         "The offset must be less than the file's size, 2747 bytes \\(0x00000ABB\\)\\.")):
    offset_case(cid, steps, "harness: message box: " + message + "$")
offset_case("FO_refused_stays_open", "value=ABB,ok,value=62C,ok", "harness: offset now: " + FO_SECOND)

# The offset lasts until another file is opened, as on Windows: Reprocess and
# Export JPEG use it
offset_case("FO_reprocess_keeps", "value=62C,ok", "harness: offset after reprocess: " + FO_SECOND)
offset_case("FO_next_file_resets", "value=62C,ok", "harness: offset file 2: start=0x00000000 soi=0 ",
            names=("e_two.jpg", "e_two.jpg"))
offset_case("FO_export_dialog", "value=62C,ok",
            "harness: dialog: Export JPEG Settings \\(Source file start offset: 0x0000062c, ", extra="--export-menus")
offset_case("FO_export_second", "value=62C,ok", "harness: menu Export JPEG\\.\\.\\. wrote=1 size=1067$",
            extra="--export-menus")
# A File Overlay stays in place (here on the second JPEG's APP0 version)
offset_case("FO_overlay_kept", "value=62C,ok",
            "harness: overlay with offset: version=\\[1\\.2\\] overlays=1 len=2 offset=0x00000637 "
            "header=2@0x00000637$", extra="--overlay on,offset=637,value=0102,ok")
# A batch decodes its files from their start, and leaves the offset in place
batch_case("BP_offset_not_used", BP_RUN, "^harness: cat: Start Offset: 0x00000000$", names=("bp_tree", "e_two.jpg"),
           extra="--offset value=62C,ok --batch-cat out/a.jpg.txt")
batch_case("BP_offset_kept", BP_RUN, "harness: batch after reprocess offset: " + FO_SECOND,
           names=("bp_tree", "e_two.jpg"), extra="--offset value=62C,ok")

# --- IS: Tools > Image Search Fwd and Rev ------------------------------------------------
# They did nothing. As on Windows, Fwd decodes the file from the next SOI marker
# (FF D8 FF) after the start offset and Rev from the one before it, or from the
# start of the file if there is none. gui --search chooses them (fwd, rev),
# presses their shortcuts (ctrl2, ctrl1) or moves the file away for the next
# step (gone), printing the summary of --offset after each.
def search_case(cid, steps, expect, names=("e_two.jpg",), extra="", forbid="-"):
    case(cid, "IS", "gui", list(names), ("--search " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


IS_FIRST = "start=0x00000064 soi=1 scan=1 sig=100 assessment=skipped preview=64x48 md5=[0-9a-f]{8} ref=same$"
IS_NONE_AFTER = "harness: message box: No JPEG found after offset 0x{} \\(no SOI marker, FF D8 FF\\)\\.$"
IS_NONE_BEFORE = ("harness: message box: No JPEG found before offset 0x{} \\(no SOI marker, FF D8 FF\\), so the file "
                  "is decoded from its start\\.$")

# The menu items, where Windows has them; off until a file is open
search_case("IS_menu_fwd", "fwd",
            'harness: search Image Search Fwd menu=&Tools after="-" separator=0 enabled=1 tip="Search for next Image" '
            "shortcut=Ctrl\\+2$")
search_case("IS_menu_rev", "fwd",
            'harness: search Image Search Rev menu=&Tools after="Image Search Fwd" separator=0 enabled=1 '
            'tip="Search for previous Image" shortcut=Ctrl\\+1$')
search_case("IS_off_no_file", "fwd,rev", "harness: search rev: unavailable$", extra="--search-at 0")
search_case("IS_off_failed_open", "fwd", "harness: search fwd: unavailable$", names=("f4_no_such_file.jpg",))
# They stay on after a file that couldn't be opened, or was empty
for cid, name in (("IS_not_analyzed", "f4_no_such_file.jpg"), ("IS_not_analyzed_empty", "f4_empty.jpg")):
    search_case(cid, "fwd",
                "harness: message box: The file shown couldn't be analyzed, so it can't be searched\\.$",
                names=("e_two.jpg", name), extra="--search-at 2")

# Fwd: each JPEG in turn, then a message, and the offset stays
search_case("IS_fwd_first", "fwd", "harness: search fwd: " + IS_FIRST)
search_case("IS_fwd_second", "fwd,fwd", "harness: search fwd: " + FO_SECOND)
search_case("IS_fwd_none", "fwd,fwd,fwd", IS_NONE_AFTER.format("0000062C"))
search_case("IS_fwd_none_stays", "fwd,fwd,fwd", "harness: search after reprocess: " + FO_SECOND)
# From the offset File > Offset set (here inside the first JPEG)
search_case("IS_fwd_from_offset", "fwd", "harness: search fwd: " + FO_SECOND, extra="--offset value=100,ok")
# With the scan at the start decoded: Windows marked the image source changed
# before decoding again, and so does this (see FO_after_scan_decoded)
search_case("IS_fwd_after_scan_decoded", "fwd",
            "harness: search fwd: start=0x000005C8 soi=1 scan=1 sig=1480 assessment=skipped preview=64x48 "
            "md5=[0-9a-f]{8} ref=same$", names=("fo_start_two.jpg",))

# Rev: the JPEG before, or with none, the start of the file (as on Windows),
# where there is nothing before to search (Windows did nothing)
search_case("IS_rev_previous", "fwd,fwd,rev", "harness: search rev: " + IS_FIRST)
search_case("IS_rev_none_to_start", "rev", "harness: search rev: " + FO_START, extra="--offset value=64,ok")
search_case("IS_rev_none_message", "fwd,rev", IS_NONE_BEFORE.format("00000064"))
search_case("IS_rev_at_start", "rev",
            "harness: message box: The file is decoded from its start, so there is nothing before it to search\\.$")
search_case("IS_rev_finds_start", "rev",
            "harness: search rev: start=0x00000000 soi=1 scan=1 sig=0 assessment=done preview=64x48 "
            "md5=[0-9a-f]{8} ref=same$", names=("fo_start_two.jpg",), extra="--offset value=5C8,ok")
search_case("IS_rev_from_offset", "rev", "harness: search rev: " + FO_SECOND, extra="--offset value=700,ok")

# The shortcuts, Ctrl+2 and Ctrl+1 (Cmd on macOS), as on Windows
search_case("IS_ctrl2", "ctrl2,ctrl2", "harness: search ctrl2: " + FO_SECOND)
search_case("IS_ctrl1", "ctrl2,ctrl2,ctrl1", "harness: search ctrl1: " + IS_FIRST)

# The offset found lasts as File > Offset's does: Reprocess and Export JPEG
# use it, and opening a file starts from 0 again
search_case("IS_reprocess_keeps", "fwd,fwd", "harness: search after reprocess: " + FO_SECOND)
search_case("IS_next_file_resets", "fwd", "harness: search file 2: start=0x00000000 soi=0 ",
            names=("e_two.jpg", "e_two.jpg"))
search_case("IS_export_found", "fwd,fwd", "harness: menu Export JPEG\\.\\.\\. wrote=1 size=1067$",
            extra="--export-menus")

# The search sees the File Overlay, as on Windows and as the decoder does: one
# can hide an SOI or make one
search_case("IS_overlay_hides", "fwd,fwd", IS_NONE_AFTER.format("00000064"),
            extra="--overlay on,offset=62C,value=00,ok")
search_case("IS_overlay_makes", "fwd", "harness: search fwd: start=0x00000030 soi=1 ",
            extra="--overlay on,offset=30,value=FFD8FF,ok")
search_case("IS_overlay_makes_rev", "rev", "harness: search rev: start=0x00000030 soi=1 ",
            extra="--overlay on,offset=30,value=FFD8FF,ok --offset value=64,ok")

# A file moved away since it was opened: the report says so, as after
# Reprocess, and the preview goes
search_case("IS_file_gone", "gone,fwd", "harness: message box: ERROR: Couldn't open file: \\[.*e_two\\.jpg\\]$")
search_case("IS_file_gone_preview", "fwd,gone,fwd", "harness: search fwd: start=- soi=0 scan=0 .* preview=none ")

# The search reads the file a block (64 KiB) at a time. is_far.jpg has a JPEG
# at 0x10000, the last position of the first block searched from 0 (its SOI
# runs on into the next), and another at 0x20564.
write("is_far.jpg", junk * 655 + junk[:36] + c444 + junk * 655 + junk[:36] + c420)
search_case("IS_far_fwd", "fwd,fwd", "harness: search fwd: start=0x00020564 soi=1 scan=1 ", names=("is_far.jpg",))
search_case("IS_far_first", "fwd", "harness: search fwd: start=0x00010000 soi=1 scan=1 ", names=("is_far.jpg",))
search_case("IS_far_rev", "fwd,fwd,rev", "harness: search rev: start=0x00010000 soi=1 scan=1 ", names=("is_far.jpg",))


# --- LM: Tools > Lookup MCU Offset ---------------------------------------------
# gui --lookup chooses the menu item and answers its dialog: x=TEXT and y=TEXT
# (typed over the field), recalc, return, escape, close and fields, with "+"
# to choose it again. --hover moves the mouse over the preview and prints the
# image window's status bar. Files come from dc_only_jpeg(), which says where
# each MCU starts, so the offsets shown can be checked against it.
def lookup_case(cid, steps, expect, names=("lm_444.jpg",), extra="", forbid="-"):
    case(cid, "LM", "gui", list(names), ("--lookup " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def lm_level(c, x, y):
    """A different level in each block, so MCUs differ in length"""
    return (40 + 37 * (x // 8) + 23 * (y // 8) + 50 * c) % 250


def lm_file(name, *args, base=0, **kw):
    """Write a dc_only_jpeg() and return each MCU's position in the file as
    (offset, bit), for the JPEG put at offset base"""
    data, scan_off, mcu_pos = dc_only_jpeg(*args, lm_level, **kw)
    write(name, data)
    return data, [(base + scan_off + byte, bit) for byte, bit in mcu_pos]


def lm_want(x, y, mcu, pos, rng=r"\(0\.\.39\)x\(0\.\.23\)", step="recalc"):
    """The dialog's fields after a calculation: pos is (offset, bit), or None
    for an MCU that wasn't decoded"""
    off = "Not decoded" if pos is None else f"0x{pos[0]:08X} : {pos[1]}"
    return (f'harness: lookup {step} \\(range={rng} x="{x}" y="{y}" mcu="\\[{mcu[0]},{mcu[1]}\\]" '
            f'offset="{off}" focus=')


LM_Y3 = [(1, 1, 1), (2, 1, 1), (3, 1, 1)]
# 36x20, so 5x3 MCUs of 8x8 and a decoded image of 40x24 (padded to whole MCUs)
lm444, LM444 = lm_file("lm_444.jpg", 36, 20, LM_Y3)
# 4:2:0, MCUs of 16x16: 4x3 of them, 64x48
_, LM420 = lm_file("lm_420.jpg", 50, 34, [(1, 2, 2), (2, 1, 1), (3, 1, 1)])
# Y 2x1, MCUs of 16x8 (wider than high): 3x3 of them, 48x24
_, LMH2 = lm_file("lm_h2.jpg", 40, 24, [(1, 2, 1), (2, 1, 1), (3, 1, 1)])
# A restart interval of 2 MCUs: RST0 before MCU 2, RST1 before MCU 4, ...
_, LMRST = lm_file("lm_rst.jpg", 40, 24, LM_Y3, restart=2)
# And with two 0xFF fill bytes before each RST marker
_, LMRSTFILL = lm_file("lm_rst_fill.jpg", 40, 24, LM_Y3, restart=2, fill=2)
# Cut short in MCU [2,1] (MCU 7), without an EOI and with one after the cut
lm_cut = LM444[7][0] + 1
write("lm_trunc.jpg", lm444[:lm_cut])
write("lm_trunc_eoi.jpg", lm444[:lm_cut] + b"\xff\xd9")
# And with fill bytes before that EOI (T.81 B.1.1.2)
write("lm_trunc_fill.jpg", lm444[:lm_cut] + b"\xff\xff\xd9")
# Cut short at the end of a file of 0xFFFFFFFF bytes (the largest opened),
# which starts with other data. Sparse, as lm_big.bin is.
LM_HUGE = 0xFFFFFFFF - lm_cut
with open(files_dir / "lm_huge.bin", "wb") as f:
    f.write(bytes(random.Random(5).randrange(256) for _ in range(4096)))
    f.seek(LM_HUGE)
    f.write(lm444[:lm_cut])
# After another JPEG, for File > Offset: offsets are the file's, not the JPEG's
write("lm_two.jpg", c444 + lm444)
LM_TWO = [(len(c444) + off, bit) for off, bit in LM444]
# More than 256 MB in, where the decoder's 32-bit map of MCU positions kept
# only 28 bits of the offset. A sparse file: little is written to disk.
LM_BIG = 0x10000100
with open(files_dir / "lm_big.bin", "wb") as f:
    f.seek(LM_BIG)
    f.write(lm444)
LM_BIG_POS = [(LM_BIG + off, bit) for off, bit in LM444]
LM_ALL = ",".join(f"x={mx * 8 + 3},y={my * 8 + 5},recalc" for my in range(3) for mx in range(5)) + ",close"
LM_ALL_H2 = ",".join(f"x={mx * 16 + 9},y={my * 8 + 2},recalc" for my in range(3) for mx in range(3)) + ",close"
NOT_CALC = 'mcu="Not Calculated" offset="Not Calculated"'

# The menu item, where Windows has it; off until a file is open
lookup_case("LM_menu", "close", 'harness: lookup menu=&Tools after="Image Search Rev" separator=1 enabled=1 '
            'tip="Calculate file offset for an MCU"$')
lookup_case("LM_off_no_file", "close", "harness: menu Lookup MCU Offset unavailable$", extra="--lookup-at 0")
# It stays on after a file that couldn't be opened, or was empty
for cid, name in (("LM_not_analyzed", "f4_no_such_file.jpg"), ("LM_not_analyzed_empty", "f4_empty.jpg")):
    lookup_case(cid, "close", "harness: message box: The file shown couldn't be analyzed, so it has no MCUs to look "
                "up\\.$", names=("lm_444.jpg", name), extra="--lookup-at 2")
# With no scan decoded, Windows offered the dialog with a range of 0 to
# 4294967295 and read a null map
LM_NO_SCAN = ("harness: message box: No JPEG image was decoded in the file shown, so there are no MCUs to look up\\. "
              "The report says why\\.$")
lookup_case("LM_no_decode_image", "close", "harness: message box: No image was decoded, so there are no MCUs to look "
            "up: Options > Scan Segment > Decode Image is off\\.$", extra="--no-decode-image")
lookup_case("LM_progressive", "close", LM_NO_SCAN, names=("ok_progressive.jpg",))
lookup_case("LM_psd", "close", LM_NO_SCAN, names=("psd_rgb_rle.psd",))
# Nor after a file that was decoded
lookup_case("LM_progressive_after_decoded", "close", LM_NO_SCAN, names=("lm_444.jpg", "ok_progressive.jpg"),
            extra="--lookup-at 2", forbid="harness: lookup dialog:")

# The dialog: Windows' fields, the range being the decoded image's (padded
# to whole MCUs, as the preview is), with the MCU added
lookup_case("LM_dialog", "close", 'harness: lookup dialog: Lookup File Offset of MCU \\(range=\\(0\\.\\.39\\)x'
            '\\(0\\.\\.23\\) x="0" y="0" ' + NOT_CALC + ' focus=x\\(all selected\\)\\)$')
lookup_case("LM_range_420", "close", 'harness: lookup dialog: .*range=\\(0\\.\\.63\\)x\\(0\\.\\.47\\) ',
            names=("lm_420.jpg",))

# Each MCU at the position dc_only_jpeg() wrote it, from a pixel inside it
for cid, ind in (("LM_first", 0), ("LM_bit", 1), ("LM_row_end", 4), ("LM_row_next", 5), ("LM_middle", 7),
                 ("LM_last", 14)):
    mx, my = ind % 5, ind // 5
    lookup_case(cid, LM_ALL, lm_want(mx * 8 + 3, my * 8 + 5, (mx, my), LM444[ind]))
# A pixel in the padding is in the last MCU
lookup_case("LM_padding", "x=39,y=23,recalc,close", lm_want(39, 23, (4, 2), LM444[14]))
lookup_case("LM_mcu_edges", "x=7,y=8,recalc,close", lm_want(7, 8, (0, 1), LM444[5]))
# MCUs of 16x16, and of 16x8 (so X and Y can't be mixed up)
lookup_case("LM_420", "x=17,y=15,recalc,close", lm_want(17, 15, (1, 0), LM420[1], r"\(0\.\.63\)x\(0\.\.47\)"),
            names=("lm_420.jpg",))
lookup_case("LM_420_last", "x=63,y=47,recalc,close", lm_want(63, 47, (3, 2), LM420[11], r"\(0\.\.63\)x\(0\.\.47\)"),
            names=("lm_420.jpg",))
for cid, ind in (("LM_h2_across", 1), ("LM_h2_down", 3), ("LM_h2_last", 8)):
    mx, my = ind % 3, ind // 3
    lookup_case(cid, LM_ALL_H2, lm_want(mx * 16 + 9, my * 8 + 2, (mx, my), LMH2[ind], r"\(0\.\.47\)x\(0\.\.23\)"),
                names=("lm_h2.jpg",))

# After a restart marker, the MCU starts after the marker. (On Windows too,
# the position recorded was before it, where the previous MCU's bits ended.)
for cid, ind in (("LM_rst_first", 2), ("LM_rst_second", 4), ("LM_rst_row", 10), ("LM_rst_between", 3)):
    mx, my = ind % 5, ind // 5
    lookup_case(cid, LM_ALL, lm_want(mx * 8 + 3, my * 8 + 5, (mx, my), LMRST[ind]), names=("lm_rst.jpg",))
# Also with Detailed Decode, which decodes the MCUs it reports separately
lookup_case("LM_rst_detail", LM_ALL, lm_want(19, 5, (2, 0), LMRST[2]), names=("lm_rst.jpg", "lm_rst.jpg"),
            extra="--lookup-at 2 --detail-dlg on,x=0,y=0,len=15,ok --detail-dlg-at 1")

# A scan cut short: the MCU it ends in starts where it did, and the ones after
# aren't decoded. (They said 0x00000000 : 0, as the hover did, and the first
# of each later row the decoder's position after the end: past the end of the
# file, or at the EOI marker.)
lookup_case("LM_trunc_last", LM_ALL, lm_want(19, 13, (2, 1), LM444[7]), names=("lm_trunc.jpg",))
lookup_case("LM_trunc_after", LM_ALL, lm_want(27, 13, (3, 1), None), names=("lm_trunc.jpg",))
lookup_case("LM_trunc_next_row", LM_ALL, lm_want(3, 21, (0, 2), None), names=("lm_trunc.jpg",))
lookup_case("LM_trunc_eoi_marker", LM_ALL, lm_want(27, 13, (3, 1), None), names=("lm_trunc_eoi.jpg",))
lookup_case("LM_trunc_eoi_next_row", LM_ALL, lm_want(3, 21, (0, 2), None), names=("lm_trunc_eoi.jpg",))
# Fill bytes before the EOI aren't scan data either: the decoder reads them
# as data, and the MCUs after the cut started in them
lookup_case("LM_trunc_fill_last", LM_ALL, lm_want(19, 13, (2, 1), LM444[7]), names=("lm_trunc_fill.jpg",))
lookup_case("LM_trunc_fill_after", LM_ALL, lm_want(27, 13, (3, 1), None), names=("lm_trunc_fill.jpg",))
lookup_case("LM_trunc_fill_next_row", LM_ALL, lm_want(3, 21, (0, 2), None), names=("lm_trunc_fill.jpg",))
# Fill bytes before RST markers, then an EOI without them: the end of the scan
# data is still the EOI
lookup_case("LM_rst_fill_after", LM_ALL, lm_want(19, 5, (2, 0), LMRSTFILL[2]), names=("lm_rst_fill.jpg",))
lookup_case("LM_rst_fill_last", LM_ALL, lm_want(35, 21, (4, 2), LMRSTFILL[14]), names=("lm_rst_fill.jpg",))
# At the end of a file of 0xFFFFFFFF bytes, the scan's position after the end
# wrapped around to 0: the decoder read the next row from the start of the
# file, and its first MCU showed 0x00000001 : 4
LM_HUGE_STEPS = f"--offset value={LM_HUGE:X},ok"
lookup_case("LM_huge_last", LM_ALL, lm_want(19, 13, (2, 1), (LM_HUGE + LM444[7][0], LM444[7][1])),
            names=("lm_huge.bin",), extra=LM_HUGE_STEPS)
lookup_case("LM_huge_next_row", LM_ALL, lm_want(3, 21, (0, 2), None), names=("lm_huge.bin",), extra=LM_HUGE_STEPS)
lookup_case("LM_huge_hover", "close", 'harness: hover 3,21: "MCU \\[0000,0002\\]" "File: not decoded" ',
            names=("lm_huge.bin",), extra=LM_HUGE_STEPS + " --hover 3,21")
# The image window's status bar says so too, and agrees with the dialog
lookup_case("LM_hover_not_decoded", "close", 'harness: hover 27,13: "MCU \\[0003,0001\\]" "File: not decoded" ',
            names=("lm_trunc.jpg",), extra="--hover 3,5/27,13")
lookup_case("LM_hover_same", "close", f'harness: hover 13,10: "MCU \\[0001,0001\\]" "File 0x{LM444[6][0]:08x}:'
            f'{LM444[6][1]}" ', extra="--hover 13,10")

# Offsets in the file, with the JPEG decoded from an offset (File > Offset)
lookup_case("LM_offset", "x=13,y=10,recalc,close", lm_want(13, 10, (1, 1), LM_TWO[6]), names=("lm_two.jpg",),
            extra=f"--offset value={len(c444):X},ok")
# More than 256 MB into the file (the offsets wrapped around)
lookup_case("LM_big", "x=13,y=10,recalc,close", lm_want(13, 10, (1, 1), LM_BIG_POS[6]), names=("lm_big.bin",),
            extra=f"--offset value={LM_BIG:X},ok")
lookup_case("LM_big_hover", "close", f'harness: hover 39,23: "MCU \\[0004,0002\\]" "File 0x{LM_BIG_POS[14][0]:08x}:'
            f'{LM_BIG_POS[14][1]}" ', names=("lm_big.bin",), extra=f"--offset value={LM_BIG:X},ok --hover 39,23")
# The file opened last, not the one before
lookup_case("LM_second_file", "x=17,y=15,recalc,close",
            lm_want(17, 15, (1, 0), LM420[1], r"\(0\.\.63\)x\(0\.\.47\)"), names=("lm_444.jpg", "lm_420.jpg"),
            extra="--lookup-at 2")

# What can be typed: a whole number in the range, spaces around it allowed.
# (Windows said "Pixel X coord out of range", and for text that wasn't a
# number, calculated from the last number.)
LM_X_RANGE = "harness: message box: X must be a whole number from 0 to 39\\.$"
for cid, text in (("LM_x_too_large", "40"), ("LM_x_not_number", "abc"), ("LM_x_empty", ""), ("LM_x_negative", "-1"),
                  ("LM_x_huge", "99999999999999999999"), ("LM_x_hex", "0x10")):
    lookup_case(cid, f"x={text},recalc,close", LM_X_RANGE)
lookup_case("LM_y_too_large", "y=24,recalc,close", "harness: message box: Y must be a whole number from 0 to 23\\.$")
lookup_case("LM_refused_no_result", "x=40,recalc,close", 'harness: lookup recalc \\(.* x="40" y="0" ' + NOT_CALC)
lookup_case("LM_spaces", "x=%2013%20,y=10,recalc,close", lm_want("%2013%20".replace("%20", " "), 10, (1, 1), LM444[6]))
lookup_case("LM_largest", "x=39,y=23,recalc,close", lm_want(39, 23, (4, 2), LM444[14]), forbid="message box")

# Return calculates (on Windows it closed the dialog, as OK); Escape and
# Close close it
lookup_case("LM_return", "x=13,y=10,return,close", lm_want(13, 10, (1, 1), LM444[6], step="return"))
lookup_case("LM_return_stays", "x=13,y=10,return,close", "harness: lookup dialog close ",
            forbid="closed the dialog")
lookup_case("LM_escape", "escape+close", "harness: lookup dialog: .*focus=x\\(all selected\\)\\)$",
            forbid="didn't close|not shown|still open")
lookup_case("LM_close", "close+close", "harness: lookup dialog close ", forbid="didn't close|not shown|still open")
# A result shown is for the pixel in the fields: changing one clears it (on
# Windows it stayed until Recalc)
lookup_case("LM_edit_clears", "x=13,y=10,recalc,x=14,close", 'harness: lookup typed x=14 \\(.* x="14" y="10" '
            + NOT_CALC)
# The dialog opens afresh each time
lookup_case("LM_reopen", "x=13,y=10,recalc,close+close", 'harness: lookup dialog close \\(.* x="0" y="0" '
            + NOT_CALC)


# --- DQ: Tools > Search Executable for DQT -----------------------------------
# gui --dqt chooses the menu item and answers its file dialog with each file
# (or cancel), then prints the report's search section and a "harness: dqt
# found ORDER TABLE ENTRIES @ OFFSET" line for each match.
def dqt_case(cid, choices, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "DQ", "gui", list(names), ("--dqt " + choices + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


DQ_STD = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
          14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
          49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]   # T.81 Table K.1, row order
DQ_ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,
             6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38,
             31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]   # zigzag position -> row-order index


def dqt_table0(data):
    """Table 0 of a JPEG's DQT segments, in row order"""
    i = marker_pos(data, 0xDB)
    end = i + 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    j = i + 4
    while j < end:
        pq, tq = data[j] >> 4, data[j] & 15
        size = 2 if pq else 1
        vals = [int.from_bytes(data[j + 1 + k * size:j + 1 + (k + 1) * size], "big") for k in range(64)]
        if tq == 0:
            table = [0] * 64
            for k, v in enumerate(vals):
                table[DQ_ZIGZAG[k]] = v
            return table
        j += 1 + 64 * size
    raise ValueError("no DQT table 0")


def dqt_pattern(table, order, entries):
    """A table as a program might store it: order "pre-zigzag" (zigzag order,
    as a DQT segment has it) or "post-zigzag" (row order), entries "1-big",
    "2-big", "2-little", "4-big" or "4-little"."""
    width, endian = int(entries[0]), entries[2:]
    return b"".join(table[DQ_ZIGZAG[k] if order == "pre-zigzag" else k].to_bytes(width, endian) for k in range(64))


DQ_ORDERS = ("pre-zigzag", "post-zigzag")
DQ_ENTRIES = ("1-big", "2-big", "2-little", "4-big", "4-little")
dq_rnd = random.Random(11)
DQ_444 = dqt_table0(c444)
# The standard table in every form, the first at offset 0 and the last ending
# at the end of the file; ok_444.jpg's luminance table in two; and a 16-bit
# table (dq_16bit.jpg's) as 2-byte big-endian entries
DQ_16BIT = [v * 9 for v in DQ_STD]
dq_exe = bytearray()
DQ_AT = {}
for order in DQ_ORDERS:
    for entries in DQ_ENTRIES:
        if dq_exe:
            dq_exe += dq_rnd.randbytes(97)
        DQ_AT[("standard", order, entries)] = len(dq_exe)
        dq_exe += dqt_pattern(DQ_STD, order, entries)
for table, name, order, entries in ((DQ_444, "file", "pre-zigzag", "1-big"),
                                    (DQ_444, "file", "post-zigzag", "2-little"),
                                    (DQ_16BIT, "16bit", "pre-zigzag", "2-big")):
    dq_exe += dq_rnd.randbytes(53)
    DQ_AT[(name, order, entries)] = len(dq_exe)
    dq_exe += dqt_pattern(table, order, entries)
dq_exe += dq_rnd.randbytes(50)
DQ_AT_END = len(dq_exe)
dq_exe += dqt_pattern(DQ_STD, "post-zigzag", "4-little")
write("dq_exe.bin", bytes(dq_exe))
write("dq_empty.bin", b"")
# ok_444.jpg with table 0 defined again before the frame, with 16-bit entries
# (some over 255)
sof = marker_pos(c444, 0xC0)
dq16 = seg(0xDB, bytes([0x10]) + b"".join(DQ_16BIT[DQ_ZIGZAG[k]].to_bytes(2, "big") for k in range(64)))
write("dq_16bit.jpg", c444[:sof] + dq16 + c444[sof:])
# An app, as the macOS open panel can return one: a folder whose Info.plist
# names its program
dq_app = files_dir / "dq_app.app" / "Contents"
(dq_app / "MacOS").mkdir(parents=True, exist_ok=True)
(dq_app / "MacOS" / "dqprog").write_bytes(bytes(dq_exe))
(dq_app / "Info.plist").write_text(
    '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0">\n<dict>\n'
    "  <key>CFBundleExecutable</key>\n  <string>dqprog</string>\n"
    "  <key>CFBundlePackageType</key>\n  <string>APPL</string>\n</dict>\n</plist>\n")
(files_dir / "dq_folder").mkdir(exist_ok=True)
# Too large to search: 4 GB and a byte (sparse, as lm_big.bin is)
with open(files_dir / "dq_4gb.bin", "wb") as f:
    f.seek(0x100000000)
    f.write(b"\0")


def dq_found(table, order, entries, at):
    return f"harness: dqt found {order} {table} {entries} @ 0x{at:08X}$"


# The menu item, where Windows has it, and its file dialog. It needs no file.
dqt_case("DQ_menu", "cancel", 'harness: dqt menu=&Tools after="File Overlay\\.\\.\\." separator=0 enabled=1 '
         'tip="Find DQT Table in Executable"$')
dqt_case("DQ_dialog", "cancel", 'harness: dqt dialog: Open Executable file for Search \\(filters="All Files \\(\\*\\)\\|'
         'Executable \\(\\*\\.exe\\)\\|DLL Library \\(\\*\\.dll\\)\\|Dynamic Library \\(\\*\\.dylib\\)" '
         'selected="All Files \\(\\*\\)" mode=existing-file\\)$')
dqt_case("DQ_cancel", "cancel", "harness: dqt no search$", forbid="message box")
dqt_case("DQ_no_file", "dq_exe.bin", dq_found("standard", "pre-zigzag", "1-big", 0), extra="--dqt-at 0")
dqt_case("DQ_no_file_tables", "dq_exe.bin", "harness: dqt tables=standard$", extra="--dqt-at 0")
dqt_case("DQ_no_file_standard_only", "dq_exe.bin", "harness: dqt matches=11$", extra="--dqt-at 0")

# The report's section, as on Windows
dqt_case("DQ_header", "dq_exe.bin", "harness: dqt report: \\*\\*\\* Searching Executable for DQT \\*\\*\\*$")
dqt_case("DQ_filename", "dq_exe.bin", "harness: dqt report:   Filename: \\[/.*/dq_exe\\.bin\\]$")
dqt_case("DQ_size", "dq_exe.bin", f"harness: dqt report:   Size:     \\[{len(dq_exe)}\\]$")
dqt_case("DQ_tables", "dq_exe.bin", "harness: dqt tables=standard,file$")
dqt_case("DQ_tables_name", "dq_exe.bin", "harness: dqt report:       Matching \\[.*ok_444\\.jpg\\]$")
dqt_case("DQ_done", "dq_exe.bin", "harness: dqt report:   Done Search$")

# The standard table in each form: at the start of the file and at its end
# (Windows compared neither), and nothing else
for (table, order, entries), at in DQ_AT.items():
    if table == "standard":
        dqt_case(f"DQ_std_{order.split('-')[0]}_{entries.replace('-', '_')}", "dq_exe.bin",
                 dq_found(table, order, entries, at))
dqt_case("DQ_std_at_end", "dq_exe.bin", dq_found("standard", "post-zigzag", "4-little", DQ_AT_END))
# The file shown's luminance table
dqt_case("DQ_file_zigzag", "dq_exe.bin", dq_found("file", "pre-zigzag", "1-big", DQ_AT[("file", "pre-zigzag", "1-big")]))
dqt_case("DQ_file_rows", "dq_exe.bin",
         dq_found("file", "post-zigzag", "2-little", DQ_AT[("file", "post-zigzag", "2-little")]))
dqt_case("DQ_count", "dq_exe.bin", "harness: dqt matches=13$")
# The file shown after another, and a second search
dqt_case("DQ_second_file", "dq_exe.bin", "harness: dqt report:       Matching \\[.*ok_420\\.jpg\\]$",
         names=("ok_444.jpg", "ok_420.jpg"), extra="--dqt-at 2")
dqt_case("DQ_twice", "dq_exe.bin+dq_exe.bin", "harness: dqt matches=13$", forbid="no search")

# A table of one value (here 1, as at quality 100) would match any run of that
# byte, so it isn't searched for. (Windows' note had "_T(" in it.)
dqt_case("DQ_constant", "dq_exe.bin", "harness: dqt report:   NOTE: Because the JPEG's DQT Luminance table is "
         "constant value \\(0x01\\),$", names=("lm_444.jpg",))
dqt_case("DQ_constant_next", "dq_exe.bin", "harness: dqt report:         matching for this table has been disabled\\.$",
         names=("lm_444.jpg",))
dqt_case("DQ_constant_tables", "dq_exe.bin", "harness: dqt tables=standard$", names=("lm_444.jpg",))
dqt_case("DQ_constant_standard_only", "dq_exe.bin", "harness: dqt matches=11$", names=("lm_444.jpg",))
# A file shown without a luminance table (Windows searched for all 0s, and
# called it a constant table)
dqt_case("DQ_no_table", "dq_exe.bin", "harness: dqt report:   NOTE: The file shown has no DQT Luminance table "
         "\\(table 0\\): only the JPEG Standard table$", names=("psd_rgb_rle.psd",), forbid="constant value")
dqt_case("DQ_no_table_tables", "dq_exe.bin", "harness: dqt tables=standard$", names=("psd_rgb_rle.psd",))
dqt_case("DQ_not_analyzed", "dq_exe.bin", "harness: dqt tables=standard$", names=("f4_no_such_file.jpg",),
         forbid="NOTE")
# A table with entries over 255 has no 1-byte form, but the standard table
# still does (Windows skipped both)
dqt_case("DQ_16bit_found", "dq_exe.bin", dq_found("file", "pre-zigzag", "2-big", DQ_AT[("16bit", "pre-zigzag", "2-big")]),
         names=("dq_16bit.jpg",))
dqt_case("DQ_16bit_skip", "dq_exe.bin", "harness: dqt report:           DQT Table is not baseline, skipping 1-byte "
         "search$", names=("dq_16bit.jpg",))
dqt_case("DQ_16bit_standard", "dq_exe.bin", dq_found("standard", "pre-zigzag", "1-big", 0), names=("dq_16bit.jpg",))

# What can be chosen: an empty file, an app (its program is searched), a
# folder, a file that has gone, and one too large for 32-bit offsets
dqt_case("DQ_empty", "dq_empty.bin", "harness: message box: \\[/.*/dq_empty\\.bin\\] is empty: there is nothing to "
         "search\\.$", forbid="BufFileSet|BufLoadWindow")
dqt_case("DQ_app", "dq_app.app", "harness: dqt report:   Filename: \\[/.*/dq_app\\.app/Contents/MacOS/dqprog\\]$")
dqt_case("DQ_app_found", "dq_app.app", "harness: dqt matches=13$")
dqt_case("DQ_folder", "dq_folder", "harness: message box: \\[/.*/dq_folder\\] is a folder: choose a file to search\\.$")
dqt_case("DQ_missing", "dq_no_such.bin", "harness: message box: ERROR: Couldn't open file \\[/.*/dq_no_such\\.bin\\]: "
         "\\[No such file or directory\\]$")
dqt_case("DQ_too_large", "dq_4gb.bin", "harness: message box: \\[/.*/dq_4gb\\.bin\\] is too large to search: "
         "4294967297 bytes \\(4 GB or more\\)\\.$")
dqt_case("DQ_too_large_no_search", "dq_4gb.bin", "harness: dqt no search$")

# --- Tools > Add Camera/SW to DB ------------------------------------------------
def add_db_case(cid, steps, expect, names=("ad_cam.jpg",), extra="", forbid="-"):
    case(cid, "AD", "gui", list(names), ("--add-db " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def rx(text):
    """text as a grep -E pattern that matches it literally"""
    return "".join("\\" + c if c in ".[]()*+?{}|^$\\" else c for c in text)


AD_SIG, AD_ROT = "013BA18D5561625796E986FDBC09F846", "01AC57E12793DFA7C46C704625C5AF0F"   # ok_444.jpg's tables
AD_Q50_SIG, AD_Q50_ROT = "017FEB124293E03634B25E85B537A2E9", "0133FC69E52E74319F110B4486B8B0D8"


def ad_exif(make, model, software=None, makernote=False):
    """EXIF with a make and model, and maybe a software and a maker note"""
    ifd0_entries = [entry(0x010F, ascii_(make)), entry(0x0110, ascii_(model))]
    if software is not None:
        ifd0_entries.append(entry(0x0131, ascii_(software)))
    ifds = {}
    if makernote:
        ifd0_entries.append(entry(0x8769, longs("<", 0x100)))
        ifds[0x100] = tiff_ifd("<", 0x100, [(0x927C, 7, 8, b"MAKERNOT")])
    ifds[8] = tiff_ifd("<", 8, ifd0_entries)
    return app1_exif(tiff("<", ifds))


# A camera the report suggests ("new signature for known camera": make, model
# and maker notes, an EXIF Software that isn't an editor's); a camera the
# built-in database has at 2x1 only (Sony DSC-W1), at 1x1 and at 2x1; and the
# tables of quality 50, whose signature sorts after the rotated one
write("ad_cam.jpg", insert_after_app0(c444, ad_exif("TestCam", "Model 1", "FW 1.0", makernote=True)))
write("ad_sony.jpg", insert_after_app0(c444, ad_exif("SONY", "DSC-W1")))
write("ad_sony_422.jpg", insert_after_app0(c422, ad_exif("SONY", "DSC-W1")))
write("ad_q50.jpg", jpeg(color, quality=50, subsampling=0))


def user_db(entries, version="03", count=None, end=True):
    """A user database file as Windows wrote it. entries: (make, model, editor,
    quality, sig, sigrot, css, swtrim, swdisp); version 01 has no sigrot."""
    def s(text):
        return text.encode("utf-16-le") + b"\0\0" if version == "03" else text.encode("latin-1") + b"\0"
    out = s("JPEGsnoop") + s(version) + s("*DB*") + struct.pack("<I", len(entries) if count is None else count)
    for e in entries:
        fields = list(e)
        if version == "01":
            del fields[5]
        out += b"".join(s(str(f)) for f in fields)
    return out + (s("*Z*") if end else b"")


AD_CAM_ENTRY = ("TestCam", "Model 1", 1, "fine", AD_SIG, AD_ROT, "1x1", "", "")
AD_WIN03 = [AD_CAM_ENTRY,
            ("", "", 2, "high", AD_SIG, AD_ROT, "1x1", "", "Łódź"),
            ("Odd", "Thing", 9, "", AD_SIG, AD_ROT, "1x1", "", "")]
write("ad_db_win03.dat", user_db(AD_WIN03))
write("ad_db_win02.dat", user_db([AD_CAM_ENTRY], version="02"))
write("ad_db_win01.dat", user_db([("TestCam", "Model 1", 1, "normal", AD_SIG, "", "1x1", "", "")], version="01"))
# Version 01 kept only the signature of the tables as they are: here quality
# 50's, which sorts after the rotated one (the one added first)
write("ad_db_win01_q50.dat", user_db([("", "", 2, "", AD_Q50_SIG, "", "1x1", "", "GIMP"),
                                      ("???", "???", 1, "", AD_Q50_SIG, "", "1x1", "", "")], version="01"))
write("ad_db_builtin.dat", user_db([("SONY", "DSC-W1", 1, "", AD_SIG, AD_ROT, "2x1", "", ""),
                                    ("SONY", "DSC-W1", 1, "", AD_SIG, AD_ROT, "1x1", "", "")]))


def ad_fake(n):
    return [("Fake", f"Model {i}", 1, "", f"01{i:030X}", f"02{i:030X}", "1x1", "", "") for i in range(n)]


write("ad_db_260.dat", user_db(ad_fake(260)))
write("ad_db_300.dat", user_db(ad_fake(300)))
write("ad_db_301.dat", user_db(ad_fake(301)))
AD_NOTDB = b"not a JPEGsnoop database\n" * 8
write("ad_db_notdb.dat", AD_NOTDB)
write("ad_db_v04.dat", user_db([AD_CAM_ENTRY], version="04"))
write("ad_db_trunc.dat", user_db(AD_WIN03[:2], count=3, end=False))
write("ad_db_noend.dat", user_db(AD_WIN03[:1], end=False))
# Databases near the size the start reads (4 MiB): AD_CAM_ENTRY, added, makes
# the first exactly that size, and the second 2 bytes more. Their one entry is
# software with a long name and signatures no file here has.
AD_LIMIT = 4 * 1024 * 1024
AD_ENTRY_BYTES = len(user_db([AD_CAM_ENTRY])) - len(user_db([]))


def ad_padded(total):
    def db(name):
        return user_db([("", "", 2, "", "03" + "0" * 30, "04" + "0" * 30, "1x1", "", name)])
    out = db("P" * ((total - len(db(""))) // 2))
    assert len(out) == total
    return out


write("ad_db_limit.dat", ad_padded(AD_LIMIT - AD_ENTRY_BYTES))
write("ad_db_over.dat", ad_padded(AD_LIMIT - AD_ENTRY_BYTES + 2))
with open(files_dir / "ad_db_big.dat", "wb") as f:
    f.write(user_db([AD_CAM_ENTRY]))
    f.seek(4 * 1024 * 1024)
    f.write(b"\0")

AD_DIALOG = "harness: add-db dialog: Add Compression Signature to Database \\("
AD_ADDED = "harness: message box: Added Compression signature to database$"
AD_ALREADY = "harness: message box: Compression signature already in database$"
AD_NONE = "harness: add-db files: none$"
AD_CAM_LINE = "\\*CAM:\\[TestCam +\\] \\[Model 1 +\\] \\[fine +\\] Yes"


def ad_count(n, when="after"):
    return f"harness: add-db report {when}: Searching Compression Signatures: \\(3347 built-in, {n} user\\(\\*\\) \\)$"


def ad_entry(n, editor, make, model, quality, sig, sigrot, css, software):
    return rx(f'harness: add-db entry {n}: editor={editor} make="{make}" model="{model}" quality="{quality}" '
              f'sig={sig} sigrot={sigrot} css={css} swtrim="" software="{software}"') + "$"


def ad_note(rest, when="before"):
    return f"harness: add-db report {when}: NOTE: User database \\[/.*/JPEGsnoop_db\\.dat\\] {rest}$"


# The menu item, where Windows has it; it needs a file
add_db_case("AD_menu", "cancel", 'harness: add-db menu=&Tools after="Export TIFF\\.\\.\\." separator=1 enabled=1 tip=""$')
add_db_case("AD_no_file_disabled", "ok", "harness: add-db menu=.* enabled=0 ", extra="--add-db-at 0")
add_db_case("AD_no_file_no_dialog", "ok", "harness: add-db dialog not shown$", extra="--add-db-at 0")

# The dialog: the file's EXIF fields and signature, which can't be changed,
# and the source the report suggests
add_db_case("AD_dialog_cam", "cancel", AD_DIALOG + rx(f'make="TestCam" model="Model 1" exif-software="FW 1.0" '
                                                      f'sig={AD_SIG} quality="" source=cam software="FW 1.0" '
                                                      'read-only=1)') + "$")
add_db_case("AD_dialog_sw", "cancel", AD_DIALOG + rx(f'make="???" model="???" exif-software="" sig={AD_SIG} '
                                                     'quality="" source=sw software="" read-only=1)') + "$",
            names=("ok_444.jpg",))
add_db_case("AD_dialog_unsure", "cancel", AD_DIALOG + rx('make="TestMake" model="TestModel" exif-software="" ') +
            ".* source=unsure ", names=("r_exif.jpg",))
add_db_case("AD_dialog_q50_sig", "cancel", AD_DIALOG + ".* sig=" + AD_Q50_SIG + " ", names=("ad_q50.jpg",))
# Nothing is sent: the note says where it goes (Windows' note said it went to
# the ImpulseAdventure database too)
add_db_case("AD_note", "cancel", "harness: add-db note: .*Nothing is sent over the network\\. /  / User database: "
            "/.*/JPEGsnoop_db\\.dat$", forbid="ImpulseAdventure")

# Cancel and Escape add nothing; Return is OK
add_db_case("AD_cancel", "cancel", AD_NONE, forbid="message box")
add_db_case("AD_cancel_report", "cancel", ad_count(0))
add_db_case("AD_escape", "escape", AD_NONE, forbid="message box")
add_db_case("AD_return", "quality=fine,return", AD_ADDED)
add_db_case("AD_return_entry", "quality=fine,return",
            ad_entry(1, 1, "TestCam", "Model 1", "fine", AD_SIG, AD_ROT, "1x1", ""))

# A camera: written as Windows wrote it, without the software; searched from
# then on, so the file now matches a camera in the database (Class 3)
AD_CAM = "quality=fine,ok"
add_db_case("AD_cam_added", AD_CAM, AD_ADDED)
add_db_case("AD_cam_entry", AD_CAM, ad_entry(1, 1, "TestCam", "Model 1", "fine", AD_SIG, AD_ROT, "1x1", ""))
add_db_case("AD_cam_file", AD_CAM, "harness: add-db file: header=JPEGsnoop version=03 utf16=1 count=1$")
add_db_case("AD_cam_file_end", AD_CAM, "harness: add-db file end=\\*Z\\* rest=0$")
add_db_case("AD_cam_files", AD_CAM, "harness: add-db files: JPEGsnoop_db\\.dat$")
add_db_case("AD_cam_count", AD_CAM, ad_count(1))
add_db_case("AD_cam_report", AD_CAM, "harness: add-db report after: " + AD_CAM_LINE)
add_db_case("AD_cam_assessment_before", AD_CAM, "harness: add-db report before: ASSESSMENT: Class 4 - ")
add_db_case("AD_cam_assessment_after", AD_CAM, "harness: add-db report after: ASSESSMENT: Class 3 - Image has high "
            "probability of being original$")
add_db_case("AD_cam_next_start", AD_CAM, 'harness: add-db next start: user=1 note=""$')
add_db_case("AD_cam_next_start_entry", AD_CAM, rx(f'harness: add-db next start entry 1: editor=1 make="TestCam" '
                                                  f'model="Model 1" quality="fine" sig={AD_SIG} sigrot={AD_ROT} '
                                                  'css=1x1 software=""') + "$")
# In the folder, not beside it, and no Signatures.dat (the Qt port wrote one
# beside the folder at every start)
add_db_case("AD_in_folder", AD_CAM, "harness: add-db beside folder: 0 0$")
add_db_case("AD_start_writes_nothing", "cancel", "harness: add-db beside folder: 0 0$", names=("ok_444.jpg",))

# Software: its name, without spaces around it, and no make or model
AD_SW = "software=%20%20GIMP%20%20,quality=%2090%20,ok"
add_db_case("AD_sw_entry", AD_SW, ad_entry(1, 2, "", "", "90", AD_SIG, AD_ROT, "1x1", "GIMP"), names=("ok_444.jpg",))
add_db_case("AD_sw_report", AD_SW, "harness: add-db report after: \\*SW :\\[GIMP +\\]", names=("ok_444.jpg",))
add_db_case("AD_sw_empty", "software=,ok,cancel", "harness: message box: You indicated that this file is processed\\. "
            "Please enter software name\\.$", names=("ok_444.jpg",))
add_db_case("AD_sw_empty_stays", "software=,ok,cancel", "harness: add-db ok left the dialog open ",
            names=("ok_444.jpg",))
add_db_case("AD_sw_spaces", "software=%20%20,ok,cancel", "harness: add-db ok left the dialog open ",
            names=("ok_444.jpg",))
add_db_case("AD_sw_then_named", "software=,ok,software=GIMP,ok",
            ad_entry(1, 2, "", "", "", AD_SIG, AD_ROT, "1x1", "GIMP"), names=("ok_444.jpg",))
# Not sure: the make and model, and the software
add_db_case("AD_unsure_entry", "software=Thing,ok",
            ad_entry(1, 3, "TestMake", "TestModel", "", AD_SIG, AD_ROT, "1x1", "Thing"), names=("r_exif.jpg",))
add_db_case("AD_unsure_report", "software=Thing,ok", "harness: add-db report after: \\*\\?\\? :\\[TestMake +\\]",
            names=("r_exif.jpg",))
# The lower signature first, as on Windows
add_db_case("AD_sig_order", "source=cam,ok", ad_entry(1, 1, "???", "???", "", AD_Q50_ROT, AD_Q50_SIG, "1x1", ""),
            names=("ad_q50.jpg",))
# A newline would end the field early when read back, and put every field
# after it out of step. (The EXIF make shows one as ".", but one can be pasted.)
add_db_case("AD_newline", "paste-software=Two%0ALines,ok", 'harness: add-db next start entry 1: editor=2 make="" '
            'model="" quality="" sig=' + AD_SIG + ' sigrot=' + AD_ROT + ' css=1x1 software="Two Lines"$',
            names=("ok_444.jpg",))

# Already in the database (Windows added it again, or added one the next start
# dropped as built in)
add_db_case("AD_twice", "ok+ok", AD_ALREADY)
add_db_case("AD_twice_count", "ok+ok", "harness: add-db file: .* count=1$")
add_db_case("AD_twice_quality", "quality=fine,ok+quality=normal,ok", AD_ALREADY)
add_db_case("AD_twice_unsure", "ok+source=unsure,ok", AD_ALREADY)
add_db_case("AD_twice_sw_other_name", "software=GIMP,ok+software=XnView,ok", "harness: add-db file: .* count=2$",
            names=("ok_444.jpg",))
add_db_case("AD_builtin_cam", "source=cam,ok", AD_ALREADY, names=("ad_sony_422.jpg",))
add_db_case("AD_builtin_cam_none", "source=cam,ok", AD_NONE, names=("ad_sony_422.jpg",))
add_db_case("AD_builtin_sw", "software=IJG%20Library,ok", AD_ALREADY, names=("ok_444.jpg",))
# The same camera with other subsampling isn't built in, and stays at the next
# start (Windows dropped it)
add_db_case("AD_other_css_added", "source=cam,ok", AD_ADDED, names=("ad_sony.jpg",))
add_db_case("AD_other_css_kept", "source=cam,ok", "harness: add-db next start: user=1 ", names=("ad_sony.jpg",))

# Nothing to add
add_db_case("AD_no_signature", "ok", "harness: message box: No compression signature could be made for the file "
            "shown: that needs a frame header \\(SOF\\) and quantization tables \\(DQT\\)\\. There is nothing to add "
            "to the database\\.$", names=("psd_rgb_rle.psd",))
add_db_case("AD_no_signature_dialog", "ok", "harness: add-db dialog not shown$", names=("psd_rgb_rle.psd",))
add_db_case("AD_not_analyzed", "ok", "harness: message box: The file shown couldn't be analyzed, so it has no "
            "compression signature to add\\.$", names=("ok_444.jpg", "f4_no_such_file.jpg"), extra="--add-db-at 2")

# A database Windows wrote, read at the start (the Qt port never read one):
# version 03 (UTF-16; Windows read only each character's low byte), 02 and 01
# (single bytes)
W03 = "--user-db ad_db_win03.dat"
add_db_case("AD_load_03_count", "cancel", ad_count(3, "before"), extra=W03)
add_db_case("AD_load_03_cam", "cancel", "harness: add-db report before: " + AD_CAM_LINE, extra=W03)
add_db_case("AD_load_03_unicode", "cancel", "harness: add-db report before: \\*SW :\\[Łódź +\\]", extra=W03)
add_db_case("AD_load_03_unknown_editor", "cancel", "harness: add-db report before: \\*\\?\\? :\\[Odd +\\]", extra=W03)
add_db_case("AD_load_03_assessment", "cancel", "harness: add-db report before: ASSESSMENT: Class 3 - ", extra=W03)
add_db_case("AD_load_03_add", "source=sw,software=New,ok", "harness: add-db file: .* count=4$", extra=W03)
add_db_case("AD_load_03_unknown_editor_kept", "source=sw,software=New,ok", 'harness: add-db entry 3: editor=0 make="Odd" ',
            extra=W03)
add_db_case("AD_load_03_no_bak", "source=sw,software=New,ok", "harness: add-db files: JPEGsnoop_db\\.dat$", extra=W03)
add_db_case("AD_load_02", "cancel", "harness: add-db report before: " + AD_CAM_LINE, extra="--user-db ad_db_win02.dat")
add_db_case("AD_load_01", "cancel", "harness: add-db report before: \\*CAM:\\[TestCam +\\] \\[Model 1 +\\] \\[normal +\\] "
            "Yes", extra="--user-db ad_db_win01.dat")
# An older version's file is kept when it is replaced, as Windows kept it
AD_NEW_SW = "source=sw,software=New,ok"
AD_KEPT = ("harness: message box: Added Compression signature to database\\. The file it replaced was kept as "
           "\\[/.*/JPEGsnoop_db\\.dat\\.bak\\]\\.$")
add_db_case("AD_load_02_kept", AD_NEW_SW, AD_KEPT, extra="--user-db ad_db_win02.dat")
add_db_case("AD_load_02_bak", AD_NEW_SW, "harness: add-db bak md5=" + md5(user_db([AD_CAM_ENTRY], version="02")) + "$",
            extra="--user-db ad_db_win02.dat")
add_db_case("AD_load_02_now_03", AD_NEW_SW, "harness: add-db file: header=JPEGsnoop version=03 utf16=1 count=2$",
            extra="--user-db ad_db_win02.dat")
# One the built-in database has is dropped, but not one with other subsampling
add_db_case("AD_load_builtin", "cancel", ad_count(1, "before"), names=("ad_sony.jpg",),
            extra="--user-db ad_db_builtin.dat")
add_db_case("AD_load_builtin_kept", "cancel", "harness: add-db report before: \\*CAM:\\[SONY +\\] \\[DSC-W1 +\\] "
            "\\[ +\\] Yes", names=("ad_sony.jpg",), extra="--user-db ad_db_builtin.dat")
# 256 or more entries (Windows read the count's low byte: 4 of 260)
add_db_case("AD_load_260", "cancel", ad_count(260, "before"), extra="--user-db ad_db_260.dat")
add_db_case("AD_load_301", "cancel", ad_count(300, "before"), extra="--user-db ad_db_301.dat")
add_db_case("AD_load_301_note", "cancel", ad_note("has 301 entries: only the first 300 are used"),
            extra="--user-db ad_db_301.dat")
add_db_case("AD_full", "ok", "harness: message box: ERROR: Can only store maximum of 300 extra signatures in local "
            "DB$", extra="--user-db ad_db_300.dat")
add_db_case("AD_full_unchanged", "ok", "harness: add-db file: .* count=300$", extra="--user-db ad_db_300.dat")

# A file that can't be read in full: the report says so, its entries before
# the problem are used, and it is kept when replaced (Windows replaced one
# without its header at the start, and read the rest as far as it could)
add_db_case("AD_notdb_note", "cancel", ad_note("isn't a JPEGsnoop user database"), extra="--user-db ad_db_notdb.dat")
add_db_case("AD_notdb_kept", "ok", AD_KEPT, extra="--user-db ad_db_notdb.dat")
add_db_case("AD_notdb_bak", "ok", "harness: add-db bak md5=" + md5(AD_NOTDB) + "$", extra="--user-db ad_db_notdb.dat")
add_db_case("AD_notdb_now", "ok", "harness: add-db file: header=JPEGsnoop version=03 utf16=1 count=1$",
            extra="--user-db ad_db_notdb.dat")
add_db_case("AD_notdb_note_gone", "ok", 'harness: add-db next start: user=1 note=""$', extra="--user-db ad_db_notdb.dat")
add_db_case("AD_v04_note", "cancel", ad_note("is version 04, which this version of JPEGsnoop can't read"),
            extra="--user-db ad_db_v04.dat")
add_db_case("AD_v04_kept", "ok", AD_KEPT, extra="--user-db ad_db_v04.dat")
add_db_case("AD_trunc_note", "cancel", ad_note("is damaged: 2 of its 3 entries could be read"),
            extra="--user-db ad_db_trunc.dat")
add_db_case("AD_trunc_count", "cancel", ad_count(2, "before"), extra="--user-db ad_db_trunc.dat")
add_db_case("AD_noend_note", "cancel", ad_note("is damaged after entry 1"), extra="--user-db ad_db_noend.dat")
add_db_case("AD_big_note", "cancel", ad_note("is too large to be a user database"), extra="--user-db ad_db_big.dat")
add_db_case("AD_unreadable_note", "cancel", ad_note("couldn't be opened: .*"),
            extra="--user-db ad_db_win03.dat --user-db-mode 000")
add_db_case("AD_unreadable_kept", "ok", "harness: message box: ERROR: The signature wasn't added: "
            "\\[/.*/JPEGsnoop_db\\.dat\\] couldn't be kept as \\[/.*/JPEGsnoop_db\\.dat\\.bak\\], so it wasn't "
            "replaced: ", extra="--user-db ad_db_win03.dat --user-db-mode 000")
add_db_case("AD_unreadable_count", "ok", ad_count(0), extra="--user-db ad_db_win03.dat --user-db-mode 000")

# A folder that isn't there: not added, so not searched either
add_db_case("AD_no_folder", "ok", "harness: message box: ERROR: The signature wasn't added: "
            "\\[/.*/missing/JPEGsnoop_db\\.dat\\] couldn't be written: there is no folder \\[/.*/missing\\]$",
            extra="--db-dir missing")
add_db_case("AD_no_folder_count", "ok", ad_count(0), extra="--db-dir missing")
# Options > Configuration: the new folder's database is searched and added to
# (Windows kept the old folder's entries and wrote them over the new file)
AD_FOLDERS = "--user-db ad_db_win03.dat --user-db other:ad_db_win02.dat"
add_db_case("AD_folder_before", "folder=other+" + AD_NEW_SW, ad_count(3, "before"), extra=AD_FOLDERS)
add_db_case("AD_folder_after", "folder=other+" + AD_NEW_SW, ad_count(2), extra=AD_FOLDERS)
add_db_case("AD_folder_file", "folder=other+" + AD_NEW_SW, "harness: add-db file: .* count=2$", extra=AD_FOLDERS)
add_db_case("AD_folder_none", "folder=+ok", "harness: message box: ERROR: The signature wasn't added: No folder is set "
            "for the user database \\(Options > Configuration\\)$")

# Version 01's signature is found whichever signature is added first (the
# lower one: here the rotated one, which version 01 didn't keep)
W01Q = "--user-db ad_db_win01_q50.dat"
add_db_case("AD_load_01_rot_sw", "source=sw,software=GIMP,ok", AD_ALREADY, names=("ad_q50.jpg",), extra=W01Q)
add_db_case("AD_load_01_rot_cam", "source=cam,ok", AD_ALREADY, names=("ad_q50.jpg",), extra=W01Q)
add_db_case("AD_load_01_rot_unchanged", "source=sw,software=GIMP,ok", "harness: add-db file: header=JPEGsnoop "
            "version=01 utf16=0 count=2$", names=("ad_q50.jpg",), extra=W01Q)

# No larger than the start reads: one that would be is refused, and the file
# is left as it was
add_db_case("AD_limit_added", AD_CAM, AD_ADDED, extra="--user-db ad_db_limit.dat")
add_db_case("AD_limit_next_start", AD_CAM, 'harness: add-db next start: user=2 note=""$', extra="--user-db ad_db_limit.dat")
AD_OVER = "--user-db ad_db_over.dat"
add_db_case("AD_over", AD_CAM, "harness: message box: ERROR: The signature wasn't added: \\[/.*/JPEGsnoop_db\\.dat\\] "
            f"would be {AD_LIMIT + 2} bytes with it, more than the {AD_LIMIT} that JPEGsnoop reads, so it wasn't "
            "changed$", extra=AD_OVER)
add_db_case("AD_over_next_start", AD_CAM, 'harness: add-db next start: user=1 note=""$', extra=AD_OVER)
add_db_case("AD_over_unchanged", AD_CAM, "harness: add-db file: .* count=1$", extra=AD_OVER)
add_db_case("AD_over_count", AD_CAM, ad_count(1), extra=AD_OVER)

# Another copy of JPEGsnoop, started with this one, adds to the same file:
# both entries are kept, whichever adds first, and one it added is already in
AD_OTHER_SW = ad_entry(1, 2, "", "", "", AD_SIG, AD_ROT, "1x1", "GIMP")
add_db_case("AD_other_first", "other=GIMP+" + AD_CAM, "harness: add-db file: .* count=2$")
add_db_case("AD_other_first_entry", "other=GIMP+" + AD_CAM, AD_OTHER_SW)
add_db_case("AD_other_first_mine", "other=GIMP+" + AD_CAM,
            ad_entry(2, 1, "TestCam", "Model 1", "fine", AD_SIG, AD_ROT, "1x1", ""))
add_db_case("AD_other_first_count", "other=GIMP+" + AD_CAM, ad_count(2))
add_db_case("AD_other_second", AD_CAM + "+other=GIMP", "harness: add-db file: .* count=2$")
add_db_case("AD_other_second_said", AD_CAM + "+other=GIMP", "harness: add-db other copy added GIMP: added$")
add_db_case("AD_other_same", "other=GIMP+source=sw,software=GIMP,ok", AD_ALREADY)
add_db_case("AD_other_same_count", "other=GIMP+source=sw,software=GIMP,ok", "harness: add-db file: .* count=1$")
# While another copy writes it (holds its lock), it isn't changed; a lock left
# by a copy that stopped is taken over
add_db_case("AD_locked", "lock+" + AD_CAM, "harness: message box: ERROR: The signature wasn't added: "
            "\\[/.*/JPEGsnoop_db\\.dat\\] is being changed by another copy of JPEGsnoop \\(it holds "
            "\\[/.*/JPEGsnoop_db\\.dat\\.lock\\]\\)\\. Try again\\.$")
add_db_case("AD_locked_none", "lock+" + AD_CAM, AD_NONE)
add_db_case("AD_locked_count", "lock+" + AD_CAM, ad_count(0))
add_db_case("AD_stale_lock", "stale-lock+" + AD_CAM, AD_ADDED)
add_db_case("AD_stale_lock_gone", "stale-lock+" + AD_CAM, "harness: add-db files: JPEGsnoop_db\\.dat$")
# A folder that can't be written to: the lock can't be made
AD_RO = "--db-dir ro --db-dir-mode 555"
add_db_case("AD_readonly", AD_CAM, "harness: message box: ERROR: The signature wasn't added: "
            "\\[/.*/ro/JPEGsnoop_db\\.dat\\] couldn't be written: \\[/.*/ro/JPEGsnoop_db\\.dat\\.lock\\] couldn't "
            "be made$", extra=AD_RO)
add_db_case("AD_readonly_none", AD_CAM, AD_NONE, extra=AD_RO)
# A file that can't be written to: not added, so not searched either
AD_RO_FILE = "--user-db ad_db_win03.dat --user-db-mode 444"
add_db_case("AD_readonly_file", AD_NEW_SW, "harness: message box: ERROR: The signature wasn't added: "
            "\\[/.*/JPEGsnoop_db\\.dat\\] couldn't be written: .*not writable", extra=AD_RO_FILE)
add_db_case("AD_readonly_file_count", AD_NEW_SW, ad_count(3), extra=AD_RO_FILE)


# --- Tools > Manage Local DB ----------------------------------------------------
# Driven through --add-db's manage openings; ad_db_win03.dat unless said
def manage_db_case(cid, openings, expect, extra="--user-db ad_db_win03.dat", forbid="-"):
    case(cid, "MD", "gui", ["ad_cam.jpg"], ("--add-db " + openings + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def md_dialog(state):
    return rx(f"harness: manage-db dialog: Manage User Database ({state})") + "$"


def md_step(step, state):
    return rx(f"harness: manage-db {step} ({state})") + "$"


def md_row(n, *cols):
    return rx(f"harness: manage-db row {n}: " + "|".join(cols)) + "$"


def md_file(count, version="03"):
    return f"harness: add-db file: header=JPEGsnoop version={version} utf16={int(version == '03')} count={count}$"


def md_next(n):
    return f'harness: add-db next start: user={n} note=""$'


MD_KEPT_BAK = "harness: message box: Removed from the user database\\. The file it replaced was kept as " \
              "\\[/.*/JPEGsnoop_db\\.dat\\.bak\\]\\.$"
MD_STATE3 = 'rows=3 selected=none current=1 remove=0 remove-all=1 count="3 entries"'
# The same entry twice (Windows added it again); a version 02 file (single
# bytes) that as version 03 would be 2 bytes over the size the start reads
# without its camera entry, and one that would be exactly that size
write("ad_db_dup.dat", user_db([AD_CAM_ENTRY, AD_CAM_ENTRY]))


def md_big(name):
    return ("", "", 2, "", "05" + "0" * 30, "06" + "0" * 30, "1x1", "", name)


MD_BIG_NAME = (AD_LIMIT + 2 - len(user_db([md_big("")]))) // 2
assert len(user_db([md_big("B" * MD_BIG_NAME)])) == AD_LIMIT + 2
write("ad_db_v02_big.dat", user_db([AD_CAM_ENTRY, md_big("B" * MD_BIG_NAME)], version="02"))
write("ad_db_v02_limit.dat", user_db([AD_CAM_ENTRY, md_big("B" * (MD_BIG_NAME - 1))], version="02"))

# The menu item, after Add Camera/SW to DB as on Windows, needs no file
manage_db_case("MD_menu", "manage,cancel", 'harness: manage-db menu=&Tools after="Add Camera/SW to DB\\.\\.\\." '
               'separator=0 enabled=1 tip="Manage the local User signatures"$')
manage_db_case("MD_no_file", "manage,cancel", md_dialog(MD_STATE3), extra="--add-db-at 0 --user-db ad_db_win03.dat")
manage_db_case("MD_no_file_enabled", "manage,cancel", "harness: manage-db menu=.* enabled=1 ",
               extra="--add-db-at 0 --user-db ad_db_win03.dat")

# Every field but the rotated signature, in the file's order, by source.
# (Windows listed make, model and quality, with a software name as the make.)
manage_db_case("MD_dialog", "manage,cancel", md_dialog(MD_STATE3))
manage_db_case("MD_columns", "manage,cancel",
               "harness: manage-db columns: Source\\|Make\\|Model\\|Software\\|Quality\\|Subsampling\\|Signature$")
manage_db_case("MD_row_cam", "manage,cancel", md_row(1, "Camera", "TestCam", "Model 1", "", "fine", "1x1", AD_SIG))
manage_db_case("MD_row_sw", "manage,cancel", md_row(2, "Software", "", "", "Łódź", "high", "1x1", AD_SIG))
manage_db_case("MD_row_not_set", "manage,cancel", md_row(3, "Not set", "Odd", "Thing", "", "", "1x1", AD_SIG))
manage_db_case("MD_row_unknown", "source=unsure,software=Paint,quality=q,ok+manage,cancel",
               md_row(1, "Unknown", "TestCam", "Model 1", "Paint", "q", "1x1", AD_SIG), extra="")
manage_db_case("MD_row_long", "manage,cancel", md_row(1, "Software", "", "", "P" * 200 + "\u2026", "", "1x1",
                                                      "03" + "0" * 30), extra="--user-db ad_db_limit.dat")
manage_db_case("MD_note", "manage,cancel", "harness: manage-db note: Remove takes the entries chosen out of this list, "
               "and OK takes them out of the file\\. The signatures built into JPEGsnoop aren't listed\\. /  / User "
               "database: /.*/JPEGsnoop_db\\.dat$")
manage_db_case("MD_empty", "manage,cancel", md_dialog('rows=0 selected=none current=0 remove=0 remove-all=0 '
                                                      'count="There are no entries. Tools > Add Camera/SW to DB adds '
                                                      'them."'), extra="")
manage_db_case("MD_empty_ok", "manage,ok", AD_NONE, extra="")
manage_db_case("MD_no_folder", "folder=+manage,cancel", "harness: manage-db note: No folder is set for the user "
               "database \\(Options > Configuration\\), so it has no entries\\.$")
manage_db_case("MD_no_folder_count", "folder=+manage,cancel",
               md_dialog('rows=0 selected=none current=0 remove=0 remove-all=0 count="There are no entries."'))
manage_db_case("MD_unreadable", "manage,cancel",
               md_dialog('rows=0 selected=none current=0 remove=0 remove-all=0 count="No entries could be read."'),
               extra="--user-db ad_db_win03.dat --user-db-mode 000")
manage_db_case("MD_folder", "folder=other+manage,cancel", md_dialog('rows=1 selected=none current=1 remove=0 '
                                                                    'remove-all=1 count="1 entry"'), extra=AD_FOLDERS)

# Choosing and removing: several at once, the row after them chosen next
manage_db_case("MD_select", "manage,select=2,cancel",
               md_step("select=2", 'rows=3 selected=2 current=2 remove=1 remove-all=1 count="3 entries"'))
manage_db_case("MD_select_two", "manage,select=1;3,cancel",
               md_step("select=1;3", 'rows=3 selected=1;3 current=3 remove=1 remove-all=1 count="3 entries"'))
MD_LEFT2 = 'count="2 entries, 1 removed: OK takes it out of the file."'
manage_db_case("MD_remove_next", "manage,select=2,remove,cancel",
               md_step("remove", "rows=2 selected=2 current=2 remove=1 remove-all=1 " + MD_LEFT2))
manage_db_case("MD_remove_last", "manage,select=3,remove,cancel",
               md_step("remove", "rows=2 selected=2 current=2 remove=1 remove-all=1 " + MD_LEFT2))
manage_db_case("MD_remove_two", "manage,select=1;2,remove,cancel",
               md_step("remove", 'rows=1 selected=1 current=1 remove=1 remove-all=1 count="1 entry, 2 removed: OK '
                                 'takes them out of the file."'))
manage_db_case("MD_remove_rows", "manage,select=2,remove,list,cancel",
               md_row(2, "Not set", "Odd", "Thing", "", "", "1x1", AD_SIG))
manage_db_case("MD_delete_key", "manage,select=1,delete,cancel",
               md_step("delete", "rows=2 selected=1 current=1 remove=1 remove-all=1 " + MD_LEFT2))
manage_db_case("MD_backspace_key", "manage,select=1,backspace,cancel",
               md_step("backspace", "rows=2 selected=1 current=1 remove=1 remove-all=1 " + MD_LEFT2))
manage_db_case("MD_remove_none_chosen", "manage,remove,delete,cancel", md_step("delete", MD_STATE3))
manage_db_case("MD_remove_all", "manage,remove-all,cancel",
               md_step("remove-all", 'rows=0 selected=none current=0 remove=0 remove-all=0 count="0 entries, 3 '
                                     'removed: OK takes them out of the file."'))

# Cancel leaves the file as it was; OK takes out what was removed, quietly
manage_db_case("MD_cancel", "manage,select=1,remove,cancel", md_file(3), forbid="message box")
manage_db_case("MD_escape", "manage,select=1,remove,escape", md_file(3), forbid="message box")
manage_db_case("MD_ok", "manage,select=2,remove,ok", md_file(2), forbid="message box")
manage_db_case("MD_ok_entry", "manage,select=2,remove,ok", ad_entry(2, 0, "Odd", "Thing", "", AD_SIG, AD_ROT, "1x1", ""))
manage_db_case("MD_ok_end", "manage,select=2,remove,ok", "harness: add-db file end=\\*Z\\* rest=0$")
manage_db_case("MD_ok_next_start", "manage,select=2,remove,ok", md_next(2))
manage_db_case("MD_ok_report", "manage,select=1,remove,ok", ad_count(2),
               forbid="harness: add-db report after: \\*CAM:\\[TestCam")
manage_db_case("MD_return", "manage,select=2,remove,return", md_file(2))
manage_db_case("MD_ok_two", "manage,select=1;3,remove,ok", ad_entry(1, 2, "", "", "high", AD_SIG, AD_ROT, "1x1", "Łódź"))
manage_db_case("MD_ok_two_count", "manage,select=1;3,remove,ok", md_file(1))
manage_db_case("MD_ok_all", "manage,remove-all,ok", md_file(0))
manage_db_case("MD_ok_all_count", "manage,remove-all,ok", ad_count(0))
manage_db_case("MD_twice", "manage,select=1,remove,ok+manage,cancel",
               md_dialog('rows=2 selected=none current=1 remove=0 remove-all=1 count="2 entries"'))
manage_db_case("MD_add_after", "manage,select=1,remove,ok+" + AD_CAM, AD_ADDED)
manage_db_case("MD_add_after_entry", "manage,select=1,remove,ok+" + AD_CAM,
               ad_entry(3, 1, "TestCam", "Model 1", "fine", AD_SIG, AD_ROT, "1x1", ""))
# OK with none removed doesn't write the file (Windows wrote it again)
manage_db_case("MD_none_ok", "manage,ok", md_file(1, "02"), extra="--user-db ad_db_win02.dat", forbid="bak")

# An older version's file, or one that couldn't be read in full, is kept first
manage_db_case("MD_v02", "manage,select=1,remove,ok", MD_KEPT_BAK, extra="--user-db ad_db_win02.dat")
manage_db_case("MD_v02_file", "manage,select=1,remove,ok", md_file(0), extra="--user-db ad_db_win02.dat")
manage_db_case("MD_v02_bak", "manage,select=1,remove,ok",
               "harness: add-db bak md5=" + md5(user_db([AD_CAM_ENTRY], version="02")) + "$",
               extra="--user-db ad_db_win02.dat")
manage_db_case("MD_v01", "manage,select=1,remove,ok", md_file(0), extra="--user-db ad_db_win01.dat")
manage_db_case("MD_trunc_note", "manage,cancel", "harness: manage-db note: .* / NOTE: User database "
               "\\[/.*/JPEGsnoop_db\\.dat\\] is damaged: 2 of its 3 entries could be read\\. Only the entries read are "
               "listed, and if any are removed, the file is kept as /.*/JPEGsnoop_db\\.dat\\.bak first\\.$",
               extra="--user-db ad_db_trunc.dat")
manage_db_case("MD_trunc_rows", "manage,cancel", md_dialog('rows=2 selected=none current=1 remove=0 remove-all=1 '
                                                           'count="2 entries"'), extra="--user-db ad_db_trunc.dat")
manage_db_case("MD_trunc_kept", "manage,select=1,remove,ok", MD_KEPT_BAK, extra="--user-db ad_db_trunc.dat")
manage_db_case("MD_trunc_bak", "manage,select=1,remove,ok",
               "harness: add-db bak md5=" + md5(user_db(AD_WIN03[:2], count=3, end=False)) + "$",
               extra="--user-db ad_db_trunc.dat")
manage_db_case("MD_trunc_file", "manage,select=1,remove,ok", md_file(1), extra="--user-db ad_db_trunc.dat")
manage_db_case("MD_301_rows", "manage,cancel", "harness: manage-db dialog: Manage User Database \\(rows=300 ",
               extra="--user-db ad_db_301.dat")
manage_db_case("MD_301_file", "manage,select=1,remove,ok", md_file(299), extra="--user-db ad_db_301.dat")
manage_db_case("MD_301_bak", "manage,select=1,remove,ok", "harness: add-db bak md5=" + md5(user_db(ad_fake(301))) + "$",
               extra="--user-db ad_db_301.dat")
# The same entry twice: each row removed takes out one
manage_db_case("MD_dup_one", "manage,select=1,remove,ok", md_file(1), extra="--user-db ad_db_dup.dat")
manage_db_case("MD_dup_both", "manage,select=1;2,remove,ok", md_file(0), extra="--user-db ad_db_dup.dat")

# No larger than the start reads: a version 02 file grows as version 03
manage_db_case("MD_size", "manage,select=1,remove,ok", "harness: message box: ERROR: The entry wasn't removed: "
               f"\\[/.*/JPEGsnoop_db\\.dat\\] would be {AD_LIMIT + 2} bytes as version 03, more than the {AD_LIMIT} "
               "that JPEGsnoop reads, so it wasn't changed$", extra="--user-db ad_db_v02_big.dat")
manage_db_case("MD_size_unchanged", "manage,select=1,remove,ok", md_file(2, "02"), extra="--user-db ad_db_v02_big.dat",
               forbid="bak")
manage_db_case("MD_size_count", "manage,select=1,remove,ok", ad_count(2), extra="--user-db ad_db_v02_big.dat")
manage_db_case("MD_size_limit", "manage,select=1,remove,ok", md_next(1), extra="--user-db ad_db_v02_limit.dat")
manage_db_case("MD_size_big_removed", "manage,select=2,remove,ok", md_file(1), extra="--user-db ad_db_v02_big.dat")

# Another copy of JPEGsnoop: the dialog lists the file as it is, and what the
# other adds or takes out while it is open stays so (Windows listed and wrote
# what it read at the start)
manage_db_case("MD_other_listed", "other=Extra+manage,cancel", md_row(4, "Software", "", "", "Extra", "", "1x1", AD_SIG))
manage_db_case("MD_other_added", "manage,select=1,remove,other=Fresh,ok", md_file(3))
manage_db_case("MD_other_added_entry", "manage,select=1,remove,other=Fresh,ok",
               ad_entry(3, 2, "", "", "", AD_SIG, AD_ROT, "1x1", "Fresh"))
manage_db_case("MD_other_removed", "other=Extra+manage,other-remove=Extra,select=1,remove,ok", md_file(2))
manage_db_case("MD_other_removed_same", "other=Extra+manage,other-remove=Extra,select=4,remove,ok", md_file(3),
               forbid="message box")
manage_db_case("MD_other_removed_count", "other=Extra+manage,other-remove=Extra,select=4,remove,ok", ad_count(3))
# The file made unreadable or damaged by another program while the dialog is
# open: an entry removed that isn't in what can be read may be in the rest, so
# nothing is written and the message says why. One that is found is taken out,
# and the file kept, as for a file damaged before.
MD_UNREADABLE_LATER = "manage,file-mode=000,select=1,remove,ok+file-mode=644"
MD_NOT_REMOVED = "harness: message box: ERROR: The entry wasn't removed: \\[/.*/JPEGsnoop_db\\.dat\\] "
manage_db_case("MD_unreadable_later", MD_UNREADABLE_LATER, MD_NOT_REMOVED + "couldn't be opened: .*$")
manage_db_case("MD_unreadable_later_two", "manage,file-mode=000,select=1;2,remove,ok+file-mode=644",
               "harness: message box: ERROR: The entries weren't removed: \\[/.*/JPEGsnoop_db\\.dat\\] couldn't be "
               "opened: .*$")
manage_db_case("MD_unreadable_later_file", MD_UNREADABLE_LATER, md_file(3))
MD_DAMAGED_LATER = "manage,db-file=ad_db_trunc.dat,select=3,remove,ok"
manage_db_case("MD_damaged_later", MD_DAMAGED_LATER, MD_NOT_REMOVED + "is damaged: 2 of its 3 entries could be read$")
manage_db_case("MD_damaged_later_kept", MD_DAMAGED_LATER, "harness: add-db files: JPEGsnoop_db\\.dat$")
manage_db_case("MD_damaged_later_found", "manage,db-file=ad_db_trunc.dat,select=1,remove,ok", MD_KEPT_BAK)
manage_db_case("MD_damaged_later_found_file", "manage,db-file=ad_db_trunc.dat,select=1,remove,ok", md_file(1))
# The lock: held by another copy, left by one that stopped, can't be made
MD_LOCKED = ("\\[/.*/JPEGsnoop_db\\.dat\\] is being changed by another copy of JPEGsnoop \\(it holds "
             "\\[/.*/JPEGsnoop_db\\.dat\\.lock\\]\\)\\. Try again\\.$")
manage_db_case("MD_locked", "lock+manage,select=1,remove,ok",
               "harness: message box: ERROR: The entry wasn't removed: " + MD_LOCKED)
manage_db_case("MD_locked_two", "lock+manage,select=1;2,remove,ok",
               "harness: message box: ERROR: The entries weren't removed: " + MD_LOCKED)
manage_db_case("MD_locked_unchanged", "lock+manage,select=1,remove,ok", md_file(3))
manage_db_case("MD_stale_lock", "stale-lock+manage,select=1,remove,ok", md_file(2))
manage_db_case("MD_stale_lock_gone", "stale-lock+manage,select=1,remove,ok", "harness: add-db files: JPEGsnoop_db\\.dat$")
manage_db_case("MD_readonly", "manage,select=1,remove,ok", "harness: message box: ERROR: The entry wasn't removed: "
               "\\[/.*/ro/JPEGsnoop_db\\.dat\\] couldn't be written: \\[/.*/ro/JPEGsnoop_db\\.dat\\.lock\\] couldn't "
               "be made$", extra="--db-dir ro --db-dir-mode 555 --user-db ad_db_win03.dat")
manage_db_case("MD_readonly_file", "manage,select=1,remove,ok", "harness: message box: ERROR: The entry wasn't "
               "removed: \\[/.*/JPEGsnoop_db\\.dat\\] couldn't be written: .*not writable", extra=AD_RO_FILE)
manage_db_case("MD_readonly_file_count", "manage,select=1,remove,ok", ad_count(3), extra=AD_RO_FILE)

# --- View > Overlays > MCU Grid -------------------------------------------------
# White images, so that the grid's dots are the only dark pixels: MCUs of 8x8
# (4:4:4 and grayscale), 16x16 (4:2:0) and 16x8 (4:2:2), and one of 40x20,
# which the decoder rounds up to whole MCUs (48x32)
WHITE = Image.new("RGB", (64, 48), (255, 255, 255))
write("mg_white_444.jpg", jpeg(WHITE, quality=95, subsampling=0))
write("mg_white_420.jpg", jpeg(WHITE, quality=95, subsampling=2))
write("mg_white_422.jpg", jpeg(WHITE, quality=95, subsampling=1))
write("mg_white_gray.jpg", jpeg(WHITE.convert("L"), quality=95))
write("mg_white_odd.jpg", jpeg(Image.new("RGB", (40, 20), (255, 255, 255)), quality=95, subsampling=2))
# Larger than the image window at 100%, to scroll
write("mg_white_big.jpg", jpeg(Image.new("RGB", (1024, 1024), (255, 255, 255)), quality=95, subsampling=2))


def mcu_grid_case(cid, names, steps, expect, extra="", forbid="-"):
    case(cid, "MG", "gui", list(names), ("--mcu-grid " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def mg_drawn(when, zoom, screen, cols, rows, scale=1):
    return rx(f"harness: mcu-grid {when}: checked=1 zoom={zoom} scale={scale} screen={screen} cols={cols} "
              f"rows={rows} gap=3 colours=202020 stray=0") + "$"


def mg_none(when, checked, zoom, screen, scale=1):
    return rx(f"harness: mcu-grid {when}: checked={checked} zoom={zoom} scale={scale} screen={screen} grid=none") + "$"


MG_420 = ("mg_white_420.jpg",)
MG_ON = "zoom=100,toggle"
MG_COLS_8 = "0,8,16,24,32,40,48,56"
MG_ROWS_8 = "0,8,16,24,32,40"
# The menu item: in View > Overlays, with Windows' tip; Control-G on macOS,
# where Command-G is Edit > Find Next's key. Checkable, off at first, and
# needs no file open.
mcu_grid_case("MG_menu", MG_420, "grid", rx('harness: mcu-grid menu=Overlays after="-" separator=0 enabled=1 '
              'tip="Display MCU Boundary grid" shortcut=Meta+G checkable=1 checked=0') + "$")
mcu_grid_case("MG_off", MG_420, "zoom=100,grid", mg_none("grid", 0, 1, "64x48"))
mcu_grid_case("MG_toggle", MG_420, MG_ON, "harness: mcu-grid toggle: checked=1 find-next=0$")
mcu_grid_case("MG_toggle_off", MG_420, MG_ON + ",toggle", mg_none("file 1", 0, 1, "64x48"))
mcu_grid_case("MG_key", MG_420, "zoom=100,key", "harness: mcu-grid key: checked=1 find-next=0$")
mcu_grid_case("MG_key_drawn", MG_420, "zoom=100,key", mg_drawn("file 1", 1, "64x48", "0,16,32,48", "0,16,32"))
# Command-G is Find Next's (it was both, so neither ran)
MG_FIND = "--edit key:find,what:Precision,return,escape,activate"
mcu_grid_case("MG_cmd_g", MG_420, "cmd-g", "harness: mcu-grid cmd-g: checked=0 find-next=1$", extra=MG_FIND)
mcu_grid_case("MG_cmd_g_grid_on", MG_420, "cmd-g,key,cmd-g", "harness: mcu-grid cmd-g: checked=1 find-next=2$",
              extra=MG_FIND)
mcu_grid_case("MG_before_file", MG_420, "toggle,zoom=100", mg_drawn("file 1", 1, "64x48", "0,16,32,48", "0,16,32"),
              extra="--mcu-grid-at 0")
mcu_grid_case("MG_before_file_enabled", MG_420, "toggle", "harness: mcu-grid menu=.* enabled=1 ",
              extra="--mcu-grid-at 0")

# A dotted line, one screen pixel wide, along the top and left of each MCU;
# the image unchanged elsewhere. MCUs of 16x16, 8x8, 16x8, and a grayscale
# image's 8x8; an image rounded up to whole MCUs.
mcu_grid_case("MG_420", MG_420, MG_ON + ",grid", mg_drawn("grid", 1, "64x48", "0,16,32,48", "0,16,32"))
mcu_grid_case("MG_444", ("mg_white_444.jpg",), MG_ON, mg_drawn("file 1", 1, "64x48", MG_COLS_8, MG_ROWS_8))
mcu_grid_case("MG_422", ("mg_white_422.jpg",), MG_ON, mg_drawn("file 1", 1, "64x48", "0,16,32,48", MG_ROWS_8))
mcu_grid_case("MG_gray", ("mg_white_gray.jpg",), MG_ON, mg_drawn("file 1", 1, "64x48", MG_COLS_8, MG_ROWS_8))
mcu_grid_case("MG_odd", ("mg_white_odd.jpg",), MG_ON, mg_drawn("file 1", 1, "48x32", "0,16,32", "0,16"))
# Each zoom; none at 12.5%, as on Windows
mcu_grid_case("MG_zoom_12", MG_420, MG_ON + ",zoom=12.5", mg_none("file 1", 1, 0.125, "8x6"))
mcu_grid_case("MG_zoom_25", MG_420, MG_ON + ",zoom=25", mg_drawn("file 1", 0.25, "16x12", "0,4,8,12", "0,4,8"))
mcu_grid_case("MG_zoom_50", MG_420, MG_ON + ",zoom=50", mg_drawn("file 1", 0.5, "32x24", "0,8,16,24", "0,8,16"))
mcu_grid_case("MG_zoom_150", MG_420, MG_ON + ",zoom=150", mg_drawn("file 1", 1.5, "96x72", "0,24,48,72", "0,24,48"))
mcu_grid_case("MG_zoom_200", MG_420, MG_ON + ",zoom=200", mg_drawn("file 1", 2, "128x96", "0,32,64,96", "0,32,64"))
mcu_grid_case("MG_zoom_800", ("mg_white_444.jpg",), MG_ON + ",zoom=800",
              mg_drawn("file 1", 8, "512x384", "0,64,128,192,256,320,384,448", "0,64,128,192,256,320"))
# A Retina screen: still one screen pixel wide
mcu_grid_case("MG_retina_100", MG_420, MG_ON, mg_drawn("file 1", 1, "64x48", "0,16,32,48", "0,16,32", scale=2),
              extra="--scale 2")
mcu_grid_case("MG_retina_200", MG_420, MG_ON + ",zoom=200",
              mg_drawn("file 1", 2, "128x96", "0,32,64,96", "0,32,64", scale=2), extra="--scale 2")
mcu_grid_case("MG_retina_50", MG_420, MG_ON + ",zoom=50",
              mg_drawn("file 1", 0.5, "32x24", "0,8,16,24", "0,8,16", scale=2), extra="--scale 2")
# Another channel, and back
mcu_grid_case("MG_channel", MG_420, MG_ON + ",channel=Cb,grid", mg_drawn("grid", 1, "64x48", "0,16,32,48", "0,16,32"))
mcu_grid_case("MG_channel_back", MG_420, MG_ON + ",channel=Cb,channel=RGB",
              mg_drawn("file 1", 1, "64x48", "0,16,32,48", "0,16,32"))
# Later files: the grid stays on, with each file's MCUs; no image (a
# progressive JPEG), and a Photoshop file's image, which has no MCUs
MG_FILES = ("mg_white_444.jpg", "mg_white_420.jpg", "ok_progressive.jpg", "psd_rgb_rle.psd", "mg_white_422.jpg")
mcu_grid_case("MG_next_file", MG_FILES, MG_ON, mg_drawn("file 2", 1, "64x48", "0,16,32,48", "0,16,32"))
mcu_grid_case("MG_no_image", MG_FILES, MG_ON, "harness: mcu-grid file 3: checked=1 preview=none$")
mcu_grid_case("MG_psd", MG_FILES, MG_ON, mg_none("file 4", 1, 1, "37x23"))
mcu_grid_case("MG_after_psd", MG_FILES, MG_ON, mg_drawn("file 5", 1, "64x48", "0,16,32,48", MG_ROWS_8))
# Scrolling copies what the image window shows and paints only what comes into
# view, and making it larger paints only what it adds: the dots painted then
# must fall where painting it all again puts them. (On a Retina screen the
# window grab has one pixel per point.)
MG_BIG = ("mg_white_big.jpg",)


def mg_buffer(scroll, window, dots="[1-9][0-9]*"):
    return (rx(f"harness: mcu-grid buffer: scroll={scroll} window={window} buffer={window} grid pixels=") + dots +
            " changed by painting it all=0$")


mcu_grid_case("MG_scroll", MG_BIG, MG_ON + ",scroll=7;5,buffer", mg_buffer("7,5", "400x400"))
mcu_grid_case("MG_scroll_again", MG_BIG, MG_ON + ",scroll=7;5,scroll=40;33,buffer", mg_buffer("40,33", "400x400"))
mcu_grid_case("MG_scroll_back", MG_BIG, MG_ON + ",scroll=40;33,scroll=12;30,buffer", mg_buffer("12,30", "400x400"))
mcu_grid_case("MG_scroll_150", MG_BIG, MG_ON + ",zoom=150,scroll=101;77,buffer", mg_buffer("101,77", "400x400"))
mcu_grid_case("MG_scroll_50", MG_BIG, MG_ON + ",zoom=50,scroll=13;9,buffer", mg_buffer("13,9", "400x400"))
mcu_grid_case("MG_scroll_retina", MG_BIG, MG_ON + ",scroll=1;1,scroll=2;2,buffer", mg_buffer("2,2", "400x400"),
              extra="--scale 2")
mcu_grid_case("MG_scroll_retina_more", MG_BIG, MG_ON + ",scroll=2;2,scroll=7;5,buffer", mg_buffer("7,5", "400x400"),
              extra="--scale 2")
mcu_grid_case("MG_resize", MG_BIG, MG_ON + ",scroll=40;33,size=600;600,buffer", mg_buffer("40,33", "600x600"))
mcu_grid_case("MG_scroll_grid_off", MG_BIG, "zoom=100,scroll=7;5,buffer", mg_buffer("7,5", "400x400", dots="0"))


# --- The red line where the image ends inside its MCUs --------------------------
# Black images, so that the line's red and white pixels and the grid's dark
# grey ones all differ from the image: 61x45 inside MCUs of 8x8 (4:4:4 and
# grayscale) and 16x16 (4:2:0), both 64x48; 61x48 and 64x45, which end inside
# MCUs one way only; 64x48, which fills them; and 1021x1019, to scroll
BLACK = (0, 0, 0)
write("ie_444.jpg", jpeg(Image.new("RGB", (61, 45), BLACK), quality=95, subsampling=0))
write("ie_420.jpg", jpeg(Image.new("RGB", (61, 45), BLACK), quality=95, subsampling=2))
write("ie_gray.jpg", jpeg(Image.new("L", (61, 45), 0), quality=95))
write("ie_right.jpg", jpeg(Image.new("RGB", (61, 48), BLACK), quality=95, subsampling=0))
write("ie_bottom.jpg", jpeg(Image.new("RGB", (64, 45), BLACK), quality=95, subsampling=0))
write("ie_full.jpg", jpeg(Image.new("RGB", (64, 48), BLACK), quality=95, subsampling=0))
write("ie_big.jpg", jpeg(Image.new("RGB", (1021, 1019), BLACK), quality=95, subsampling=2))


def end_case(cid, names, steps, expect, extra="", forbid="-"):
    case(cid, "IE", "gui", list(names), ("--mcu-grid " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def ie_line(when, zoom, screen, right, bottom, red, white, scale=1, grid=0):
    """The line's report: right and bottom are (place, from, to[, first red]) or None"""
    def part(where, along):
        if where is None:
            return "none"
        at, lo, hi, first = (where + (0,))[:4]
        return f"{at} {along}={lo}-{hi} every=3 from={first}"
    return rx(f"harness: image-end {when}: zoom={zoom} scale={scale} screen={screen} right={part(right, 'rows')} "
              f"bottom={part(bottom, 'cols')} red={red} white={white} stray=0 grid={grid} other=0") + "$"


IE_444 = ("ie_444.jpg",)
# Just right of the image's own width, as tall as its own height, and just
# below it, as wide (not at the corner between them); red on every third
# pixel from the top or the left, white between; with the grid off. MCUs of
# 8x8, 16x16 and a grayscale image's 8x8.
IE_61X45 = {"right": (61, 0, 44), "bottom": (45, 0, 60), "red": 36, "white": 70}
end_case("IE_444", IE_444, "zoom=100", ie_line("file 1", 1, "64x48", **IE_61X45))
end_case("IE_420", ("ie_420.jpg",), "zoom=100", ie_line("file 1", 1, "64x48", **IE_61X45))
end_case("IE_gray", ("ie_gray.jpg",), "zoom=100", ie_line("file 1", 1, "64x48", **IE_61X45))
# Inside the MCUs one way only: one line; none for an image that fills them
end_case("IE_right_only", ("ie_right.jpg",), "zoom=100",
         ie_line("file 1", 1, "64x48", (61, 0, 47), None, red=16, white=32))
end_case("IE_bottom_only", ("ie_bottom.jpg",), "zoom=100",
         ie_line("file 1", 1, "64x48", None, (45, 0, 63), red=22, white=42))
end_case("IE_full", ("ie_full.jpg",), "zoom=100", ie_line("file 1", 1, "64x48", None, None, red=0, white=0))
# Each zoom, also 12.5% (where there is no grid), the places cut short to
# whole screen pixels as Windows' were (61 x 1.5 = 91.5 is 91)
end_case("IE_zoom_12", IE_444, "zoom=12.5", ie_line("file 1", 0.125, "8x6", (7, 0, 4), (5, 0, 6), red=5, white=7))
end_case("IE_zoom_25", IE_444, "zoom=25", ie_line("file 1", 0.25, "16x12", (15, 0, 10), (11, 0, 14), red=9, white=17))
end_case("IE_zoom_50", IE_444, "zoom=50", ie_line("file 1", 0.5, "32x24", (30, 0, 21), (22, 0, 29), red=18, white=34))
end_case("IE_zoom_150", IE_444, "zoom=150",
         ie_line("file 1", 1.5, "96x72", (91, 0, 66), (67, 0, 90), red=54, white=104))
end_case("IE_zoom_200", IE_444, "zoom=200",
         ie_line("file 1", 2, "128x96", (122, 0, 89), (90, 0, 121), red=71, white=141))
end_case("IE_zoom_800", IE_444, "zoom=800",
         ie_line("file 1", 8, "512x384", (488, 0, 359), (360, 0, 487), red=283, white=565))
# A Retina screen: still one screen pixel wide, in the same screen pixels
end_case("IE_retina_100", IE_444, "zoom=100", ie_line("file 1", 1, "64x48", scale=2, **IE_61X45), extra="--scale 2")
end_case("IE_retina_200", IE_444, "zoom=200",
         ie_line("file 1", 2, "128x96", (122, 0, 89), (90, 0, 121), red=71, white=141, scale=2), extra="--scale 2")
end_case("IE_retina_50", IE_444, "zoom=50",
         ie_line("file 1", 0.5, "32x24", (30, 0, 21), (22, 0, 29), red=18, white=34, scale=2), extra="--scale 2")
# The grid is drawn over it, as on Windows: its dots cover the 8 places where
# its columns cross the bottom line (3 of them red); the grid is unchanged
end_case("IE_grid_over", IE_444, "zoom=100,toggle",
         ie_line("file 1", 1, "64x48", (61, 0, 44), (45, 1, 60, 3), red=33, white=65, grid=254))
end_case("IE_grid_unchanged", IE_444, "zoom=100,toggle",
         rx("harness: mcu-grid file 1: checked=1 zoom=1 scale=1 screen=64x48 cols=0,8,16,24,32,40,48,56 "
            "rows=0,8,16,24,32,40 gap=3 colours=202020 stray=0") + "$")
end_case("IE_grid_12", IE_444, "zoom=12.5,toggle",
         ie_line("file 1", 0.125, "8x6", (7, 0, 4), (5, 0, 6), red=5, white=7))
# Another channel, and back
end_case("IE_channel", IE_444, "zoom=100,channel=Cb,end", ie_line("end", 1, "64x48", **IE_61X45))
end_case("IE_channel_back", IE_444, "zoom=100,channel=Cb,channel=RGB", ie_line("file 1", 1, "64x48", **IE_61X45))
# Each file's own: gone for one that fills its MCUs and a Photoshop file's
# image (no MCUs), and back after them
IE_FILES = ("ie_444.jpg", "ie_full.jpg", "psd_rgb_rle.psd", "ie_right.jpg")
end_case("IE_next_full", IE_FILES, "zoom=100", ie_line("file 2", 1, "64x48", None, None, red=0, white=0))
end_case("IE_psd", IE_FILES, "zoom=100", ie_line("file 3", 1, "37x23", None, None, red=0, white=0))
end_case("IE_after_psd", IE_FILES, "zoom=100", ie_line("file 4", 1, "64x48", (61, 0, 47), None, red=16, white=32))
# Scrolled into view (only what comes into view is painted) or shown by
# making the window larger: as painting it all again draws it
IE_BIG = ("ie_big.jpg",)
IE_BUFFER = "harness: image-end buffer: red pixels=[1-9][0-9]*$"
IE_REPAINTED = "changed by painting it all=([1-9]|-)"
end_case("IE_big", IE_BIG, "zoom=100", ie_line("file 1", 1, "1024x1024", (1021, 0, 1018), (1019, 0, 1020),
                                               red=681, white=1359))
end_case("IE_scroll", IE_BIG, "zoom=100,scroll=640;640,scroll=9999;9999,buffer", IE_BUFFER, forbid=IE_REPAINTED)
end_case("IE_scroll_150", IE_BIG, "zoom=150,scroll=1100;1100,scroll=9999;9999,buffer", IE_BUFFER,
         forbid=IE_REPAINTED)
end_case("IE_scroll_50", IE_BIG, "zoom=50,scroll=100;100,scroll=9999;9999,buffer", IE_BUFFER, forbid=IE_REPAINTED)
end_case("IE_scroll_grid", IE_BIG, "zoom=100,toggle,scroll=640;640,scroll=9999;9999,buffer", IE_BUFFER,
         forbid=IE_REPAINTED)
end_case("IE_resize", IE_BIG, "zoom=100,scroll=9999;9999,size=600;600,buffer", IE_BUFFER, forbid=IE_REPAINTED)
# Scrolled back a little with it in view: the dots copied must fall where the
# ones painted next to them do
end_case("IE_scroll_back", IE_BIG, "zoom=100,scroll=9999;9999,scroll=642;653,buffer", IE_BUFFER, forbid=IE_REPAINTED)
end_case("IE_scroll_back_grid", IE_BIG, "zoom=100,toggle,scroll=9999;9999,scroll=642;653,buffer", IE_BUFFER,
         forbid=IE_REPAINTED)
end_case("IE_scroll_back_150", IE_BIG, "zoom=150,scroll=9999;9999,scroll=1154;1186,buffer", IE_BUFFER,
         forbid=IE_REPAINTED)


# --- View > Toolbar and Status Bar ----------------------------------------------
def bars_case(cid, steps, expect, names=("mg_white_420.jpg",), extra="", forbid="-"):
    case(cid, "VB", "gui", list(names), ("--bars " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


def vb_state(toolbar, status):
    return f"toolbar={toolbar} checked={toolbar} statusbar={status} checked={status} image-statusbar={status}"


def vb_step(step, toolbar, status, report, view):
    return rx(f"harness: bars {step}: {vb_state(toolbar, status)} report={report} image-view={view}") + "$"


# The menu items, where Windows has them, with Windows' tips; both shown and
# checked at first
bars_case("VB_menu_toolbar", "statusbar", rx('harness: bars &Toolbar menu=&View after="Overlays" separator=1 '
          'enabled=1 tip="Show or hide the toolbar" checkable=1') + "$")
bars_case("VB_menu_status", "statusbar", rx('harness: bars &Status Bar menu=&View after="&Toolbar" separator=0 '
          'enabled=1 tip="Show or hide the status bar" checkable=1') + "$")
bars_case("VB_at_first", "statusbar", rx("harness: bars at first: " + vb_state(1, 1)) + "$")
# The main window's status bar holds only the messages (it had a label that
# always said "1234"); the MCU, file position and YCC are in the image window's
VB_WIDGETS = "harness: bars widgets: main="
bars_case("VB_main_widgets", "widgets", "^" + VB_WIDGETS + "none image=")
bars_case("VB_main_widgets_before_file", "widgets", "^" + VB_WIDGETS + "none image=", extra="--bars-at 0")
bars_case("VB_main_widgets_shown_again", "statusbar,statusbar,widgets", "^" + VB_WIDGETS + "none image=")
bars_case("VB_image_widgets", "widgets",
          '^harness: bars widgets: .* image=mcuLabel="[^"]*"\\|fileLabel="[^"]*"\\|yccLabel="[^"]*"$')
# Status Bar: this window's and the image window's (which has the MCU, file
# position and YCC that Windows' one status bar had); the report and the
# image take the space
bars_case("VB_status_off", "statusbar", vb_step("statusbar", 1, 0, "taller", "taller"))
bars_case("VB_status_on", "statusbar,statusbar", vb_step("statusbar", 1, 1, "shorter", "shorter"))
# Toolbar: the report takes the space
bars_case("VB_toolbar_off", "toolbar", vb_step("toolbar", 0, 1, "taller", "same"))
bars_case("VB_toolbar_on", "toolbar,toolbar", vb_step("toolbar", 1, 1, "shorter", "same"))
bars_case("VB_both_off", "toolbar,statusbar", vb_step("statusbar", 0, 0, "taller", "taller"))
# The check mark follows the toolbar however it is hidden: the toolbar area's
# context menu (which names it Toolbar), or the toolbar itself
bars_case("VB_context_menu", "context", "harness: bars context menu: &Toolbar toolbar's=1$")
bars_case("VB_context_off", "context", vb_step("context", 0, 1, "taller", "same"))
bars_case("VB_context_on", "context,context", vb_step("context", 1, 1, "shorter", "same"))
bars_case("VB_hide_toolbar", "hide-toolbar", vb_step("hide-toolbar", 0, 1, "taller", "same"))
bars_case("VB_hide_then_menu", "hide-toolbar,toolbar", vb_step("toolbar", 1, 1, "shorter", "same"))
# Before any file is open (the image window isn't shown yet), and later files
bars_case("VB_before_file", "statusbar,toolbar", vb_step("toolbar", 0, 0, "taller", "not shown"),
          extra="--bars-at 0")
bars_case("VB_before_file_image", "statusbar", rx("harness: bars file 1: " + vb_state(1, 0)) + "$",
          extra="--bars-at 0")
bars_case("VB_next_file", "statusbar,toolbar", rx("harness: bars file 2: " + vb_state(0, 0)) + "$",
          names=("mg_white_420.jpg", "mg_white_444.jpg"))


# --- File > Print Setup ----------------------------------------------------------
# The shared printer writes a PDF (switching to one makes its paper A4): no
# printer and no system panel are ever used
def print_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "PS", "gui", list(names), ("--print " + steps + " " + extra).strip(), timeout=60, expect=expect,
         forbid=forbid)


def ps_pdf(step, orientation, size, pages="[0-9]+", paper="A4", rng="all"):
    w, h = size
    return (rx(f"harness: print {step}: format=pdf orientation={orientation} paper={paper} range={rng} ") +
            "margins=[^ ]+ " + rx("pdf pages=") + pages + rx(f" page={w}x{h} ({orientation})") + "$")


def ps_state(step, fmt, orientation, paper, margins="[^ ]+"):
    return (rx(f"harness: print {step}: format={fmt} orientation={orientation} paper={paper} range=all ") +
            "margins=" + margins + "$")


PS_SIZES = "A3|A4|A5|B4|B5|Letter / ANSI A|Legal|Tabloid / ANSI B|Executive (7.5 x 10 in)"


def ps_pdf_dialog(paper, orientation):
    return rx(f'harness: print pdf setup dialog: title="Print Setup" paper={paper} orientation={orientation} '
              f'sizes={PS_SIZES} note="Printing makes a PDF file, which the system\'s page layout panel doesn\'t '
              f'set up. These set its pages."') + "$"


PS_A4 = (595, 842)
PS_A4_WIDE = (842, 595)
PS_MANY = "([2-9]|[1-9][0-9]+)"
# The menu item, where Windows has it, with MFC's tip
print_case("PS_menu", "setup=cancel", rx('harness: print menu=&File after="Print Preview" separator=0 enabled=1 '
           'tip="Change the printer and printing options"') + "$")
# Its dialog edits the printer Print and Print Preview use, for the session.
# For a printer that makes PDF files (no printer set up, or after Save as PDF
# in the print panel), this version's own: Qt's macOS dialog, the system's
# page layout panel, does nothing for one (the driver's answer does the same).
# It starts from the printer's paper and orientation, and keeps its margins
# and its file.
print_case("PS_pdf_dialog", "setup=cancel", ps_pdf_dialog("A4", "portrait"))
print_case("PS_pdf_dialog_current", "setup=landscape,setup=letter,setup=cancel",
           ps_pdf_dialog("Letter / ANSI A", "landscape"))
print_case("PS_landscape", "setup=landscape", ps_state("setup=landscape", "pdf", "landscape", "A4"))
print_case("PS_pdf_keeps", "setup=landscape,setup=letter", ps_state("setup=letter", "pdf", "landscape",
           "Letter / ANSI A", rx("3.5,3.5,3.5,3.5mm")))
# The system's dialog for this Mac's printer (answered by the driver). A4
# first: the printer's own paper differs from Mac to Mac.
print_case("PS_native_dialog", "printer=native,setup=landscape", "harness: print setup dialog: same printer=1$")
print_case("PS_native_paper", "printer=native,setup=a4", ps_state("setup=a4", "native", "portrait", "A4"))
print_case("PS_native", "printer=native,setup=a4,setup=landscape", ps_state("setup=landscape", "native", "landscape",
           "A4"))
print_case("PS_native_preview", "printer=native,setup=landscape,preview",
           "harness: print preview: same printer=1 orientation=landscape pages=" + PS_MANY + "$")
# Save as PDF in the print panel makes the printer one that makes PDF files:
# Print Setup then shows its own dialog, starting from the printer's paper as
# it is then (Qt makes it A4, the PDF default, unless it was set)
PS_SAVE_PDF = "printer=native,print=save-pdf"
print_case("PS_save_pdf", PS_SAVE_PDF + ",setup=landscape", ps_pdf_dialog("A4", "portrait"))
print_case("PS_save_pdf_print", PS_SAVE_PDF + ",setup=landscape,print", ps_pdf("print", "landscape", PS_A4_WIDE,
                                                                               PS_MANY))
print_case("PS_save_pdf_set", "printer=native,setup=letter,print=save-pdf,setup=landscape",
           ps_pdf_dialog("Letter / ANSI A", "portrait"))
print_case("PS_landscape_print", "setup=landscape,print", ps_pdf("print", "landscape", PS_A4_WIDE, PS_MANY))
print_case("PS_landscape_preview", "setup=landscape,preview",
           "harness: print preview: same printer=1 orientation=landscape pages=" + PS_MANY + "$")
# Each of two jobs (told apart by their labels)
for n in (1, 2):
    print_case(f"PS_landscape_again_{n}", "setup=landscape,print#1,print#2",
               ps_pdf(f"print#{n}", "landscape", PS_A4_WIDE, PS_MANY), forbid="page=595x842")
print_case("PS_portrait_back", "setup=landscape,setup=portrait,print", ps_pdf("print", "portrait", PS_A4, PS_MANY))
print_case("PS_letter", "setup=letter,print", ps_pdf("print", "portrait", (612, 792), PS_MANY, paper="Letter / ANSI A"))
print_case("PS_letter_landscape", "setup=letter,setup=landscape,print",
           ps_pdf("print", "landscape", (792, 612), PS_MANY, paper="Letter / ANSI A"))
print_case("PS_cancel", "setup=landscape,setup=cancel,print", ps_pdf("print", "landscape", PS_A4_WIDE, PS_MANY))
print_case("PS_cancel_first", "setup=cancel,print", ps_pdf("print", "portrait", PS_A4, PS_MANY))
# Print and Print Preview share it too: what Print's dialog sets, Preview uses
print_case("PS_print_printer", "print", 'harness: print dialog: title="Print Document" same printer=1 ')
print_case("PS_preview_printer", "preview", "harness: print preview: same printer=1 ")
print_case("PS_print_to_preview", "print=landscape,preview", "harness: print preview: same printer=1 "
           "orientation=landscape ")
print_case("PS_print_cancel", "print=cancel", "harness: print print=cancel: .* pdf=none$")
# A range chosen for one job isn't kept for the next
print_case("PS_selection_offered", "select=200,print", "harness: print dialog: .* selection offered=1 range=all$")
print_case("PS_selection_not_offered", "select-none,print", "harness: print dialog: .* selection offered=0 range=all$")
print_case("PS_selection", "select=200,print=selection", ps_pdf("print=selection", "portrait", PS_A4, "1",
           rng="selection"))
print_case("PS_selection_next", "select=200,print=selection,select-none,print",
           ps_pdf("print", "portrait", PS_A4, PS_MANY))
print_case("PS_selection_preview", "select=200,print=selection,preview",
           "harness: print preview: same printer=1 orientation=portrait pages=" + PS_MANY + "$")
# Before any file is open
print_case("PS_before_file", "setup=landscape,print", ps_pdf("print", "landscape", PS_A4_WIDE, "1"),
           extra="--print-at 0")


# --- File > Recent File --------------------------------------------------------
# The steps run before the file given is opened (--recent-at 0) unless said.
# A space in them is percent-encoded (the runner splits the flags at spaces).
def recent_case(cid, steps, expect, names=("ok_black.jpg",), extra="--recent-at 0", forbid="-"):
    case(cid, "RF", "gui", list(names), ("--recent " + steps.replace(" ", "%20") + " " + extra).strip(), timeout=30,
         expect=expect, forbid=forbid)


def rf_item(n, name):
    return f"&{n} [^|]*" + rx(f"{name} (tip {name})")


def rf_menu(*names):
    return "harness: recent menu: " + " \\| ".join(rf_item(n, name) for n, name in enumerate(names, 1)) + "$"


def rf_stored(*names):
    return rx("harness: recent stored: " + (",".join(names) if names else "none")) + "$"


def rf_name(shown):
    return rx(f'harness: recent name: "{shown}" chars={len(shown)}') + "$"


# The placeholder, greyed, where Windows has it, while there are none
recent_case("RF_place", "menu", rx('harness: recent menu=&File after="Print Setup..." separator=1 enabled=0 tip=""') +
            "$")
recent_case("RF_empty", "menu", rx("harness: recent menu: Recent File (off)") + "$")
recent_case("RF_empty_stored", "menu", rf_stored())
# A file opened goes first, once; four at most (MFC's), the oldest dropped
recent_case("RF_open", "open=ok_444.jpg", rf_menu("ok_444.jpg"))
recent_case("RF_open_stored", "open=ok_444.jpg", rf_stored("ok_444.jpg"))
recent_case("RF_order", "open=ok_444.jpg,open=ok_420.jpg,open=ok_422.jpg",
            rf_menu("ok_422.jpg", "ok_420.jpg", "ok_444.jpg"))
RF_FIVE = "open=ok_444.jpg,open=ok_420.jpg,open=ok_422.jpg,open=ok_gray.jpg,open=ok_small.jpg"
recent_case("RF_max", RF_FIVE, rf_menu("ok_small.jpg", "ok_gray.jpg", "ok_422.jpg", "ok_420.jpg"))
recent_case("RF_max_stored", RF_FIVE, rf_stored("ok_small.jpg", "ok_gray.jpg", "ok_422.jpg", "ok_420.jpg"))
# (Taking one off doesn't bring back one dropped before)
recent_case("RF_max_then_gone", RF_FIVE.replace("open=ok_small.jpg", "open-copy=ok_small.jpg:small.jpg") +
            ",gone=small.jpg,choose=1", rf_stored("ok_gray.jpg", "ok_422.jpg", "ok_420.jpg"))
recent_case("RF_again", "open=ok_444.jpg,open=ok_420.jpg,open=ok_444.jpg", rf_stored("ok_444.jpg", "ok_420.jpg"))
recent_case("RF_same_file", "open=ok_444.jpg,open=../files/ok_444.jpg", rf_stored("ok_444.jpg"))
recent_case("RF_same_file_link", "open=ok_444.jpg,link=ok_444.jpg:other.jpg", rf_stored("other.jpg"))
# The files given are opened as File > Open does
recent_case("RF_given", "menu", rf_menu("ok_444.jpg", "ok_black.jpg"), names=("ok_black.jpg", "ok_444.jpg"),
            extra="--recent-at 2")
# Choosing an item opens it again; one that can't be opened any more says so
# and is taken off, as MFC did
recent_case("RF_choose", "open=ok_444.jpg,open=ok_420.jpg,choose=2",
            "harness: recent chose 2: report file=ok_444\\.jpg$")
recent_case("RF_choose_first", "open=ok_444.jpg,open=ok_420.jpg,choose=2", rf_stored("ok_444.jpg", "ok_420.jpg"))
RF_GONE = "open-copy=ok_444.jpg:copy.jpg,open=ok_420.jpg,gone=copy.jpg,choose=2"
recent_case("RF_choose_gone", RF_GONE, "harness: message box: ERROR: Couldn't open file: \\[/.*/copy\\.jpg\\]$")
recent_case("RF_choose_gone_off", RF_GONE, rf_stored("ok_420.jpg"))
recent_case("RF_choose_back", RF_GONE.replace(",choose=2", ",back=copy.jpg,choose=2"), rf_stored("copy.jpg", "ok_420.jpg"))
# Not a file that can't be opened; an empty file, as MFC (it opens); Reprocess
# changes nothing
recent_case("RF_not_added", "open=nosuch.jpg", "harness: recent open nosuch.jpg: opened=0$",
            forbid="harness: recent (stored|menu): .*nosuch")
recent_case("RF_empty_file", "open=dq_empty.bin", rf_stored("dq_empty.bin"))
recent_case("RF_reprocess", "open=ok_444.jpg,open=ok_420.jpg,reprocess", rf_stored("ok_420.jpg", "ok_444.jpg"))
# The list a previous start saved, a file gone from it kept until chosen
RF_PREV = "--recent-at 0 --recent-stored ok_444.jpg+nosuch.jpg+ok_420.jpg"
recent_case("RF_start", "menu", rf_menu("ok_444.jpg", "nosuch.jpg", "ok_420.jpg"), extra=RF_PREV)
recent_case("RF_start_gone", "choose=2", rf_menu("ok_444.jpg", "ok_420.jpg"), extra=RF_PREV)
# How an item names a file: the home folder as "~"; a long path shortened in
# the middle, as MFC did; a long name alone; "&" doubled
recent_case("RF_name_short", "name=/tmp/a.jpg", rf_name("/tmp/a.jpg"))
recent_case("RF_name_home", "name=~/Pictures/a.jpg", rf_name("~/Pictures/a.jpg"))
recent_case("RF_name_long", "name=/Volumes/Photos/2026/November/Holiday in the mountains/Day 3/IMG_1234.jpg",
            rf_name("/\u2026/Holiday in the mountains/Day 3/IMG_1234.jpg"))
recent_case("RF_name_home_long", "name=~/Pictures/2026/November/Holiday in the mountains/Day 3/IMG_1234.jpg",
            rf_name("~/\u2026/Holiday in the mountains/Day 3/IMG_1234.jpg"))
# 50 characters is kept whole; 51 is shortened
recent_case("RF_name_50", "name=/Volumes/Photos/" + "a" * 30 + ".jpg", rf_name("/Volumes/Photos/" + "a" * 30 + ".jpg"))
recent_case("RF_name_51", "name=/Volumes/Photos/" + "a" * 31 + ".jpg", rf_name("/\u2026/Photos/" + "a" * 31 + ".jpg"))
recent_case("RF_name_only", "name=~/Pictures/A very long file name that is more than fifty characters long.jpg",
            rf_name("A very long file name that is more than fifty characters long.jpg"))
recent_case("RF_name_amp", "name=/tmp/R%26D.jpg", rf_name("/tmp/R&&D.jpg"))
recent_case("RF_amp_item", "open-copy=ok_444.jpg:long folder name one/long folder name two/R%26D photo.jpg",
            "harness: recent menu: &1 [^|]*/R&&D photo\\.jpg \\(tip R&D photo\\.jpg\\)$")


# --- The toolbar's New button ----------------------------------------------------
def new_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "NB", "gui", list(names), ("--new " + steps + " " + extra).strip(), timeout=30, expect=expect,
         forbid=forbid)


NB_FILE_ITEMS = ("Save Log|Reprocess File|Offset|Image Search Fwd|Image Search Rev|Lookup MCU Offset|File Overlay|"
                 "Export JPEG|Export TIFF|Add Camera/SW to DB")


def nb_start(step, recent):
    """What is shown at the start, with no file"""
    return rx(f"harness: new {step}: report=empty image-window=hidden preview=none histograms=empty file-items=none "
              f"select-all=0 find=0 dqt=1 recent={recent}") + "$"


# The button, first on the toolbar, with Windows' tip and Ctrl+N (Cmd+N on
# macOS); in no menu, as on Windows
new_case("NB_button", "button", rx('harness: new button: toolbar=File position=1 tip="Create a new document" '
         'tooltip="New" shortcut=Ctrl+N menus=none') + "$")
# No file shown, as at the start (the item that needs no file stays on, and
# File > Recent File is kept)
new_case("NB_like_start", "button", nb_start("button", 1))
new_case("NB_start_state", "button", nb_start("at first", 0), extra="--new-at 0")
new_case("NB_before_file", "button", nb_start("button", 0), extra="--new-at 0")
new_case("NB_histograms", "button", "harness: new button: .* histograms=empty ", extra="--histo")
new_case("NB_histograms_before", "button", "harness: new at first: .* histograms=drawn ", extra="--histo")
new_case("NB_reprocess", "button,reprocess", nb_start("reprocess", 1))
new_case("NB_open_after", "button,open=ok_420.jpg", "harness: new open=ok_420.jpg: report=[0-9]+ lines "
         "image-window=shown preview=64x48 histograms=empty file-items=" + rx(NB_FILE_ITEMS) + " select-all=1 find=1 "
         "dqt=1 recent=2$")
# Its key, also while the toolbar is hidden (as Windows' Ctrl+N), and in the
# image window, which was part of Windows' main window
new_case("NB_key", "key", nb_start("key", 1))
new_case("NB_key_hidden_toolbar", "hide-toolbar,key", nb_start("key", 1))
new_case("NB_key_image", "key-image", nb_start("key-image", 1))
new_case("NB_key_image_hidden_toolbar", "hide-toolbar,key-image", nb_start("key-image", 1))



# --- Opening a file from outside the app: a drop, Finder, the command line -----------
def outside_case(cid, steps, expect, names=("ok_444.jpg",), extra="", forbid="-"):
    case(cid, "OF", "gui", list(names), ("--outside " + steps + " " + extra).strip(), timeout=60, expect=expect,
         forbid=forbid)


def of_state(step, report, recent, window="normal", active=None, when=""):
    """What a step leaves: the file the report is of, File > Recent File's files, the main window"""
    return (rx(f"harness: outside {step}: {when}report={report} lines=") + "[0-9]+" +
            rx(f" recent={recent} window={window} ") + ("active=" + rx(active) + "$" if active else "active="))


# A file name with a space, an accent and "&" (dropped and sent as a URL)
write("of photo \u00e9 & 1.jpg", c420)
OF_NAME = "of%20photo%20%C3%A9%20%26%201.jpg"
# Big enough that the decoder lets the window repaint more than once while it
# decodes it
of_rnd = random.Random(2026)
write("of_big.jpg", jpeg(Image.frombytes("RGB", (2400, 2400), of_rnd.randbytes(2400 * 2400 * 3)), quality=90,
                         subsampling=2))

# Dropped on the report: opened as File > Open opens one (also put first in
# File > Recent File), taken as a copy; the first of several; also before any
# file, an empty file (it opens), a name with a space, an accent and "&"
outside_case("OF_drop", "drop=ok_420.jpg", of_state("drop=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg"))
outside_case("OF_drop_copy", "drop=ok_420.jpg", rx("harness: outside drop=ok_420.jpg: drag=copy drop=copy") + "$")
outside_case("OF_drop_first", "drop=ok_gray.jpg+ok_420.jpg",
             of_state("drop=ok_gray.jpg+ok_420.jpg", "ok_gray.jpg", "ok_gray.jpg|ok_444.jpg"))
outside_case("OF_drop_before_file", "drop=ok_420.jpg", of_state("drop=ok_420.jpg", "ok_420.jpg", "ok_420.jpg"),
             extra="--outside-at 0")
outside_case("OF_drop_empty_file", "drop=dq_empty.bin",
             rx('harness: outside drop=dq_empty.bin: report="ERROR: File length is zero, no decoding done. [') + ".*" +
             rx('/dq_empty.bin]" lines=1 recent=dq_empty.bin|ok_444.jpg '))
outside_case("OF_drop_name", "drop=" + OF_NAME,
             of_state("drop=" + OF_NAME, "of photo \u00e9 & 1.jpg", "of photo \u00e9 & 1.jpg|ok_444.jpg"))
# A folder can't be opened: the usual message, and it isn't added
outside_case("OF_drop_folder", "drop=dq_folder", "harness: message box: ERROR: Couldn't open file: \\[/.*/dq_folder\\]$")
outside_case("OF_drop_folder_not_added", "drop=dq_folder", "harness: outside drop=dq_folder: .* recent=ok_444\\.jpg ")
# Not taken: a drag that offers only to move the file, text, a web address; a
# drop anywhere but the report (the image window, the histograms), as on
# Windows; while a dialog is open
outside_case("OF_drop_move_only", "drop-move=ok_420.jpg", rx("harness: outside drop-move=ok_420.jpg: drag=refused") + "$")
outside_case("OF_drop_move_only_kept", "drop-move=ok_420.jpg",
             of_state("drop-move=ok_420.jpg", "ok_444.jpg", "ok_444.jpg"))
outside_case("OF_drop_text", "drop-text=photo.jpg", rx("harness: outside drop-text=photo.jpg: drag=refused") + "$")
outside_case("OF_drop_web", "drop-web=https%3A//example.com/a.jpg",
             rx("harness: outside drop-web=https%3A//example.com/a.jpg: drag=refused") + "$")
outside_case("OF_drop_image_window", "drop-image=ok_420.jpg",
             rx("harness: outside drop-image=ok_420.jpg: drag=refused") + "$")
outside_case("OF_drop_histogram", "drop-histogram=ok_420.jpg",
             rx("harness: outside drop-histogram=ok_420.jpg: drag=refused") + "$")
outside_case("OF_drop_in_dialog", "in-dialog=drop=ok_420.jpg", rx("harness: outside drop=ok_420.jpg: drag=refused") + "$")
outside_case("OF_drop_in_dialog_kept", "in-dialog=drop=ok_420.jpg",
             of_state("in-dialog=drop=ok_420.jpg", "ok_444.jpg", "ok_444.jpg"))
# A drop doesn't bring the window forward (the image window stays active)
outside_case("OF_drop_not_front", "activate-image,drop=ok_420.jpg",
             of_state("drop=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="image"))

# Sent by Finder (Open With, a drop on the Dock icon): opened the same way;
# the first of several, as for a drop; before any file; the name; not a web
# address
outside_case("OF_finder", "finder=ok_420.jpg", of_state("finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg"))
outside_case("OF_finder_first", "finder=ok_gray.jpg+ok_420.jpg",
             of_state("finder=ok_gray.jpg+ok_420.jpg", "ok_gray.jpg", "ok_gray.jpg|ok_444.jpg"))
outside_case("OF_finder_before_file", "finder=ok_420.jpg", of_state("finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg"),
             extra="--outside-at 0")
outside_case("OF_finder_name", "finder=" + OF_NAME,
             of_state("finder=" + OF_NAME, "of photo \u00e9 & 1.jpg", "of photo \u00e9 & 1.jpg|ok_444.jpg"))
outside_case("OF_finder_web", "finder-url=https%3A//example.com/a.jpg",
             of_state("finder-url=https%3A//example.com/a.jpg", "ok_444.jpg", "ok_444.jpg"))
# It brings the main window forward, out of the Dock if minimized there
outside_case("OF_image_active", "activate-image", of_state("activate-image", "ok_444.jpg", "ok_444.jpg", active="image"))
outside_case("OF_finder_front", "activate-image,finder=ok_420.jpg",
             of_state("finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="main"))
outside_case("OF_minimized", "minimize", of_state("minimize", "ok_444.jpg", "ok_444.jpg", window="minimized"))
outside_case("OF_finder_minimized", "minimize,finder=ok_420.jpg",
             of_state("finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="main"))
# While a dialog is open it waits, and is opened once the dialog closes; a
# later file replaces one waiting
outside_case("OF_finder_in_dialog", "in-dialog=finder=ok_420.jpg",
             of_state("in-dialog=finder=ok_420.jpg", "ok_444.jpg", "ok_444.jpg", when="during the dialog: "))
outside_case("OF_finder_after_dialog", "in-dialog=finder=ok_420.jpg",
             of_state("in-dialog=finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="main"))
outside_case("OF_finder_later_in_dialog", "in-dialog=finder=ok_gray.jpg;finder=ok_420.jpg",
             of_state("in-dialog=finder=ok_gray.jpg;finder=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg"))

# While a file is being decoded (the decoder lets the window repaint), a file
# from Finder or dropped waits until it is done, and is opened then
OF_DECODE = "in-decode=of_big.jpg:"
for kind, step in (("finder", "finder=ok_420.jpg"), ("drop", "drop=ok_420.jpg")):
    outside_case(f"OF_{kind}_in_decode", OF_DECODE + step,
                 rx(f"harness: outside {OF_DECODE}{step}: during the decode=1 just after: same report=1 "
                    "report=of_big.jpg ") + ".*" + rx(" recent=of_big.jpg|ok_444.jpg "))
    outside_case(f"OF_{kind}_after_decode", OF_DECODE + step,
                 of_state(OF_DECODE + step, "ok_420.jpg", "ok_420.jpg|of_big.jpg|ok_444.jpg"))

# Named when the app is started (as on Windows: JPEGsnoop.exe <file>): opened
# once the window shows; the last of several; a name relative to the current
# folder; nothing with an option (Windows' command-line mode, not ported), or
# for an empty argument; a file that can't be opened says so
OF_START = "--outside-at 0"
outside_case("OF_args", "args=ok_420.jpg", of_state("args=ok_420.jpg", "ok_420.jpg", "ok_420.jpg"), extra=OF_START)
# (Not brought forward: shown with the image window active)
outside_case("OF_args_not_front", "activate-image,args=ok_420.jpg",
             of_state("args=ok_420.jpg", "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="image"))
outside_case("OF_args_last", "args=ok_gray.jpg+ok_420.jpg",
             of_state("args=ok_gray.jpg+ok_420.jpg", "ok_420.jpg", "ok_420.jpg"), extra=OF_START)
outside_case("OF_args_here", "args-here=ok_420.jpg", of_state("args-here=ok_420.jpg", "ok_420.jpg", "ok_420.jpg"),
             extra=OF_START)
outside_case("OF_args_name", "args=" + OF_NAME,
             of_state("args=" + OF_NAME, "of photo \u00e9 & 1.jpg", "of photo \u00e9 & 1.jpg"), extra=OF_START)
outside_case("OF_args_option", "args=-i+ok_420.jpg", of_state("args=-i+ok_420.jpg", "empty", "none"), extra=OF_START)
outside_case("OF_args_option_after", "args=ok_420.jpg+-o+log.txt",
             of_state("args=ok_420.jpg+-o+log.txt", "empty", "none"), extra=OF_START)
outside_case("OF_args_empty", "args=", of_state("args=", "empty", "none"), extra=OF_START)
outside_case("OF_args_empty_last", "args=ok_420.jpg+", of_state("args=ok_420.jpg+", "ok_420.jpg", "ok_420.jpg"),
             extra=OF_START)
outside_case("OF_args_missing", "args=nosuch.jpg",
             "harness: message box: ERROR: Couldn't open file: \\[/.*/nosuch\\.jpg\\]$", extra=OF_START)
outside_case("OF_args_missing_not_added", "args=nosuch.jpg", "harness: outside args=nosuch\\.jpg: .* recent=none ",
             extra=OF_START)
# ".." after a symbolic link to another folder: macOS goes up from where the
# link leads (Windows took ".." from the name alone, as its system does), so
# the file opened, shown and listed is the one there, not the one beside the
# link; "." is dropped; a name whose folder before ".." doesn't exist can't be
# opened (macOS can't open it either)
tree("of_link", {"real/of_target.jpg": c420, "real/sub/keep": b"", "of_target.jpg": c444})
try:
    os.symlink("real/sub", files_dir / "of_link/alias")
except FileExistsError:
    pass
OF_LINKED = "of_link/alias/../of_target.jpg"
OF_REAL = f"report=of_link/real/of_target.jpg size={len(c420)} recent=of_link/real/of_target.jpg"
outside_case("OF_args_link", f"args={OF_LINKED},paths", rx(f"harness: outside paths: {OF_REAL}") + "$", extra=OF_START)
outside_case("OF_args_here_link", "args-here=of_link/alias/.././of_target.jpg,paths",
             rx(f"harness: outside paths: {OF_REAL}") + "$", extra=OF_START)
outside_case("OF_args_link_twice", "args=of_link/real/../alias/../of_target.jpg,paths",
             rx(f"harness: outside paths: {OF_REAL}") + "$", extra=OF_START)
outside_case("OF_args_dot", "args-here=./of_link/./real//of_target.jpg,paths",
             rx(f"harness: outside paths: {OF_REAL}") + "$", extra=OF_START)
outside_case("OF_args_up_missing", "args=nosuch/../ok_420.jpg",
             "harness: message box: ERROR: Couldn't open file: \\[/.*/nosuch/\\.\\./ok_420\\.jpg\\]$", extra=OF_START)
# (File > Recent File lists a name opened with ".." in it the same way)
outside_case("OF_open_link", f"open={OF_LINKED},paths",
             rx(f"harness: outside paths: report=of_link/alias/../of_target.jpg size={len(c420)} "
                "recent=of_link/real/of_target.jpg") + "$", extra=OF_START)

# In File > Print or Print Setup, a file from Finder waits until it is done.
# Qt shows macOS's panels for them with no modal Qt window, and handles
# events just before Print's panel: opened then, the new file's report would
# be printed.
for kind in ("print", "setup"):
    step = f"in-{kind}=finder=ok_420.jpg"
    outside_case(f"OF_finder_in_{kind}", step,
                 rx(f"harness: outside {step}: during the panel: modal=0 report=ok_444.jpg lines=") + "[0-9]+" +
                 rx(" recent=ok_444.jpg "))
    outside_case(f"OF_finder_in_{kind}_kept", step,
                 rx(f"harness: outside {step}: the panel closed: same report=1") + "$")
    outside_case(f"OF_finder_after_{kind}", step,
                 of_state(step, "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="main"))
outside_case("OF_finder_in_print_printed", "in-print=finder=ok_420.jpg",
             rx("harness: outside in-print=finder=ok_420.jpg: printed: pdf pages=") + "[1-9]")
# Print Setup with the printer making PDFs, as with no printer set up (the
# cases above are then skipped: there is no page layout panel): this
# version's own dialog, modal
OF_PDF_SETUP = "in-pdf-setup=finder=ok_420.jpg"
outside_case("OF_finder_in_pdf_setup", OF_PDF_SETUP,
             rx(f"harness: outside {OF_PDF_SETUP}: during the dialog CPdfPageSetupDialog: modal=1 report=ok_444.jpg "
                "lines=") + "[0-9]+" + rx(" recent=ok_444.jpg "))
outside_case("OF_finder_in_pdf_setup_kept", OF_PDF_SETUP,
             rx(f"harness: outside {OF_PDF_SETUP}: the dialog closed: same report=1") + "$")
outside_case("OF_finder_after_pdf_setup", OF_PDF_SETUP,
             of_state(OF_PDF_SETUP, "ok_420.jpg", "ok_420.jpg|ok_444.jpg", active="main"))


# --- The toolbar's Copy and About buttons ------------------------------------------
# Windows' toolbar: New, Open, Save Log | Cut, Copy, Paste | Print, About. Copy
# and About are the Edit and Help items themselves, with Windows' tips; Cut and
# Paste (which could only change the read-only report) aren't added. The zoom
# follows, as before. gui --buttons prints the toolbar and each button.
def button_case(cid, extra, expect, names=("ok_444.jpg",), forbid="-"):
    case(cid, "TB", "gui", list(names), extra, timeout=30, expect=expect, forbid=forbid)


button_case("TB_layout", "--buttons",
            rx("harness: buttons: New|Open Image|Save Log|-|Copy|-|Print|About|-|Zoom In|Zoom Out") + "$")
button_case("TB_copy", "--buttons",
            rx('harness: button Copy: tip="Copy the selection and put it on the Clipboard" key=Ctrl+C menus=&Edit '
               "on=0 icon=1") + "$")
button_case("TB_about", "--buttons",
            rx('harness: button About: tip="About JPEGsnoop" key= menus=&Help on=1 icon=1') + "$")
# Clicked, Copy copies the selection, as Edit > Copy does (the whole report,
# part of it, with the same clipboard formats); without one it is off, and a
# click does nothing, also before any file
button_case("TB_copy_all", "--edit selectall,button:copy",
            "harness: edit button:copy: selected=all .* clipboard=report$")
button_case("TB_copy_part", "--edit selectall,key:shift-up,button:copy",
            "harness: edit button:copy: selected=part chars=[0-9]+ copy=1 .* clipboard=selection$")
button_case("TB_copy_html", "--edit selectall,button:copy",
            f"harness: edit clipboard formats=text/plain,text/html html colours={LIGHT_COLOURS} fonts=Courier "
            f"report colours={LIGHT_COLOURS}$")
button_case("TB_copy_off", "--edit button:copy",
            "harness: edit button:copy: selected=none chars=[0-9]+ copy=0 .* report=same clipboard=unchanged$")
button_case("TB_copy_before_file", "--edit button:copy --edit-before",
            "harness: edit button:copy: selected=none chars=[0-9]+ copy=0 .* clipboard=unchanged$")
# About opens the About box, as Help > About JPEGsnoop does
button_case("TB_about_opens", "--about-button",
            f"harness: about: About JPEGsnoop \\(JPEGsnoop / Version {ver_re} / Copyright [^ ]+ 2017 Calvin Hass / "
            "Qt 6\\.[0-9]+\\.[0-9]+\\)$")
button_case("TB_about_closes", "--about-button", "harness: about closed=1$")


# --- Removed: Check for Updates, Edit > Undo, View > Adjust, Submit signature ------
# Options > Check for Updates did nothing, Edit > Undo and View > Adjust were
# always greyed out, and Options > Configuration's "Submit signature to Online
# DB" sent nothing: Windows' update check and online database are no longer
# relevant. All are gone, with the update check's settings in Configuration
# and in the license terms shown at the first start, which are now shorter.
# gui --menus prints the menus; settings mode prints the two dialogs and which
# of the settings are stored.
def removed_case(cid, mode, files, flags, expect, forbid="-"):
    case(cid, "RM", mode, files, flags, timeout=30, expect=expect, forbid=forbid)


removed_case("RM_edit_menu", "gui", ["ok_444.jpg"], "--menus",
             rx("harness: menu &Edit: &Copy|Select All|-|Find...|Find Next") + "$")
removed_case("RM_view_menu", "gui", ["ok_444.jpg"], "--menus",
             rx("harness: menu &View: Image Chan|Image Zoom|Overlays|-|&Toolbar|&Status Bar") + "$")
removed_case("RM_options_menu", "gui", ["ok_444.jpg"], "--menus",
             rx("harness: menu &Options: DHT Expand|Hide Unknown EXIF Tags|Maker Notes|Signature Search|Relaxed Parsing|"
                "-|Scan Segment|-|Configuration...") + "$")
removed_case("RM_no_items", "gui", ["ok_444.jpg"], "--menus", "^harness: menu &Help: ",
             forbid="^harness: menu .*(Undo|Adjust|Check for Updates)")
removed_case("RM_config_dialog", "settings", "-", "-",
             rx("harness: config dialog groups=Directory for User Database|Processing Options|Report Settings|Help "
                "heights=87|60|60|60 below=13 checkboxes=Auto Reprocess files on change in Options (slower)") + "$")
removed_case("RM_license", "settings", "-", "-",
             rx("harness: license groups=Software Agreement: checkboxes=I have read and agree to the terms above "
                "below=10") + "$")
removed_case("RM_settings_not_stored", "settings", "-", "-", "^harness: removed settings stored=none$")


# --- CL: command-line mode ---------------------------------------------------------
# Started with an option ("-i", "-b", ...), the app analyzes files without its
# window and quits, as on Windows (master's CMyCommandParser, DoCmdLineCore()
# and CmdLineHelp()); it used to ignore the options. cmdline mode runs it as
# main() does, in a folder of its own holding copies of the files given, and
# prints the exit status and the files there, what was written to the
# standard output and error, and each log. OWN are cmdline's own options
# (--put DEST=NAME and the rest), ARGS the app's arguments.
def cl_case(cid, names, args, expect, forbid="-", own=""):
    case(cid, "CL", "cmdline", names, (own + " -- " + args).strip(), timeout=60, expect=expect, forbid=forbid)


def cl_status(status, files):
    return f"^harness: cmdline status={status} files={rx('|'.join(files))}$"


def cl_log(name, line):
    return f"^harness: cmdline log {rx(name)}: {line}$"


CL_OK = ["ok_420.jpg"]
CL_LOG = ["log.txt", "ok_420.jpg"]
CL_I = "-i ok_420.jpg -o log.txt"

# The command summary (master's), on the standard output; -h and -? as -help
cl_case("CL_help", CL_OK, "-help",
        "^harness: cmdline out:    -offset_pos <###>  : Decode from byte ### \\(decimal\\) in file$")
cl_case("CL_help_version", CL_OK, "-help", f"^harness: cmdline out: JPEGsnoop v{ver_re}$")
cl_case("CL_help_program", CL_OK, "-help",
        "^harness: cmdline out: " + rx("JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt <parameters>") + "$")
cl_case("CL_help_status", CL_OK, "-help", cl_status(0, CL_OK))
# (An unknown option shows the summary too, after an error)
cl_case("CL_help_h", CL_OK, "-h", cl_status(0, CL_OK), forbid="^harness: cmdline err:")
cl_case("CL_help_q", CL_OK, "-?", cl_status(0, CL_OK), forbid="^harness: cmdline err:")
cl_case("CL_help_first", CL_OK, "-help " + CL_I, cl_status(0, CL_OK))

# Arguments in error: the error, then the summary, and nothing done. (Master
# showed an unknown option in a message box, and ignored the rest.)
cl_case("CL_unknown_flag", CL_OK, "-x " + CL_I, "^harness: cmdline err: ERROR: Unknown command-line flag \\[-x\\]$")
cl_case("CL_unknown_flag_nothing_done", CL_OK, "-x " + CL_I, cl_status(1, CL_OK))
cl_case("CL_unknown_flag_help", CL_OK, "-x " + CL_I, "^harness: cmdline out:  Zero or more of the following")
cl_case("CL_unknown_flag_case", CL_OK, "-I ok_420.jpg", "^harness: cmdline err: ERROR: Unknown command-line flag \\[-I\\]$")
cl_case("CL_missing_value", CL_OK, "-i",
        "^harness: cmdline err: ERROR: Command-line flag \\[-i\\] needs a value after it$")
cl_case("CL_missing_value_o", CL_OK, "-i ok_420.jpg -o",
        "^harness: cmdline err: ERROR: Command-line flag \\[-o\\] needs a value after it$")
cl_case("CL_missing_value_status", CL_OK, "-i ok_420.jpg -o", cl_status(1, CL_OK))
cl_case("CL_offset_not_number", CL_OK, CL_I + " -offset_pos 12ab",
        "^harness: cmdline err: ERROR: Offset for command-line flag \\[-offset_pos\\] isn't a decimal number "
        "below 4294967296: \\[12ab\\]$")
cl_case("CL_offset_too_big", CL_OK, CL_I + " -offset_pos 4294967296",
        "^harness: cmdline err: ERROR: Offset for command-line flag \\[-offset_pos\\] isn't a decimal number")
cl_case("CL_offset_too_big_status", CL_OK, CL_I + " -offset_pos 4294967296", cl_status(1, CL_OK))
cl_case("CL_no_input", CL_OK, "-scan -o log.txt",
        "^harness: cmdline err: ERROR: No file to analyze: give -i <fname_in>, -b <dir> or -br <dir>$")
cl_case("CL_no_input_status", CL_OK, "-scan -o log.txt", cl_status(1, CL_OK))

# Only file names: the window opens the last (MainWindow::openArguments()).
# macOS's old "-psn_" argument isn't an option.
cl_case("CL_window_files_only", ["ok_420.jpg", "ok_444.jpg"], "ok_444.jpg ok_420.jpg",
        "^harness: cmdline window input=ok_420\\.jpg$")
cl_case("CL_window_psn", CL_OK, "-psn_0_1234 ok_420.jpg", "^harness: cmdline window input=ok_420\\.jpg$")

# -i: the report File > Open shows, saved to -o's log; nothing written to the
# terminal. A file named without an option is -i's; the last one counts.
cl_case("CL_file_log", CL_OK, CL_I, cl_log("log.txt", "  Filename: \\[ok_420\\.jpg\\]"))
cl_case("CL_file_log_offset", CL_OK, CL_I, cl_log("log.txt", "Start Offset: 0x00000000"))
cl_case("CL_file_log_assessment", CL_OK, CL_I, cl_log("log.txt", "  ASSESSMENT: Class [0-9] - .*"))
cl_case("CL_file_status", CL_OK, CL_I, cl_status(0, CL_LOG))
cl_case("CL_file_quiet", CL_OK, CL_I, cl_status(0, CL_LOG), forbid="^harness: cmdline (out|err):")
cl_case("CL_file_no_log", CL_OK, "-i ok_420.jpg", cl_status(0, CL_OK))
cl_case("CL_file_plain_name", CL_OK, "ok_420.jpg -o log.txt", cl_log("log.txt", "  Filename: \\[ok_420\\.jpg\\]"))
cl_case("CL_file_last", ["ok_420.jpg", "ok_444.jpg"], "-i ok_444.jpg -i ok_420.jpg -o log.txt",
        cl_log("log.txt", "  Filename: \\[ok_420\\.jpg\\]"))
cl_case("CL_file_dash_name", CL_OK, "-i -x.jpg -o log.txt", cl_log("log.txt", "  Filename: \\[-x\\.jpg\\]"),
        own="--put -x.jpg=ok_420.jpg")
cl_case("CL_file_dash_log", CL_OK, "-i ok_420.jpg -o -log.txt", cl_status(0, ["-log.txt", "ok_420.jpg"]))
cl_case("CL_file_unicode", CL_OK, "-i \u00e9t\u00e9.jpg -o log.txt",
        cl_log("log.txt", "  Filename: \\[\u00e9t\u00e9\\.jpg\\]"), own="--put \u00e9t\u00e9.jpg=ok_420.jpg")
cl_case("CL_file_beats_batch", CL_OK, "-b . " + CL_I, cl_status(0, CL_LOG))
cl_case("CL_file_log_folder", CL_OK, "-i ok_420.jpg -o out/log.txt", cl_status(0, ["ok_420.jpg", "out/log.txt"]))
cl_case("CL_file_empty", ["s_empty.jpg"], "-i s_empty.jpg -o log.txt",
        cl_log("log.txt", "ERROR: File length is zero, no decoding done\\. \\[s_empty\\.jpg\\]"))
# A file that isn't decoded, empty or over 4 GiB: an error, also without -o
# (the report said so, but the exit status was 0, and without -o nothing
# did). The size is in hex, as on Windows (it was decimal after "0x", in the
# window's report too).
cl_case("CL_file_empty_error", ["s_empty.jpg"], "-i s_empty.jpg",
        "^harness: cmdline err: ERROR: File length is zero, no decoding done\\. \\[s_empty\\.jpg\\]$")
cl_case("CL_file_empty_status", ["s_empty.jpg"], "-i s_empty.jpg", cl_status(1, ["s_empty.jpg"]))
CL_BIG = "--sparse big.jpg=4294967296"
cl_case("CL_file_too_large", CL_OK, "-i big.jpg -ext_all -done",
        "^harness: cmdline err: ERROR: File too large for this version of JPEGsnoop\\. \\[Size=0x100000000\\] "
        "\\[big\\.jpg\\]$", own=CL_BIG)
cl_case("CL_file_too_large_status", CL_OK, "-i big.jpg -ext_all -done", cl_status(1, ["big.jpg", "ok_420.jpg"]),
        own=CL_BIG)
cl_case("CL_file_too_large_log", CL_OK, "-i big.jpg -o log.txt",
        cl_log("log.txt", "ERROR: File too large for this version of JPEGsnoop\\. \\[Size=0x100000000\\]"), own=CL_BIG)
cl_case("CL_batch_too_large", CL_OK, "-b .",
        "^harness: cmdline err: ERROR: File too large for this version of JPEGsnoop\\. \\[Size=0x100000000\\] "
        "\\[\\./big\\.jpg\\]$", own=CL_BIG)
with open(files_dir / "cl_big.jpg", "wb") as f_big:
    f_big.truncate(4294967296)
case("CL_gui_too_large", "CL", "gui", ["cl_big.jpg"], "--log", timeout=60,
     expect="^ERROR: File too large for this version of JPEGsnoop\\. \\[Size=0x100000000\\]$")
# A file that can't be read: said so, with the log saying it too (as
# master's did)
cl_case("CL_file_missing", CL_OK, "-i nosuch.jpg -o log.txt",
        "^harness: cmdline err: ERROR: Couldn't open file \\[nosuch\\.jpg\\]: No such file or directory$")
cl_case("CL_file_missing_log", CL_OK, "-i nosuch.jpg -o log.txt",
        cl_log("log.txt", "ERROR: Couldn't open file: \\[nosuch\\.jpg\\]"))
cl_case("CL_file_missing_status", CL_OK, "-i nosuch.jpg -o log.txt", cl_status(1, CL_LOG))
cl_case("CL_file_unreadable", CL_OK, CL_I,
        "^harness: cmdline err: ERROR: Couldn't open file \\[ok_420\\.jpg\\]: Permission denied$", own="--chmod 000:ok_420.jpg")
cl_case("CL_file_folder", CL_OK, "-i sub -o log.txt", "^harness: cmdline err: ERROR: Couldn't open file \\[sub\\]: ",
        own="--mkdir sub")
# A log that would replace the file analyzed (master only checked in its
# debug build), by its name or through a link; one that can't be written
cl_case("CL_log_same_file", CL_OK, "-i ok_420.jpg -o ok_420.jpg",
        "^harness: cmdline err: ERROR: The log \\[ok_420\\.jpg\\] would replace the file analyzed \\[ok_420\\.jpg\\]$")
cl_case("CL_log_same_file_kept", CL_OK, "-i ok_420.jpg -o ok_420.jpg",
        f"^harness: cmdline file ok_420\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same$")
cl_case("CL_log_link_to_file", CL_OK, CL_I, "^harness: cmdline err: ERROR: The log \\[log\\.txt\\] would replace",
        own="--link log.txt=ok_420.jpg")
cl_case("CL_log_link_to_file_kept", CL_OK, CL_I,
        f"^harness: cmdline file ok_420\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same$",
        own="--link log.txt=ok_420.jpg")
cl_case("CL_log_unwritable", CL_OK, "-i ok_420.jpg -o ro/log.txt",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[ro/log\\.txt\\]: ", own="--mkdir ro --chmod 500:ro")
cl_case("CL_log_unwritable_status", CL_OK, "-i ok_420.jpg -o ro/log.txt", cl_status(1, CL_OK),
        own="--mkdir ro --chmod 500:ro")

# The options, for this run only: each changes the report, and no saved
# setting changes (master's parser set them in its settings, which it saved
# only when something else had changed them). The saved settings apply too.
cl_case("CL_opt_scandump", CL_OK, CL_I + " -scandump", cl_log("log.txt", "  Scan Data: \\(after bitstuff removed\\)"))
cl_case("CL_opt_scandump_off", CL_OK, CL_I, cl_log("log.txt", "  Filename: .*"), forbid="Scan Data: \\(after")
cl_case("CL_opt_dhtexp", CL_OK, CL_I + " -dhtexp", cl_log("log.txt", "  Expanded Form of Codes:"))
cl_case("CL_opt_dhtexp_off", CL_OK, CL_I, cl_log("log.txt", "  Filename: .*"), forbid="Expanded Form of Codes")
cl_case("CL_opt_histo_y", CL_OK, CL_I + " -histo_y", cl_log("log.txt", "  Y Histogram in DC: \\(DCT sums\\) Full"))
cl_case("CL_opt_histo_y_rgb", CL_OK, CL_I + " -histo_y", cl_log("log.txt", "  RGB histogram in DC \\(after clip\\):"))
cl_case("CL_opt_histo_y_off", CL_OK, CL_I, cl_log("log.txt", "  Filename: .*"), forbid="Y Histogram in DC")
cl_case("CL_opt_maker", ["r_canon_crw.jpg"], "-i r_canon_crw.jpg -o log.txt -maker",
        cl_log("log.txt", "    \\[Canon\\.ImageType +\\] = \"CRW:EOS 300D DIGITAL CMOS RAW\""))
cl_case("CL_opt_maker_off", ["r_canon_crw.jpg"], "-i r_canon_crw.jpg -o log.txt",
        cl_log("log.txt", "    Makernote decode option not enabled\\."))
cl_case("CL_opt_scan", CL_OK, CL_I + " -scan", cl_log("log.txt", "\\*\\*\\* Decoding SCAN Data \\*\\*\\*"),
        own="--setting General\\DecScanImg=false")
cl_case("CL_opt_scan_saved_off", CL_OK, CL_I, cl_log("log.txt", "  Filename: .*"), forbid="Decoding SCAN Data",
        own="--setting General\\DecScanImg=false")
cl_case("CL_opt_exif_hide_unk", ["r_exif.jpg"], "-i r_exif.jpg -o log.txt -exif_hide_unk",
        cl_log("log.txt", "  Filename: .*"), forbid="GPS\\.0x0050", own="--setting General\\ExifHideUnk=false")
cl_case("CL_opt_exif_saved_shown", ["r_exif.jpg"], "-i r_exif.jpg -o log.txt", cl_log("log.txt", ".*GPS\\.0x0050.*"),
        own="--setting General\\ExifHideUnk=false")
cl_case("CL_opt_saved_applies", CL_OK, CL_I, cl_log("log.txt", "  Scan Data: \\(after bitstuff removed\\)"),
        own="--setting General\\DumpScan=true")
CL_ALL_OPTS = " -scan -maker -scandump -histo_y -dhtexp -exif_hide_unk"
cl_case("CL_opt_not_saved", CL_OK, CL_I + CL_ALL_OPTS, "^harness: cmdline settings changed=none$")
cl_case("CL_opt_not_saved_over_off", CL_OK, CL_I + CL_ALL_OPTS, "^harness: cmdline settings changed=none$",
        own="--setting General\\DecScanImg=false --setting General\\ExifHideUnk=false --setting General\\DecMaker=false")

# Where to decode from (master's DoAnalyzeOffset()). e_two.jpg is 100 junk
# bytes, a JPEG, 100 junk bytes, another JPEG; fo_start_two.jpg a JPEG, 100
# junk bytes, another JPEG. The last of the -offset_ options counts.
two_second = f"0x{len(c444) + len(junk):08x}"
cl_case("CL_offset_pos", ["e_two.jpg"], "-i e_two.jpg -o log.txt -offset_pos 100",
        cl_log("log.txt", "Start Offset: 0x00000064"))
cl_case("CL_offset_start", ["e_two.jpg"], "-i e_two.jpg -o log.txt -offset_pos 100 -offset_start",
        cl_log("log.txt", "Start Offset: 0x00000000"))
cl_case("CL_offset_srch1_after_junk", ["e_two.jpg"], "-i e_two.jpg -o log.txt -offset_srch1",
        cl_log("log.txt", "Start Offset: 0x00000064"))
cl_case("CL_offset_srch1_at_start", ["fo_start_two.jpg"], "-i fo_start_two.jpg -o log.txt -offset_srch1",
        cl_log("log.txt", "Start Offset: 0x00000000"))
cl_case("CL_offset_srch2", ["fo_start_two.jpg"], "-i fo_start_two.jpg -o log.txt -offset_srch2",
        cl_log("log.txt", "Start Offset: " + two_second))
cl_case("CL_offset_srch2_one", CL_OK, CL_I + " -offset_srch2", cl_log("log.txt", "Start Offset: 0x00000000"))
cl_case("CL_offset_report_once", ["fo_start_two.jpg"], "-i fo_start_two.jpg -o log.txt -offset_srch2",
        cl_log("log.txt", "Start Offset: " + two_second), forbid="Start Offset: 0x00000000")
# Past the end: an error, in the report too (master decoded 0x00 bytes)
n420 = len(c420)
cl_case("CL_offset_past_end", CL_OK, f"{CL_I} -offset_pos {n420}",
        f"^harness: cmdline err: ERROR: The offset to decode from, {n420} \\(0x{n420:08x}\\), isn't in the file, "
        f"which is {n420} bytes \\[ok_420\\.jpg\\]$")
cl_case("CL_offset_past_end_log", CL_OK, f"{CL_I} -offset_pos {n420}",
        cl_log("log.txt", f"ERROR: The offset to decode from, {n420} .*"))
cl_case("CL_offset_past_end_status", CL_OK, f"{CL_I} -offset_pos {n420}", cl_status(1, CL_LOG))
cl_case("CL_offset_last_byte", CL_OK, f"{CL_I} -offset_pos {n420 - 1}", cl_status(0, CL_LOG))

# -ext_all: every JPEG in the file, named from the log's name without its
# extension, or else from FILE.export (as master). -ext_dht_avi adds the
# standard Huffman tables a frame lacks (master's did nothing); a Motion JPEG
# AVI's frames get them anyway, as Export JPEG gives them by default.
cl_case("CL_ext_all", ["e_two.jpg"], "-i e_two.jpg -ext_all",
        cl_status(0, ["e_two.jpg", "e_two.jpg.export.000001.jpg", "e_two.jpg.export.000002.jpg"]))
cl_case("CL_ext_all_first", ["e_two.jpg"], "-i e_two.jpg -ext_all",
        f"^harness: cmdline file e_two\\.jpg\\.export\\.000001\\.jpg size={len(c444)} md5={md5(c444)} ")
cl_case("CL_ext_all_second", ["e_two.jpg"], "-i e_two.jpg -ext_all",
        f"^harness: cmdline file e_two\\.jpg\\.export\\.000002\\.jpg size={len(c420)} md5={md5(c420)} ")
cl_case("CL_ext_all_log_name", ["e_two.jpg"], "-i e_two.jpg -o log.txt -ext_all",
        cl_status(0, ["e_two.jpg", "log.000001.jpg", "log.000002.jpg", "log.txt"]))
cl_case("CL_ext_all_log_report", ["e_two.jpg"], "-i e_two.jpg -o log.txt -ext_all",
        cl_log("log.txt", "  Filename: \\[e_two\\.jpg\\]"))
cl_case("CL_ext_all_mjpeg", ["e_mjpeg_frames.bin"], "-i e_mjpeg_frames.bin -ext_all",
        f"^harness: cmdline file e_mjpeg_frames\\.bin\\.export\\.000001\\.jpg size={len(mjpeg_frame)} "
        f"md5={md5(mjpeg_frame)} dht=0 new$")
cl_case("CL_ext_dht_avi", ["e_mjpeg_frames.bin"], "-i e_mjpeg_frames.bin -ext_all -ext_dht_avi",
        f"^harness: cmdline file e_mjpeg_frames\\.bin\\.export\\.000002\\.jpg size={len(mjpeg_std)} "
        f"md5={md5(mjpeg_std)} dht=1 new$")
cl_case("CL_ext_all_avi", ["e_mjpeg.avi"], "-i e_mjpeg.avi -ext_all",
        f"^harness: cmdline file e_mjpeg\\.avi\\.export\\.000002\\.jpg size={len(mjpeg_std)} "
        f"md5={md5(mjpeg_std)} dht=1 new$")
cl_case("CL_ext_dht_avi_alone", ["e_mjpeg_frames.bin"], "-i e_mjpeg_frames.bin -ext_dht_avi",
        cl_status(0, ["e_mjpeg_frames.bin"]))
cl_case("CL_ext_all_missing", CL_OK, "-i nosuch.jpg -ext_all", cl_status(1, CL_OK))
cl_case("CL_ext_all_unwritable", ["e_two.jpg"], "-i e_two.jpg -o ro/log.txt -ext_all",
        "^harness: cmdline err: ERROR: Couldn't open file for write \\[ro/log\\.000001\\.jpg\\]",
        own="--mkdir ro --chmod 500:ro")

# -b and -br: each .jpg and .jpeg file in the folder (and with -br, in its
# subfolders), its log next to it; -o isn't used. With -ext_all, every file.
CL_TREE = "--put sub/a.jpg=ok_444.jpg --put B.JPEG=ok_gray.jpg --put notes.dat=ok_444.jpg"
CL_TREE_FILES = ["ok_420.jpg", "ok_444.jpg", "ok_gray.jpg"]
cl_case("CL_batch", CL_TREE_FILES, "-b .",
        cl_status(0, ["B.JPEG", "B.JPEG.txt", "notes.dat", "ok_420.jpg", "ok_420.jpg.txt", "ok_444.jpg",
                      "ok_444.jpg.txt", "ok_gray.jpg", "ok_gray.jpg.txt", "sub/a.jpg"]), own=CL_TREE)
cl_case("CL_batch_subfolders", CL_TREE_FILES, "-br .",
        cl_status(0, ["B.JPEG", "B.JPEG.txt", "notes.dat", "ok_420.jpg", "ok_420.jpg.txt", "ok_444.jpg",
                      "ok_444.jpg.txt", "ok_gray.jpg", "ok_gray.jpg.txt", "sub/a.jpg", "sub/a.jpg.txt"]), own=CL_TREE)
cl_case("CL_batch_log", CL_TREE_FILES, "-br .", cl_log("sub/a.jpg.txt", "  Filename: \\[\\./sub/a\\.jpg\\]"), own=CL_TREE)
cl_case("CL_batch_quiet", CL_TREE_FILES, "-br .", "^harness: cmdline status=0 ", forbid="^harness: cmdline (out|err):",
        own=CL_TREE)
cl_case("CL_batch_ignores_o", CL_TREE_FILES, "-b . -o log.txt", "^harness: cmdline status=0 ",
        forbid="^harness: cmdline status=.*[=|]log\\.txt", own=CL_TREE)
cl_case("CL_batch_ext_all", CL_TREE_FILES, "-b . -ext_all",
        "^harness: cmdline status=0 files=.*\\|notes\\.dat\\|notes\\.dat\\.export\\.000001\\.jpg\\|notes\\.dat\\.txt\\|",
        own=CL_TREE)
cl_case("CL_batch_offset", ["e_two.jpg"], "-b . -offset_pos 100", cl_log("e_two.jpg.txt", "Start Offset: 0x00000064"))
cl_case("CL_batch_missing", CL_OK, "-b nosuch",
        "^harness: cmdline err: ERROR: Couldn't read the folder \\[nosuch\\]: No such file or directory$")
cl_case("CL_batch_missing_status", CL_OK, "-b nosuch", cl_status(1, CL_OK))
cl_case("CL_batch_unreadable_folder", CL_TREE_FILES, "-br .",
        "^harness: cmdline err: ERROR: Couldn't read the folder \\[\\./locked\\]: Permission denied$",
        own=CL_TREE + " --mkdir locked --put locked/x.jpg=ok_444.jpg --chmod 000:locked")
cl_case("CL_batch_unreadable_file", CL_TREE_FILES, "-b .",
        "^harness: cmdline err: ERROR: Couldn't open file \\[\\./B\\.JPEG\\]: Permission denied$",
        own=CL_TREE + " --chmod 000:B.JPEG")
cl_case("CL_batch_unreadable_file_others", CL_TREE_FILES, "-b .",
        "^harness: cmdline status=1 files=B\\.JPEG\\|notes\\.dat\\|ok_420\\.jpg\\|ok_420\\.jpg\\.txt\\|",
        own=CL_TREE + " --chmod 000:B.JPEG")

# No log or export replaces one of the batch's files (with -ext_all, every
# file in the folder), before or after it is analyzed, nor by a name in
# another case (on a disk that ignores case, as macOS's usually do). Master
# replaced it, and then analyzed what had replaced it.
CL_KEEP_EXPORT = "--put a.jpg=ok_444.jpg --put a.jpg.export.000001.jpg=ok_420.jpg"
cl_case("CL_batch_keeps_export", CL_OK, "-b . -ext_all",
        f"^harness: cmdline file a\\.jpg\\.export\\.000001\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same$",
        own=CL_KEEP_EXPORT)
cl_case("CL_batch_keeps_export_error", CL_OK, "-b . -ext_all",
        "^harness: cmdline err: ERROR: Couldn't write \\[\\./a\\.jpg\\.export\\.000001\\.jpg\\]: it would replace one "
        "of the files analyzed$", own=CL_KEEP_EXPORT)
cl_case("CL_batch_keeps_export_status", CL_OK, "-b . -ext_all", "^harness: cmdline status=1 ", own=CL_KEEP_EXPORT)
cl_case("CL_batch_keeps_export_analyzed", CL_OK, "-b . -ext_all",
        cl_log("a.jpg.export.000001.jpg.txt", "  Filename: \\[\\./a\\.jpg\\.export\\.000001\\.jpg\\]"),
        own=CL_KEEP_EXPORT)
CL_KEEP_LOG = "--put x.dat=ok_444.jpg --put x.dat.txt=ok_420.jpg"
cl_case("CL_batch_keeps_log", CL_OK, "-b . -ext_all",
        f"^harness: cmdline file x\\.dat\\.txt size={len(c420)} md5={md5(c420)} dht=[0-9]+ same$", own=CL_KEEP_LOG)
cl_case("CL_batch_keeps_log_error", CL_OK, "-b . -ext_all",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[\\./x\\.dat\\.txt\\]: it would replace one of the files "
        "analyzed$", own=CL_KEEP_LOG)
cl_case("CL_batch_keeps_case", CL_OK, "-b . -ext_all",
        f"^harness: cmdline file a\\.jpg\\.export\\.000001\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same$",
        own="--put A.JPG=ok_444.jpg --put a.jpg.export.000001.jpg=ok_420.jpg")
cl_case("CL_batch_writes_others", CL_OK, "-b . -ext_all",
        "^harness: cmdline file a\\.jpg\\.txt size=[0-9]+ md5=[0-9a-f]+ dht=0 new$", own=CL_KEEP_EXPORT)
# Nor the file one of them is a link to (a.jpg leading to a.jpg.txt, its
# log's name), a link on the way (b.jpg's log's name), or a link to a
# folder on the way (d.jpg's)
CL_KEEP_TARGET = "--put a.jpg.txt=ok_444.jpg --link a.jpg=a.jpg.txt"
cl_case("CL_batch_keeps_link_target", CL_OK, "-b .",
        f"^harness: cmdline file a\\.jpg\\.txt size={len(c444)} md5={md5(c444)} dht=[0-9]+ same$", own=CL_KEEP_TARGET)
cl_case("CL_batch_keeps_link_target_error", CL_OK, "-b .",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[\\./a\\.jpg\\.txt\\]: it would replace one of the files "
        "analyzed$", own=CL_KEEP_TARGET)
cl_case("CL_batch_keeps_link_target_status", CL_OK, "-b .", "^harness: cmdline status=1 ", own=CL_KEEP_TARGET)
CL_KEEP_WAY = "--put b.jpg=ok_444.jpg --put real.bin=ok_420.jpg --link b.jpg.txt=real.bin --link a.jpg=b.jpg.txt"
cl_case("CL_batch_keeps_link_on_way", CL_OK, "-b .",
        f"^harness: cmdline file b\\.jpg\\.txt size={len(c420)} md5={md5(c420)} dht=[0-9]+ same link$", own=CL_KEEP_WAY)
cl_case("CL_batch_keeps_link_on_way_error", CL_OK, "-b .",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[\\./b\\.jpg\\.txt\\]: it would replace one of the files "
        "analyzed$", own=CL_KEEP_WAY)
CL_KEEP_FOLDER = "--put real/x.jpg=ok_420.jpg --link d.jpg.txt=real --link a.jpg=d.jpg.txt/x.jpg --put d.jpg=ok_444.jpg"
cl_case("CL_batch_keeps_folder_link", CL_OK, "-b .",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[\\./d\\.jpg\\.txt\\]: it would replace one of the files "
        "analyzed$", own=CL_KEEP_FOLDER)
cl_case("CL_batch_keeps_folder_link_kept", CL_OK, "-b .",
        f"^harness: cmdline file a\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same link$", own=CL_KEEP_FOLDER)
# (".." after a link to a folder goes up from where it led, as macOS goes:
# a.jpg leads to deep/t.jpg.txt, not ./t.jpg.txt)
CL_KEEP_UP = ("--put deep/t.jpg.txt=ok_444.jpg --put deep/t.jpg=ok_420.jpg --mkdir deep/inner --link up=deep/inner "
              "--link a.jpg=up/../t.jpg.txt")
cl_case("CL_batch_keeps_link_up", CL_OK, "-br .",
        f"^harness: cmdline file deep/t\\.jpg\\.txt size={len(c444)} md5={md5(c444)} dht=[0-9]+ same$", own=CL_KEEP_UP)

# With -i, the file analyzed: Extract All's first name, given as it is, or
# as a link (which was removed, and the export put in its place), or a link
# to a folder on the way, given as the log
CL_EXPORT_SELF = "-i frame.000001.jpg -o frame.txt -ext_all"
CL_EXPORT_SELF_ERR = ("^harness: cmdline err: ERROR: Couldn't write \\[\\./frame\\.000001\\.jpg\\]: it would replace the "
                      "file analyzed$")
cl_case("CL_file_export_self", CL_OK, CL_EXPORT_SELF, CL_EXPORT_SELF_ERR, own="--put frame.000001.jpg=ok_420.jpg")
cl_case("CL_file_export_self_link", CL_OK, CL_EXPORT_SELF, CL_EXPORT_SELF_ERR, own="--link frame.000001.jpg=ok_420.jpg")
cl_case("CL_file_export_self_link_kept", CL_OK, CL_EXPORT_SELF,
        f"^harness: cmdline file frame\\.000001\\.jpg size={len(c420)} md5={md5(c420)} dht=[0-9]+ same link$",
        own="--link frame.000001.jpg=ok_420.jpg")
cl_case("CL_file_export_self_link_status", CL_OK, CL_EXPORT_SELF, "^harness: cmdline status=1 ",
        own="--link frame.000001.jpg=ok_420.jpg")
CL_LOG_FOLDER = "--put real/x.jpg=ok_420.jpg --link d.txt=real"
cl_case("CL_log_folder_link", CL_OK, "-i d.txt/x.jpg -o d.txt",
        "^harness: cmdline err: ERROR: Couldn't write the log \\[d\\.txt\\]: it would replace the file analyzed$",
        own=CL_LOG_FOLDER)
cl_case("CL_log_folder_link_status", CL_OK, "-i d.txt/x.jpg -o d.txt", cl_status(1, CL_OK + ["real/x.jpg"]),
        own=CL_LOG_FOLDER)

# Qt doesn't read the app's arguments: given "-i -platform -o LOG", it took
# "-o" for its platform plugin and stopped the app
cl_case("CL_qt_option_name", CL_OK, "-i -platform -o log.txt", cl_log("log.txt", "  Filename: \\[-platform\\]"),
        own="--put -platform=ok_420.jpg")
cl_case("CL_qt_option_help", CL_OK, "-i -platform -o log.txt -help", cl_status(0, CL_OK))
# The same through the app itself, as started in Terminal: main() as built,
# and the bundle. (Only the summary and an error: a run with a file reads
# your own settings.)
CL_APP_QT = "-i -platform -o log.txt"
cl_case("CL_app_qt_option_help", CL_OK, CL_APP_QT + " -help", cl_status(0, CL_OK), forbid="^harness: cmdline err:",
        own="--app")
cl_case("CL_app_qt_option_help_out", CL_OK, CL_APP_QT + " -help",
        "^harness: cmdline out: " + rx("JPEGsnoopQt.app/Contents/MacOS/JPEGsnoopQt <parameters>") + "$", own="--app")
cl_case("CL_app_qt_option_error", CL_OK, CL_APP_QT + " -x",
        "^harness: cmdline err: ERROR: Unknown command-line flag \\[-x\\]$", own="--app")
cl_case("CL_app_qt_option_error_status", CL_OK, CL_APP_QT + " -x", cl_status(1, CL_OK), own="--app")

# -done: said at the end, also after an error
cl_case("CL_done", CL_OK, CL_I + " -done", "^harness: cmdline out: JPEGsnoop operations complete$")
cl_case("CL_done_after_error", CL_OK, "-i nosuch.jpg -done", "^harness: cmdline out: JPEGsnoop operations complete$")
cl_case("CL_no_done", CL_OK, CL_I, cl_status(0, CL_LOG), forbid="operations complete")

# The license terms not accepted: said so, and nothing done (master showed
# them). The summary needs no license.
cl_case("CL_eula", CL_OK, CL_I,
        "^harness: cmdline err: ERROR: The license terms haven't been accepted\\. Open JPEGsnoop and accept them "
        "first\\.$", own="--no-eula")
cl_case("CL_eula_nothing_done", CL_OK, CL_I, cl_status(1, CL_OK), own="--no-eula")
cl_case("CL_eula_help", CL_OK, "-help", cl_status(0, CL_OK), own="--no-eula")
cl_case("CL_eula_not_saved", CL_OK, CL_I, "^harness: cmdline settings changed=none$", own="--no-eula")


# --- File > Open's types of file --------------------------------------------------
# Windows' list, in its order: the port's had no AVI Movie. gui --open chooses
# File > Open Image and answers its dialog.
def open_case(cid, choice, expect, names=("ok_420.jpg",), extra=""):
    case(cid, "OD", "gui", list(names), f"--open {choice} {extra}".strip(), timeout=30, expect=expect)


OD_TYPES = ("JPEG Image (*.jpg *.jpeg)|Thumbnail (*.thm)|AVI Movie (*.avi)|QuickTime Movie (*.mov)|"
            "Digital Negative (*.dng)|RAW Image (*.crw *.cr2 *.nef *.orf *.pef)|PDF Files (*.pdf)|"
            "Photoshop Files (*.psd)|All Files (*)")
open_case("OD_types", "cancel", "^" + rx(f'harness: open dialog: Open (filters="{OD_TYPES}" '))
open_case("OD_first_type", "cancel",
          "^harness: open dialog: .* " + rx('selected="JPEG Image (*.jpg *.jpeg)" mode=existing-file read-only=1)')
          + "$")
open_case("OD_cancel", "cancel", "^" + rx("harness: open cancel: report file=ok_420.jpg avi=0") + "$")
open_case("OD_avi", "e_mjpeg.avi", "^" + rx("harness: open e_mjpeg.avi: report file=e_mjpeg.avi avi=1") + "$")
open_case("OD_avi_before_file", "e_mjpeg.avi",
          "^" + rx("harness: open e_mjpeg.avi: report file=e_mjpeg.avi avi=1") + "$", extra="--open-at 0")

with open(out_dir / "cases.tsv", "w") as f:
    for row in cases:
        f.write("\t".join(row) + "\n")
print(f"{len(cases)} cases, {len(list(files_dir.iterdir()))} files")
