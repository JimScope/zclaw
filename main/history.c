#include "history.h"

#include <string.h>

// Records sit back to back in one arena: header, tool_id, tool_name, content,
// each string NUL-terminated, the record padded to 4 bytes. Adding a record
// drops the oldest ones until it fits.
#define FLAG_ASSISTANT   0x01
#define FLAG_TOOL_USE    0x02
#define FLAG_TOOL_RESULT 0x04
#define TOOL_ID_MAX      64
#define TOOL_NAME_MAX    32

typedef struct {
    uint32_t seq;
    uint16_t size;      // whole record, multiple of 4
    uint8_t flags;
    uint8_t id_len;     // bytes of tool_id including the NUL
    uint8_t name_len;   // bytes of tool_name including the NUL
    uint8_t pad[3];
} history_rec_t;

// The size and length fields are 16/8 bits wide; a record of maximal strings must
// fit both them and the arena.
_Static_assert(TOOL_ID_MAX <= 255 && TOOL_NAME_MAX <= 255, "id_len/name_len are uint8_t");
_Static_assert(sizeof(history_rec_t) + TOOL_ID_MAX + TOOL_NAME_MAX + MAX_MESSAGE_LEN + 3 <= 65535,
               "record size is uint16_t");
_Static_assert(sizeof(history_rec_t) + TOOL_ID_MAX + TOOL_NAME_MAX + MAX_MESSAGE_LEN + 3 <= HISTORY_ARENA_BYTES,
               "a maximal record must fit the arena");

static uint8_t s_arena[HISTORY_ARENA_BYTES] __attribute__((aligned(4)));
static size_t s_used;
static int s_count;
static uint32_t s_next_seq = 1;

static history_rec_t *rec_at(size_t off)
{
    return (history_rec_t *)(s_arena + off);
}

static size_t capped_len(const char *s, size_t cap)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (n + 1 < cap && s[n] != '\0') {
        n++;
    }
    return n;
}

static void drop_oldest(void)
{
    size_t first = rec_at(0)->size;
    memmove(s_arena, s_arena + first, s_used - first);
    s_used -= first;
    s_count--;
}

void history_clear(void)
{
    s_used = 0;
    s_count = 0;
}

void history_add(const char *role, const char *content, bool is_tool_use, bool is_tool_result,
                 const char *tool_id, const char *tool_name)
{
    size_t id_n = capped_len(tool_id, TOOL_ID_MAX);
    size_t name_n = capped_len(tool_name, TOOL_NAME_MAX);
    size_t text_n = capped_len(content, MAX_MESSAGE_LEN);
    size_t size = sizeof(history_rec_t) + id_n + 1 + name_n + 1 + text_n + 1;
    size = (size + 3u) & ~(size_t)3u;

    while (s_count > 0 && (s_count >= HISTORY_MAX_RECORDS || s_used + size > HISTORY_ARENA_BYTES)) {
        drop_oldest();
    }

    history_rec_t *rec = rec_at(s_used);
    memset(rec, 0, sizeof(*rec));
    rec->seq = s_next_seq++;
    rec->size = (uint16_t)size;
    rec->flags = (role && strcmp(role, "assistant") == 0 ? FLAG_ASSISTANT : 0) |
                 (is_tool_use ? FLAG_TOOL_USE : 0) | (is_tool_result ? FLAG_TOOL_RESULT : 0);
    rec->id_len = (uint8_t)(id_n + 1);
    rec->name_len = (uint8_t)(name_n + 1);

    char *p = (char *)(rec + 1);
    memcpy(p, tool_id ? tool_id : "", id_n);
    p[id_n] = '\0';
    p += id_n + 1;
    memcpy(p, tool_name ? tool_name : "", name_n);
    p[name_n] = '\0';
    p += name_n + 1;
    memcpy(p, content ? content : "", text_n);
    p[text_n] = '\0';

    s_used += size;
    s_count++;
}

history_mark_t history_mark(void)
{
    return s_next_seq;
}

int history_rollback(history_mark_t mark)
{
    size_t off = 0;
    for (int i = 0; i < s_count; i++) {
        history_rec_t *rec = rec_at(off);
        if (rec->seq >= mark) {
            int dropped = s_count - i;
            s_used = off;
            s_count = i;
            return dropped;
        }
        off += rec->size;
    }
    return 0;
}

int history_view(conversation_msg_t *out, int max)
{
    size_t off = 0;
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++) {
        history_rec_t *rec = rec_at(off);
        const char *id = (const char *)(rec + 1);
        const char *name = id + rec->id_len;
        out[n].role = (rec->flags & FLAG_ASSISTANT) ? "assistant" : "user";
        out[n].is_tool_use = (rec->flags & FLAG_TOOL_USE) != 0;
        out[n].is_tool_result = (rec->flags & FLAG_TOOL_RESULT) != 0;
        out[n].tool_id = id;
        out[n].tool_name = name;
        out[n].content = name + rec->name_len;
        n++;
        off += rec->size;
    }
    return n;
}

int history_count(void)
{
    return s_count;
}
