import struct, sys, zlib
# Minimal 24-bit BMP -> PNG converter (no dependencies). Usage: bmp2png.py in.bmp out.png
d = open(sys.argv[1], "rb").read()
off = struct.unpack_from("<I", d, 10)[0]
w, h = struct.unpack_from("<ii", d, 18)
bpp = struct.unpack_from("<H", d, 28)[0]
assert bpp == 24
row = (w * 3 + 3) & ~3
rows = []
for y in range(abs(h)):
    src_y = abs(h) - 1 - y if h > 0 else y
    r = d[off + src_y * row: off + src_y * row + w * 3]
    rgb = bytearray(len(r))
    rgb[0::3], rgb[1::3], rgb[2::3] = r[2::3], r[1::3], r[0::3]
    rows.append(b"\x00" + bytes(rgb))
def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, abs(h), 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
