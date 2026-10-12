#ifndef AGENT_PROMPT_H
#define AGENT_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

// Size of the agent's buffer for the composed system prompt.
#define AGENT_SYSTEM_PROMPT_BUF_SIZE 2816  // full prompt with the screen hint is ~2.45 KB

const char *agent_build_system_prompt(char *buf, size_t buf_len);

#endif // AGENT_PROMPT_H
