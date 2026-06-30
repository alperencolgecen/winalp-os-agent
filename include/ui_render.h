/* ui_render.h — Raylib + Dear ImGui/ImPlot holographic HUD */
#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <stdbool.h>
#include <string.h>
#include "winalp.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_render_init(int width, int height, const char *title);
void ui_render_frame(AgentState state, float amplitude);
void ui_render_push_chat(const char *role, const char *text, const char *source_icon);
void ui_render_set_context_label(const char *label);
void ui_render_set_profile_label(const char *label);
bool ui_render_should_close(void);
void ui_render_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_RENDER_H */
