# AGENTS.md — libcdp

## Project

**libcdp** is a pure C library for the Chrome DevTools Protocol (CDP).
It parses and generates CDP JSON messages. It is:

- **Syscall-free** — no direct system calls in the library code.
- **Callback-free** — no callback registrations; the caller drives the loop.
- **Transport-agnostic** — the caller owns the WebSocket connection.

The library operates at the *message* level: given a JSON string from the
wire it classifies it (command / response / event), extracts key fields,
and pushes a typed `cdp_event_t` onto an internal ring buffer. Conversely,
the caller invokes typed command builders (e.g. `cdp_page_navigate`) and
reads the resulting JSON from an output buffer.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design.

Key invariants:
- Every `cdp_ctx_t` is self-contained; no global state.
- The event queue is a fixed-size ring buffer (default 64 entries).
- Command IDs auto-increment unless `auto_id` is disabled in config.
- JSON parsing is *scanning*, not a full DOM parser — only top-level
  keys are extracted via `json_find_key`.
- Output is appended linearly and consumed on `cdp_get_output`.

## ADRs

All architectural decision records live in `docs/decisions/`. See
ADR-006 for the plumbing model (caller manages transport).

## Build

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

## Testing

| Test            | Purpose                                      |
|-----------------|----------------------------------------------|
| cdp_smoke       | Core lifecycle, command builders, parsing    |
| cdp_dialectic   | Client ↔ Target round-trip verification      |
| cdp_errors      | Edge cases: NULL, malformed, overflow        |

## Fuzzing

```bash
cmake -B build -S . -DCDP_FUZZ=ON
cmake --build build
./build/cdp_fuzz_feed corpus/
```
