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

void plugin_manager_shutdown(void);

#endif /* PLUGIN_MANAGER_H */
