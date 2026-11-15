# G13 Native AI Core progress

Status: SOFTWARE_PASS. Hardware: HARDWARE_NOT_VERIFIED.

Acceptance basis: host, ASan/UBSan and x86-64 / AArch64 QEMU guest runs only.
Nothing here was run on Raspberry Pi 5 hardware, an accelerator or a real LLM
runtime.

## Implemented

- `kernel/ai_core.{h,c}`: service layer free of privileged kernel state.
  - Model registry: "EAIM" image with magic, version, bounded name, bounded
    edge count and FNV-1a integrity checksum. Duplicate, truncated, bad-magic
    and bad-checksum images are rejected. Per-service memory budget; a model
    with active jobs cannot be unregistered.
  - Inference: byte-level bigram model with deterministic greedy decoding.
    This is real local inference proving the pipeline, but a reference runtime
    only. It is not an LLM.
  - Bounded jobs: fixed job table, bounded prompt and output, per-step token
    cap, incremental reads by offset, cancellation that keeps partial output.
  - Capability bits: register, submit, cancel, read.
- `kernel/ai_worker.{h,c}`: process-isolated worker on the RROS process model.
  - The worker is its own process. Its service state lives in heap memory
    owned by the worker process and reserved in the worker's address space, so
    other processes cannot reach it (`os_ai_worker_memory_reachable` is 0 for
    parent and clients; the heap refuses frees by other owners).
  - Clients use bounded request messages with copy-in and copy-out and never
    receive pointers into worker memory. Oversize input is rejected early.
  - Capabilities are granted per client process by the worker's parent only;
    revoking removes access and that client's jobs.
  - Jobs are bound to the submitting client; other clients cannot read, cancel,
    query or release them.
  - Crash containment: if the worker process terminates, clients get a clean
    `OS_AI_WORKER_ERR_DEAD`, other processes keep running, the parent stops
    the worker (memory erased and freed, process reaped) and restarts a clean
    worker with no retained registry or jobs.
  - Model output is data only. Neither module has an execution, file or
    hardware path (checked by `tests/gates/G13_test.py`).
- `kernel/ai_selftest.c`: one self-test run on the host and inside both guests.
- Guest integration: `EXPERIMENTALOS_G13_TEST` builds for x86-64 and AArch64,
  runner `scripts/run-qemu-input.py <arch> G13`.

## Not implemented (outside G13 acceptance; not claimed)

- Hardware (MMU/privilege) isolation of the worker: isolation is enforced by
  the RROS process model, ownership tracking and message interface, the same
  level as the existing G05 process model in these guests.
- GGUF/ONNX or other external model formats and runtimes; any real LLM.
- Scheduling the worker thread on a preemptive timer; pumping is cooperative.
- External provider adapters and credential handling (G14+ scope).
- Any accelerator or Raspberry Pi 5 hardware execution: HARDWARE_NOT_VERIFIED.
