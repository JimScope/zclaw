#ifndef SD_ACCESS_H
#define SD_ACCESS_H

#include <stdbool.h>

// Who may use the SD card, and when it may be unmounted. Several tasks read
// and write files; only the display task mounts and unmounts. An I/O error
// marks the card as failing: no new user may start, and the unmount waits
// until the last user is done. Callers serialise access to the struct.
typedef struct {
    bool mounted;
    bool failing;
    unsigned users;
} sd_access_t;

void sd_access_mounted(sd_access_t *a);

// Start using the card; false when it is not mounted or failing.
bool sd_access_begin(sd_access_t *a);
void sd_access_end(sd_access_t *a);

// Report an I/O error.
void sd_access_fail(sd_access_t *a);

// Mounted and not failing.
bool sd_access_usable(const sd_access_t *a);

// True when a failing card has no users left: the caller unmounts it now
// (the card then counts as not mounted).
bool sd_access_take_unmount(sd_access_t *a);

#endif // SD_ACCESS_H
