/*
 * Host tests for SD card access coordination.
 */

#include <stdio.h>

#include "sd_access.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(begin_needs_a_mounted_card)
{
    sd_access_t a = {0};
    ASSERT(!sd_access_begin(&a));
    sd_access_mounted(&a);
    ASSERT(sd_access_usable(&a));
    ASSERT(sd_access_begin(&a));
    ASSERT(a.users == 1);
    sd_access_end(&a);
    ASSERT(a.users == 0);
    return 0;
}

TEST(failing_card_waits_for_current_user)
{
    sd_access_t a = {0};
    sd_access_mounted(&a);
    ASSERT(sd_access_begin(&a));      /* agent writing the task log */
    sd_access_fail(&a);               /* display read error */
    ASSERT(!sd_access_usable(&a));
    ASSERT(!sd_access_begin(&a));     /* no new user */
    ASSERT(!sd_access_take_unmount(&a));  /* still in use */
    sd_access_end(&a);
    ASSERT(sd_access_take_unmount(&a));
    ASSERT(!a.mounted);
    ASSERT(!sd_access_take_unmount(&a));  /* only once */
    return 0;
}

TEST(failing_card_without_users_unmounts_at_once)
{
    sd_access_t a = {0};
    sd_access_mounted(&a);
    sd_access_fail(&a);
    ASSERT(sd_access_take_unmount(&a));
    return 0;
}

TEST(healthy_card_is_never_unmounted)
{
    sd_access_t a = {0};
    ASSERT(!sd_access_take_unmount(&a));
    sd_access_mounted(&a);
    ASSERT(!sd_access_take_unmount(&a));
    sd_access_fail(&a);
    ASSERT(sd_access_take_unmount(&a));
    sd_access_mounted(&a);              /* a remount clears the failure */
    ASSERT(sd_access_usable(&a));
    ASSERT(!sd_access_take_unmount(&a));
    return 0;
}

TEST(end_without_begin_does_not_underflow)
{
    sd_access_t a = {0};
    sd_access_mounted(&a);
    sd_access_end(&a);
    ASSERT(a.users == 0);
    sd_access_fail(&a);
    ASSERT(sd_access_take_unmount(&a));
    return 0;
}

int test_sd_access_all(void)
{
    int failures = 0;

    printf("\nSD Access Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(begin_needs_a_mounted_card);
    RUN(failing_card_waits_for_current_user);
    RUN(failing_card_without_users_unmounts_at_once);
    RUN(healthy_card_is_never_unmounted);
    RUN(end_without_begin_does_not_underflow);

#undef RUN
    return failures;
}
