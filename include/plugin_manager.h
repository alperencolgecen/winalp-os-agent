/* plugin_manager.h — Sandboxed Lua plugin loader & lifecycle manager */
#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H
#include <stdbool.h>

bool plugin_manager_init(const char *plugins_dir);

/* Scan plugins/ dir and populate internal registry */
int  plugin_manager_scan(void);

/* Activate / deactivate by plugin name */
bool plugin_manager_activate(const char *name);
bool plugin_manager_deactivate(const char *name);

/* Build guide string listing all active plugins + their actions (caller frees) */
char *plugin_manager_build_guide(void);

/* Sandbox: check if action is allowed (not blacklisted) */
bool plugin_manager_is_action_allowed(const char *action);

/* Hot-reload watcher — call each frame; returns number of (re)loaded plugins */
int  plugin_manager_watch(void);

/* Get combined action list from all active plugins (caller frees) */
char *plugin_manager_get_actions(void);

/* Register a callback for when plugins add/remove actions */
typedef void (*PluginActionCb)(const char *action_name, bool added, void *ud);
void plugin_manager_set_action_cb(PluginActionCb cb, void *ud);

/* Execute a plugin action — returns true if a plugin handled it */
bool plugin_manager_execute_action(const char *action, const char *path,
                                    const char *content, void *ud);

void plugin_manager_shutdown(void);

#endif /* PLUGIN_MANAGER_H */
