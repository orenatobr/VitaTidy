#!/usr/bin/env python3
"""Checks a built VPK against the rules the Vita installer enforces. Usage: check_vpk.py file.vpk

Fails (exit 1) when:
- the title id is not four capital letters plus five digits (error 0x80101130), or
- a PNG in sce_sys is not 8-bit indexed and non-interlaced (error 0x8010113D).
"""
import re
import struct
import sys
import zipfile


def main(path: str) -> int:
    errors = []
    z = zipfile.ZipFile(path)
    names = z.namelist()
    for required in ("sce_sys/param.sfo", "eboot.bin"):
        if required not in names:
            errors.append(f"missing {required}")

    sfo = z.read("sce_sys/param.sfo") if "sce_sys/param.sfo" in names else b""
    ids = set(re.findall(rb"[A-Z]{4}\d{5}", sfo))
    title = re.search(rb"([A-Z]{4}\d{5})\x00", sfo)
    if not title:
        errors.append("param.sfo has no title id of the form XXXX00000")

    for name in names:
        if name.startswith("sce_sys/") and name.endswith(".png"):
            data = z.read(name)
            if data[:8] != b"\x89PNG\r\n\x1a\n":
                errors.append(f"{name}: not a PNG")
                continue
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
            if (depth, ctype, interlace) != (8, 3, 0):
                errors.append(f"{name}: bit depth {depth}, color type {ctype}, interlace {interlace} (need 8, 3, 0)")

    for e in errors:
        print("FAIL:", e)
    if not errors:
        print(f"OK: {path} ({len(names)} files, title id {title.group(1).decode() if title else '?'})")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]) if len(sys.argv) == 2 else 2)
