/*
 * cdp_dialectic.c — Client <--> Target round-trip test
 *
 * Verifies that a client can produce commands that a target context
 * can parse, and vice-versa.
 */

#include "cdp.h"
#include <stdio.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-55s ", name); } while (0)
#define PASS() do { tests_passed++; printf("[PASS]\n"); } while (0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); } while (0)

/* ------------------------------------------------------------------ */
/*  Test: client command → target parses → target responds            */
/* ------------------------------------------------------------------ */
static void test_client_to_target(void)
{
    TEST("client sends Page.navigate -> target parses");
    cdp_ctx_t *client = cdp_create();
    cdp_ctx_t *target = cdp_create();

    /* Client builds a command */
    uint32_t id = cdp_page_navigate(client, "https://example.com");

    /* Grab the wire-format output */
    char wire[4096];
    int n = cdp_get_output(client, wire, sizeof(wire));
    if (n <= 0) { FAIL("client produced no output"); goto done; }

    /* Feed to target (simulates receiving over WebSocket) */
    if (cdp_feed_input(target, wire, (size_t)n) != 0) {
        FAIL("target rejected client message"); goto done;
    }

    /* Target should produce a COMMAND_SENT event */
    cdp_event_t ev;
    if (cdp_next_event(target, &ev) != 0) {
        FAIL("target produced no event"); goto done;
    }
    if (ev.type != CDP_EVENT_COMMAND_SENT) {
        FAIL("expected COMMAND_SENT"); goto done;
    }
    if (ev.data.response.id != id) {
        FAIL("command ID mismatch"); goto done;
    }

    PASS();
done:
    cdp_destroy(client);
    cdp_destroy(target);
}

/* ------------------------------------------------------------------ */
/*  Test: target response → client parses                             */
/* ------------------------------------------------------------------ */
static void test_target_to_client(void)
{
    TEST("target sends response -> client parses");
    cdp_ctx_t *client = cdp_create();
    cdp_ctx_t *target = cdp_create();

    /* Simulate a target generating a response to command id=1 */
    const char *resp_json =
        "{\"id\":1,\"result\":{\"frameId\":\"ABC\",\"loaderId\":\"DEF\"}}";
    if (cdp_feed_input(client, resp_json, strlen(resp_json)) != 0) {
        FAIL("client rejected response"); goto done;
    }

    cdp_event_t ev;
    if (cdp_next_event(client, &ev) != 0) {
        FAIL("client produced no event"); goto done;
    }
    if (ev.type != CDP_EVENT_RESPONSE_RECEIVED) {
        FAIL("expected RESPONSE_RECEIVED"); goto done;
    }
    if (ev.data.response.id != 1) {
        FAIL("wrong id"); goto done;
    }
    if (ev.data.response.is_error) {
        FAIL("unexpected error flag"); goto done;
    }
    if (!strstr(ev.data.response.result, "ABC")) {
        FAIL("result content missing"); goto done;
    }

    PASS();
done:
    cdp_destroy(client);
    cdp_destroy(target);
}

/* ------------------------------------------------------------------ */
// Test: round-trip — client sends, target receives and responds,
//         client receives the response
/* ------------------------------------------------------------------ */
static void test_full_roundtrip(void)
{
    TEST("full round-trip: client -> target -> client");
    cdp_ctx_t *client = cdp_create();
    cdp_ctx_t *target = cdp_create();

    /* Client sends DOM.getDocument */
    uint32_t id = cdp_dom_get_document(client, 3, 0);

    /* Transfer client output to target */
    char wire[4096];
    int n = cdp_get_output(client, wire, sizeof(wire));
    cdp_feed_input(target, wire, (size_t)n);

    /* Target produces response */
    char resp[4096];
    int rlen = snprintf(resp, sizeof(resp),
                        "{\"id\":%u,\"result\":{\"root\":{\"nodeId\":1,"
                        "\"nodeName\":\"#document\"}}}",
                        (unsigned)id);

    /* Transfer target response to client */
    cdp_feed_input(client, resp, (size_t)rlen);

    /* Drain the COMMAND_SENT event from the command builder */
    cdp_event_t ev;
    if (cdp_next_event(client, &ev) != 0 ||
        ev.type != CDP_EVENT_COMMAND_SENT) {
        FAIL("expected COMMAND_SENT first"); goto done;
    }

    /* Now get the RESPONSE_RECEIVED */
    if (cdp_next_event(client, &ev) != 0) {
        FAIL("client got no event"); goto done;
    }
    if (ev.type != CDP_EVENT_RESPONSE_RECEIVED) {
        FAIL("wrong event type"); goto done;
    }
    if (ev.data.response.id != id) {
        FAIL("id mismatch"); goto done;
    }

    PASS();
done:
    cdp_destroy(client);
    cdp_destroy(target);
}

/* ------------------------------------------------------------------ */
/*  Test: event forwarding — target event forwarded to client          */
/* ------------------------------------------------------------------ */
static void test_event_forwarding(void)
{
    TEST("target event forwarded to client");
    cdp_ctx_t *client = cdp_create();

    const char *event_json =
        "{\"method\":\"Network.requestWillBeSent\","
        "\"params\":{\"requestId\":\"1\",\"url\":\"https://example.com\"}}";

    cdp_feed_input(client, event_json, strlen(event_json));

    cdp_event_t ev;
    if (cdp_next_event(client, &ev) != 0) {
        FAIL("no event"); return;
    }
    if (ev.type != CDP_EVENT_NETWORK_REQUEST_WILL_BE_SENT) {
        FAIL("wrong event type"); return;
    }
    if (ev.data.event_data.domain != CDP_DOMAIN_NETWORK) {
        FAIL("wrong domain"); return;
    }

    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: multiple commands queued                                    */
/* ------------------------------------------------------------------ */
static void test_multiple_commands(void)
{
    TEST("multiple commands produce sequential output");
    cdp_ctx_t *client = cdp_create();
    cdp_ctx_t *target = cdp_create();

    cdp_page_navigate(client, "https://a.com");
    cdp_dom_get_document(client, 3, 0);
    cdp_runtime_evaluate(client, "1+1", 1, 0);

    char wire[8192];
    int n = cdp_get_output(client, wire, sizeof(wire));
    if (n <= 0) { FAIL("no output"); goto done; }

    /* Should contain all three methods */
    if (!strstr(wire, "Page.navigate"))   { FAIL("missing Page.navigate"); goto done; }
    if (!strstr(wire, "DOM.getDocument")) { FAIL("missing DOM.getDocument"); goto done; }
    if (!strstr(wire, "Runtime.evaluate")){ FAIL("missing Runtime.evaluate"); goto done; }

    PASS();
done:
    cdp_destroy(client);
    cdp_destroy(target);
}

/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== cdp_dialectic ===\n");
    test_client_to_target();
    test_target_to_client();
    test_full_roundtrip();
    test_event_forwarding();
    test_multiple_commands();
    printf("--- %d/%d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
