# Electrical pre-power review record

Status: **NOT_VERIFIED**. This is a review checklist, not permission to connect
or energize equipment. Complete it for one exact configuration, preserve the
evidence, and obtain independent engineering approval of a separate physical
procedure. No live measurement or power-up sequence is authorized here.
Software PASS never completes a row below.

For the distinction between disconnected inspection, separately reviewed
electrical verification and blocked physical operations, see the
[English](COMMUNITY_SAFETY.md) or [Danish](COMMUNITY_SAFETY.da.md) community
guide. An unpowered continuity check requires the separately defined and
independently reviewed E05/E07 procedure; it does not authorize connections
or live testing.

Record release/source SHA, board revision/RAM/stepping, accessory revisions,
PSU and cable identifiers, proposed connections, reviewer, review date and
document revisions. Redact personal identifiers before sharing the record.

| ID | Required action and acceptance criterion | Evidence required | Result |
|---|---|---|---|
| E01 | Match board and every connector to the exact vendor drawing. Establish pin 1, viewing direction, connector numbering and accessory compatibility independently. | Annotated vendor drawing and second-review record; no inferred pinout from wire colours or a different Pi. | NOT_VERIFIED |
| E02 | Verify the PSU label, output profiles, approved cable and board power-entry method against vendor requirements. The BOM's 5.1 V / 5 A supply capability is not a measured board current budget. | Label photographs, vendor specifications and independent output-measurement record from an approved engineering procedure. | NOT_VERIFIED |
| E03 | Account for the board, cooling and each accessory's maximum/starting current; check cable, connector and separate peripheral-port limits. Record required margin and supply profile. | Itemized worst-case current budget and vendor limits. Unknown current or missing limit means STOP. | NOT_VERIFIED |
| E04 | Verify GPIO/UART logic compatibility and absolute limits at both endpoints. A 5 V signal or an adapter power pin must not be assumed compatible with a 3.3 V input. | Exact endpoint specifications and measured adapter signal levels; qualified reviewer selects a safe measurement method. | NOT_VERIFIED |
| E05 | Complete the proposed pin-to-pin table below, including common reference, signal direction and every unused/power conductor. Exclude unintended power injection, backfeeding and competing supplies. | Signed pinout/polarity review, connector-orientation photographs and continuity evidence from an unpowered inspection procedure. | NOT_VERIFIED |
| E06 | Inspect cable damage, exposed conductors, connector offset/reversal, standoffs, screws, cooler/enclosure clearances, debris and conductive work surfaces. | Disconnected photographs and inspection record; no unresolved mechanical short-circuit path. | NOT_VERIFIED |
| E07 | Have a qualified engineer define circuit-appropriate unpowered continuity/short checks, instrument settings and acceptance limits. A continuity beep alone does not establish a safe power rail. | Instrument identification, proposed points, readings, acceptance criteria and independent interpretation. | NOT_VERIFIED |
| E08 | Establish per-pin, aggregate GPIO, debug-adapter and peripheral current limits; no motors, relays, actuators, batteries or external power stages belong in this review configuration. | Vendor limits and load budget for every proposed conductor; no guessed numeric GPIO limit. | NOT_VERIFIED |
| E09 | Define a separately reviewed incremental power-up plan with isolation, current/voltage monitoring, cooling, shutdown/recovery method and explicit stop thresholds. | Board-specific engineering procedure and completed E01-E08 evidence. Do not execute it under this document. | NOT_VERIFIED |
| E10 | Reconcile all results with SAFETY_GATE.json, firmware compatibility and target-entry restrictions. Obtain independent approval before any physical procedure is enabled. | Approval tied to the exact configuration, source revision and artifact SHA-256; missing/failed/contradictory evidence means STOP. | NOT_VERIFIED |

## Proposed connection record (fill before review, not before wiring)

| Board connector and physical pin | Signal and direction | Reviewed voltage/current limit | Other endpoint and physical pin | Power/backfeed risk | Drawing revision and independent check |
|---|---|---|---|---|---|
| NOT_VERIFIED | NOT_VERIFIED | NOT_VERIFIED | NOT_VERIFIED | NOT_VERIFIED | NOT_VERIFIED |

Measurement records must identify the instrument/range, conditions, units,
observed value, uncertainty/acceptance criterion and reviewer. Any measurement
requiring energized equipment remains NOT_VERIFIED until a separate qualified
procedure is approved and executed; this checklist supplies no live steps.

Stop for uncertain pinout/polarity, damaged parts, shorts, incompatible logic,
missing current budget, unapproved firmware or any unresolved critical gate.
Unexpected heat, smell, undervoltage, overcurrent or resets are also mandatory
stop conditions in any subsequently approved procedure, not prompts to improvise
live troubleshooting.
