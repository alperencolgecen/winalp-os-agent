/* system_agent.h — JSON action parser + OS command executor */
#ifndef SYSTEM_AGENT_H
#define SYSTEM_AGENT_H
#include <stdbool.h>

/* Returns true if a complete action was parsed and queued */
bool system_agent_feed(const char *token);

/* Flush + execute any pending action (call after LLM finishes) */
void system_agent_flush(void);

/* Register a confirmation callback for destructive actions */
typedef bool (*ConfirmCallback)(const char *description, void *ud);
void system_agent_set_confirm_cb(ConfirmCallback cb, void *ud);

/* Register a callback to receive read_file content */
typedef void (*FileContentCallback)(const char *content, void *ud);
void system_agent_set_content_cb(FileContentCallback cb, void *ud);

/* Register a callback for <think>...</think> reasoning blocks */
typedef void (*ReasoningCallback)(const char *reasoning, void *ud);
void system_agent_set_reasoning_cb(ReasoningCallback cb, void *ud);

#endif /* SYSTEM_AGENT_H */
