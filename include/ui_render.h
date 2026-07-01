/* ui_render.h — Raylib + Dear ImGui/ImPlot holographic HUD */
#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <stdbool.h>
#include <string.h>
#include "winalp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char path[512];
    char label[256];
    char arch[64];            /* GGUF architecture hint (e.g. "qwen2", "llama") */
    unsigned long long size_mb;
    int tier;                 /* 0=hafif(<1.5GB), 1=orta(1.5-8GB), 2=guclu(>8GB) */
    int compat;               /* 0=unknown, 1=red(not enough RAM), 2=yellow(borderline), 3=green(OK) */
    int flags;                /* bit 0: VLM-capable (mmproj companion found) */
} ModelEntry;

/* Model selection screen — returns strdup'd path or NULL on exit */
char *ui_render_model_select(ModelEntry *models, int n_models);

/* Blocking confirmation overlay — returns true (Yes) or false (No) */
bool ui_render_confirm_blocking(const char *title, const char *msg);

/* Keyboard text input — poll in main loop */
bool ui_render_has_text_input(void);
void ui_render_get_text_input(char *out, int out_len);

void ui_render_init(int width, int height, const char *title);
void ui_render_frame(AgentState state, float amplitude);
void ui_render_push_chat(const char *role, const char *text, const char *source_icon);
void ui_render_set_context_label(const char *label);
void ui_render_set_profile_label(const char *label);
void ui_render_set_task_strip(const char *tasks);
void ui_render_set_theme_float(const char *key, float value);
bool ui_render_should_close(void);
void ui_render_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_RENDER_H */
