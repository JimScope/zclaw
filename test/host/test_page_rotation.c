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

#undef RUN
    return failures;
}
