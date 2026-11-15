# G18 — Browser and Shared Application Services

Status: SOFTWARE_PASS (host-tested, ASan/UBSan clean; hardware HARDWARE_NOT_VERIFIED). Nothing here was run
on a Raspberry Pi 5 and nothing touched a real network.

## Implemented

- **URL and HTTP layer** (`kernel/browser_net.{h,c}`): strict http(s) URL parsing/resolution with dot-segment
  normalization; rejects userinfo, control characters, `%00`, `javascript:`/`data:`/other schemes. HTTP/1.1 GET
  builder (no cookies or credentials) and a bounded response parser that rejects chunked/compressed bodies,
  bare LF, conflicting or inexact Content-Length. Host allow-list policy (empty list denies everything),
  at most 3 redirects with the policy re-checked at each hop. The transport is an injected callback.
- **HTML subset** (`kernel/browser_html.{h,c}`): bounded parser (8 KiB, 192 nodes, depth 16), text renderer with
  wrapping, numbered links, image/input placeholders. `style` is dropped; inline `on*` handlers are counted and
  never executed. Malformed or over-limit input fails closed.
- **ESJS v1** (`kernel/browser_js.{h,c}`): sandboxed integer/string script subset with fuel, output, variable and
  string-pool limits. No functions, objects, DOM, network or storage access.
- **Images** (`kernel/browser_image.{h,c}`): strict PPM (P6) and 24-bit BMP decoders up to 128x128 with ASCII view.
- **Browser session** (`kernel/browser.{h,c}`): atomic navigation, history, link following, scroll/viewport,
  script output display, image fetch with content-type checking.
- **Document service and printing** (`kernel/docsvc.{h,c}`): bounded text documents, pagination, save/load via
  `os_storage`, print spooler with owner approval bound to a job digest, single-use, cancel, and a preview
  backend. The hardware backend always fails closed.

## Verification

`scripts/test-browser-host.sh` builds `tests/gates/G18_browser_test.c` plain and with ASan/UBSan; every
check must pass in both builds (`tests/gates/G18_test.py`). Transport is a fixture; no sockets are used.

## Not implemented (compatibility limits)

- No network stack and no TLS: https is refused; no real internet or server was contacted.
- No CSS, no DOM access from scripts, no forms submission, cookies, chunked or compressed responses.
- ESJS is not a general JavaScript engine; real-world sites will not work.
- No PNG/JPEG/GIF decoders (refused); only PPM and BMP.
- No printer driver: only the memory preview sink works.
- Browser is not wired into the compositor/GUI.

## Hardware status

HARDWARE_NOT_VERIFIED. Real-site, TLS and printer compatibility are compatibility-pending.
