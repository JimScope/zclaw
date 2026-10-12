/*
 * Host tests for the web OTA request policy.
 */

#include <stdio.h>

#include "web_ota_policy.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(pin_matches_only_the_exact_pin)
{
    ASSERT(web_ota_pin_matches("4821", "4821"));
    ASSERT(!web_ota_pin_matches("4822", "4821"));
    ASSERT(!web_ota_pin_matches("482", "4821"));
    ASSERT(!web_ota_pin_matches("48210", "4821"));
    ASSERT(!web_ota_pin_matches("", "4821"));
    ASSERT(!web_ota_pin_matches(NULL, "4821"));
    ASSERT(!web_ota_pin_matches("", ""));
    return 0;
}

TEST(request_status_checks_in_order)
{
    ASSERT(web_ota_request_status(0, 1000, true, false) == 400);
    ASSERT(web_ota_request_status(1001, 1000, true, false) == 413);
    ASSERT(web_ota_request_status(1001, 1000, false, false) == 413);
    ASSERT(web_ota_request_status(500, 1000, false, false) == 401);
    ASSERT(web_ota_request_status(500, 1000, false, true) == 401);
    ASSERT(web_ota_request_status(500, 1000, true, true) == 409);
    ASSERT(web_ota_request_status(1000, 1000, true, false) == 0);
    return 0;
}

int test_web_ota_policy_all(void)
{
    int failures = 0;

    printf("\nWeb OTA Policy Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(pin_matches_only_the_exact_pin);
    RUN(request_status_checks_in_order);

#undef RUN
    return failures;
}
