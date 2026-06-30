/* prompt_engine.h — System prompt synthesizer */
#ifndef PROMPT_ENGINE_H
#define PROMPT_ENGINE_H
#include <stdbool.h>

bool prompt_engine_init(const char *prompts_dir);

/* Set active template by name (filename without .txt) */
bool prompt_engine_set_template(const char *name);

/* Build the full system prompt string (caller frees) */
char *prompt_engine_build(const char *profile_summary,
                           const char *plugin_guide);

void prompt_engine_reload(void);   /* re-scan prompts/ dir */
void prompt_engine_shutdown(void);

#endif /* PROMPT_ENGINE_H */
