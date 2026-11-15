#!/usr/bin/env python3
"""Deterministic FAT / GPT / disk-image builder for ExperimentalOS release candidates (host tool).

Builds: a FAT12/16/32 volume from a directory tree (8.3 names only), and a GPT disk image with one EFI
System Partition. Output depends only on the input bytes, so builds are reproducible.
This tool does not flash anything and never touches block devices.
"""

from __future__ import annotations

import argparse
import struct
import sys
import uuid
import zlib
from pathlib import Path

SECTOR = 512
FIXED_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1  # 2026-01-01
FIXED_TIME = 0
ESP_TYPE_GUID = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
DISK_GUID = uuid.UUID("6f0e5a50-52a5-4a56-9b7a-4558504f5321")
PART_GUID = uuid.UUID("6f0e5a50-52a5-4a56-9b7a-4558504f5322")


class ImageError(Exception):
    pass


def valid_83(name: str) -> bool:
    base, _, ext = name.partition(".")
    ok = lambda s, n: 0 < len(s) <= n and all(c.isupper() or c.isdigit() or c in "_-" for c in s)
    return ok(base, 8) and (ext == "" or ok(ext, 3)) and name.count(".") <= 1


def name83(name: str) -> bytes:
    base, _, ext = name.partition(".")
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def read_tree(root: Path) -> dict:
    tree: dict = {}
    for child in sorted(root.iterdir(), key=lambda p: p.name):
        if not valid_83(child.name):
            raise ImageError(f"name not representable as 8.3 uppercase: {child.name}")
        tree[child.name] = read_tree(child) if child.is_dir() else child.read_bytes()
    return tree


def make_fat(tree: dict, size: int, fat_bits: int, hidden: int = 0, label: str = "EXPERIMENTAL") -> bytes:
    if size % SECTOR:
        raise ImageError("size must be a multiple of 512")
    total = size // SECTOR
    spc = 1
    while True:
        reserved = 32 if fat_bits == 32 else 1
        root_sectors = 0 if fat_bits == 32 else 32
        # iterate FAT size
        fat_sectors = 1
        while True:
            data_sectors = total - reserved - 2 * fat_sectors - root_sectors
            clusters = data_sectors // spc
            entry_bits = fat_bits
            need = ((clusters + 2) * entry_bits + 7) // 8
            need_sectors = (need + SECTOR - 1) // SECTOR
            if need_sectors <= fat_sectors:
                break
            fat_sectors = need_sectors
        limits = {12: (1, 4084), 16: (4085, 65524), 32: (65525, 0x0FFFFFF4)}[fat_bits]
        if clusters > limits[1] and spc < 64:
            spc *= 2
            continue
        break
    if not limits[0] <= clusters <= limits[1]:
        raise ImageError(f"cluster count {clusters} invalid for FAT{fat_bits}")
    cbytes = spc * SECTOR
    fat = [0] * (clusters + 2)
    fat[0] = 0x0FFFFF00 | 0xF8 if fat_bits == 32 else (0xFF00 | 0xF8 if fat_bits == 16 else 0xFF8)
    fat[1] = 0x0FFFFFFF if fat_bits == 32 else (0xFFFF if fat_bits == 16 else 0xFFF)
    eoc = {12: 0xFFF, 16: 0xFFFF, 32: 0x0FFFFFFF}[fat_bits]
    data = bytearray()
    next_cluster = [2]

    def alloc(content: bytes) -> int:
        count = max(1, (len(content) + cbytes - 1) // cbytes) if content else 0
        if count == 0:
            return 0
        first = next_cluster[0]
        if first - 2 + count > clusters:
            raise ImageError("image too small for contents")
        for i in range(count):
            fat[first + i] = first + i + 1 if i + 1 < count else eoc
        next_cluster[0] += count
        data.extend(content.ljust(count * cbytes, b"\0"))
        return first

    def entry(name: bytes, attr: int, cluster: int, length: int) -> bytes:
        return struct.pack("<11sBBBHHHHHHHI", name, attr, 0, 0, FIXED_TIME, FIXED_DATE, FIXED_DATE,
                           cluster >> 16, FIXED_TIME, FIXED_DATE, cluster & 0xFFFF, length)

    def write_dir(node: dict, parent: int, is_root: bool) -> int:
        entries_needed = len(node) + (0 if is_root else 2)
        dir_bytes = max(1, (entries_needed * 32 + cbytes - 1) // cbytes) * cbytes
        first = next_cluster[0]
        count = dir_bytes // cbytes
        if first - 2 + count > clusters:
            raise ImageError("image too small for contents")
        for i in range(count):
            fat[first + i] = first + i + 1 if i + 1 < count else eoc
        next_cluster[0] += count
        start = len(data)
        data.extend(b"\0" * dir_bytes)
        records = []
        if not is_root:
            records.append(entry(b".          ", 0x10, first, 0))
            records.append(entry(b"..         ", 0x10, parent, 0))
        for name, value in node.items():
            if isinstance(value, dict):
                child = write_dir(value, 0 if is_root else first, False)
                records.append(entry(name83(name), 0x10, child, 0))
            else:
                records.append(entry(name83(name), 0x20, alloc(value), len(value)))
        blob = b"".join(records)
        data[start:start + len(blob)] = blob
        return first

    root_blob = b""
    if fat_bits == 32:
        root_cluster = write_dir(tree, 0, True)
        assert root_cluster == 2
    else:
        records = []
        for name, value in tree.items():
            if isinstance(value, dict):
                records.append(entry(name83(name), 0x10, write_dir(value, 0, False), 0))
            else:
                records.append(entry(name83(name), 0x20, alloc(value), len(value)))
        if len(records) * 32 > root_sectors * SECTOR:
            raise ImageError("FAT12/16 root directory full")
        root_blob = b"".join(records).ljust(root_sectors * SECTOR, b"\0")

    used = next_cluster[0] - 2
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x58\x90" if fat_bits == 32 else b"\xEB\x3C\x90"
    boot[3:11] = b"EXPOS1.0"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, spc, reserved, 2,
                     0 if fat_bits == 32 else 512, total if total < 65536 and fat_bits != 32 else 0, 0xF8,
                     0 if fat_bits == 32 else fat_sectors, 32, 64, hidden, 0 if total < 65536 and fat_bits != 32 else total)
    volid = 0x45585031
    label_b = label.ljust(11).encode("ascii")
    if fat_bits == 32:
        struct.pack_into("<IHHIHH12sBBBI11s8s", boot, 36, fat_sectors, 0, 0, 2, 1, 6, b"\0" * 12,
                         0x80, 0, 0x29, volid, label_b, b"FAT32   ")
    else:
        struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, volid, label_b,
                         b"FAT16   " if fat_bits == 16 else b"FAT12   ")
    boot[510:512] = b"\x55\xAA"

    if fat_bits == 32:
        raw_fat = b"".join(struct.pack("<I", v) for v in fat)
    elif fat_bits == 16:
        raw_fat = b"".join(struct.pack("<H", v) for v in fat)
    else:
        out = bytearray()
        for i in range(0, len(fat), 2):
            a = fat[i]
            b = fat[i + 1] if i + 1 < len(fat) else 0
            out += bytes([a & 0xFF, ((a >> 8) & 0x0F) | ((b & 0x0F) << 4), (b >> 4) & 0xFF])
        raw_fat = bytes(out)
    raw_fat = raw_fat.ljust(fat_sectors * SECTOR, b"\0")

    image = bytearray(size)
    image[0:SECTOR] = boot
    pos = reserved * SECTOR
    if fat_bits == 32:
        fsinfo = bytearray(SECTOR)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<IIII", fsinfo, 484, 0x61417272, clusters - used, next_cluster[0], 0)
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)
        image[SECTOR:2 * SECTOR] = fsinfo
        image[6 * SECTOR:7 * SECTOR] = boot
        image[7 * SECTOR:8 * SECTOR] = fsinfo
    image[pos:pos + len(raw_fat)] = raw_fat
    pos += fat_sectors * SECTOR
    image[pos:pos + len(raw_fat)] = raw_fat
    pos += fat_sectors * SECTOR
    if root_blob:
        image[pos:pos + len(root_blob)] = root_blob
        pos += len(root_blob)
    image[pos:pos + len(data)] = data
    return bytes(image)


def make_gpt_disk(partition: bytes, total_sectors: int, first_lba: int = 2048) -> bytes:
    part_sectors = len(partition) // SECTOR
    last_usable_end = total_sectors - 34
    if first_lba + part_sectors - 1 > last_usable_end:
        raise ImageError("disk too small")
    disk = bytearray(total_sectors * SECTOR)
    mbr = bytearray(SECTOR)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0xEE, b"\xFF\xFF\xFF", 1,
                                min(total_sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = mbr
    entries = bytearray(128 * 128)
    name = "EFI System".encode("utf-16-le")
    entries[0:128] = ESP_TYPE_GUID.bytes_le + PART_GUID.bytes_le + struct.pack(
        "<QQQ", first_lba, first_lba + part_sectors - 1, 0) + name.ljust(72, b"\0")
    entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF

    def header(my: int, alt: int, entries_lba: int) -> bytes:
        h = bytearray(92)
        h[0:8] = b"EFI PART"
        struct.pack_into("<IIIIQQQQ", h, 8, 0x00010000, 92, 0, 0, my, alt, 34, last_usable_end)
        h[56:72] = DISK_GUID.bytes_le
        struct.pack_into("<QIII", h, 72, entries_lba, 128, 128, entries_crc)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xFFFFFFFF)
        return bytes(h).ljust(SECTOR, b"\0")

    disk[SECTOR:2 * SECTOR] = header(1, total_sectors - 1, 2)
    disk[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    disk[first_lba * SECTOR:first_lba * SECTOR + len(partition)] = partition
    backup_entries_lba = total_sectors - 33
    disk[backup_entries_lba * SECTOR:backup_entries_lba * SECTOR + len(entries)] = entries
    disk[(total_sectors - 1) * SECTOR:] = header(total_sectors - 1, 1, backup_entries_lba)
    return bytes(disk)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fat")
    f.add_argument("tree"), f.add_argument("out"), f.add_argument("--size-mib", type=int, required=True)
    f.add_argument("--bits", type=int, choices=(12, 16, 32), default=16)
    d = sub.add_parser("gpt-disk")
    d.add_argument("tree"), d.add_argument("out")
    d.add_argument("--partition-mib", type=int, required=True)
    d.add_argument("--disk-mib", type=int, required=True)
    d.add_argument("--bits", type=int, choices=(16, 32), default=32)
    args = parser.parse_args()
    try:
        tree = read_tree(Path(args.tree))
        if args.cmd == "fat":
            Path(args.out).write_bytes(make_fat(tree, args.size_mib * 1024 * 1024, args.bits))
        else:
            part = make_fat(tree, args.partition_mib * 1024 * 1024, args.bits, hidden=2048)
            Path(args.out).write_bytes(make_gpt_disk(part, args.disk_mib * 1024 * 1024 // SECTOR))
    except ImageError as exc:
        print(f"mkimage: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
