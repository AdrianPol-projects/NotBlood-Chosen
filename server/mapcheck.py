"""Checks a Blood .map file before the relay passes it on to other players.

The same checks as the game's loader (dbLoadMap and dbCheckMapStructure in source/blood/src/db.cpp):
the header, every count against the engine's limits, every index into the map's own sectors, walls
and tiles, and the file's CRC. A map that passes can't make the game read or write outside its
arrays. Run it by hand on files: python3 mapcheck.py FILE.MAP...
"""

import struct
import sys
import zlib

MAX_SECTORS = 4096
MAX_WALLS = 16384
MAX_SPRITES = 16384
MAX_TILES = 30720
MAX_STATUS = 1024
X_SECTOR, X_WALL, X_SPRITE = 60, 24, 56
SECTOR, WALL, SPRITE = 40, 32, 44
HEADER_NEW, HEADER_OLD = 0x7474614D, 0x4D617474


class MapError(ValueError):
    pass


def crypt(data, key):
    return bytes(b ^ ((key + i) & 0xFF) for i, b in enumerate(data))


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def read(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise MapError("the file ends too early")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b


def check_map(data):
    """Returns a short summary ("123 sectors, ...") or raises MapError saying what is wrong."""
    if len(data) < 6 + 37 + 4:
        raise MapError("too small to be a map")
    r = Reader(data)
    sig, version = struct.unpack("<4sH", r.read(6))
    if sig != b"BLM\x1a":
        raise MapError("not a Blood map")
    if version & 0xFF00 == 0x700:
        v7 = True
    elif version & 0xFF00 == 0x600:
        v7 = False
    else:
        raise MapError("unknown map version %#x" % version)
    head = r.read(37)
    song = struct.unpack_from("<I", head, 22)[0]
    if song not in (0, HEADER_NEW, HEADER_OLD):
        head = crypt(head, HEADER_NEW)
    (_x, _y, _z, _ang, _sect, sky, _vis, song, _par, rev, nsectors, nwalls, nsprites) = struct.unpack("<iiihhhiiBihhh", head)
    if v7 and song not in (HEADER_NEW, HEADER_OLD, 0) or not v7 and song:
        raise MapError("damaged header")
    if not (0 < nsectors <= MAX_SECTORS and 0 < nwalls <= MAX_WALLS and 0 <= nsprites <= MAX_SPRITES and 0 <= sky <= 8):
        raise MapError("damaged header (%d sectors, %d walls, %d sprites)" % (nsectors, nwalls, nsprites))
    xsect, xwall, xspr = X_SECTOR, X_WALL, X_SPRITE
    if v7:
        extra = crypt(r.read(128), nwalls)
        xspr, xwall, xsect = struct.unpack_from("<iii", extra, 0x40)
        if not (0 <= xspr <= X_SPRITE and 0 <= xwall <= X_WALL and 0 <= xsect <= X_SECTOR):
            raise MapError("damaged header (extra data sizes)")
    r.read(2 << sky)

    def tile(n, what, i):
        if not 0 <= n < MAX_TILES:
            raise MapError("damaged %s %d (tile %d)" % (what, i, n))

    for i in range(nsectors):
        s = r.read(SECTOR)
        if v7:
            s = crypt(s, rev * SECTOR)
        wallptr, wallnum = struct.unpack_from("<hh", s, 0)
        if wallptr < 0 or wallnum < 0 or wallptr + wallnum > nwalls:
            raise MapError("damaged sector %d (walls)" % i)
        tile(struct.unpack_from("<h", s, 16)[0], "sector", i)
        tile(struct.unpack_from("<h", s, 24)[0], "sector", i)
        if struct.unpack_from("<h", s, 38)[0] > 0:
            r.read(xsect)
    for i in range(nwalls):
        w = r.read(WALL)
        if v7:
            w = crypt(w, (rev * SECTOR) | HEADER_NEW)
        point2, nextwall, nextsector, _cstat, picnum, overpicnum = struct.unpack_from("<hhhHhh", w, 8)
        if not (0 <= point2 < nwalls and -1 <= nextwall < nwalls and -1 <= nextsector < nsectors):
            raise MapError("damaged wall %d (links)" % i)
        tile(picnum, "wall", i)
        tile(overpicnum, "wall", i)
        if struct.unpack_from("<h", w, 30)[0] > 0:
            r.read(xwall)
    for i in range(nsprites):
        s = r.read(SPRITE)
        if v7:
            s = crypt(s, (rev * SPRITE) | HEADER_NEW)
        tile(struct.unpack_from("<h", s, 14)[0], "sprite", i)
        sectnum, statnum = struct.unpack_from("<hh", s, 24)
        if not 0 <= statnum <= MAX_STATUS:
            raise MapError("damaged sprite %d (status)" % i)
        if statnum < MAX_STATUS and not 0 <= sectnum < nsectors:
            raise MapError("damaged sprite %d (sector)" % i)
        if struct.unpack_from("<h", s, 42)[0] > 0:
            r.read(xspr)
    crc = struct.unpack("<I", r.read(4))[0]
    if zlib.crc32(data[:r.pos - 4]) != crc:
        raise MapError("damaged file (CRC)")
    return "%d sectors, %d walls, %d sprites" % (nsectors, nwalls, nsprites)


if __name__ == "__main__":
    bad = 0
    for path in sys.argv[1:]:
        try:
            with open(path, "rb") as f:
                print("%s: OK, %s" % (path, check_map(f.read())))
        except (OSError, MapError) as e:
            bad += 1
            print("%s: %s" % (path, e))
    sys.exit(1 if bad else 0)
