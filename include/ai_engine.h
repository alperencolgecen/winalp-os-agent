/* ai_engine.h — llama.cpp C API wrapper (LLM loading / streaming) */
#ifndef AI_ENGINE_H
#define AI_ENGINE_H
#include <stdbool.h>

typedef void (*TokenCallback)(const char *token, void *userdata);

void  ai_engine_set_system_prompt(const char *prompt);
void  ai_engine_set_dynamic_context(const char *ctx);
bool  ai_engine_load(const char *model_path, int n_gpu_layers);
bool  ai_engine_load_auto(const char *model_path);
bool  ai_engine_is_loaded(void);
void  ai_engine_infer(const char *prompt, TokenCallback cb, void *userdata);
void  ai_engine_push_history(const char *role, const char *content);
void  ai_engine_reset_context(void);
void  ai_engine_unload(void);
float ai_engine_tokens_per_sec(void);
int   ai_engine_context_usage(void);

#endif /* AI_ENGINE_H */
