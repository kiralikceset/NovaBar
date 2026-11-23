#!/usr/bin/env python3
"""Builds an unsigned NovaBar.msix for Microsoft Store upload (the Store signs it).

An MSIX is a ZIP with AppxManifest.xml, AppxBlockMap.xml (SHA-256 of every 64 KB block
of every file) and [Content_Types].xml. Files are stored uncompressed, like `makeappx /nc`.
Usage:  python3 store/make_msix.py   (run from the repo root after ./build.sh)
"""
import base64, configparser, hashlib, os, sys, zipfile
from xml.sax.saxutils import escape
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORE = os.path.join(ROOT, "store")
OUT = os.path.join(ROOT, "dist")
BLOCK = 65536

def assets():
    """Package logos rendered from the app icon (largest frame of src/app.ico)."""
    ico = Image.open(os.path.join(ROOT, "src", "app.ico"))
    ico.size = max(ico.ico.sizes())
    src = ico.convert("RGBA")
    def logo(size, pad):
        canvas = Image.new("RGBA", size, (0, 0, 0, 0))
        side = int(min(size) * (1 - 2 * pad))
        im = src.resize((side, side), Image.LANCZOS)
        canvas.paste(im, ((size[0] - side) // 2, (size[1] - side) // 2), im)
        return canvas
    out = {}
    for name, size, pad in [("StoreLogo", (50, 50), 0.04), ("Square44x44Logo", (44, 44), 0.04),
                            ("Square150x150Logo", (150, 150), 0.22), ("Wide310x150Logo", (310, 150), 0.22)]:
        path = os.path.join(STORE, "Assets", name + ".png")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        logo(size, pad).save(path)
        out["Assets/" + name + ".png"] = path
    return out

def manifest():
    cfg = configparser.ConfigParser(); cfg.optionxform = str
    cfg.read(os.path.join(STORE, "identity.ini"), encoding="utf-8")
    idn = cfg["Identity"]
    v = idn["Version"].split(".")
    if len(v) != 4 or v[3] != "0":
        sys.exit("Version must look like 1.2.3.0 (the Store requires the last part to be 0)")
    t = open(os.path.join(STORE, "AppxManifest.template.xml"), encoding="utf-8").read()
    for k, val in [("IDENTITY_NAME", idn["Name"]), ("PUBLISHER", idn["Publisher"]),
                   ("PUBLISHER_DISPLAY_NAME", idn["PublisherDisplayName"]), ("VERSION", idn["Version"])]:
        t = t.replace("{{" + k + "}}", escape(val, {'"': "&quot;"}))
    if "PLACEHOLDER" in idn["Name"] or "00000000-0000" in idn["Publisher"]:
        print("warning: identity.ini still holds placeholder values; Partner Center will reject this package")
    return t.encode("utf-8")

def blockmap(files):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap" HashMethod="http://www.w3.org/2001/04/xmlenc#sha256">']
    for name, data in files:
        lfh = 30 + len(name.encode("utf-8"))
        out.append('<File Name="%s" Size="%d" LfhSize="%d">' % (escape(name.replace("/", "\\")), len(data), lfh))
        for i in range(0, len(data), BLOCK):
            h = base64.b64encode(hashlib.sha256(data[i:i + BLOCK]).digest()).decode()
            out.append('<Block Hash="%s"/>' % h)
        out.append("</File>")
    out.append("</BlockMap>")
    return "\r\n".join(out).encode("utf-8")

CONTENT_TYPES = (b'<?xml version="1.0" encoding="UTF-8"?>'
    b'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    b'<Default Extension="exe" ContentType="application/x-msdownload"/>'
    b'<Default Extension="png" ContentType="image/png"/>'
    b'<Override PartName="/AppxManifest.xml" ContentType="application/vnd.ms-appx.manifest+xml"/>'
    b'<Override PartName="/AppxBlockMap.xml" ContentType="application/vnd.ms-appx.blockmap+xml"/>'
    b'</Types>')

def main():
    exe = os.path.join(ROOT, "NovaBar.exe")
    if not os.path.exists(exe): sys.exit("NovaBar.exe not found: run ./build.sh first")
    files = [("NovaBar.exe", open(exe, "rb").read())]
    for name, path in sorted(assets().items()): files.append((name, open(path, "rb").read()))
    files.append(("AppxManifest.xml", manifest()))
    bm = blockmap(files)
    os.makedirs(OUT, exist_ok=True)
    target = os.path.join(OUT, "NovaBar.msix")
    with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as z:
        for name, data in files + [("AppxBlockMap.xml", bm), ("[Content_Types].xml", CONTENT_TYPES)]:
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_STORED
            z.writestr(zi, data)
    verify(target)
    print("wrote", target, os.path.getsize(target), "bytes")

def verify(path):
    """Re-reads the package and checks the block map against the stored files and header sizes."""
    import re, struct
    raw = open(path, "rb").read()
    with zipfile.ZipFile(path) as z:
        bm = z.read("AppxBlockMap.xml").decode()
        for zi in z.infolist():
            assert zi.compress_type == zipfile.ZIP_STORED and not zi.extra, zi.filename
            sig, = struct.unpack_from("<I", raw, zi.header_offset); assert sig == 0x04034b50
            nlen, elen = struct.unpack_from("<HH", raw, zi.header_offset + 26); assert elen == 0
        for m in re.finditer(r'<File Name="([^"]+)" Size="(\d+)" LfhSize="(\d+)">(.*?)</File>', bm, re.S):
            name = m.group(1).replace("\\", "/"); data = z.read(name)
            assert len(data) == int(m.group(2)) and int(m.group(3)) == 30 + len(name.encode())
            hashes = re.findall(r'Hash="([^"]+)"', m.group(4))
            assert hashes == [base64.b64encode(hashlib.sha256(data[i:i + BLOCK]).digest()).decode() for i in range(0, len(data), BLOCK)]
        listed = set(x.replace("\\", "/") for x in re.findall(r'<File Name="([^"]+)"', bm))
        assert listed == set(z.namelist()) - {"AppxBlockMap.xml", "[Content_Types].xml"}
    print("package check ok")

if __name__ == "__main__":
    main()
