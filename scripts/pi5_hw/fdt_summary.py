#!/usr/bin/env python3
"""Minimal read-only FDT summary (node count, chosen/aliases, PL011 nodes). Offline helper."""
import struct
import sys

blob = open(sys.argv[1], "rb").read()
magic, total, off_struct, off_strings, _, ver, _, _, size_strings, size_struct = struct.unpack(">10I", blob[:40])
assert magic == 0xD00DFEED
pos, end = off_struct, off_struct + size_struct
path, nodes, props = [], 0, {}
def cstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode()
while pos < end:
    tok = struct.unpack(">I", blob[pos:pos + 4])[0]
    pos += 4
    if tok == 1:
        name = cstr(blob, pos)
        pos = (pos + len(name) + 1 + 3) & ~3
        path.append(name)
        nodes += 1
        props["/" + "/".join(p for p in path if p)] = {}
    elif tok == 2:
        path.pop()
    elif tok == 3:
        ln, no = struct.unpack(">II", blob[pos:pos + 8])
        pos += 8
        val = blob[pos:pos + ln]
        pos = (pos + ln + 3) & ~3
        props["/" + "/".join(p for p in path if p)][cstr(blob, off_strings + no)] = val
    elif tok == 9:
        break
print("total nodes", nodes, "blob bytes", len(blob))
print("chosen", {k: v for k, v in props.get("/chosen", {}).items() if k in ("stdout-path", "bootargs")})
print("aliases", {k: v.rstrip(b"\0").decode(errors="replace") for k, v in props.get("/aliases", {}).items() if k.startswith(("serial", "uart"))})
for p, d in props.items():
    comp = d.get("compatible", b"")
    if b"arm,pl011" in comp or b"pcie" in comp or b"rp1" in comp or b"gic" in comp:
        print(p, comp.rstrip(b"\0").replace(b"\0", b",").decode(), "status=", d.get("status", b"okay").rstrip(b"\0").decode())
