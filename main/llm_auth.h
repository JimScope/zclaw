#ifndef LLM_AUTH_H
#define LLM_AUTH_H

#include <stdbool.h>
#include <stddef.h>

// Build a "Bearer <api_key>" authorization header into auth_header.
bool llm_build_bearer_auth_header(const char *api_key, char *auth_header, size_t auth_header_size);

#endif // LLM_AUTH_H
