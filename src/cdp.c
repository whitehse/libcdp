/*
 * cdp.c — Chrome DevTools Protocol client library (implementation)
 *
 * Copyright (c) 2025 libcdp contributors — MIT licence
 */

#include "cdp.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ================================================================== */
/*  Internal helpers                                                  */
/* ================================================================== */

/* Ring-buffer event queue (embedded, no dynamic allocation) */
#define CDP_DEFAULT_QUEUE_SIZE  64

typedef struct {
    cdp_event_t *items;
    uint32_t     capacity;
    uint32_t     head;
    uint32_t     tail;
    uint32_t     count;
} event_queue_t;

/* Output ring buffer */
#define CDP_OUTPUT_BUF_SIZE  (1024 * 1024)  /* 1 MiB output buffer */

struct cdp_ctx {
    cdp_role_t    role;
    cdp_config_t  config;

    /* Command ID counter */
    uint32_t      next_id;

    /* Session ID */
    char          session_id[CDP_MAX_SESSION_ID_LEN];

    /* Event queue */
    event_queue_t eq;

    /* Output buffer (circular, but we use it linearly for simplicity) */
    char         *output_buf;
    size_t        output_len;
    size_t        output_cap;
};

/* ---- event queue helpers ---- */

static int eq_init(event_queue_t *q, uint32_t cap)
{
    q->items = (cdp_event_t *)calloc(cap, sizeof(cdp_event_t));
    if (!q->items) return -1;
    q->capacity = cap;
    q->head = q->tail = q->count = 0;
    return 0;
}

static void eq_free(event_queue_t *q)
{
    free(q->items);
    q->items = NULL;
    q->capacity = q->head = q->tail = q->count = 0;
}

static int eq_push(event_queue_t *q, const cdp_event_t *ev)
{
    if (q->count >= q->capacity) return -1;   /* full */
    q->items[q->tail] = *ev;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return 0;
}

static int eq_pop(event_queue_t *q, cdp_event_t *out)
{
    if (q->count == 0) return -1;              /* empty */
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return 0;
}

/* ---- output buffer ---- */

static int output_append(cdp_ctx_t *ctx, const char *s, size_t len)
{
    if (ctx->output_len + len + 1 > ctx->output_cap) return -1;
    memcpy(ctx->output_buf + ctx->output_len, s, len);
    ctx->output_len += len;
    ctx->output_buf[ctx->output_len] = '\0';
    return 0;
}

/* ---- minimal JSON scanning (no full parser) ---- */

/* Find value for a top-level string key: returns pointer after the
   opening '"' of the value, or NULL.  Very simple — assumes flat
   objects (no nested braces in the value position). */
static const char *json_find_key(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;

    while (*p) {
        /* skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '"') { p++; continue; }
        p++;  /* skip opening quote */
        if (strncmp(p, key, klen) == 0 && p[klen] == '"') {
            p += klen + 1;  /* skip key + closing quote */
            /* skip : and whitespace */
            while (*p == ' ' || *p == '\t' || *p == ':') p++;
            return p;
        }
        /* advance past this key's value */
        /* find closing quote of this key */
        while (*p && *p != '"') p++;
        if (*p) p++;
        /* skip : and find value end */
        while (*p == ' ' || *p == '\t' || *p == ':') p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p) p++;
        } else if (*p == '{') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
        } else if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }
        } else {
            /* number/bool/null — skip to comma or } */
            while (*p && *p != ',' && *p != '}') p++;
        }
    }
    return NULL;
}

/* Extract a JSON string value (without quotes) into buf.
   Returns number of chars written (excluding NUL), or -1. */
static int json_extract_string(const char *vp, char *buf, size_t bufsz)
{
    if (!vp || *vp != '"') return -1;
    vp++;
    size_t i = 0;
    while (*vp && *vp != '"' && i < bufsz - 1) {
        if (*vp == '\\' && vp[1]) {
            vp++;
            switch (*vp) {
                case '"':  buf[i++] = '"';  break;
                case '\\': buf[i++] = '\\'; break;
                case 'n':  buf[i++] = '\n'; break;
                case 't':  buf[i++] = '\t'; break;
                default:   buf[i++] = *vp;  break;
            }
        } else {
            buf[i++] = *vp;
        }
        vp++;
    }
    buf[i] = '\0';
    return (int)i;
}

/* Extract an integer value. */
static int json_extract_int(const char *vp, int32_t *out)
{
    if (!vp) return -1;
    if (*vp == '"') vp++;  /* tolerate quoted integers */
    char *end = NULL;
    long v = strtol(vp, &end, 10);
    if (end == vp) return -1;
    *out = (int32_t)v;
    return 0;
}

/* Extract a sub-object or sub-array as a raw JSON string. */
static int json_extract_raw(const char *vp, char *buf, size_t bufsz)
{
    if (!vp) return -1;
    char open, close;
    if (*vp == '{')      { open = '{'; close = '}'; }
    else if (*vp == '[') { open = '['; close = ']'; }
    else if (*vp == '"') {
        return json_extract_string(vp, buf, bufsz);
    } else {
        /* scalar — copy until , or } or ] */
        size_t i = 0;
        while (*vp && *vp != ',' && *vp != '}' && *vp != ']' &&
               i < bufsz - 1) {
            buf[i++] = *vp++;
        }
        buf[i] = '\0';
        return (int)i;
    }

    int depth = 0;
    const char *start = vp;
    do {
        if (*vp == open) depth++;
        else if (*vp == close) depth--;
        vp++;
    } while (*vp && depth > 0);

    size_t len = (size_t)(vp - start);
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    return (int)len;
}

/* ---- domain classification ---- */

static cdp_domain_t classify_domain(const char *method)
{
    if (strncmp(method, "Page.", 5) == 0)       return CDP_DOMAIN_PAGE;
    if (strncmp(method, "DOM.", 4) == 0)        return CDP_DOMAIN_DOM;
    if (strncmp(method, "Runtime.", 8) == 0)    return CDP_DOMAIN_RUNTIME;
    if (strncmp(method, "Network.", 8) == 0)    return CDP_DOMAIN_NETWORK;
    if (strncmp(method, "Target.", 7) == 0)     return CDP_DOMAIN_TARGET;
    if (strncmp(method, "CSS.", 4) == 0)        return CDP_DOMAIN_CSS;
    if (strncmp(method, "Emulation.", 10) == 0) return CDP_DOMAIN_EMULATION;
    if (strncmp(method, "Input.", 6) == 0)      return CDP_DOMAIN_INPUT;
    if (strncmp(method, "Browser.", 8) == 0)    return CDP_DOMAIN_BROWSER;
    return CDP_DOMAIN_OTHER;
}

/* Map event method names to our event types */
static cdp_event_type_t classify_event_type(cdp_domain_t dom,
                                             const char *method)
{
    if (dom == CDP_DOMAIN_PAGE) {
        if (strstr(method, "loadEventFired"))     return CDP_EVENT_PAGE_LOAD_FIRED;
        if (strstr(method, "frameNavigated"))     return CDP_EVENT_PAGE_FRAME_NAVIGATED;
    } else if (dom == CDP_DOMAIN_DOM) {
        if (strstr(method, "documentUpdated"))    return CDP_EVENT_DOM_DOCUMENT_UPDATED;
        if (strstr(method, "attributeModified"))  return CDP_EVENT_DOM_ATTRIBUTE_MODIFIED;
        if (strstr(method, "childNodeInserted"))  return CDP_EVENT_DOM_CHILD_NODE_INSERTED;
    } else if (dom == CDP_DOMAIN_NETWORK) {
        if (strstr(method, "requestWillBeSent"))  return CDP_EVENT_NETWORK_REQUEST_WILL_BE_SENT;
        if (strstr(method, "responseReceived"))   return CDP_EVENT_NETWORK_RESPONSE_RECEIVED;
    } else if (dom == CDP_DOMAIN_RUNTIME) {
        if (strstr(method, "consoleAPICalled"))   return CDP_EVENT_RUNTIME_CONSOLE_API_CALLED;
    } else if (dom == CDP_DOMAIN_TARGET) {
        if (strstr(method, "targetCreated"))      return CDP_EVENT_TARGET_CREATED;
        if (strstr(method, "targetDestroyed"))    return CDP_EVENT_TARGET_DESTROYED;
    } else if (dom == CDP_DOMAIN_CSS) {
        if (strstr(method, "stylesheetAdded"))    return CDP_EVENT_CSS_STYLESHEET_ADDED;
    }
    return CDP_EVENT_EVENT_RECEIVED;  /* generic event */
}

/* ---- JSON output builder ---- */

static uint32_t emit_command(cdp_ctx_t *ctx, const char *method,
                             const char *params_json)
{
    uint32_t id;
    if (ctx->config.auto_id) {
        id = ctx->next_id++;
    } else {
        id = ctx->next_id;
    }

    char buf[CDP_MAX_METHOD_LEN + CDP_MAX_PARAMS_LEN + 256];
    int  n;

    if (params_json && params_json[0]) {
        n = snprintf(buf, sizeof(buf),
                     "{\"id\":%u,\"method\":\"%s\",\"params\":%s}",
                     (unsigned)id, method, params_json);
    } else {
        n = snprintf(buf, sizeof(buf),
                     "{\"id\":%u,\"method\":\"%s\",\"params\":{}}",
                     (unsigned)id, method);
    }

    /* Append session ID if set */
    if (ctx->session_id[0] && n > 0 && (size_t)n < sizeof(buf) - 100) {
        /* Insert sessionId before closing } */
        buf[n - 1] = '\0';  /* remove closing } */
        n = snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     ",\"sessionId\":\"%s\"}", ctx->session_id);
        n = (int)strlen(buf);
    }

    if (n > 0) {
        output_append(ctx, buf, (size_t)n);
        /* emit newline separator */
        output_append(ctx, "\n", 1);
    }

    /* Push COMMAND_SENT event */
    cdp_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = CDP_EVENT_COMMAND_SENT;
    ev.data.response.id = id;
    eq_push(&ctx->eq, &ev);

    return id;
}

/* ================================================================== */
/*  Public API — lifecycle                                            */
/* ================================================================== */

cdp_ctx_t *cdp_create(void)
{
    cdp_config_t cfg;
    cfg.event_queue_size = CDP_DEFAULT_QUEUE_SIZE;
    cfg.max_message_size = 16u * 1024u * 1024u;
    cfg.auto_id = 1;
    return cdp_create_with_config(&cfg);
}

cdp_ctx_t *cdp_create_with_config(const cdp_config_t *config)
{
    if (!config) return NULL;

    cdp_ctx_t *ctx = (cdp_ctx_t *)calloc(1, sizeof(cdp_ctx_t));
    if (!ctx) return NULL;

    ctx->config = *config;
    ctx->role   = CDP_ROLE_CLIENT;
    ctx->next_id = 1;

    uint32_t qsz = config->event_queue_size > 0
                         ? config->event_queue_size
                         : CDP_DEFAULT_QUEUE_SIZE;
    if (eq_init(&ctx->eq, qsz) != 0) {
        free(ctx);
        return NULL;
    }

    ctx->output_cap = CDP_OUTPUT_BUF_SIZE;
    ctx->output_buf = (char *)calloc(1, ctx->output_cap);
    if (!ctx->output_buf) {
        eq_free(&ctx->eq);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void cdp_destroy(cdp_ctx_t *ctx)
{
    if (!ctx) return;
    eq_free(&ctx->eq);
    free(ctx->output_buf);
    free(ctx);
}

void cdp_reset(cdp_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->eq.head = ctx->eq.tail = ctx->eq.count = 0;
    ctx->output_len = 0;
    if (ctx->output_buf) ctx->output_buf[0] = '\0';
    ctx->next_id = 1;
    ctx->session_id[0] = '\0';
}

/* ================================================================== */
/*  Input / output                                                    */
/* ================================================================== */

int cdp_feed_input(cdp_ctx_t *ctx, const char *json, size_t len)
{
    if (!ctx || !json || len == 0) return -1;
    if (len > ctx->config.max_message_size) {
        /* push overflow error event */
        cdp_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = CDP_EVENT_QUEUE_OVERFLOW;
        snprintf(ev.data.error, CDP_MAX_ERROR_LEN,
                 "message exceeds max_message_size");
        eq_push(&ctx->eq, &ev);
        return -1;
    }

    /* Determine message type */
    const char *id_val  = json_find_key(json, "id");
    const char *method  = json_find_key(json, "method");
    const char *error   = json_find_key(json, "error");

    cdp_event_t ev;
    memset(&ev, 0, sizeof(ev));

    if (id_val && !method) {
        /* This is a response (has id, no method) */
        cdp_response_t *resp = &ev.data.response;
        json_extract_int(id_val, (int32_t *)&resp->id);

        if (error && *error == '{') {
            /* Error response */
            resp->is_error = 1;
            ev.type = CDP_EVENT_ERROR_RECEIVED;

            const char *code_str = json_find_key(error, "code");
            if (code_str) json_extract_int(code_str, &resp->error_code);

            const char *msg_str = json_find_key(error, "message");
            if (msg_str)
                json_extract_string(msg_str, resp->error_message,
                                    CDP_MAX_ERROR_MSG_LEN);
        } else {
            /* Success response */
            resp->is_error = 0;
            ev.type = CDP_EVENT_RESPONSE_RECEIVED;

            const char *result = json_find_key(json, "result");
            if (result)
                json_extract_raw(result, resp->result,
                                 CDP_MAX_RESULT_LEN);
        }

    } else if (id_val && method) {
        /* This is a command (has both id and method) */
        cdp_response_t *resp = &ev.data.response;
        json_extract_int(id_val, (int32_t *)&resp->id);
        ev.type = CDP_EVENT_COMMAND_SENT;
        resp->is_error = 0;

    } else if (method) {
        /* This is an event (has method) */
        cdp_event_data_t *ed = &ev.data.event_data;
        json_extract_string(method, ed->method, CDP_MAX_METHOD_LEN);
        ed->domain = classify_domain(ed->method);
        ev.type = classify_event_type(ed->domain, ed->method);

        const char *params = json_find_key(json, "params");
        if (params)
            json_extract_raw(params, ed->params, CDP_MAX_PARAMS_LEN);

    } else {
        /* Unrecognised */
        ev.type = CDP_EVENT_ERROR;
        snprintf(ev.data.error, CDP_MAX_ERROR_LEN,
                 "unrecognised message format");
    }

    if (eq_push(&ctx->eq, &ev) != 0) {
        /* queue full */
        cdp_event_t overflow;
        memset(&overflow, 0, sizeof(overflow));
        overflow.type = CDP_EVENT_QUEUE_OVERFLOW;
        snprintf(overflow.data.error, CDP_MAX_ERROR_LEN,
                 "event queue full");
        /* Try once more — if still full we lose the event */
        eq_push(&ctx->eq, &overflow);
        return -1;
    }

    return 0;
}

int cdp_next_event(cdp_ctx_t *ctx, cdp_event_t *out)
{
    if (!ctx || !out) return -1;
    return eq_pop(&ctx->eq, out) == 0 ? 0 : -1;
}

int cdp_get_output(cdp_ctx_t *ctx, char *buf, size_t buf_size)
{
    if (!ctx || !buf || buf_size == 0) return -1;
    if (ctx->output_len == 0) {
        buf[0] = '\0';
        return 0;
    }
    size_t n = ctx->output_len < buf_size - 1
                   ? ctx->output_len
                   : buf_size - 1;
    memcpy(buf, ctx->output_buf, n);
    buf[n] = '\0';

    /* Consume output */
    ctx->output_len = 0;
    ctx->output_buf[0] = '\0';
    return (int)n;
}

/* ================================================================== */
/*  Status                                                            */
/* ================================================================== */

int cdp_has_pending_events(const cdp_ctx_t *ctx)
{
    return ctx && ctx->eq.count > 0;
}

int cdp_event_count(const cdp_ctx_t *ctx)
{
    return ctx ? (int)ctx->eq.count : 0;
}

int cdp_has_pending_output(const cdp_ctx_t *ctx)
{
    return ctx && ctx->output_len > 0;
}

/* ================================================================== */
/*  Generic command                                                   */
/* ================================================================== */

uint32_t cdp_send_command(cdp_ctx_t *ctx, const char *method,
                          const char *params_json)
{
    if (!ctx || !method) return 0;
    return emit_command(ctx, method, params_json);
}

/* ================================================================== */
/*  Session management                                                */
/* ================================================================== */

void cdp_set_session_id(cdp_ctx_t *ctx, const char *session_id)
{
    if (!ctx) return;
    if (session_id) {
        strncpy(ctx->session_id, session_id, CDP_MAX_SESSION_ID_LEN - 1);
        ctx->session_id[CDP_MAX_SESSION_ID_LEN - 1] = '\0';
    } else {
        ctx->session_id[0] = '\0';
    }
}

int cdp_target_attach_and_set_session(cdp_ctx_t *ctx,
                                       const char *target_id)
{
    if (!ctx || !target_id) return -1;
    /* Send Target.attachToTarget */
    char params[512];
    snprintf(params, sizeof(params),
             "{\"targetId\":\"%s\",\"flatten\":true}", target_id);
    cdp_send_command(ctx, "Target.attachToTarget", params);
    return 0;
}

/* ================================================================== */
/*  Page domain                                                       */
/* ================================================================== */

uint32_t cdp_page_navigate(cdp_ctx_t *ctx, const char *url)
{
    if (!ctx || !url) return 0;
    char params[CDP_MAX_URL_LEN + 64];
    snprintf(params, sizeof(params), "{\"url\":\"%s\"}", url);
    return cdp_send_command(ctx, "Page.navigate", params);
}

uint32_t cdp_page_reload(cdp_ctx_t *ctx, int ignore_cache)
{
    if (!ctx) return 0;
    char params[128];
    snprintf(params, sizeof(params), "{\"ignoreCache\":%s}",
             ignore_cache ? "true" : "false");
    return cdp_send_command(ctx, "Page.reload", params);
}

uint32_t cdp_page_capture_screenshot(cdp_ctx_t *ctx,
                                      const char *format,
                                      int quality)
{
    if (!ctx) return 0;
    char params[256];
    if (format && quality >= 0) {
        snprintf(params, sizeof(params),
                 "{\"format\":\"%s\",\"quality\":%d}", format, quality);
    } else if (format) {
        snprintf(params, sizeof(params), "{\"format\":\"%s\"}", format);
    } else {
        params[0] = '\0';
    }
    return cdp_send_command(ctx, "Page.captureScreenshot", params);
}

uint32_t cdp_page_set_document_content(cdp_ctx_t *ctx,
                                        const char *frame_id,
                                        const char *html)
{
    if (!ctx || !frame_id || !html) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"frameId\":\"%s\",\"html\":\"%s\"}", frame_id, html);
    return cdp_send_command(ctx, "Page.setDocumentContent", params);
}

/* ================================================================== */
/*  DOM domain                                                        */
/* ================================================================== */

uint32_t cdp_dom_get_document(cdp_ctx_t *ctx, int depth,
                               int pierce)
{
    if (!ctx) return 0;
    char params[128];
    snprintf(params, sizeof(params),
             "{\"depth\":%d,\"pierce\":%s}", depth,
             pierce ? "true" : "false");
    return cdp_send_command(ctx, "DOM.getDocument", params);
}

uint32_t cdp_dom_get_outer_html(cdp_ctx_t *ctx, int32_t node_id)
{
    if (!ctx) return 0;
    char params[64];
    snprintf(params, sizeof(params), "{\"nodeId\":%d}", (int)node_id);
    return cdp_send_command(ctx, "DOM.getOuterHTML", params);
}

uint32_t cdp_dom_set_outer_html(cdp_ctx_t *ctx, int32_t node_id,
                                 const char *outer_html)
{
    if (!ctx || !outer_html) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"nodeId\":%d,\"outerHTML\":\"%s\"}",
             (int)node_id, outer_html);
    return cdp_send_command(ctx, "DOM.setOuterHTML", params);
}

uint32_t cdp_dom_set_attributes_as_text(cdp_ctx_t *ctx,
                                         int32_t node_id,
                                         const char *text,
                                         const char *name)
{
    if (!ctx || !text) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    if (name) {
        snprintf(params, sizeof(params),
                 "{\"nodeId\":%d,\"text\":\"%s\",\"name\":\"%s\"}",
                 (int)node_id, text, name);
    } else {
        snprintf(params, sizeof(params),
                 "{\"nodeId\":%d,\"text\":\"%s\"}",
                 (int)node_id, text);
    }
    return cdp_send_command(ctx, "DOM.setAttributesAsText", params);
}

uint32_t cdp_dom_remove_node(cdp_ctx_t *ctx, int32_t node_id)
{
    if (!ctx) return 0;
    char params[64];
    snprintf(params, sizeof(params), "{\"nodeId\":%d}", (int)node_id);
    return cdp_send_command(ctx, "DOM.removeNode", params);
}

uint32_t cdp_dom_query_selector(cdp_ctx_t *ctx, int32_t node_id,
                                 const char *selector)
{
    if (!ctx || !selector) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"nodeId\":%d,\"selector\":\"%s\"}",
             (int)node_id, selector);
    return cdp_send_command(ctx, "DOM.querySelector", params);
}

uint32_t cdp_dom_query_selector_all(cdp_ctx_t *ctx, int32_t node_id,
                                     const char *selector)
{
    if (!ctx || !selector) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"nodeId\":%d,\"selector\":\"%s\"}",
             (int)node_id, selector);
    return cdp_send_command(ctx, "DOM.querySelectorAll", params);
}

/* ================================================================== */
/*  Runtime domain                                                    */
/* ================================================================== */

uint32_t cdp_runtime_evaluate(cdp_ctx_t *ctx, const char *expression,
                               int return_by_value,
                               int await_promise)
{
    if (!ctx || !expression) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"expression\":\"%s\",\"returnByValue\":%s,"
             "\"awaitPromise\":%s}",
             expression,
             return_by_value ? "true" : "false",
             await_promise ? "true" : "false");
    return cdp_send_command(ctx, "Runtime.evaluate", params);
}

uint32_t cdp_runtime_call_function_on(cdp_ctx_t *ctx,
                                       const char *function_declaration,
                                       const char *object_id,
                                       const char *arguments_json)
{
    if (!ctx || !function_declaration) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    if (object_id && arguments_json) {
        snprintf(params, sizeof(params),
                 "{\"functionDeclaration\":\"%s\","
                 "\"objectId\":\"%s\",\"arguments\":%s}",
                 function_declaration, object_id, arguments_json);
    } else if (object_id) {
        snprintf(params, sizeof(params),
                 "{\"functionDeclaration\":\"%s\","
                 "\"objectId\":\"%s\"}",
                 function_declaration, object_id);
    } else {
        snprintf(params, sizeof(params),
                 "{\"functionDeclaration\":\"%s\"}",
                 function_declaration);
    }
    return cdp_send_command(ctx, "Runtime.callFunctionOn", params);
}

uint32_t cdp_runtime_get_properties(cdp_ctx_t *ctx,
                                     const char *object_id,
                                     int own_only)
{
    if (!ctx || !object_id) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"objectId\":\"%s\",\"ownOnly\":%s}",
             object_id, own_only ? "true" : "false");
    return cdp_send_command(ctx, "Runtime.getProperties", params);
}

/* ================================================================== */
/*  Network domain                                                    */
/* ================================================================== */

uint32_t cdp_network_enable(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "Network.enable", NULL) : 0;
}

uint32_t cdp_network_disable(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "Network.disable", NULL) : 0;
}

uint32_t cdp_network_get_cookies(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "Network.getCookies", NULL) : 0;
}

uint32_t cdp_network_set_cookie(cdp_ctx_t *ctx, const char *name,
                                 const char *value,
                                 const char *domain,
                                 const char *path)
{
    if (!ctx || !name || !value) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"name\":\"%s\",\"value\":\"%s\","
             "\"domain\":\"%s\",\"path\":\"%s\"}",
             name, value,
             domain ? domain : "",
             path ? path : "/");
    return cdp_send_command(ctx, "Network.setCookie", params);
}

uint32_t cdp_network_clear_browser_cookies(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "Network.clearBrowserCookies",
                                  NULL) : 0;
}

/* ================================================================== */
/*  CSS domain                                                        */
/* ================================================================== */

uint32_t cdp_css_enable(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "CSS.enable", NULL) : 0;
}

uint32_t cdp_css_disable(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "CSS.disable", NULL) : 0;
}

uint32_t cdp_css_get_stylesheet_text(cdp_ctx_t *ctx,
                                      const char *stylesheet_id)
{
    if (!ctx || !stylesheet_id) return 0;
    char params[256];
    snprintf(params, sizeof(params),
             "{\"styleSheetId\":\"%s\"}", stylesheet_id);
    return cdp_send_command(ctx, "CSS.getStyleSheetText", params);
}

uint32_t cdp_css_set_style_text(cdp_ctx_t *ctx,
                                 const char *style_sheet_id,
                                 const char *range,
                                 const char *text)
{
    if (!ctx || !style_sheet_id || !text) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    if (range) {
        snprintf(params, sizeof(params),
                 "{\"styleSheetId\":\"%s\",\"range\":%s,"
                 "\"text\":\"%s\"}",
                 style_sheet_id, range, text);
    } else {
        snprintf(params, sizeof(params),
                 "{\"styleSheetId\":\"%s\",\"text\":\"%s\"}",
                 style_sheet_id, text);
    }
    return cdp_send_command(ctx, "CSS.setStyleText", params);
}

/* ================================================================== */
/*  Target domain                                                     */
/* ================================================================== */

uint32_t cdp_target_get_targets(cdp_ctx_t *ctx)
{
    return ctx ? cdp_send_command(ctx, "Target.getTargets", NULL) : 0;
}

uint32_t cdp_target_create_target(cdp_ctx_t *ctx, const char *url)
{
    if (!ctx || !url) return 0;
    char params[CDP_MAX_URL_LEN + 32];
    snprintf(params, sizeof(params), "{\"url\":\"%s\"}", url);
    return cdp_send_command(ctx, "Target.createTarget", params);
}

uint32_t cdp_target_close_target(cdp_ctx_t *ctx,
                                  const char *target_id)
{
    if (!ctx || !target_id) return 0;
    char params[CDP_MAX_TARGET_ID_LEN + 32];
    snprintf(params, sizeof(params),
             "{\"targetId\":\"%s\"}", target_id);
    return cdp_send_command(ctx, "Target.closeTarget", params);
}

uint32_t cdp_target_attach_to_target(cdp_ctx_t *ctx,
                                      const char *target_id,
                                      int flatten)
{
    if (!ctx || !target_id) return 0;
    char params[CDP_MAX_TARGET_ID_LEN + 64];
    snprintf(params, sizeof(params),
             "{\"targetId\":\"%s\",\"flatten\":%s}",
             target_id, flatten ? "true" : "false");
    return cdp_send_command(ctx, "Target.attachToTarget", params);
}

uint32_t cdp_target_activate_target(cdp_ctx_t *ctx,
                                     const char *target_id)
{
    if (!ctx || !target_id) return 0;
    char params[CDP_MAX_TARGET_ID_LEN + 32];
    snprintf(params, sizeof(params),
             "{\"targetId\":\"%s\"}", target_id);
    return cdp_send_command(ctx, "Target.activateTarget", params);
}

/* ================================================================== */
/*  Emulation domain                                                  */
/* ================================================================== */

uint32_t cdp_emulation_set_device_metrics_override(
             cdp_ctx_t *ctx, int width, int height,
             double device_scale_factor, int mobile)
{
    if (!ctx) return 0;
    char params[256];
    snprintf(params, sizeof(params),
             "{\"width\":%d,\"height\":%d,"
             "\"deviceScaleFactor\":%.1f,\"mobile\":%s}",
             width, height, device_scale_factor,
             mobile ? "true" : "false");
    return cdp_send_command(ctx,
                            "Emulation.setDeviceMetricsOverride",
                            params);
}

uint32_t cdp_emulation_set_user_agent_override(
             cdp_ctx_t *ctx, const char *user_agent)
{
    if (!ctx || !user_agent) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"userAgent\":\"%s\"}", user_agent);
    return cdp_send_command(ctx,
                            "Emulation.setUserAgentOverride",
                            params);
}

/* ================================================================== */
/*  Input domain                                                      */
/* ================================================================== */

uint32_t cdp_input_dispatch_mouse_event(
             cdp_ctx_t *ctx, const char *type,
             double x, double y, int button)
{
    if (!ctx || !type) return 0;
    char params[256];
    snprintf(params, sizeof(params),
             "{\"type\":\"%s\",\"x\":%.1f,\"y\":%.1f,"
             "\"button\":\"%s\"}",
             type, x, y,
             button == 0 ? "left" :
             button == 1 ? "middle" : "right");
    return cdp_send_command(ctx, "Input.dispatchMouseEvent", params);
}

uint32_t cdp_input_dispatch_key_event(
             cdp_ctx_t *ctx, const char *type,
             int key_code, const char *text)
{
    if (!ctx || !type) return 0;
    char params[256];
    if (text) {
        snprintf(params, sizeof(params),
                 "{\"type\":\"%s\",\"windowsVirtualKeyCode\":%d,"
                 "\"text\":\"%s\"}",
                 type, key_code, text);
    } else {
        snprintf(params, sizeof(params),
                 "{\"type\":\"%s\",\"windowsVirtualKeyCode\":%d}",
                 type, key_code);
    }
    return cdp_send_command(ctx, "Input.dispatchKeyEvent", params);
}

uint32_t cdp_input_dispatch_touch_event(
             cdp_ctx_t *ctx, const char *type,
             const char *touch_points_json)
{
    if (!ctx || !type || !touch_points_json) return 0;
    char params[CDP_MAX_PARAMS_LEN];
    snprintf(params, sizeof(params),
             "{\"type\":\"%s\",\"touchPoints\":%s}",
             type, touch_points_json);
    return cdp_send_command(ctx, "Input.dispatchTouchEvent", params);
}
