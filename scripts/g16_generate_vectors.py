#!/usr/bin/env python3
"""Generate CNRB v1 / node-identity vectors by running the upstream ChromaNeural transport.py.

Usage: g16_generate_vectors.py <ChromaNeural checkout> <output manifest>
The upstream module is executed, not copied. Data are derived by a fixed rule so
only digests of the upstream-produced blobs are stored.
"""

from __future__ import annotations

import base64
import hashlib
import importlib.util
import sys
from pathlib import Path

KEY = bytes(range(32))
CASES = [
    ("raw-empty", "raw", 0),
    ("raw-small", "raw", 5),
    ("cpl-source", "cpl", 41),
    ("cpa-bytes", "cpa", 256),
    ("prisme-asm-chunk", "prisme-asm", 32768),
    ("optb-chunk-plus", "optb", 32769),
    ("raw-max", "raw", 1048576),
]
ED25519_PUBLIC_KEYS = [  # RFC 8032 section 7.1 test vectors 1 and 2
    "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
    "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
]


def data_for(length: int) -> bytes:
    return bytes((i * 31 + 7) & 255 for i in range(length))


def main() -> int:
    checkout, output = Path(sys.argv[1]), Path(sys.argv[2])
    spec = importlib.util.spec_from_file_location("cn_transport", checkout / "client" / "chroma" / "transport.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    lines = ["# Generated from upstream ChromaNeural client/chroma/transport.py (executed, not copied).",
             f"key {KEY.hex()}"]
    for name, kind, length in CASES:
        data = data_for(length)
        blob = module.pack(data, KEY, kind)
        out, meta = module.unpack(blob, KEY)
        assert out == data and meta["format"] == kind
        lines.append(f"case {name} {kind} {length} {hashlib.sha256(blob).hexdigest()} {len(blob)}")
    for hexkey in ED25519_PUBLIC_KEYS:
        raw = bytes.fromhex(hexkey)
        lines.append(f"identity {hashlib.sha256(raw).hexdigest()} {base64.b64encode(raw).decode()}")
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
