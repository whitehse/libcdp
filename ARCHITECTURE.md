# ARCHITECTURE.md — libcdp

## Overview

libcdp is a pure C library that sits between the application and a
WebSocket transport layer. It does not perform any network I/O. Its
sole responsibility is:

1. **Parse** incoming CDP JSON messages (responses and events).
2. **Generate** outgoing CDP JSON commands.
3. **Classify** messages by domain and event type.
4. **Queue** parsed events for the caller to consume.

```
┌────────────┐   wire JSON    ┌────────────┐   cdp_event_t    ┌────────────┐
│  WebSocket │ ─────────────► │   libcdp   │ ────────────────► │  Caller    │
│  transport │ ◄───────────── │   cdp_ctx  │ ◄──────────────── │  application│
└────────────┘   wire JSON    └────────────┘   cdp_send_cmd()  └────────────┘
```

## Components

### 1. Context (`cdp_ctx_t`)

Opaque struct containing:
- Role (client or target)
- Configuration (queue size, max message size, auto-id)
- Command ID counter
- Session ID
- Ring-buffer event queue
- Linear output buffer

### 2. JSON Scanner

Not a full JSON DOM parser. Uses `json_find_key` to locate top-level
keys by scanning for `"key":` patterns. Handles nested braces/brackets
to extract sub-objects as raw strings. This keeps the code small and
avoids heap allocation for the JSON DOM.

### 3. Message Classification

Messages are classified by inspecting top-level keys:
- Has `"id"` and no `"method"` → **response** (success or error)
- Has `"method"` → **event**
- Events are further classified by domain (Page, DOM, Runtime, etc.)
  and mapped to specific `cdp_event_type_t` values.

### 4. Command Builders

Each domain function (e.g. `cdp_page_navigate`) formats a JSON command
string into the output buffer. Format:
```json
{"id":N,"method":"Domain.method","params":{...}}
```
If a session ID is set, it's appended as `"sessionId":"..."`.

### 5. Event Queue

Fixed-size ring buffer. Events are pushed on `cdp_feed_input` and
popped on `cdp_next_event`. When the queue is full, a
`CDP_EVENT_QUEUE_OVERFLOW` event replaces the oldest.

## Invariants

1. **No global state.** All state lives in `cdp_ctx_t`.
2. **No heap after init.** After `cdp_create`, no `malloc`/`free` in
   normal operation (ring buffer and output buffer are pre-allocated).
3. **Thread safety is the caller's responsibility.** The context is not
   internally synchronized.
4. **No network I/O.** The library never opens sockets or reads files.
5. **No JSON DOM.** We scan, not parse. No `json_object` tree is built.

## Deliberate Absences

| What's absent          | Why                                                     |
|------------------------|---------------------------------------------------------|
| WebSocket              | Caller manages transport (ADR-006)                      |
| Full JSON parser       | Scanning is sufficient for CDP's flat message structure |
| Callbacks              | Pull model is simpler and more predictable              |
| Network I/O            | Syscall-free design goal (ADR-001)                      |
| Thread safety          | Caller responsibility                                   |
| Dynamic queue growth   | Bounded memory usage; overflow is signaled              |

## Domain Support

libcdp supports the most commonly used CDP domains:
Page, DOM, Runtime, Network, Target, CSS, Emulation, Input, Browser.

Other domains are classified as `CDP_DOMAIN_OTHER` but the
`cdp_send_command` generic builder works for any domain.
