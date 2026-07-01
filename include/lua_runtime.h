/* lua_runtime.h — Embedded Lua engine + hot-reload watcher */
#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H
#include <stdbool.h>

/* Opaque Lua state handle — defined in implementation */
typedef struct lua_State lua_State;

/* Initialise the runtime (no Lua state created yet) */
bool lua_runtime_init(void);

/* Create a new sandboxed Lua state and register WinAlp API */
lua_State *lua_runtime_new_state(const char *sandbox_data_dir);

/* Run a Lua script file in the given state; returns true on success */
bool lua_runtime_dofile(lua_State *L, const char *path);

/* Run a Lua string chunk; returns true on success */
bool lua_runtime_dostring(lua_State *L, const char *chunk);

/* Close a Lua state */
void lua_runtime_close(lua_State *L);

/* Add a script to the watch list (hot-reload tracking) */
bool lua_runtime_exec_file(const char *path);

/* Poll watched directory for changes — call each frame */
void lua_runtime_watch(const char *scripts_dir);

/* Shut down all states */
void lua_runtime_shutdown(void);

/* Get the last Lua error message (thread-local) */
const char *lua_runtime_last_error(void);

#endif /* LUA_RUNTIME_H */
