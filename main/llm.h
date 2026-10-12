#ifndef LLM_H
#define LLM_H

#include "config.h"
#include "esp_err.h"
#include <stdbool.h>

// Initialize the LLM HTTP client
esp_err_t llm_init(void);

// Send a request to LLM API
// request_json: the complete API request body (OpenAI chat completions format)
// response_buf: buffer to store the response
// response_buf_size: size of response buffer
// Returns ESP_OK on success
esp_err_t llm_request(const char *request_json, char *response_buf, size_t response_buf_size);

// Get API URL
const char *llm_get_api_url(void);

// Get default model
const char *llm_get_default_model(void);

// Get current model (user-configured or default)
const char *llm_get_model(void);

#ifdef TEST_BUILD
// Host-test helper: indicates whether an API key is currently loaded in runtime state.
bool llm_test_has_api_key(void);
#endif

#endif // LLM_H
