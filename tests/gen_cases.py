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


def dc_only_jpeg(width, height, comps, level):
    """Baseline JPEG made of flat 8x8 blocks (DC only, quantiser 1), with any
    sampling factors and any level, even outside 0..255, which Pillow can't
    write. comps: [(id, h, v)]; level(comp_index, x, y) is the component's
    level for the block whose top-left image pixel is (x, y).
    Returns (file, offset of the scan data, [(byte, bit) where each MCU starts
    in the scan data])."""
    hmax, vmax = max(c[1] for c in comps), max(c[2] for c in comps)
    dc = huff_codes(DC_COUNTS, DC_VALUES)
    bits, pred, mcu_pos = Bits(), [0] * len(comps), []
    for my in range(-(-height // (8 * vmax))):
        for mx in range(-(-width // (8 * hmax))):
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
case("S5_update_date_format", "S5", "settings", "-", expect="is_today=1")
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
     expect="ERROR: Can't overwrite source file|case-sensitive disk, skipped", forbid="source unchanged=0")
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
edit_case("T_undo_ignored", "key:undo", "harness: edit key:undo: selected=none .* undo=0 .* report=same ",
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

with open(out_dir / "cases.tsv", "w") as f:
    for row in cases:
        f.write("\t".join(row) + "\n")
print(f"{len(cases)} cases, {len(list(files_dir.iterdir()))} files")
