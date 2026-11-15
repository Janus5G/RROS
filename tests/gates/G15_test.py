#!/usr/bin/env python3
"""Validate the optional RoboForge MCP adapter (host-controlled).

The adapter is a client-side, opt-in integration. A live exchange with a real
RoboForge gateway process is executed when a pinned checkout and Node.js are
available; it is NOT a ChromaNeural or hardware test.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PINNED = "b0dc46f860faedb6f00a36b2c636a2b31c2b290e"
CHECKOUT = Path(os.environ.get("ROBOFORGE_SRC", ROOT / "build" / "external" / "RoboForge"))

REQUIRED = [
    "G15-CONTRACT-THREE-FROZEN-TOOLS", "G15-CONTRACT-NO-ARBITRARY-DISPATCH",
    "G15-CONTRACT-REFRACT-IS-CONTROLLED", "G15-POLICY-DISABLED-BY-DEFAULT",
    "G15-TRANSPORT-DISABLED-DENIES-LOOPBACK", "G15-CALL-DISABLED-REFUSED",
    "G15-TRANSPORT-LOOPBACK-ALLOWED", "G15-TRANSPORT-REMOTE-ADDRESS-DENIED",
    "G15-TRANSPORT-FORGED-HOST-DENIED", "G15-TRANSPORT-CROSS-ORIGIN-DENIED",
    "G15-TRANSPORT-CROSS-SITE-DENIED", "G15-HTTP-HEADERS-ACCEPTED",
    "G15-HTTP-HEADERS-REJECTED-STATELESS-ONLY", "G15-LIFE-REAL-INITIALIZE-REPLY-ACCEPTED",
    "G15-CALL-REFUSED-BEFORE-FINGERPRINT-VERIFIED", "G15-SCHEMA-FROZEN-TOOLS-FINGERPRINT",
    "G15-SCHEMA-REAL-TOOLS-LIST-VERIFIED", "G15-SCHEMA-CHANGE-REVOKES-VERIFICATION",
    "G15-SCHEMA-EXTRA-OR-MISSING-TOOLS-DENIED", "G15-SCHEMA-MALFORMED-LIST-DENIED",
    "G15-UNTRUSTED-REAL-CATALOG-FLAGGED-UNTRUSTED", "G15-CALL-REAL-STATUS-REPLY-ACCEPTED",
    "G15-SCHEMA-MODULE-ID-PATTERN-ENFORCED", "G15-SCHEMA-MODULE-ID-MAX-LENGTH-ACCEPTED",
    "G15-REFRACT-DISABLED-WITHOUT-OPT-IN", "G15-REFRACT-SOURCE-BOUNDS-ENFORCED",
    "G15-REFRACT-SOURCE-JSON-ESCAPED", "G15-REFRACT-REAL-DISABLED-REPLY-IS-TOOL-ERROR",
    "G15-RECOVERY-CLIENT-READY-AFTER-TOOL-ERROR", "G15-LIMITS-REPLY-32KIB-ENFORCED",
    "G15-LIMITS-REQUEST-12KIB-SOURCE-4096", "G15-RECOVERY-OVERFLOW-LEAVES-CLIENT-IDLE",
    "G15-RECOVERY-OFFLINE-GATEWAY-TIMES-OUT",
]

LIVE_REQUIRED = [
    "G15-LIVE-INITIALIZE", "G15-LIVE-TOOLS-FINGERPRINT-MATCHES-PINNED", "G15-LIVE-CATALOG",
    "G15-LIVE-STATUS-KNOWN-MODULE", "G15-LIVE-UNKNOWN-MODULE-REJECTED-BY-GATEWAY",
    "G15-LIVE-RECOVERS-AFTER-REMOTE-ERROR", "G15-LIVE-REFRACT-REFUSED-WITHOUT-OPT-IN",
]


def check(condition: bool, test_id: str, message: str) -> None:
    if not condition:
        raise AssertionError(f"FAIL {test_id}: {message}")
    print(f"PASS {test_id}: {message}")


def wsl_wrap(command: list[str]) -> list[str]:
    root = subprocess.run(["wsl", "--exec", "wslpath", "-a", str(ROOT)], text=True,
                          encoding="utf-8", capture_output=True, check=True).stdout.strip()
    shell_command = "cd " + shlex.quote(root) + " && " + " ".join(shlex.quote(p) for p in command)
    return ["wsl", "--exec", "sh", "-lc", shell_command]


def run_command(command: list[str]) -> str:
    print(f"COMMAND: {' '.join(command)}")
    argv = wsl_wrap(command) if os.name == "nt" else command
    result = subprocess.run(argv, cwd=ROOT, text=True, encoding="utf-8", stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}):\n{result.stdout}")
    return result.stdout.strip()


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_gateway(enabled: bool, port: int, extra: dict[str, str] | None = None) -> subprocess.Popen:
    env = {k: v for k, v in os.environ.items() if not k.startswith(("ROBOFORGE_", "REFRACT_"))}
    env["PORT"] = str(port)
    env["HOST"] = "127.0.0.1"
    if enabled:
        env["ROBOFORGE_MCP_ENABLED"] = "1"
    env.update(extra or {})
    proc = subprocess.Popen(["node", "server.js"], cwd=CHECKOUT / "backend", env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    deadline = time.time() + 15
    while time.time() < deadline:
        with socket.socket() as s:
            s.settimeout(0.2)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return proc
        if proc.poll() is not None:
            raise RuntimeError("RoboForge backend exited: " + (proc.stdout.read() if proc.stdout else ""))
        time.sleep(0.1)
    proc.kill()
    raise RuntimeError("RoboForge backend did not start")


def post(port: int, body: str, headers: dict[str, str] | None = None, method: str = "POST"):
    h = {"Content-Type": "application/json", "Accept": "application/json"}
    h.update(headers or {})
    req = urllib.request.Request(f"http://127.0.0.1:{port}/mcp", data=body.encode() if method == "POST" else None,
                                 headers=h, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def static_contract_checks() -> None:
    source = (CHECKOUT / "backend" / "mcp" / "gateway.js").read_text(encoding="utf-8")
    check("MAX_REQUEST=12288" in source.replace(" ", "") and "MAX_REPLY=32768" in source.replace(" ", ""),
          "G15-SOURCE-LIMITS", "real gateway limits are 12288 request and 32768 reply bytes")
    names = re.findall(r"name:'(roboforge_[a-z_0-9]+)'", source)
    adapter = (ROOT / "kernel" / "roboforge_adapter.c").read_text(encoding="utf-8")
    adapter_names = re.findall(r'\{ "(roboforge_[a-z0-9_]+)"', adapter)
    check(sorted(set(names)) == sorted(adapter_names) and len(adapter_names) == 3, "G15-SOURCE-TOOL-NAMES-MATCH",
          "adapter contracts equal the tools defined by the real gateway")
    check("'2025-11-25'" in source and "'2025-06-18'" in source and "'2025-03-26'" in source,
          "G15-SOURCE-PROTOCOL-VERSIONS", "real gateway supports the three protocol versions the adapter allows")
    check(f"{PINNED}" == subprocess.run(["git", "-C", str(CHECKOUT), "rev-parse", "HEAD"], text=True,
                                         capture_output=True).stdout.strip(),
          "G15-SOURCE-PINNED-REVISION", "checkout is the pinned RoboForge revision")


def live_session() -> list[str]:
    port = free_port()
    proc = start_gateway(True, port)
    try:
        argv = wsl_wrap(["build/g15/roboforge-session"]) if os.name == "nt" else ["build/g15/roboforge-session"]
        session = subprocess.Popen(argv, cwd=ROOT, text=True, encoding="utf-8", stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        lines: list[str] = []
        assert session.stdin and session.stdout
        while True:
            line = session.stdout.readline()
            if not line:
                break
            line = line.rstrip("\r\n")
            if line.startswith(("REQ ", "NOTIFY ")):
                kind, body = line.split(" ", 1)
                status, reply = post(port, body)
                if kind == "NOTIFY":
                    check(status == 202, "G15-LIVE-NOTIFICATION-ACCEPTED", "gateway acknowledged notification with 202")
                    session.stdin.write("NONE\n")
                else:
                    check(status == 200, "G15-LIVE-HTTP-200", "gateway replied 200 to a request")
                    session.stdin.write("RSP " + reply + "\n")
                session.stdin.flush()
            else:
                lines.append(line)
                print(line)
        session.wait(timeout=20)
        check(session.returncode == 0, "G15-LIVE-SESSION-EXIT", "adapter session exited cleanly")
        return lines
    finally:
        proc.terminate()
        proc.wait(timeout=10)


def live_transport_checks() -> None:
    off = free_port()
    proc = start_gateway(False, off)
    try:
        status, _ = post(off, "{}")
        check(status == 404, "G15-LIVE-GATEWAY-DISABLED-BY-DEFAULT", "gateway answers 404 unless explicitly enabled")
    finally:
        proc.terminate()
        proc.wait(timeout=10)
    port = free_port()
    proc = start_gateway(True, port)
    try:
        check(post(port, "", method="GET")[0] == 405, "G15-LIVE-GET-REJECTED", "stateless gateway rejects GET")
        check(post(port, "{}", {"Host": "attacker.invalid"})[0] == 403, "G15-LIVE-FORGED-HOST-REJECTED",
              "gateway rejects forged Host")
        check(post(port, "{}", {"Origin": "http://evil.test"})[0] == 403, "G15-LIVE-CROSS-ORIGIN-REJECTED",
              "gateway rejects cross origin")
        check(post(port, "{}", {"Mcp-Session-Id": "x"})[0] == 400, "G15-LIVE-SESSION-HEADER-REJECTED",
              "gateway rejects session ids")
        big = '{"jsonrpc":"2.0","id":1,"method":"ping","params":{"x":"' + "a" * 13000 + '"}}'
        check(post(port, big)[0] == 413, "G15-LIVE-OVERSIZE-REJECTED", "gateway rejects requests over 12 KiB")
        status, body = post(port, json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                              "params": {"protocolVersion": "2025-11-25"}}))
        check(status == 200 and json.loads(body)["result"]["protocolVersion"] == "2025-11-25",
              "G15-LIVE-PROTOCOL-2025-11-25", "gateway negotiates 2025-11-25")
        status, body = post(port, json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                                              "params": {"name": "bash", "arguments": {}}}))
        check(json.loads(body).get("error", {}).get("code") == -32602, "G15-LIVE-UNKNOWN-TOOL-REJECTED",
              "gateway refuses tools it does not define")
    finally:
        proc.terminate()
        proc.wait(timeout=10)


def main() -> int:
    output = run_command(["sh", "scripts/test-roboforge-host.sh"])
    print(output)
    check("FAIL G15" not in output, "G15-NO-FAILURES", "host tests reported no failures")
    check(output.count("G15 RESULT: PASS") == 2, "G15-HOST", "adapter tests pass in plain and ASan/UBSan builds")
    check("PASS G15-ROBOFORGE-SANITIZERS:" in output, "G15-SANITIZERS", "adapter passes ASan and UBSan")
    for test_id in REQUIRED:
        check(f"PASS {test_id}\n" in output + "\n", test_id, "required acceptance check present and passing")

    forbidden = re.compile(r"\b(system|exec[lv]p?e?|popen|fopen|open|fork|ioctl|syscall|socket|connect|bind|listen)\s*\(")
    for name in ("roboforge_adapter.c", "mcp_client.c"):
        src = (ROOT / "kernel" / name).read_text(encoding="utf-8")
        check(not forbidden.search(src), f"G15-NO-AUTHORITY-{name}",
              f"{name} has no process, file, socket or hardware path")
    adapter = (ROOT / "kernel" / "roboforge_adapter.c").read_text(encoding="utf-8")
    check(not re.search(r"os_dm_|os_mcp_registry_approve|os_mcp_server_set_owner", adapter), "G15-ADAPTER-NO-PRIVILEGE",
          "adapter does not reach deployment, approval or owner-confirmation functions")
    others = " ".join(p.read_text(encoding="utf-8", errors="ignore")
                      for p in (ROOT / "kernel").glob("*.c") if p.name not in ("roboforge_adapter.c", "compat_registry.c"))
    check("roboforge" not in others.lower(), "G15-NOT-A-REMOTE-TRANSFER-SERVICE",
          "no other kernel module references or proxies the RoboForge gateway")

    live_status = "NOT_RUN"
    if shutil.which("node") and (CHECKOUT / "backend" / "server.js").exists():
        static_contract_checks()
        live_transport_checks()
        lines = live_session()
        joined = "\n".join(lines) + "\n"
        check("FAIL G15" not in joined and "G15-LIVE RESULT: PASS" in joined, "G15-LIVE-SESSION",
              "adapter session against a real local RoboForge gateway passed")
        for test_id in LIVE_REQUIRED:
            check(f"PASS {test_id}\n" in joined, test_id, "required live check present and passing")
        live_status = "EXECUTED_PASS"
    else:
        print("WARNING G15-LIVE: Node.js or the pinned RoboForge checkout is unavailable; live checks NOT RUN.")
    print(f"G15 live RoboForge gateway session: {live_status} (not a ChromaNeural client test, not hardware)")

    progress = (ROOT / "docs" / "G15_PROGRESS.md").read_text(encoding="utf-8")
    check("Not implemented" in progress and "HARDWARE_NOT_VERIFIED" in progress and "ChromaNeural" in progress,
          "G15-LIMITS-RECORDED", "progress record lists unimplemented scope and unverified integration")
    print("G15 software acceptance checks complete; hardware remains HARDWARE_NOT_VERIFIED.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL G15: {exc}", file=sys.stderr)
        raise SystemExit(1)
