"""Makes the one-file editor (mapcore.js put inside index.html), which opens straight from a
phone's downloads and needs nothing else: python3 build.py [OUT.html]
Also refreshes the copy the relay serves at /mapmaker (server/static/mapmaker.html)."""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
html = open(os.path.join(here, "index.html"), encoding="utf-8").read()
one = html
for name in ("mapcore.js", "view3d.js"):
    js = open(os.path.join(here, name), encoding="utf-8").read()
    tag = '<script src="%s"></script>' % name
    assert tag in one, tag
    one = one.replace(tag, "<script>\n" + js.replace("</script", "<\\/script") + "\n</script>")
core = open(os.path.join(here, "mapcore.js"), encoding="utf-8").read()
version = re.search(r"VERSION = '([^']+)'", core).group(1)
outs = sys.argv[1:] or []
outs.append(os.path.join(here, "..", "..", "server", "static", "mapmaker.html"))
for out in outs:
    with open(out, "w", encoding="utf-8") as f:
        f.write(one)
    print("wrote", out, "version", version, len(one), "bytes")
