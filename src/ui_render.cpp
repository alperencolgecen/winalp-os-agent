#include "../include/ui_render.h"
#include "../include/logger.h"

#include "raylib.h"
#include "imgui.h"
#include "imgui_impl_raylib.h"

static int s_width = 1280;
static int s_height = 720;
static int s_fontSize = 10;

static char s_contextLabel[256] = "";
static char s_profileLabel[256] = "";
static bool s_shouldClose = false;

static void ui_draw_orb(AgentState state, float amplitude) {
    float radius = 80.0f + amplitude * 20.0f;
    Color baseColor;
    switch (state) {
        case AGENT_STATE_LISTENING: baseColor = (Color){ 0, 212, 255, 255 }; break;
        case AGENT_STATE_THINKING:  baseColor = (Color){ 123, 47, 190, 255 }; break;
        case AGENT_STATE_WRITING:   baseColor = (Color){ 0, 200, 180, 255 }; break;
        case AGENT_STATE_ACTING:    baseColor = (Color){ 245, 158, 11, 255 }; break;
    }

    DrawCircleV((Vector2){ s_width / 2.0f, s_height / 2.0f - 40 }, radius, baseColor);
    DrawCircleV((Vector2){ s_width / 2.0f, s_height / 2.0f - 40 }, radius * 0.7f,
                (Color){ baseColor.r, baseColor.g, baseColor.b, 80 });
}

static void ui_draw_hud_panels(void) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)s_width, 24), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##taskstrip", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("  WinAlp v%d.%d.%d", WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);
    if (s_contextLabel[0]) { ImGui::SameLine(); ImGui::Text("  |  Context: %s", s_contextLabel); }
    if (s_profileLabel[0]) { ImGui::SameLine(); ImGui::Text("  |  %s", s_profileLabel); }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 24), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, (float)s_height - 24), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::Begin("##context", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("Active Context");
    ImGui::Separator();
    if (s_contextLabel[0]) ImGui::TextWrapped("%s", s_contextLabel);
    else ImGui::Text("(none)");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2((float)s_width - 220, 24), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, (float)s_height - 24), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::Begin("##profile", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("Profile");
    ImGui::Separator();
    if (s_profileLabel[0]) ImGui::TextWrapped("%s", s_profileLabel);
    else ImGui::Text("(no profile loaded)");
    ImGui::End();
}

void ui_render_init(int width, int height, const char *title) {
    s_width = width;
    s_height = height;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);
    SetTargetFPS(WINALP_TARGET_FPS);

    ImGui_ImplRaylib_Init(width, height);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.05f, 0.08f, 0.85f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.85f, 0.90f, 1.00f);

    winalp_log(WINALP_LOG_INFO, "UI initialized: %dx%d", width, height);
}

void ui_render_frame(AgentState state, float amplitude) {
    if (WindowShouldClose()) {
        s_shouldClose = true;
        return;
    }

    s_width = GetScreenWidth();
    s_height = GetScreenHeight();

    ImGui_ImplRaylib_NewFrame();

    BeginDrawing();
    ClearBackground((Color){ 8, 12, 20, 255 });

    ui_draw_orb(state, amplitude);

    ImGui_ImplRaylib_RenderDrawData();
    EndDrawing();
}

bool ui_render_should_close(void) {
    return s_shouldClose;
}

void ui_render_shutdown(void) {
    ImGui_ImplRaylib_Shutdown();
    CloseWindow();
}

void ui_render_push_chat(const char *role, const char *text, const char *source_icon) {
    (void)role; (void)text; (void)source_icon;
}

void ui_render_set_context_label(const char *label) {
    strncpy(s_contextLabel, label, sizeof(s_contextLabel) - 1);
}

void ui_render_set_profile_label(const char *label) {
    strncpy(s_profileLabel, label, sizeof(s_profileLabel) - 1);
}
