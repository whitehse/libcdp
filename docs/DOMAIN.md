# docs/DOMAIN.md — CDP Domain Reference

## Chrome DevTools Protocol Overview

The Chrome DevTools Protocol (CDP) provides a way to instrument Chromium-
based browsers. Communication happens over WebSocket using JSON messages.

## Message Types

### Commands (Client → Browser)

```json
{
    "id": 1,
    "method": "Page.navigate",
    "params": {"url": "https://example.com"}
}
```

### Responses (Browser → Client)

Success:
```json
{
    "id": 1,
    "result": {"frameId": "ABC", "loaderId": "DEF"}
}
```

Error:
```json
{
    "id": 1,
    "error": {"code": -32600, "message": "Invalid Request"}
}
```

### Events (Browser → Client)

```json
{
    "method": "Page.loadEventFired",
    "params": {"timestamp": 1234.5}
}
```

## WebSocket Transport Model

libcdp does **not** manage the WebSocket connection. The caller is
responsible for:

1. Connecting to the browser's WebSocket endpoint (e.g., `ws://localhost:9222/devtools/page/...`)
2. Sending the JSON from `cdp_get_output()` over the WebSocket
3. Receiving JSON from the WebSocket and passing it to `cdp_feed_input()`
4. Processing events from `cdp_next_event()`

## Session Management

When connecting to a browser with multiple targets (tabs), each target
has a session ID. Commands must include the `sessionId` field to target
a specific tab.

libcdp manages this via:
- `cdp_set_session_id()` — sets the session ID for subsequent commands
- `cdp_target_attach_and_set_session()` — attaches to a target and sets the session

## Supported Domains

### Page
Page lifecycle and navigation.

| Method | Description |
|--------|-------------|
| `Page.navigate` | Navigate to a URL |
| `Page.reload` | Reload the page |
| `Page.captureScreenshot` | Capture a screenshot |
| `Page.setDocumentContent` | Set the document HTML |

### DOM
Document Object Model inspection and manipulation.

| Method | Description |
|--------|-------------|
| `DOM.getDocument` | Get the document root |
| `DOM.getOuterHTML` | Get outer HTML of a node |
| `DOM.setOuterHTML` | Set outer HTML of a node |
| `DOM.setAttributesAsText` | Set attributes as text |
| `DOM.removeNode` | Remove a node |
| `DOM.querySelector` | Query for a single element |
| `DOM.querySelectorAll` | Query for all matching elements |

### Runtime
JavaScript execution and inspection.

| Method | Description |
|--------|-------------|
| `Runtime.evaluate` | Evaluate a JavaScript expression |
| `Runtime.callFunctionOn` | Call a function on an object |
| `Runtime.getProperties` | Get properties of an object |

### Network
Network monitoring and cookie management.

| Method | Description |
|--------|-------------|
| `Network.enable` | Enable network monitoring |
| `Network.disable` | Disable network monitoring |
| `Network.getCookies` | Get all cookies |
| `Network.setCookie` | Set a cookie |
| `Network.clearBrowserCookies` | Clear all cookies |

### CSS
Stylesheet inspection and modification.

| Method | Description |
|--------|-------------|
| `CSS.enable` | Enable CSS domain |
| `CSS.disable` | Disable CSS domain |
| `CSS.getStyleSheetText` | Get stylesheet text |
| `CSS.setStyleText` | Set style text |

### Target
Target (tab/page) management.

| Method | Description |
|--------|-------------|
| `Target.getTargets` | List all targets |
| `Target.createTarget` | Create a new target |
| `Target.closeTarget` | Close a target |
| `Target.attachToTarget` | Attach to a target |
| `Target.activateTarget` | Activate a target |

### Emulation
Device and user-agent emulation.

| Method | Description |
|--------|-------------|
| `Emulation.setDeviceMetricsOverride` | Set device metrics |
| `Emulation.setUserAgentOverride` | Set user agent |

### Input
Input event dispatch.

| Method | Description |
|--------|-------------|
| `Input.dispatchMouseEvent` | Dispatch a mouse event |
| `Input.dispatchKeyEvent` | Dispatch a key event |
| `Input.dispatchTouchEvent` | Dispatch a touch event |

## Generic Command

For domains not covered by the typed API, use:
```c
uint32_t id = cdp_send_command(ctx, "Security.enable", NULL);
```

This works for any CDP domain or method.
