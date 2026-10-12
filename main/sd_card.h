#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

#define SD_MOUNT_POINT "/sdcard"
#define SD_RETRY_MS    60000

// Call periodically. Mounts on the first call and then every SD_RETRY_MS
// while the card is absent. Returns true right after a successful mount.
bool sd_card_service(int64_t now_ms);
bool sd_card_is_mounted(void);

// Other tasks wrap their file work in begin/end. begin is false when the card
// is not mounted or failing; a failing card is unmounted (by sd_card_service,
// display task) only after the last user called end.
bool sd_card_begin(void);
void sd_card_end(void);

// Report an I/O error: no new users, unmount once idle, retry later.
void sd_card_mark_failed(int64_t now_ms);

#endif // SD_CARD_H
