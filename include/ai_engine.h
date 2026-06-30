/* ai_engine.h — llama.cpp C API wrapper (LLM loading / streaming) */
#ifndef AI_ENGINE_H
#define AI_ENGINE_H
#include <stdbool.h>

typedef void (*TokenCallback)(const char *token, void *userdata);

bool  ai_engine_load(const char *model_path, int n_gpu_layers);
void  ai_engine_infer(const char *prompt, TokenCallback cb, void *userdata);
void  ai_engine_reset_context(void);
void  ai_engine_unload(void);
float ai_engine_tokens_per_sec(void);

#endif /* AI_ENGINE_H */
