/*
 * cdp_smoke.c — basic functional tests for libcdp
 */

#include "cdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", name); \
} while (0)

#define PASS() do { tests_passed++; printf("[PASS]\n"); } while (0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); } while (0)

/* ------------------------------------------------------------------ */
/*  Test: create / destroy                                            */
/* ------------------------------------------------------------------ */
static void test_create_destroy(void)
{
    TEST("create / destroy");
    cdp_ctx_t *ctx = cdp_create();
    if (!ctx) { FAIL("cdp_create returned NULL"); return; }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: NULL safety                                                 */
/* ------------------------------------------------------------------ */
static void test_null_safety(void)
{
    TEST("NULL safety");
    cdp_destroy(NULL);  /* must not crash */
    cdp_reset(NULL);
    if (cdp_feed_input(NULL, "{}", 2) != -1)  { FAIL("feed NULL ctx"); return; }
    if (cdp_next_event(NULL, NULL) != -1)      { FAIL("next NULL"); return; }
    if (cdp_has_pending_events(NULL) != 0)     { FAIL("pending NULL"); return; }
    if (cdp_event_count(NULL) != 0)            { FAIL("count NULL"); return; }
    if (cdp_send_command(NULL, "X.y", NULL) != 0) { FAIL("cmd NULL"); return; }
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: feed response → RESPONSE_RECEIVED                           */
/* ------------------------------------------------------------------ */
static void test_feed_response(void)
{
    TEST("feed response -> RESPONSE_RECEIVED");
    cdp_ctx_t *ctx = cdp_create();
    const char *json = "{\"id\":1,\"result\":{\"root\":{\"nodeId\":1}}}";
    if (cdp_feed_input(ctx, json, strlen(json)) != 0) {
        FAIL("feed_input failed"); cdp_destroy(ctx); return;
    }
    cdp_event_t ev;
    if (cdp_next_event(ctx, &ev) != 0) {
        FAIL("no event"); cdp_destroy(ctx); return;
    }
    if (ev.type != CDP_EVENT_RESPONSE_RECEIVED) {
        FAIL("wrong event type"); cdp_destroy(ctx); return;
    }
    if (ev.data.response.id != 1) {
        FAIL("wrong id"); cdp_destroy(ctx); return;
    }
    if (ev.data.response.is_error) {
        FAIL("unexpected error"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: feed event → PAGE_LOAD_FIRED                                */
/* ------------------------------------------------------------------ */
static void test_feed_event(void)
{
    TEST("feed event -> PAGE_LOAD_FIRED");
    cdp_ctx_t *ctx = cdp_create();
    const char *json = "{\"method\":\"Page.loadEventFired\",\"params\":{}}";
    if (cdp_feed_input(ctx, json, strlen(json)) != 0) {
        FAIL("feed_input failed"); cdp_destroy(ctx); return;
    }
    cdp_event_t ev;
    if (cdp_next_event(ctx, &ev) != 0) {
        FAIL("no event"); cdp_destroy(ctx); return;
    }
    if (ev.type != CDP_EVENT_PAGE_LOAD_FIRED) {
        FAIL("wrong event type"); cdp_destroy(ctx); return;
    }
    if (ev.data.event_data.domain != CDP_DOMAIN_PAGE) {
        FAIL("wrong domain"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: command builders produce output                             */
/* ------------------------------------------------------------------ */
static void test_command_page_navigate(void)
{
    TEST("cdp_page_navigate produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_page_navigate(ctx, "https://example.com");
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    int n = cdp_get_output(ctx, buf, sizeof(buf));
    if (n <= 0) { FAIL("no output"); cdp_destroy(ctx); return; }
    if (!strstr(buf, "Page.navigate")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    if (!strstr(buf, "example.com")) {
        FAIL("missing url"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

static void test_command_dom_get_document(void)
{
    TEST("cdp_dom_get_document produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_dom_get_document(ctx, 3, 0);
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    if (!strstr(buf, "DOM.getDocument")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

static void test_command_runtime_evaluate(void)
{
    TEST("cdp_runtime_evaluate produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_runtime_evaluate(ctx, "1+1", 1, 0);
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    if (!strstr(buf, "Runtime.evaluate")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    if (!strstr(buf, "1+1")) {
        FAIL("missing expression"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

static void test_command_network_enable(void)
{
    TEST("cdp_network_enable produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_network_enable(ctx);
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    if (!strstr(buf, "Network.enable")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

static void test_command_css_enable(void)
{
    TEST("cdp_css_enable produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_css_enable(ctx);
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    if (!strstr(buf, "CSS.enable")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

static void test_command_target_get_targets(void)
{
    TEST("cdp_target_get_targets produces output");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id = cdp_target_get_targets(ctx);
    if (id == 0) { FAIL("id is 0"); cdp_destroy(ctx); return; }

    char buf[4096];
    cdp_get_output(ctx, buf, sizeof(buf));
    if (!strstr(buf, "Target.getTargets")) {
        FAIL("missing method"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: auto-incrementing IDs                                       */
/* ------------------------------------------------------------------ */
static void test_auto_id(void)
{
    TEST("auto-incrementing command IDs");
    cdp_ctx_t *ctx = cdp_create();
    uint32_t id1 = cdp_network_enable(ctx);
    uint32_t id2 = cdp_network_disable(ctx);
    uint32_t id3 = cdp_page_reload(ctx, 0);
    if (id2 != id1 + 1) { FAIL("IDs not sequential"); cdp_destroy(ctx); return; }
    if (id3 != id2 + 1) { FAIL("IDs not sequential"); cdp_destroy(ctx); return; }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: event count                                                 */
/* ------------------------------------------------------------------ */
static void test_event_count(void)
{
    TEST("event count after multiple feeds");
    cdp_ctx_t *ctx = cdp_create();
    cdp_feed_input(ctx, "{\"method\":\"Page.loadEventFired\",\"params\":{}}", 49);
    cdp_feed_input(ctx, "{\"method\":\"Page.frameNavigated\",\"params\":{}}", 49);
    cdp_feed_input(ctx, "{\"id\":1,\"result\":{}}", 20);
    if (cdp_event_count(ctx) != 3) {
        FAIL("expected 3 events"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test: reset                                                       */
/* ------------------------------------------------------------------ */
static void test_reset(void)
{
    TEST("reset clears state");
    cdp_ctx_t *ctx = cdp_create();
    cdp_feed_input(ctx, "{\"method\":\"Page.loadEventFired\",\"params\":{}}", 49);
    cdp_page_navigate(ctx, "https://example.com");
    cdp_reset(ctx);
    if (cdp_event_count(ctx) != 0) {
        FAIL("events not cleared"); cdp_destroy(ctx); return;
    }
    if (cdp_has_pending_output(ctx)) {
        FAIL("output not cleared"); cdp_destroy(ctx); return;
    }
    cdp_destroy(ctx);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("=== cdp_smoke ===\n");

    test_create_destroy();
    test_null_safety();
    test_feed_response();
    test_feed_event();
    test_command_page_navigate();
    test_command_dom_get_document();
    test_command_runtime_evaluate();
    test_command_network_enable();
    test_command_css_enable();
    test_command_target_get_targets();
    test_auto_id();
    test_event_count();
    test_reset();

    printf("--- %d/%d passed ---\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
