#include "../include/ui_render.h"
#include "../include/logger.h"

#include <cstdio>
#include <cmath>
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

/* 3D orb */
static Camera3D s_orbCamera = { 0 };
static float s_orbTime = 0.0f;

/* Smooth color transition state */
static AgentState s_lastState = AGENT_STATE_LISTENING;
static float s_colorLerp[4] = { 0.0f, 212.0f, 255.0f, 255.0f };
static float s_colorTarget[4] = { 0.0f, 212.0f, 255.0f, 255.0f };

static void state_color_rgba(AgentState state, float out[4]) {
    switch (state) {
        case AGENT_STATE_LISTENING: out[0]=0;  out[1]=212; out[2]=255; out[3]=255; break;
        case AGENT_STATE_THINKING:  out[0]=123; out[1]=47;  out[2]=190; out[3]=255; break;
        case AGENT_STATE_WRITING:   out[0]=0;   out[1]=200; out[2]=180; out[3]=255; break;
        case AGENT_STATE_ACTING:    out[0]=245; out[1]=158; out[2]=11;  out[3]=255; break;
    }
}

static void ui_draw_orb(AgentState state, float amplitude) {
    float radius = 1.8f + amplitude * 0.4f;

    s_orbTime += GetFrameTime();
    float dt = GetFrameTime();

    /* Smooth color interpolation (HSL-style ease, ~200ms transition) */
    if (state != s_lastState) {
        state_color_rgba(state, s_colorTarget);
        s_lastState = state;
    }
    float ease = 1.0f - powf(0.01f, dt * 10.0f);  /* ~200ms to 99% */
    for (int i = 0; i < 4; i++)
        s_colorLerp[i] += (s_colorTarget[i] - s_colorLerp[i]) * ease;

    Color col;
    col.r = (unsigned char)s_colorLerp[0];
    col.g = (unsigned char)s_colorLerp[1];
    col.b = (unsigned char)s_colorLerp[2];
    col.a = (unsigned char)s_colorLerp[3];

    /* Orbiting camera */
    float camAngle = s_orbTime * 0.15f;
    s_orbCamera.position = (Vector3){ sinf(camAngle) * 7.0f, 1.0f, cosf(camAngle) * 7.0f };
    s_orbCamera.target   = (Vector3){ 0, 0, 0 };
    s_orbCamera.up       = (Vector3){ 0, 1, 0 };
    s_orbCamera.fovy     = 35.0f;
    s_orbCamera.projection = CAMERA_PERSPECTIVE;

    BeginMode3D(s_orbCamera);

    /* Core sphere layers */
    DrawSphere((Vector3){ 0, 0, 0 }, radius, col);
    DrawSphere((Vector3){ 0, 0, 0 }, radius * 1.5f,
               (Color){ col.r, col.g, col.b, 20 });
    DrawSphereWires((Vector3){ 0, 0, 0 }, radius * 1.1f, 24, 16,
                    (Color){ 255, 255, 255, 50 });

    /* Orbital rings */
    float r1 = radius * 1.8f, r2 = radius * 2.2f, r3 = radius * 1.5f;

    DrawCircle3D((Vector3){ 0, 0, 0 }, r1, (Vector3){ 0, 1, 0 }, 0.0f,
                 (Color){ col.r, col.g, col.b, 100 });
    DrawCircle3D((Vector3){ 0, 0, 0 }, r2, (Vector3){ 1, 0, 0 },
                 s_orbTime * 30.0f, (Color){ col.r, col.g, col.b, 70 });
    DrawCircle3D((Vector3){ 0, 0, 0 }, r3, (Vector3){ 1, 1, 0 },
                 -s_orbTime * 40.0f, (Color){ 255, 255, 255, 50 });

    EndMode3D();
}

static void chat_push(const char *msg) {
    strncpy(s_chatBuffer[s_chatHead], msg, MAX_CHAT_LINE_LEN - 1);
    s_chatBuffer[s_chatHead][MAX_CHAT_LINE_LEN - 1] = '\0';
    s_chatHead = (s_chatHead + 1) % MAX_CHAT_LINES;
    if (s_chatCount < MAX_CHAT_LINES) s_chatCount++;
    s_chatScrolled = true;
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
