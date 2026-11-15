#!/usr/bin/env python3
"""Host-side Python integration for ExperimentalOS: EOSVM assembler and EOSPKG v1 package tool.

This is development tooling that runs on the host. It is NOT a Python runtime inside
ExperimentalOS. It produces byte-identical output to kernel/eosvm.c and kernel/devpkg.c
(checked by tests/gates/G17_test.py), so packages built here are verified by the OS code.
Signatures are HMAC-SHA256 with a provisioned symmetric key.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import re
import struct
import sys
from pathlib import Path

OPS = {"halt": (0, 0), "push": (1, 4), "pop": (2, 0), "dup": (3, 0), "swap": (4, 0), "add": (5, 0),
       "sub": (6, 0), "mul": (7, 0), "div": (8, 0), "mod": (9, 0), "lt": (10, 0), "eq": (11, 0),
       "jmp": (12, 2), "jz": (13, 2), "load": (14, 1), "store": (15, 1), "print": (16, 0), "putc": (17, 0)}
CODE_MAX = 1024
LOCALS = 16
PKG_MAX_PAYLOAD = 4096
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,30}$")
ARCHES = ("x86_64", "aarch64", "any")
KINDS = {"eosvm": 1, "app-image": 2}


class ToolError(Exception):
    pass


def assemble(source: str) -> bytes:
    code = bytearray()
    labels: dict[str, int] = {}
    fixups: list[tuple[int, str, int]] = []
    for number, raw in enumerate(source.split("\n"), 1):
        line = raw.split(";", 1)[0]
        if any(ord(c) >= 0x80 or (ord(c) < 0x20 and c not in "\t\r") for c in line):
            raise ToolError(f"line {number}: invalid character")
        line = line.strip(" \t\r")
        if not line:
            continue
        match = re.match(r"^([A-Za-z0-9_]+)", line)
        if not match:
            raise ToolError(f"line {number}: syntax error")
        word = match.group(1)
        rest = line[match.end():]
        if rest.startswith(":"):
            if len(word) > 15 or word in labels or len(labels) >= 32:
                raise ToolError(f"line {number}: bad label")
            labels[word] = len(code)
            line = rest[1:].strip(" \t\r")
            if not line:
                continue
            match = re.match(r"^([A-Za-z0-9_]+)", line)
            if not match:
                raise ToolError(f"line {number}: syntax error")
            word = match.group(1)
            rest = line[match.end():]
        op = OPS.get(word.lower())
        if op is None:
            raise ToolError(f"line {number}: unknown mnemonic {word!r}")
        opcode, size = op
        arg = rest.strip(" \t\r")
        if (size == 0) != (arg == "") or (arg and re.search(r"[ \t]", arg)):
            raise ToolError(f"line {number}: operand mismatch")
        code.append(opcode)
        if size == 4:
            if not re.fullmatch(r"-?[0-9]+", arg) or not -(2**31) <= int(arg) < 2**31:
                raise ToolError(f"line {number}: bad immediate")
            code += struct.pack("<i", int(arg))
        elif size == 1:
            if not re.fullmatch(r"[0-9]+", arg) or int(arg) >= LOCALS:
                raise ToolError(f"line {number}: bad local index")
            code.append(int(arg))
        elif size == 2:
            if not re.fullmatch(r"[A-Za-z0-9_]{1,15}", arg):
                raise ToolError(f"line {number}: bad label reference")
            fixups.append((len(code), arg, number))
            code += b"\0\0"
        if len(code) > CODE_MAX:
            raise ToolError(f"line {number}: program too large")
    for position, name, number in fixups:
        if name not in labels:
            raise ToolError(f"line {number}: undefined label {name!r}")
        code[position:position + 2] = struct.pack("<H", labels[name])
    if not code:
        raise ToolError("empty program")
    return bytes(code)


def manifest_json(pkg_id: str, version: int, arch: str, kind: str, dependency: str, min_abi: int, payload: bytes) -> bytes:
    if not ID_RE.fullmatch(pkg_id) or arch not in ARCHES or kind not in KINDS or version < 1:
        raise ToolError("invalid manifest field")
    if dependency and not ID_RE.fullmatch(dependency):
        raise ToolError("invalid dependency")
    if not payload or len(payload) > PKG_MAX_PAYLOAD:
        raise ToolError("invalid payload size")
    text = (f'{{"id":"{pkg_id}","version":{version},"arch":"{arch}","kind":"{kind}","dependency":"{dependency}",'
            f'"min_abi":{min_abi},"size_kb":{(len(payload) + 1023) // 1024},'
            f'"payload_sha256":"{hashlib.sha256(payload).hexdigest()}"}}')
    return text.encode("ascii")


def build_package(pkg_id: str, version: int, arch: str, kind: str, payload: bytes,
                  dependency: str = "", min_abi: int = 1, key: bytes | None = None, slot: int = 1) -> bytes:
    manifest = manifest_json(pkg_id, version, arch, kind, dependency, min_abi, payload)
    signer = slot if key else 0
    header = b"EPK1" + struct.pack("<HHIB", 1, len(manifest), len(payload), signer) + b"\0\0\0"
    body = header + manifest + payload
    digest = hashlib.sha256(body).digest()
    signature = hmac.new(key, digest, hashlib.sha256).digest() if key else b"\0" * 32
    return body + digest + signature


def inspect_package(blob: bytes) -> dict:
    if len(blob) < 80 or blob[:4] != b"EPK1":
        raise ToolError("not an EOSPKG v1 container")
    fmt, mlen, plen, signer = struct.unpack("<HHIB", blob[4:13])
    if fmt != 1 or len(blob) != 16 + mlen + plen + 64:
        raise ToolError("bad container lengths")
    body, digest, signature = blob[:-64], blob[-64:-32], blob[-32:]
    if hashlib.sha256(body).digest() != digest:
        raise ToolError("digest mismatch")
    manifest = json.loads(blob[16:16 + mlen].decode("ascii"))
    payload = blob[16 + mlen:16 + mlen + plen]
    if hashlib.sha256(payload).hexdigest() != manifest["payload_sha256"]:
        raise ToolError("payload hash mismatch")
    return {"manifest": manifest, "signer": signer, "digest": digest.hex(), "payload_len": plen}


def elf_machine(path: Path) -> str:
    """Compatibility inspection for a cross-compiled ELF object (e_machine)."""
    data = path.read_bytes()[:20]
    if data[:4] != b"\x7fELF" or len(data) < 20:
        raise ToolError("not an ELF file")
    machine = struct.unpack("<H", data[18:20])[0]
    return {62: "x86_64", 183: "aarch64"}.get(machine, f"unknown({machine})")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    asm = sub.add_parser("assemble")
    asm.add_argument("source"); asm.add_argument("output")
    pkg = sub.add_parser("package")
    pkg.add_argument("source"); pkg.add_argument("output")
    pkg.add_argument("--id", required=True); pkg.add_argument("--version", type=int, required=True)
    pkg.add_argument("--arch", default="any"); pkg.add_argument("--dependency", default="")
    pkg.add_argument("--key-hex"); pkg.add_argument("--slot", type=int, default=1)
    insp = sub.add_parser("inspect")
    insp.add_argument("package")
    args = parser.parse_args(argv)
    try:
        if args.command == "assemble":
            Path(args.output).write_bytes(assemble(Path(args.source).read_text(encoding="ascii")))
        elif args.command == "package":
            code = assemble(Path(args.source).read_text(encoding="ascii"))
            key = bytes.fromhex(args.key_hex) if args.key_hex else None
            Path(args.output).write_bytes(build_package(args.id, args.version, args.arch, "eosvm", code,
                                                         args.dependency, 1, key, args.slot))
        else:
            print(json.dumps(inspect_package(Path(args.package).read_bytes()), indent=2, sort_keys=True))
    except (ToolError, OSError, ValueError, KeyError) as exc:
        print(f"eospkg: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
