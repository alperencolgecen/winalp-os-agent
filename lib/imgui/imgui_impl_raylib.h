#ifndef IMGUI_IMPL_RAYLIB_H
#define IMGUI_IMPL_RAYLIB_H

#include "raylib.h"

bool ImGui_ImplRaylib_Init(int width, int height);
void ImGui_ImplRaylib_Shutdown(void);
void ImGui_ImplRaylib_NewFrame(void);
void ImGui_ImplRaylib_ProcessEvent(void);
void ImGui_ImplRaylib_RenderDrawData(void);

#endif
