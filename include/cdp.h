/*
 * cdp.h — Chrome DevTools Protocol client library (public API)
 *
 * Copyright (c) 2025 libcdp contributors — MIT licence
 *
 * Pure-C, syscall-free, callback-free library that parses and generates
 * CDP JSON messages.  The caller is responsible for WebSocket transport.
 */

#ifndef CDP_H
#define CDP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Version                                                           */
/* ------------------------------------------------------------------ */
#define CDP_VERSION_MAJOR 0
#define CDP_VERSION_MINOR 1
#define CDP_VERSION_PATCH 0

/* ------------------------------------------------------------------ */
/*  Role                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    CDP_ROLE_CLIENT = 0,
    CDP_ROLE_TARGET = 1
} cdp_role_t;

/* ------------------------------------------------------------------ */
/*  Limits / maxima                                                   */
/* ------------------------------------------------------------------ */
#define CDP_MAX_METHOD_LEN        128
#define CDP_MAX_PARAMS_LEN        65536
#define CDP_MAX_RESULT_LEN        65536
#define CDP_MAX_ERROR_MSG_LEN     1024
#define CDP_MAX_SESSION_ID_LEN    128
#define CDP_MAX_TARGET_ID_LEN     128
#define CDP_MAX_URL_LEN           2048
#define CDP_MAX_ERROR_LEN         512

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t event_queue_size;   /* default 64  */
    uint32_t max_message_size;   /* default 16 MiB */
    int      auto_id;            /* 1 = auto-increment command IDs */
} cdp_config_t;

/* ------------------------------------------------------------------ */
/*  Message type                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    CDP_MSG_COMMAND  = 0,
    CDP_MSG_RESPONSE = 1,
    CDP_MSG_EVENT    = 2
} cdp_msg_type_t;

/* ------------------------------------------------------------------ */
/*  Domain                                                            */
/* ------------------------------------------------------------------ */
typedef enum {
    CDP_DOMAIN_NONE       = 0,
    CDP_DOMAIN_PAGE       = 1,
    CDP_DOMAIN_DOM        = 2,
    CDP_DOMAIN_RUNTIME    = 3,
    CDP_DOMAIN_NETWORK    = 4,
    CDP_DOMAIN_TARGET     = 5,
    CDP_DOMAIN_CSS        = 6,
    CDP_DOMAIN_EMULATION  = 7,
    CDP_DOMAIN_INPUT      = 8,
    CDP_DOMAIN_BROWSER    = 9,
    CDP_DOMAIN_OTHER      = 10
} cdp_domain_t;

/* ------------------------------------------------------------------ */
/*  Response                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t id;
    int      is_error;
    int      error_code;
    char     error_message[CDP_MAX_ERROR_MSG_LEN];
    char     result[CDP_MAX_RESULT_LEN];
} cdp_response_t;

/* ------------------------------------------------------------------ */
/*  Event data                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    cdp_domain_t domain;
    char         method[CDP_MAX_METHOD_LEN];
    char         params[CDP_MAX_PARAMS_LEN];
} cdp_event_data_t;

/* ------------------------------------------------------------------ */
/*  DOM node                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    int32_t node_id;
    int32_t parent_id;
    int32_t child_count;
    char    tag_name[256];
    char    node_value[CDP_MAX_PARAMS_LEN];
    char    attributes[CDP_MAX_PARAMS_LEN];
} cdp_dom_node_t;

/* ------------------------------------------------------------------ */
/*  Page info                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    char url[CDP_MAX_URL_LEN];
    char title[CDP_MAX_URL_LEN];
    char frame_id[128];
    char mime_type[128];
    int  loading;
} cdp_page_info_t;

/* ------------------------------------------------------------------ */
/*  Event type                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    CDP_EVENT_COMMAND_SENT                  = 0,
    CDP_EVENT_RESPONSE_RECEIVED             = 1,
    CDP_EVENT_ERROR_RECEIVED                = 2,
    CDP_EVENT_EVENT_RECEIVED                = 3,
    CDP_EVENT_PAGE_LOAD_FIRED               = 4,
    CDP_EVENT_PAGE_FRAME_NAVIGATED          = 5,
    CDP_EVENT_DOM_DOCUMENT_UPDATED          = 6,
    CDP_EVENT_DOM_ATTRIBUTE_MODIFIED        = 7,
    CDP_EVENT_DOM_CHILD_NODE_INSERTED       = 8,
    CDP_EVENT_NETWORK_REQUEST_WILL_BE_SENT  = 9,
    CDP_EVENT_NETWORK_RESPONSE_RECEIVED     = 10,
    CDP_EVENT_RUNTIME_CONSOLE_API_CALLED    = 11,
    CDP_EVENT_TARGET_CREATED                = 12,
    CDP_EVENT_TARGET_DESTROYED              = 13,
    CDP_EVENT_CSS_STYLESHEET_ADDED          = 14,
    CDP_EVENT_ERROR                         = 15,
    CDP_EVENT_QUEUE_OVERFLOW                = 16
} cdp_event_type_t;

/* ------------------------------------------------------------------ */
/*  Event (pulled from the queue)                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    cdp_event_type_t type;
    union {
        cdp_response_t  response;
        cdp_event_data_t event_data;
        cdp_dom_node_t   dom_node;
        cdp_page_info_t  page_info;
        char             error[CDP_MAX_ERROR_LEN];
    } data;
} cdp_event_t;

/* ------------------------------------------------------------------ */
/*  Opaque context                                                    */
/* ------------------------------------------------------------------ */
typedef struct cdp_ctx cdp_ctx_t;

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */
cdp_ctx_t *cdp_create(void);
cdp_ctx_t *cdp_create_with_config(const cdp_config_t *config);
void       cdp_destroy(cdp_ctx_t *ctx);
void       cdp_reset(cdp_ctx_t *ctx);

/* ------------------------------------------------------------------ */
/*  Input / output                                                    */
/* ------------------------------------------------------------------ */
int  cdp_feed_input(cdp_ctx_t *ctx, const char *json, size_t len);
int  cdp_next_event(cdp_ctx_t *ctx, cdp_event_t *out);
int  cdp_get_output(cdp_ctx_t *ctx, char *buf, size_t buf_size);

/* ------------------------------------------------------------------ */
/*  Status queries                                                    */
/* ------------------------------------------------------------------ */
int      cdp_has_pending_events(const cdp_ctx_t *ctx);
int      cdp_event_count(const cdp_ctx_t *ctx);
int      cdp_has_pending_output(const cdp_ctx_t *ctx);

/* ------------------------------------------------------------------ */
/*  Generic command                                                   */
/* ------------------------------------------------------------------ */
uint32_t cdp_send_command(cdp_ctx_t *ctx, const char *method,
                          const char *params_json);

/* ------------------------------------------------------------------ */
/*  Session management                                                */
/* ------------------------------------------------------------------ */
void cdp_set_session_id(cdp_ctx_t *ctx, const char *session_id);
int  cdp_target_attach_and_set_session(cdp_ctx_t *ctx,
                                        const char *target_id);

/* ------------------------------------------------------------------ */
/*  Page domain                                                       */
/* ------------------------------------------------------------------ */
uint32_t cdp_page_navigate(cdp_ctx_t *ctx, const char *url);
uint32_t cdp_page_reload(cdp_ctx_t *ctx, int ignore_cache);
uint32_t cdp_page_capture_screenshot(cdp_ctx_t *ctx,
                                      const char *format,
                                      int quality);
uint32_t cdp_page_set_document_content(cdp_ctx_t *ctx,
                                        const char *frame_id,
                                        const char *html);

/* ------------------------------------------------------------------ */
/*  DOM domain                                                        */
/* ------------------------------------------------------------------ */
uint32_t cdp_dom_get_document(cdp_ctx_t *ctx, int depth,
                               int pierce);
uint32_t cdp_dom_get_outer_html(cdp_ctx_t *ctx, int32_t node_id);
uint32_t cdp_dom_set_outer_html(cdp_ctx_t *ctx, int32_t node_id,
                                 const char *outer_html);
uint32_t cdp_dom_set_attributes_as_text(cdp_ctx_t *ctx,
                                         int32_t node_id,
                                         const char *text,
                                         const char *name);
uint32_t cdp_dom_remove_node(cdp_ctx_t *ctx, int32_t node_id);
uint32_t cdp_dom_query_selector(cdp_ctx_t *ctx, int32_t node_id,
                                 const char *selector);
uint32_t cdp_dom_query_selector_all(cdp_ctx_t *ctx, int32_t node_id,
                                     const char *selector);

/* ------------------------------------------------------------------ */
/*  Runtime domain                                                    */
/* ------------------------------------------------------------------ */
uint32_t cdp_runtime_evaluate(cdp_ctx_t *ctx, const char *expression,
                               int return_by_value,
                               int await_promise);
uint32_t cdp_runtime_call_function_on(cdp_ctx_t *ctx,
                                       const char *function_declaration,
                                       const char *object_id,
                                       const char *arguments_json);
uint32_t cdp_runtime_get_properties(cdp_ctx_t *ctx,
                                     const char *object_id,
                                     int own_only);

/* ------------------------------------------------------------------ */
/*  Network domain                                                    */
/* ------------------------------------------------------------------ */
uint32_t cdp_network_enable(cdp_ctx_t *ctx);
uint32_t cdp_network_disable(cdp_ctx_t *ctx);
uint32_t cdp_network_get_cookies(cdp_ctx_t *ctx);
uint32_t cdp_network_set_cookie(cdp_ctx_t *ctx, const char *name,
                                 const char *value, const char *domain,
                                 const char *path);
uint32_t cdp_network_clear_browser_cookies(cdp_ctx_t *ctx);

/* ------------------------------------------------------------------ */
/*  CSS domain                                                        */
/* ------------------------------------------------------------------ */
uint32_t cdp_css_enable(cdp_ctx_t *ctx);
uint32_t cdp_css_disable(cdp_ctx_t *ctx);
uint32_t cdp_css_get_stylesheet_text(cdp_ctx_t *ctx,
                                      const char *stylesheet_id);
uint32_t cdp_css_set_style_text(cdp_ctx_t *ctx,
                                 const char *style_sheet_id,
                                 const char *range,
                                 const char *text);

/* ------------------------------------------------------------------ */
/*  Target domain                                                     */
/* ------------------------------------------------------------------ */
uint32_t cdp_target_get_targets(cdp_ctx_t *ctx);
uint32_t cdp_target_create_target(cdp_ctx_t *ctx, const char *url);
uint32_t cdp_target_close_target(cdp_ctx_t *ctx,
                                  const char *target_id);
uint32_t cdp_target_attach_to_target(cdp_ctx_t *ctx,
                                      const char *target_id,
                                      int flatten);
uint32_t cdp_target_activate_target(cdp_ctx_t *ctx,
                                     const char *target_id);

/* ------------------------------------------------------------------ */
/*  Emulation domain                                                  */
/* ------------------------------------------------------------------ */
uint32_t cdp_emulation_set_device_metrics_override(
             cdp_ctx_t *ctx, int width, int height,
             double device_scale_factor, int mobile);
uint32_t cdp_emulation_set_user_agent_override(
             cdp_ctx_t *ctx, const char *user_agent);

/* ------------------------------------------------------------------ */
/*  Input domain                                                      */
/* ------------------------------------------------------------------ */
uint32_t cdp_input_dispatch_mouse_event(
             cdp_ctx_t *ctx, const char *type,
             double x, double y, int button);
uint32_t cdp_input_dispatch_key_event(
             cdp_ctx_t *ctx, const char *type,
             int key_code, const char *text);
uint32_t cdp_input_dispatch_touch_event(
             cdp_ctx_t *ctx, const char *type,
             const char *touch_points_json);

#ifdef __cplusplus
}
#endif

#endif /* CDP_H */
