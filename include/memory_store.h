/* memory_store.h — SQLite + profile.json persistence layer */
#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H
#include <stdbool.h>

/* Initialise storage — creates profile/ tree if missing */
bool memory_store_init(const char *base_dir);
void memory_store_shutdown(void);

/* Conversation history */
bool memory_store_append_message(const char *role, const char *source,
                                  const char *content);
/* Profile key-value */
bool memory_store_set_profile(const char *key, const char *value);
bool memory_store_get_profile(const char *key, char *out, int out_len);

/* Task CRUD */
bool memory_store_upsert_task(const char *task_json);
bool memory_store_get_tasks(char *out_json, int out_len);
bool memory_store_delete_task(const char *task_id);
bool memory_store_update_task(const char *task_id,
                               const char *field,
                               const char *value);

/* Integrity check on startup */
bool memory_store_integrity_check(void);

/* DPAPI encryption toggle (default off) */
void memory_store_set_encryption(bool enable);

#endif /* MEMORY_STORE_H */
