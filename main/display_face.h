#ifndef DISPLAY_FACE_H
#define DISPLAY_FACE_H

#include <stdbool.h>
#include <stdint.h>

#include "face_state.h"

// Scan SD_MOUNT_POINT/face for <name>.r565 and <name>_2..8.r565, keep only
// files with a valid header of the right type, and update the face state's
// catalogue.
void display_face_rescan(face_state_t *st);

// Forget all frames (card removed or failed).
void display_face_clear(face_state_t *st);

// Force a full redraw on the next display_face_draw() call.
void display_face_invalidate(void);

// Draw whatever changed since the last call. Returns false on an SD I/O
// error (the caller marks the card failed and clears the catalogue).
bool display_face_draw(face_state_t *st, int64_t now_ms);

#endif // DISPLAY_FACE_H
