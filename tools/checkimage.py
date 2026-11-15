#!/usr/bin/env python3
"""Independent structural checker for ExperimentalOS release images (host tool, read-only).

Parses the protective MBR, both GPT headers/CRCs, the ESP partition, the FAT volume and reads the
boot application back from the image. This is a structural check only; it does not prove physical boot.
"""

from __future__ import annotations

import hashlib
import struct
import sys
import uuid
import zlib
from pathlib import Path

SECTOR = 512
ESP = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
PE_MACHINE = {"x86_64": 0x8664, "aarch64": 0xAA64}


class CheckError(Exception):
    pass


def need(cond: bool, msg: str) -> None:
    if not cond:
        raise CheckError(msg)


def check_gpt(disk: bytes) -> tuple[int, int]:
    need(len(disk) % SECTOR == 0 and len(disk) >= 70 * SECTOR, "disk size invalid")
    need(disk[510:512] == b"\x55\xAA" and disk[446 + 4] == 0xEE, "protective MBR missing")
    total = len(disk) // SECTOR
    result = None
    for lba, alt_expected in ((1, total - 1), (total - 1, 1)):
        h = bytearray(disk[lba * SECTOR:lba * SECTOR + 92])
        need(h[0:8] == b"EFI PART", f"GPT signature missing at LBA {lba}")
        crc = struct.unpack_from("<I", h, 16)[0]
        struct.pack_into("<I", h, 16, 0)
        need(zlib.crc32(bytes(h)) & 0xFFFFFFFF == crc, f"GPT header CRC bad at LBA {lba}")
        my, alt = struct.unpack_from("<QQ", h, 24)
        need(my == lba and alt == alt_expected, "GPT header LBA fields inconsistent")
        first_usable, last_usable = struct.unpack_from("<QQ", h, 40)
        entries_lba, count, size, ecrc = struct.unpack_from("<QIII", h, 72)
        need(size == 128 and count >= 1, "GPT entry geometry unsupported")
        entries = disk[entries_lba * SECTOR:entries_lba * SECTOR + count * size]
        need(zlib.crc32(entries) & 0xFFFFFFFF == ecrc, f"GPT entries CRC bad at LBA {lba}")
        type_guid = uuid.UUID(bytes_le=entries[0:16])
        start, end = struct.unpack_from("<QQ", entries, 32)
        need(type_guid == ESP, "partition 1 is not an EFI System Partition")
        need(first_usable <= start <= end <= last_usable, "partition outside usable range")
        need(all(b == 0 for b in entries[size:]), "unexpected extra partitions")
        if result is None:
            result = (start, end)
        need(result == (start, end), "primary and backup GPT disagree")
    return result


class Fat:
    def __init__(self, vol: bytes):
        b = vol[:SECTOR]
        need(b[510:512] == b"\x55\xAA", "FAT boot signature missing")
        self.vol = vol
        self.bps, self.spc, self.reserved, self.nfats, self.root_entries, total16 = struct.unpack_from("<HBHBHH", b, 11)
        fat16 = struct.unpack_from("<H", b, 22)[0]
        total32 = struct.unpack_from("<I", b, 32)[0]
        self.fat_sectors = fat16 or struct.unpack_from("<I", b, 36)[0]
        total = total16 or total32
        need(self.bps == SECTOR and self.spc in (1, 2, 4, 8, 16, 32, 64, 128), "bad BPB geometry")
        need(self.nfats == 2 and total * SECTOR <= len(vol), "bad BPB counts")
        root_sectors = (self.root_entries * 32 + SECTOR - 1) // SECTOR
        self.data_start = self.reserved + self.nfats * self.fat_sectors + root_sectors
        self.root_start = self.reserved + self.nfats * self.fat_sectors
        self.clusters = (total - self.data_start) // self.spc
        self.bits = 12 if self.clusters < 4085 else 16 if self.clusters < 65525 else 32
        need((self.bits == 32) == (fat16 == 0), "FAT type and BPB disagree")
        fat_bytes = vol[self.reserved * SECTOR:(self.reserved + self.fat_sectors) * SECTOR]
        second = vol[(self.reserved + self.fat_sectors) * SECTOR:(self.reserved + 2 * self.fat_sectors) * SECTOR]
        need(fat_bytes == second, "FAT copies differ")
        self.fat = fat_bytes
        if self.bits == 32:
            need(struct.unpack_from("<I", b, 44)[0] == 2, "FAT32 root cluster unexpected")
            need(vol[6 * SECTOR:7 * SECTOR] == b, "FAT32 backup boot sector differs")
        self.root_cluster = 2 if self.bits == 32 else 0

    def next(self, c: int) -> int:
        if self.bits == 32:
            return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF
        if self.bits == 16:
            return struct.unpack_from("<H", self.fat, c * 2)[0]
        v = struct.unpack_from("<H", self.fat, c * 3 // 2)[0]
        return (v >> 4) if c & 1 else (v & 0xFFF)

    def chain(self, c: int) -> bytes:
        out = bytearray()
        seen = set()
        eoc = {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[self.bits]
        while c >= 2 and c < eoc:
            need(c not in seen and c < self.clusters + 2, "FAT chain loop or out of range")
            seen.add(c)
            off = (self.data_start + (c - 2) * self.spc) * SECTOR
            out += self.vol[off:off + self.spc * SECTOR]
            c = self.next(c)
        return bytes(out)

    def listdir(self, cluster: int) -> dict:
        raw = self.vol[self.root_start * SECTOR:(self.root_start + (self.root_entries * 32) // SECTOR) * SECTOR] \
            if cluster == 0 else self.chain(cluster)
        entries = {}
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[0] == 0x2E:
                continue
            name = e[0:8].decode("ascii").rstrip()
            ext = e[8:11].decode("ascii").rstrip()
            full = name + ("." + ext if ext else "")
            cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
            entries[full] = (e[11], cl, struct.unpack_from("<I", e, 28)[0])
        return entries

    def read(self, path: str) -> bytes:
        cluster = self.root_cluster
        parts = [p for p in path.split("/") if p]
        for index, part in enumerate(parts):
            entries = self.listdir(cluster)
            need(part in entries, f"{path}: {part} not found")
            attr, cl, size = entries[part]
            if index == len(parts) - 1:
                need(not attr & 0x10, f"{path} is a directory")
                return self.chain(cl)[:size]
            need(attr & 0x10, f"{part} is not a directory")
            cluster = cl
        raise CheckError("empty path")


def check_pe(data: bytes, arch: str) -> None:
    need(data[:2] == b"MZ", "not a PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    need(data[pe:pe + 4] == b"PE\0\0", "PE signature missing")
    need(struct.unpack_from("<H", data, pe + 4)[0] == PE_MACHINE[arch], f"PE machine is not {arch}")
    need(struct.unpack_from("<H", data, pe + 24 + 68)[0] == 10, "PE subsystem is not EFI application")


def check_disk(path: Path, boot_path: str, arch: str, expected: Path) -> dict:
    disk = path.read_bytes()
    start, end = check_gpt(disk)
    vol = disk[start * SECTOR:(end + 1) * SECTOR]
    fat = Fat(vol)
    data = fat.read(boot_path)
    check_pe(data, arch)
    need(data == expected.read_bytes(), "boot application in image differs from build artifact")
    return {"fat_bits": fat.bits, "sha256": hashlib.sha256(data).hexdigest(), "partition_lba": [start, end]}


def check_fat_volume(path: Path, boot_path: str, arch: str, expected: Path) -> dict:
    fat = Fat(path.read_bytes())
    data = fat.read(boot_path)
    check_pe(data, arch)
    need(data == expected.read_bytes(), "boot application in volume differs from build artifact")
    return {"fat_bits": fat.bits, "sha256": hashlib.sha256(data).hexdigest()}


if __name__ == "__main__":
    try:
        kind, image, boot, arch, artifact = sys.argv[1:6]
        fn = check_disk if kind == "disk" else check_fat_volume
        print(fn(Path(image), boot, arch, Path(artifact)))
    except (CheckError, ValueError) as exc:
        print(f"checkimage: FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
