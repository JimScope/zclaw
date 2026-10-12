#ifndef HISTORY_H
#define HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "json_util.h"

// Conversation history packed into one small arena of variable-length records.
#define HISTORY_ARENA_BYTES  8192
#define HISTORY_MAX_RECORDS  (MAX_HISTORY_TURNS * 2)

// Opaque turn marker: records added after it have a sequence >= the mark.
typedef uint32_t history_mark_t;

void history_clear(void);

// Append a record (oldest ones are dropped until it fits). Strings are copied
// and truncated; NULL tool_id/tool_name are stored as "".
void history_add(const char *role, const char *content, bool is_tool_use, bool is_tool_result,
                 const char *tool_id, const char *tool_name);

history_mark_t history_mark(void);

// Drop every record added since the mark. Returns how many were dropped.
int history_rollback(history_mark_t mark);

// Fill out[] with pointers into the arena (valid until the next add/rollback/clear).
int history_view(conversation_msg_t *out, int max);

int history_count(void);

#endif // HISTORY_H
