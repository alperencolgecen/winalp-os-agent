#include "../include/ui_render.h"
#include "../include/logger.h"

#include <cstdio>
#include "raylib.h"
#include "imgui.h"
#include "imgui_impl_raylib.h"

#define MAX_CHAT_LINES 256
#define MAX_CHAT_LINE_LEN 512

static int s_width = 1280;
static int s_height = 720;

static char s_contextLabel[256] = "";
static char s_profileLabel[512] = "";

static bool s_shouldClose = false;

static char s_chatBuffer[MAX_CHAT_LINES][MAX_CHAT_LINE_LEN];
static int s_chatCount = 0;
static int s_chatHead = 0;
static bool s_chatScrolled = false;

static ImGuiTextFilter s_chatFilter;

static void chat_push(const char *msg) {
    strncpy(s_chatBuffer[s_chatHead], msg, MAX_CHAT_LINE_LEN - 1);
    s_chatBuffer[s_chatHead][MAX_CHAT_LINE_LEN - 1] = '\0';
    s_chatHead = (s_chatHead + 1) % MAX_CHAT_LINES;
    if (s_chatCount < MAX_CHAT_LINES) s_chatCount++;
    s_chatScrolled = true;
}

static void ui_draw_orb(AgentState state, float amplitude) {
    float radius = 80.0f + amplitude * 20.0f;
    Color baseColor;
    switch (state) {
        case AGENT_STATE_LISTENING: baseColor = (Color){ 0, 212, 255, 255 }; break;
        case AGENT_STATE_THINKING:  baseColor = (Color){ 123, 47, 190, 255 }; break;
        case AGENT_STATE_WRITING:   baseColor = (Color){ 0, 200, 180, 255 }; break;
        case AGENT_STATE_ACTING:    baseColor = (Color){ 245, 158, 11, 255 }; break;
    }
    float cx = s_width / 2.0f;
    float cy = s_height / 2.0f - 60.0f;
    DrawCircleV((Vector2){ cx, cy }, radius, baseColor);
    DrawCircleV((Vector2){ cx, cy }, radius * 0.7f,
                (Color){ baseColor.r, baseColor.g, baseColor.b, 80 });
}

static void ui_draw_top_strip(void) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)s_width, 26), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##topstrip", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus);
    ImGui::Text("WinAlp v%d.%d.%d", WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);
    if (s_contextLabel[0]) { ImGui::SameLine(); ImGui::Text(" | %s", s_contextLabel); }
    if (s_profileLabel[0]) { ImGui::SameLine(); ImGui::TextDisabled(" | %s", s_profileLabel); }
    ImGui::End();
}

static void ui_draw_left_panel(void) {
    ImGui::SetNextWindowPos(ImVec2(0, 26), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, (float)s_height - 26 - 200), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.25f);
    ImGui::Begin("##ctxpanel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::TextUnformatted("CONTEXT");
    ImGui::Separator();
    if (s_contextLabel[0])
        ImGui::TextWrapped("%s", s_contextLabel);
    else
        ImGui::TextDisabled("(no active context)");
    ImGui::End();
}

static void ui_draw_right_panel(void) {
    ImGui::SetNextWindowPos(ImVec2((float)s_width - 200, 26), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, (float)s_height - 26 - 200), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.25f);
    ImGui::Begin("##propanel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::TextUnformatted("PROFILE");
    ImGui::Separator();
    if (s_profileLabel[0])
        ImGui::TextWrapped("%s", s_profileLabel);
    else
        ImGui::TextDisabled("(no profile loaded)");
    ImGui::End();
}

static void ui_draw_bottom_chat(void) {
    ImGui::SetNextWindowPos(ImVec2(200, (float)s_height - 200), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)s_width - 400, 200), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::Begin("##chat", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    if (ImGui::SmallButton("Clear")) { s_chatCount = 0; s_chatHead = 0; }
    ImGui::SameLine();
    s_chatFilter.Draw("Filter", 180);

    ImGui::Separator();
    ImGui::BeginChild("##chatscroll", ImVec2(0, 0), false, ImGuiWindowFlags_NoNavFocus);
    if (s_chatCount > 0) {
        int start = (s_chatCount < MAX_CHAT_LINES) ? 0 : s_chatHead;
        int count = (s_chatCount < MAX_CHAT_LINES) ? s_chatCount : MAX_CHAT_LINES;
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % MAX_CHAT_LINES;
            if (s_chatBuffer[idx][0] && s_chatFilter.PassFilter(s_chatBuffer[idx]))
                ImGui::TextUnformatted(s_chatBuffer[idx]);
        }
        if (s_chatScrolled) {
            ImGui::SetScrollHereY(1.0f);
            s_chatScrolled = false;
        }
    } else {
        ImGui::TextDisabled("No messages yet.");
    }
    ImGui::EndChild();
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

    chat_push("[system] WinAlp HUD initialized");
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
    ui_draw_top_strip();
    ui_draw_left_panel();
    ui_draw_right_panel();
    ui_draw_bottom_chat();

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
    char buf[MAX_CHAT_LINE_LEN];
    if (source_icon && source_icon[0])
        snprintf(buf, sizeof(buf), "[%s][%s] %s", source_icon, role, text);
    else
        snprintf(buf, sizeof(buf), "[%s] %s", role, text);
    chat_push(buf);
}

void ui_render_set_context_label(const char *label) {
    strncpy(s_contextLabel, label, sizeof(s_contextLabel) - 1);
}

void ui_render_set_profile_label(const char *label) {
    strncpy(s_profileLabel, label, sizeof(s_profileLabel) - 1);
}
