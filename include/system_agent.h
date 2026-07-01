/* system_agent.h — JSON action parser + OS command executor */
#ifndef SYSTEM_AGENT_H
#define SYSTEM_AGENT_H
#include <stdbool.h>

/* One-shot JSON action result */
typedef struct {
    char type[64];
    char path[1024];
    char content[65536];
} AgentAction;

/* Parse a complete JSON action string; returns 1 on success, 0 on failure */
int system_agent_parse(const char *json, int len, AgentAction *out);

/* Validate a path against the sandbox whitelist */
bool system_agent_validate_path(const char *path);

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

/* Register a callback for unknown plugin actions — returns true if handled */
typedef bool (*PluginActionCallback)(const char *action, const char *path,
                                      const char *content, void *ud);
void system_agent_set_plugin_action_cb(PluginActionCallback cb, void *ud);

#endif /* SYSTEM_AGENT_H */
