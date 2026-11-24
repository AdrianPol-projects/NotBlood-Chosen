"""For maps from this editor (one loop of walls per sector). Checks what mapcheck.py doesn't: every sector's walls form a closed loop, outer loops go the way
Build wants, joined walls point at each other, sprites stand in their sector.
python3 check_structure.py FILE.MAP..."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "server"))
from mapcheck import check_map, crypt, HEADER_NEW

def load(d):
    head = crypt(d[6:43], HEADER_NEW)
    (x, y, z, ang, sect, sky, vis, song, par, rev, ns, nw, nspr) = struct.unpack("<iiihhhiiBihhh", head)
    xs, xw, xsec = struct.unpack_from("<iii", crypt(d[43:43 + 128], nw), 0x40)
    pos = 43 + 128 + (2 << sky)
    S, W, P = [], [], []
    for i in range(ns):
        s = crypt(d[pos:pos + 40], rev * 40); pos += 40
        S.append(struct.unpack_from("<hhii", s, 0) + (struct.unpack_from("<h", s, 38)[0],))
        if S[-1][4] > 0: pos += xsec
    for i in range(nw):
        w = crypt(d[pos:pos + 32], (rev * 40) | HEADER_NEW); pos += 32
        W.append(struct.unpack_from("<iihhh", w, 0) + (struct.unpack_from("<h", w, 30)[0],))
        if W[-1][5] > 0: pos += xw
    for i in range(nspr):
        s = crypt(d[pos:pos + 44], (rev * 44) | HEADER_NEW); pos += 44
        f = struct.unpack("<iiiHhbBBBBBbbhhhhhhhhhh", s)
        P.append(f)
        if f[22] > 0: pos += xs
    return S, W, P

def inside(x, y, loop):
    c = False
    for i in range(len(loop)):
        (x1, y1), (x2, y2) = loop[i], loop[i - 1]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            c = not c
    return c

bad = 0
for path in sys.argv[1:]:
    d = open(path, "rb").read()
    problems = []
    try:
        summary = check_map(d)
    except Exception as e:
        problems.append("mapcheck: %s" % e); summary = ""
    S, W, P = load(d)
    loops = []
    for si, (wp, wn, cz, fz, ex) in enumerate(S):
        if fz <= cz: problems.append("sector %d: floor not below ceiling" % si)
        seen = set(); i = wp; loop = []
        while i not in seen:
            seen.add(i); loop.append((W[i][0], W[i][1])); i = W[i][2]
            if not wp <= i < wp + wn: problems.append("sector %d: point2 leaves the sector" % si); break
        if len(seen) != wn: problems.append("sector %d: walls aren't one loop" % si)
        a = sum(loop[k][0] * loop[(k + 1) % len(loop)][1] - loop[(k + 1) % len(loop)][0] * loop[k][1] for k in range(len(loop)))
        if a <= 0: problems.append("sector %d: outer loop goes the wrong way" % si)
        loops.append(loop)
    for i, (x, y, p2, nwall, nsect, ex) in enumerate(W):
        if nwall >= 0:
            o = W[nwall]
            if o[3] != i: problems.append("wall %d: nextwall %d doesn't point back" % (i, nwall))
            if (o[0], o[1]) != (W[p2][0], W[p2][1]) or (W[o[2]][0], W[o[2]][1]) != (x, y): problems.append("wall %d: joined walls don't match" % i)
            owner = [s for s, (wp, wn, *_r) in enumerate(S) if wp <= nwall < wp + wn][0]
            if owner != nsect: problems.append("wall %d: nextsector wrong" % i)
    for i, f in enumerate(P):
        if not inside(f[0], f[1], loops[f[13]]): problems.append("sprite %d not in its sector" % i)
        if f[2] > S[f[13]][3]: problems.append("sprite %d below the floor" % i)
    joined = sum(1 for w in W if w[3] >= 0)
    if problems:
        bad += 1
        print("%s: %d problems: %s" % (path, len(problems), "; ".join(problems[:6])))
    else:
        print("%s: OK, %s, %d joined walls" % (path, summary, joined))
sys.exit(1 if bad else 0)
