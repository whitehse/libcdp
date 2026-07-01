/*
 * cdp_page_example.c — Example: navigate, get DOM, query selector, evaluate JS
 *
 * This demonstrates how a real application would use libcdp.
 * The caller is responsible for WebSocket transport; here we just
 * show the message-generation and event-processing side.
 *
 * Build:  cmake --build build && ./build/cdp_page_example
 */

#include "cdp.h"
#include <stdio.h>
#include <string.h>

static void dump_output(cdp_ctx_t *ctx)
{
    char buf[8192];
    int n = cdp_get_output(ctx, buf, sizeof(buf));
    if (n > 0) {
        printf(">> wire (%d bytes):\n%s\n", n, buf);
    }
}

static void simulate_response(cdp_ctx_t *ctx, uint32_t id,
                               const char *result_json)
{
    char resp[4096];
    snprintf(resp, sizeof(resp),
             "{\"id\":%u,\"result\":%s}", (unsigned)id, result_json);
    cdp_feed_input(ctx, resp, strlen(resp));
}

int main(void)
{
    printf("=== libcdp example: Page automation ===\n\n");

    cdp_ctx_t *client = cdp_create();

    /* 1. Navigate to a URL */
    printf("--- 1. Navigate ---\n");
    uint32_t nav_id = cdp_page_navigate(client, "https://example.com");
    printf("Sent Page.navigate (id=%u)\n", (unsigned)nav_id);
    dump_output(client);

    /* Simulate the browser's response */
    simulate_response(client, nav_id,
                      "{\"frameId\":\"F1\",\"loaderId\":\"L1\"}");

    cdp_event_t ev;
    if (cdp_next_event(client, &ev) == 0 &&
        ev.type == CDP_EVENT_RESPONSE_RECEIVED) {
        printf("Response id=%u, result=%s\n\n",
               (unsigned)ev.data.response.id,
               ev.data.response.result);
    }

    /* 2. Get the DOM document */
    printf("--- 2. DOM.getDocument ---\n");
    uint32_t dom_id = cdp_dom_get_document(client, 3, 0);
    printf("Sent DOM.getDocument (id=%u)\n", (unsigned)dom_id);
    dump_output(client);

    simulate_response(client, dom_id,
                      "{\"root\":{\"nodeId\":1,\"nodeName\":\"#document\","
                      "\"children\":[{\"nodeId\":2,\"nodeName\":\"HTML\"}]}}");

    if (cdp_next_event(client, &ev) == 0) {
        printf("Response id=%u, result=%s\n\n",
               (unsigned)ev.data.response.id,
               ev.data.response.result);
    }

    /* 3. Query a CSS selector */
    printf("--- 3. DOM.querySelector ---\n");
    uint32_t qs_id = cdp_dom_query_selector(client, 1, "h1");
    printf("Sent DOM.querySelector (id=%u)\n", (unsigned)qs_id);
    dump_output(client);

    simulate_response(client, qs_id, "{\"nodeId\":3}");

    if (cdp_next_event(client, &ev) == 0) {
        printf("Response id=%u, result=%s\n\n",
               (unsigned)ev.data.response.id,
               ev.data.response.result);
    }

    /* 4. Evaluate JavaScript */
    printf("--- 4. Runtime.evaluate ---\n");
    uint32_t rt_id = cdp_runtime_evaluate(client,
                                           "document.title", 1, 0);
    printf("Sent Runtime.evaluate (id=%u)\n", (unsigned)rt_id);
    dump_output(client);

    simulate_response(client, rt_id,
                      "{\"result\":{\"type\":\"string\","
                      "\"value\":\"Example Domain\"}}");

    if (cdp_next_event(client, &ev) == 0) {
        printf("Response id=%u, result=%s\n\n",
               (unsigned)ev.data.response.id,
               ev.data.response.result);
    }

    /* 5. Enable Network and listen for events */
    printf("--- 5. Network.enable + event ---\n");
    cdp_network_enable(client);
    dump_output(client);

    /* Simulate an incoming network event */
    const char *net_event =
        "{\"method\":\"Network.requestWillBeSent\","
        "\"params\":{\"requestId\":\"R1\","
        "\"request\":{\"url\":\"https://example.com/style.css\"}}}";
    cdp_feed_input(client, net_event, strlen(net_event));

    if (cdp_next_event(client, &ev) == 0) {
        printf("Event type=%d, method=%s\n",
               ev.type, ev.data.event_data.method);
    }

    printf("\n=== done ===\n");
    cdp_destroy(client);
    return 0;
}
