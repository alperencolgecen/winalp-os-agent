/* lua_runtime.h — Embedded Lua engine + hot-reload watcher */
#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H
#include <stdbool.h>

bool lua_runtime_init(void);
bool lua_runtime_exec_file(const char *path);
void lua_runtime_watch(const char *scripts_dir); /* hot-reload watcher */
void lua_runtime_shutdown(void);

#endif /* LUA_RUNTIME_H */
