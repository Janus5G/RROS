#!/usr/bin/env python3
"""Capture a serial console (Pi 5 debug UART) to a timestamped log with a SHA-256 sidecar.

Requires pyserial (pip install pyserial). Settings: 115200 8N1, no flow control, 3.3 V adapter.
Read-only toward the board: this tool never transmits on the port.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import re
import sys
import time
from pathlib import Path


def capture_label(value: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", value):
        raise argparse.ArgumentTypeError("label must be 1-64 ASCII letters/digits/hyphens/underscores, starting with a letter or digit")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=int, default=180, help="capture duration (default 180)")
    parser.add_argument("--out", default="logs/pi5_hardware", help="output directory")
    parser.add_argument("--label", default="session", type=capture_label,
                        help="safe 1-64 character test label (letters, digits, hyphens, underscores)")
    parser.add_argument("--stop-on", default="", help="stop early after this text appears")
    args = parser.parse_args()

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        print("pyserial is required: pip install pyserial", file=sys.stderr)
        return 2
    if args.list:
        for port in list_ports.comports():
            print(f"{port.device}\t{port.description}\tVID:PID={port.vid}:{port.pid}\tSN={port.serial_number}")
        return 0
    if not args.port:
        print("--port is required (use --list)", file=sys.stderr)
        return 2

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    started = dt.datetime.now(dt.timezone.utc)
    log = out_dir / f"{started:%Y%m%dT%H%M%SZ}_{args.label}.serial.log"
    deadline = time.monotonic() + args.seconds
    seen = ""
    with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1, timeout=0.2,
                       xonxoff=False, rtscts=False, dsrdtr=False) as port, log.open("xb") as handle:
        handle.write(f"# capture start {started.isoformat()} port={args.port} baud={args.baud} label={args.label}\n".encode())
        while time.monotonic() < deadline:
            data = port.read(4096)
            if not data:
                continue
            handle.write(data)
            handle.flush()
            if args.stop_on:
                seen = (seen + data.decode("ascii", errors="replace"))[-4096:]
                if args.stop_on in seen:
                    break
        handle.write(f"\n# capture end {dt.datetime.now(dt.timezone.utc).isoformat()}\n".encode())
    digest = hashlib.sha256(log.read_bytes()).hexdigest()
    with Path(str(log) + ".sha256").open("x", encoding="ascii") as sidecar:
        sidecar.write(f"{digest}  {log.name}\n")
    print(f"{log} sha256={digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
