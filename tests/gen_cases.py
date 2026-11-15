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


# --- F: follow-up review (docs/code-review-followup-2026-11-14.md) ----------------
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
xmp = "<x:xmpmeta>\n  <dc:title>Café</dc:title>\n</x:xmpmeta>".encode("utf-8")
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

with open(out_dir / "cases.tsv", "w") as f:
    for row in cases:
        f.write("\t".join(row) + "\n")
print(f"{len(cases)} cases, {len(list(files_dir.iterdir()))} files")
