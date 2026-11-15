# G17 — Developer Tools and Language Runtimes

Status: SOFTWARE_PASS (host-tested, ASan/UBSan clean; hardware HARDWARE_NOT_VERIFIED). Nothing here was run
on a Raspberry Pi 5. Package execution is host-tested; in-guest QEMU execution of native applications is G09.

## Implemented

- **EOSVM v1** (`kernel/eosvm.{h,c}`): bounded stack VM (18 opcodes, code <= 1024 B, 64-slot stack, 16 locals,
  output <= 1024 B, mandatory fuel limit). Bytecode is validated (opcodes, operands, jump targets) before
  running; DIV/MOD trap on zero and INT_MIN/-1; arithmetic wraps without UB. Includes a two-pass assembler
  with labels and error-line reporting.
- **Native editor** (`kernel/editor.{h,c}`): 4096-byte buffer, insert/delete/navigation/find, 128-entry undo
  journal, control bytes rejected atomically, save/load through `os_storage` (binary load rejected, buffer kept).
- **Runtime interface table** (`kernel/runtime_iface.{h,c}`): eosvm is available and executable; native-app
  and c-sdk are available but not executable through this API; python is compatibility-pending and inert.
- **SDK examples**: `sdk/examples/fib.eos`, `hello.eos` (outputs verified); AArch64 and x86-64 object builds
  of `hello.c` with the bundled clang, inspected for ELF `e_machine` (183 / 62) when the toolchain exists.
- **Development & Deployment package manager** (`kernel/devpkg.{h,c}`): EOSPKG v1 container
  (strict 8-key manifest, SHA-256 digest, HMAC-SHA256 signature from one of two provisioned keys), arch/ABI
  compatibility checks, package and version tracking, offline export/import, upgrade/downgrade/rollback
  (rollback only under recipient policy), recipient-side approval bound to digest, with expiry, single use,
  and atomic persistent state with rollback on write failure. Execution re-verifies the stored payload hash.
- **Python integration** (`tools/eospkg.py`): host tool that assembles, packages, signs and inspects; its
  output is byte-identical to the C assembler/builder, and a Python-built package is imported, installed
  and executed by the C manager (`G17_crosscheck_test.c`).
- **MCP exercise**: `os_dm_import_real_package` (host-side) lets the versioned MCP service inspect, check
  compatibility and propose/request authorization for a real package. The MCP tool set stays the frozen ten;
  there is no execute/install/approve tool; the model cannot approve; recipient approval, activation and
  execution stay host-side. Config JSON (manifest) is separate from the payload artifact.
- Tests: `G17_dev_test.c`, `G17_integration_test.c`, `G17_crosscheck_test.c`, `G17_test.py`,
  `scripts/test-devtools-host.sh` (plain and ASan/UBSan).

## Not implemented

- No guest Python interpreter: the python runtime is compatibility-pending (host tooling only).
- EOSVM is a new bounded VM, not native machine-code or Python execution; native apps use the G09 loader.
- Signatures are shared-key HMAC, not public-key; no key rotation or revocation.
- No persistent MCP transport and no remote deployment; the "node" model is the existing simulation.
- No on-device (Pi 5) editor UI, compiler or native build: AArch64 builds run on the development host.
- Hardware: HARDWARE_NOT_VERIFIED.
