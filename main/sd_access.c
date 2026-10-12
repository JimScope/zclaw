#include "sd_access.h"

void sd_access_mounted(sd_access_t *a)
{
    a->mounted = true;
    a->failing = false;
}

bool sd_access_begin(sd_access_t *a)
{
    if (!sd_access_usable(a)) {
        return false;
    }
    a->users++;
    return true;
}

void sd_access_end(sd_access_t *a)
{
    if (a->users > 0) {
        a->users--;
    }
}

void sd_access_fail(sd_access_t *a)
{
    if (a->mounted) {
        a->failing = true;
    }
}

bool sd_access_usable(const sd_access_t *a)
{
    return a->mounted && !a->failing;
}

bool sd_access_take_unmount(sd_access_t *a)
{
    if (!a->mounted || !a->failing || a->users > 0) {
        return false;
    }
    a->mounted = false;
    a->failing = false;
    return true;
}
