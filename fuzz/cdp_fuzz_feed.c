/*
 * cdp_fuzz_feed.c — Fuzz harness for cdp_feed_input
 *
 * Build with: -fsanitize=fuzzer,address
 * Run:        ./cdp_fuzz_feed corpus/
 */

#include "cdp.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Fuzz with default config */
    cdp_ctx_t *ctx = cdp_create();
    if (!ctx) return 0;

    cdp_feed_input(ctx, (const char *)data, size);

    /* Drain all events */
    cdp_event_t ev;
    while (cdp_next_event(ctx, &ev) == 0) {
        /* just consume */
    }

    cdp_destroy(ctx);
    return 0;
}
