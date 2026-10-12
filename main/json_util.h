#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include "config.h"
#include "cJSON.h"
#include <stdbool.h>

// Forward declaration
struct tool_def;

// Conversation message
typedef struct {
    const char *role;               // "user" or "assistant"
    const char *content;            // The text, tool input JSON or tool result
    bool is_tool_use;               // True if this is a tool_use response
    bool is_tool_result;            // True if this is a tool_result
    const char *tool_id;            // Tool use ID, "" unless a tool message
    const char *tool_name;          // Tool name, "" unless a tool_use
} conversation_msg_t;

// Build the complete API request JSON
// Returns allocated string (caller must free) or NULL on error
char *json_build_request(
    const char *system_prompt,
    const conversation_msg_t *history,
    int history_len,
    const char *user_message,
    const struct tool_def *tools,
    int tool_count
);

// Parse the API response, extracting:
// - text content (if present)
// - tool_use block (if present)
// Returns true on success
bool json_parse_response(
    const char *response_json,
    char *text_out,
    size_t text_out_len,
    char *tool_name_out,
    size_t tool_name_len,
    char *tool_id_out,
    size_t tool_id_len,
    cJSON **tool_input_out  // Caller must NOT free - points into parsed tree
);

// Free the parsed response (call after done with tool_input)
void json_free_parsed_response(void);

#endif // JSON_UTIL_H
