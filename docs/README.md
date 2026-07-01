# docs/README.md — libcdp Documentation

## Overview

libcdp is a pure C library for the Chrome DevTools Protocol. It provides
a simple, dependency-free API for building and parsing CDP messages.

## Quick Start

```c
#include "cdp.h"

int main(void) {
    cdp_ctx_t *ctx = cdp_create();

    /* Send a command */
    uint32_t id = cdp_page_navigate(ctx, "https://example.com");

    /* Get the JSON to send over WebSocket */
    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    /* send buf over your WebSocket... */

    /* When you receive a response from WebSocket */
    cdp_feed_input(ctx, response_json, response_len);
    cdp_event_t ev;
    while (cdp_next_event(ctx, &ev) == 0) {
        if (ev.type == CDP_EVENT_RESPONSE_RECEIVED) {
            /* process ev.data.response */
        }
    }

    cdp_destroy(ctx);
    return 0;
}
```

## API Reference

See the header file `include/cdp.h` for the full API. Key functions:

### Lifecycle
- `cdp_create()` / `cdp_create_with_config()` — create a context
- `cdp_destroy()` — free a context
- `cdp_reset()` — clear all state

### Communication
- `cdp_feed_input()` — feed incoming JSON (from WebSocket)
- `cdp_next_event()` — pop the next event from the queue
- `cdp_get_output()` — get outgoing JSON (to send over WebSocket)

### Commands
- `cdp_send_command()` — send any CDP command
- `cdp_page_*` — Page domain commands
- `cdp_dom_*` — DOM domain commands
- `cdp_runtime_*` — Runtime domain commands
- `cdp_network_*` — Network domain commands
- `cdp_css_*` — CSS domain commands
- `cdp_target_*` — Target domain commands
- `cdp_emulation_*` — Emulation domain commands
- `cdp_input_*` — Input domain commands

### Session Management
- `cdp_set_session_id()` — set the session ID for all commands
- `cdp_target_attach_and_set_session()` — attach to a target

## Building

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

## Further Reading

- [ARCHITECTURE.md](../ARCHITECTURE.md) — design and invariants
- [DOMAIN.md](DOMAIN.md) — CDP domain reference
- [ADR Index](decisions/) — architectural decision records
