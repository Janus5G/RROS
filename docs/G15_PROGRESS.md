# G15 — RoboForge Optional MCP Integration

Status: SOFTWARE_PASS (software only; hardware HARDWARE_NOT_VERIFIED). Pinned RoboForge revision:
`b0dc46f860faedb6f00a36b2c636a2b31c2b290e` (Janus5G/RoboForge, `backend/mcp/gateway.js`).

## Implemented

- `kernel/roboforge_adapter.{h,c}`: optional, opt-in client-side adapter on the G14 MCP client.
  - Three frozen tools: `roboforge_catalog_v1`, `roboforge_module_status_v1` (metadata only) and
    `roboforge_refract_compile_cpl_v1` (controlled; separate opt-in; refused by default).
  - Disabled by default; calls are refused until the `tools/list` result fingerprint (SHA-256 of the
    result object) matches the pinned definition set. Any schema change, extra or missing tool revokes it.
  - Parameter validation before sending: `module_id` matches `^[a-z][a-z0-9-]{0,63}$`; compile source is
    1..4096 bytes and JSON-escaped.
  - Loopback-only transport rules (remote address, Host, Origin, `sec-fetch-site`), stateless-only HTTP
    header rules (no session id, JSON content type, supported protocol versions 2025-11-25/06-18/03-26).
  - 12288-byte request and 32768-byte reply limits; failures leave the client idle and recoverable.
  - Tool output is untrusted data, never acted on; no shell, flashing, actuation or generic dispatch.
- The adapter is not used by any other kernel module and is not a remote Pi transfer service.
- Tests: `tests/gates/G15_roboforge_test.c` (fixtures captured from the real gateway in
  `tests/gates/fixtures/roboforge/`), `G15_roboforge_session.c` + `G15_test.py` (live session), run by
  `scripts/test-roboforge-host.sh` plain and under ASan/UBSan.

## Verified

- Host unit/contract tests against real captured gateway replies, plain and ASan/UBSan.
- Live exchange: the adapter code, driven through an HTTP transport script, against a real RoboForge
  backend process started locally from the pinned checkout with `ROBOFORGE_MCP_ENABLED=1`
  (handshake, tools/list fingerprint, catalog, status, remote error recovery, disabled-by-default,
  GET/forged Host/cross-origin/session/oversize rejection, protocol 2025-11-25).
  The live checks run only when Node.js and the pinned checkout (`build/external/RoboForge` or
  `ROBOFORGE_SRC`) are present; otherwise they are reported NOT_RUN.

## Not implemented / limits

- No ChromaNeural client test and no production enablement; RoboForge's own handover requires that separately.
- The transport script is a test harness; the kernel has no network stack use for this adapter.
- Refract compilation is not verified with a real Refract Studio compiler; only the disabled/refusal paths
  and request construction are tested. RoboForge's own mock-based Refract test needs Python and is not part of this gate.
- Our JSON parser limits strings to 1024 bytes, so compile sources above that are refused by the client
  even though the gateway allows 4096.
- Janus inference, REA MCP, industrial services and managed-grant (ChromaNeural) execution authorization
  are not implemented.
- Hardware: HARDWARE_NOT_VERIFIED.
