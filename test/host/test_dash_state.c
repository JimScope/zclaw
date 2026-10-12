/*
 * Host tests for the dashboard request bookkeeping (who owns which JPEG).
 */

#include <stdio.h>
#include <stdlib.h>

#include "dash_state.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static int s_frees;

static void count_free(void *ptr)
{
    s_frees++;
    free(ptr);
}

static void setup(dash_state_t *st)
{
    s_frees = 0;
    dash_state_init(st, count_free);
}

TEST(request_cancelled_before_shown_holds_nothing)
{
    dash_state_t st;
    setup(&st);
    uint32_t id = dash_state_request(&st, 0);
    ASSERT(st.need_fetch);
    dash_state_page(&st, false);           /* new activity cancelled the page in the same batch */
    ASSERT(!st.need_fetch);
    void *late = malloc(16);
    ASSERT(!dash_state_done(&st, id, late, 16));  /* stale: caller frees */
    free(late);
    ASSERT(st.jpeg == NULL);
    return 0;
}

TEST(result_is_freed_when_page_leaves)
{
    dash_state_t st;
    setup(&st);
    uint32_t id = dash_state_request(&st, 1);
    dash_state_page(&st, true);
    ASSERT(dash_state_done(&st, id, malloc(16), 16));
    ASSERT(st.jpeg != NULL);
    dash_state_page(&st, false);
    ASSERT(st.jpeg == NULL && s_frees == 1);
    return 0;
}

TEST(picture_is_freed_if_never_shown)
{
    dash_state_t st;
    setup(&st);
    dash_state_picture(&st, malloc(16), 16);
    ASSERT(st.index == -1 && !st.need_fetch);
    dash_state_page(&st, false);
    ASSERT(st.jpeg == NULL && s_frees == 1);
    return 0;
}

TEST(new_request_replaces_held_result)
{
    dash_state_t st;
    setup(&st);
    uint32_t first = dash_state_request(&st, 0);
    dash_state_page(&st, true);
    ASSERT(dash_state_done(&st, first, malloc(16), 16));
    uint32_t second = dash_state_request(&st, 1);
    ASSERT(second != first && s_frees == 1 && st.jpeg == NULL);
    void *stale = malloc(16);
    ASSERT(!dash_state_done(&st, first, stale, 16));
    free(stale);
    return 0;
}

TEST(failed_download_is_reported_once)
{
    dash_state_t st;
    setup(&st);
    uint32_t id = dash_state_request(&st, 0);
    dash_state_page(&st, true);
    ASSERT(dash_state_done(&st, id, NULL, 0));
    ASSERT(st.failed);
    dash_state_clear_result(&st);
    ASSERT(!st.failed && st.jpeg == NULL);
    return 0;
}

int test_dash_state_all(void)
{
    int failures = 0;

    printf("\nDash State Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(request_cancelled_before_shown_holds_nothing);
    RUN(result_is_freed_when_page_leaves);
    RUN(picture_is_freed_if_never_shown);
    RUN(new_request_replaces_held_result);
    RUN(failed_download_is_reported_once);

#undef RUN
    return failures;
}
