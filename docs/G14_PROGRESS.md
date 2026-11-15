# G14 progress — MCP framework

Status: **SOFTWARE_PASS** (host, ASan/UBSan). Hardware: **HARDWARE_NOT_VERIFIED**.

## Implemented

| Module | Purpose |
| --- | --- |
| `kernel/sha256.{h,c}` | Streaming SHA-256 (FIPS 180-4 vectors tested). |
| `kernel/jsonrpc.{h,c}` | Strict JSON-RPC 2.0 request validator (size, depth, key-count, UTF-8, duplicate/escaped keys, batches, null ids, array params, unknown members, `rpc.` methods) plus bounded member readers. |
| `kernel/mcp_registry.{h,c}` | Tool registry: SHA-256 schema fingerprints, per-tool owner approval bound to a fingerprint, expiry, capability checks, owner confirmation for side-effecting tools, replay window, schema-change (rug-pull) invalidation, revocation, hash-chained audit log that fails closed when full. |
| `kernel/mcp_server.{h,c}` | MCP server: lifecycle (initialize / initialized / ready / closed), version negotiation (`2025-06-18`, `2025-03-26`), `tools/list`, `tools/call`, `ping`, cancellation, per-connection call quota, single in-flight call, server-enforced timeout, abusive-connection close, escaped/bounded/flagged untrusted tool output. |
| `kernel/mcp_client.{h,c}` | MCP client: request building with outbound validation, response validation against the outstanding id, version check, client timeout, cancellation; results are treated as untrusted data. |
| `kernel/mcp_deploy.{h,c}` | Ten frozen `experimentalos_*_v1` Development & Deployment Manager contracts and a simulated manager (packages, nodes, plans). |

Frozen contract fingerprints are pinned in `tests/gates/G14_mcp_integration_test.c`.

## Authority boundaries

MCP can only reach the ten tool handlers. Signing (`os_dm_sign_package`), key provisioning,
node enrollment, recipient approval (`os_dm_recipient_approve`), activation (`os_dm_activate`),
rollback policy and owner confirmation are separate host-side service entry points with no wire
method. `experimentalos_install_authorize_v1` only records a request and rejects any
`approved`/`approval`/`digest`/`expiry` input. The build tool creates unsigned packages only.
Rollback needs both owner confirmation and a recipient-side rollback policy.

## Verification

`sh scripts/test-mcp-host.sh` (plain and ASan/UBSan) covers: lifecycle and version negotiation,
schema fingerprints and rug-pull rejection, untrusted output injection, authorization,
capabilities, owner confirmation, destination allow-list, quotas, timeouts, cancellation,
failure recovery and reconnect, malformed server messages, and a deployment simulation over
four simulated nodes covering tampering, malformed metadata, compatibility, dependency,
resource quota, interruption, install failure, upgrade, rollback and authorized activation.
`python scripts\gates\run_gate.py G14` records the formal evidence.

## Not implemented

- No real transport: the client and server are connected by an in-memory loopback only.
- No guest (QEMU) integration of the MCP stack; host tests only.
- Package "signatures" are a keyed SHA-256 stand-in with symmetric provisioned keys, not public-key signatures.
- Deployment is simulated in memory; there are no real packages, files, network nodes or Pi 5 targets.
- The audit log is in memory and bounded (256 entries); persistence is not implemented.
- RoboForge compatibility is G15 and is not started.
- Real Raspberry Pi 5 behaviour remains HARDWARE_NOT_VERIFIED.
