"""For maps from this editor (one loop of walls per sector). Checks what mapcheck.py doesn't: every sector's walls form a closed loop, outer loops go the way
Build wants, joined walls point at each other, sprites stand in their sector.
python3 check_structure.py FILE.MAP..."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "server"))
from mapcheck import check_map, crypt, HEADER_NEW

XSEC = [("reference",14),("state",1),("busy",17),("data",16),("txID",10),("busyWaveA",3),("busyWaveB",3),("rxID",10),("command",8),
    ("triggerOn",1),("triggerOff",1),("busyTimeA",12),("waitTimeA",12),("restState",1),("interruptable",1),("amplitude",8),("freq",8),
    ("reTriggerA",1),("reTriggerB",1),("phase",8),("wave",4),("shadeAlways",1),("shadeFloor",1),("shadeCeiling",1),("shadeWalls",1),
    ("shade",8),("panAlways",1),("panFloor",1),("panCeiling",1),("Drag",1),("Underwater",1),("Depth",3),("panVel",8),("panAngle",11),
    ("unused1",1),("decoupled",1),("triggerOnce",1),("isTriggered",1),("Key",3),("Push",1),("Vector",1),("Reserved",1),("Enter",1),
    ("Exit",1),("Wallpush",1),("color",1),("unused2",1),("busyTimeB",12),("waitTimeB",12),("stopOn",1),("stopOff",1),("ceilpal",4),
    ("offCeilZ",32),("onCeilZ",32),("offFloorZ",32),("onFloorZ",32)]
XSPR = [("reference",14),("state",1),("busy",17),("txID",10),("rxID",10),("command",8),("triggerOn",1)]

def bits(buf, spec):
    v = int.from_bytes(buf, "little"); pos = 0; out = {}
    for n, b in spec:
        x = (v >> pos) & ((1 << b) - 1); pos += b
        out[n] = x - (1 << 32) if b == 32 and x >> 31 else x
    return out

def load(d):
    head = crypt(d[6:43], HEADER_NEW)
    (x, y, z, ang, sect, sky, vis, song, par, rev, ns, nw, nspr) = struct.unpack("<iiihhhiiBihhh", head)
    xs, xw, xsec = struct.unpack_from("<iii", crypt(d[43:43 + 128], nw), 0x40)
    pos = 43 + 128 + (2 << sky)
    S, W, P, XS, XP = [], [], [], [], []
    for i in range(ns):
        s = crypt(d[pos:pos + 40], rev * 40); pos += 40
        S.append(struct.unpack_from("<hhii", s, 0) + (struct.unpack_from("<h", s, 38)[0],))
        XS.append((struct.unpack_from("<h", s, 34)[0], bits(d[pos:pos + xsec], XSEC) if S[-1][4] > 0 else None))
        if S[-1][4] > 0: pos += xsec
    for i in range(nw):
        w = crypt(d[pos:pos + 32], (rev * 40) | HEADER_NEW); pos += 32
        W.append(struct.unpack_from("<iihhh", w, 0) + (struct.unpack_from("<h", w, 30)[0],))
        if W[-1][5] > 0: pos += xw
    for i in range(nspr):
        s = crypt(d[pos:pos + 44], (rev * 44) | HEADER_NEW); pos += 44
        f = struct.unpack("<iiiHhbBBBBBbbhhhhhhhhhh", s)
        P.append(f)
        XP.append(bits(d[pos:pos + xs], XSPR) if f[22] > 0 else None)
        if f[22] > 0: pos += xs
    return S, W, P, XS, XP

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
    S, W, P, XS, XP = load(d)
    loops = []
    kinds = []
    for si, (wp, wn, cz, fz, ex) in enumerate(S):
        typ, x = XS[si]
        if fz < cz or (fz == cz and typ != 600): problems.append("sector %d: floor not below ceiling" % si)
        if typ == 600:
            if not x: problems.append("sector %d: door/lift without extra data" % si)
            elif x["offCeilZ"] != cz or x["offFloorZ"] != fz: problems.append("sector %d: starts away from its off position" % si)
            elif not (x["Push"] or x["Wallpush"]) or not x["busyTimeA"]: problems.append("sector %d: door/lift nobody can use" % si)
            elif x["onCeilZ"] >= x["onFloorZ"]: problems.append("sector %d: no room when open" % si)
            else: kinds.append("door" if x["onCeilZ"] != x["offCeilZ"] else "lift")
        elif x and x["txID"] == 2:
            if (x["Enter"], x["triggerOn"], x["triggerOnce"], x["command"]) != (1, 1, 1, 64): problems.append("sector %d: secret set up wrong" % si)
            else: kinds.append("secret")
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
    nsec = kinds.count("secret")
    counters = [x for x in XP if x and x["rxID"] == 7 and x["txID"] == 1]
    if nsec and (len(counters) != 1 or counters[0]["command"] != 64 + nsec or not counters[0]["triggerOn"]):
        problems.append("the number of secrets isn't set up")
    for i, f in enumerate(P):
        if not inside(f[0], f[1], loops[f[13]]): problems.append("sprite %d not in its sector" % i)
        if f[2] > S[f[13]][3]: problems.append("sprite %d below the floor" % i)
    joined = sum(1 for w in W if w[3] >= 0)
    if problems:
        bad += 1
        print("%s: %d problems: %s" % (path, len(problems), "; ".join(problems[:6])))
    else:
        extra = ", ".join("%d %ss" % (kinds.count(k), k) for k in ("door", "lift", "secret") if k in kinds)
        enemies = sum(1 for f in P if 200 <= f[20] < 260 and f[14] == 6)
        print("%s: OK, %s, %d joined walls%s%s" % (path, summary, joined, ", " + extra if extra else "", ", %d enemies" % enemies if enemies else ""))
sys.exit(1 if bad else 0)
