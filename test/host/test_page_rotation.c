/*
 * Host tests for face/status page rotation.
 */

#include <stdio.h>

#include "page_rotation.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(rotates_face_then_status)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 5000, 0);
    ASSERT(page_rotation_update(&r, false, 0) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 9999) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 10000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 14999) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 15000) == PAGE_FACE);
    return 0;
}

TEST(activity_forces_face_and_pauses_rotation)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 5000, 0);
    ASSERT(page_rotation_update(&r, false, 11000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, true, 12000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, true, 40000) == PAGE_FACE);
    /* rotation resumes with a full face period after activity ends */
    ASSERT(page_rotation_update(&r, false, 49999) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 50000) == PAGE_STATUS);
    return 0;
}

TEST(zero_status_time_keeps_face)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, false, 10000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 100000) == PAGE_FACE);
    return 0;
}

TEST(requested_status_shows_for_duration_then_face)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    page_rotation_show(&r, PAGE_STATUS, 15000, 1000);
    ASSERT(page_rotation_update(&r, false, 1000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 15999) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 16000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 100000) == PAGE_FACE);
    return 0;
}

TEST(activity_cancels_requested_status)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    page_rotation_show(&r, PAGE_STATUS, 15000, 0);
    ASSERT(page_rotation_update(&r, false, 1000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, true, 5000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 6000) == PAGE_FACE);
    return 0;
}

TEST(requested_status_with_rotation_resumes_full_face_period)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 5000, 0);
    page_rotation_show(&r, PAGE_STATUS, 15000, 2000);
    ASSERT(page_rotation_update(&r, false, 2000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 16999) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 17000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 26999) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 27000) == PAGE_STATUS);
    return 0;
}

TEST(remote_page_can_replace_requested_status)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    page_rotation_show(&r, PAGE_STATUS, 30000, 0);
    ASSERT(page_rotation_update(&r, false, 100) == PAGE_STATUS);
    page_rotation_show(&r, PAGE_REMOTE, 30000, 5000);
    ASSERT(page_rotation_update(&r, false, 5000) == PAGE_REMOTE);
    ASSERT(page_rotation_update(&r, false, 34999) == PAGE_REMOTE);
    ASSERT(page_rotation_update(&r, false, 35000) == PAGE_FACE);
    return 0;
}

TEST(request_made_during_activity_survives_that_activity)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, true, 0) == PAGE_FACE);       /* Fidel starts thinking */
    page_rotation_show(&r, PAGE_REMOTE, 30000, 100);              /* tool shows a picture */
    ASSERT(page_rotation_update(&r, true, 200) == PAGE_REMOTE);
    ASSERT(page_rotation_update(&r, true, 5000) == PAGE_REMOTE);  /* still talking */
    ASSERT(page_rotation_update(&r, false, 8000) == PAGE_REMOTE); /* turn over */
    ASSERT(page_rotation_update(&r, true, 9000) == PAGE_FACE);    /* next message cancels it */
    ASSERT(page_rotation_update(&r, false, 9500) == PAGE_FACE);
    return 0;
}

int test_page_rotation_all(void)
{
    int failures = 0;

    printf("\nPage Rotation Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(rotates_face_then_status);
    RUN(activity_forces_face_and_pauses_rotation);
    RUN(zero_status_time_keeps_face);
    RUN(requested_status_shows_for_duration_then_face);
    RUN(activity_cancels_requested_status);
    RUN(requested_status_with_rotation_resumes_full_face_period);
    RUN(remote_page_can_replace_requested_status);
    RUN(request_made_during_activity_survives_that_activity);

#undef RUN
    return failures;
}
