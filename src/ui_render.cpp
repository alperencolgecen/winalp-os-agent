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

/* Waveform history */
#define WAVE_LEN 256
static float s_waveform[WAVE_LEN];
static int   s_wavePos;

/* Overlay state */
static bool  s_overlay_active;
static char  s_overlay_title[128];
static char  s_overlay_msg[512];
static bool  s_overlay_result;

/* Task strip data */
static char s_task_strip[1024] = "";

/* Text input */
static char s_input_buf[512] = "";
static bool s_input_pending;

/* 3D orb */
static Camera3D s_orbCamera = { 0 };
static float s_orbTime = 0.0f;

/* Smooth color transition state */
static AgentState s_lastState = AGENT_STATE_LISTENING;
static float s_colorLerp[4] = { 0.0f, 212.0f, 255.0f, 255.0f };
static float s_colorTarget[4] = { 0.0f, 212.0f, 255.0f, 255.0f };

/* Model selection screen */
char *ui_render_model_select(ModelEntry *models, int n_models) {
    if (n_models <= 0) return NULL;

    InitWindow(800, 500, "WinAlp — Model Selection");
    SetTargetFPS(60);

    int selected = -1;
    int scroll   = 0;
    int hovered  = -1;
    Font font = GetFontDefault();

    while (!WindowShouldClose()) {
        int mw = GetScreenWidth();
        int mh = GetScreenHeight();
        Vector2 mp = GetMousePosition();
        hovered = -1;

        BeginDrawing();
        ClearBackground((Color){ 8, 12, 20, 255 });

        /* Title */
        DrawTextEx(font, "SELECT MODEL", (Vector2){ 40, 20 }, 28, 1, (Color){ 0, 212, 255, 255 });
        DrawTextEx(font, "Choose a brain model to start WinAlp",
                   (Vector2){ 40, 55 }, 14, 1, (Color){ 128, 140, 160, 255 });

        /* List models */
        int x = 40, y = 90, item_h = 48;
        for (int i = 0; i < n_models; i++) {
            int cy = y + (i - scroll) * item_h;
            if (cy + item_h < 90 || cy > mh - 20) continue;

            /* Hover/click detection */
            Rectangle rect = { (float)x, (float)cy, (float)(mw - 80), (float)item_h };
            bool inside = CheckCollisionPointRec(mp, rect);
            if (inside) hovered = i;

            /* Background */
            Color bg = (i == selected) ? (Color){ 0, 100, 180, 100 } :
                       inside           ? (Color){ 40, 50, 70, 100 } :
                                          (Color){ 15, 20, 30, 80 };
            DrawRectangleRec(rect, bg);

            /* Tier indicator */
            Color tier_col;
            switch (models[i].tier) {
                case 2: tier_col = (Color){ 0, 200, 180, 255 }; break; /* vision=teal */
                default: tier_col = (Color){ 100, 180, 255, 255 }; break; /* text=blue */
            }
            DrawCircle(x + 12, cy + item_h/2, 6, tier_col);

            /* Name + size */
            char label[512];
            snprintf(label, sizeof(label), "%s  (%llu MB)",
                     models[i].label, models[i].size_mb);
            DrawTextEx(font, label, (Vector2){ (float)(x + 30), (float)(cy + 6) },
                       16, 1, (Color){ 220, 230, 240, 255 });

            /* Tier label */
            const char *tier_str = (models[i].tier == 2) ? "Vision" : "Text";
            DrawTextEx(font, tier_str,
                       (Vector2){ (float)(mw - 120), (float)(cy + 8) },
                       12, 1, tier_col);
        }

        /* Scroll hint */
        DrawTextEx(font, "Click a model to select  |  ESC to exit",
                   (Vector2){ 40, (float)(mh - 30) }, 12, 1,
                   (Color){ 80, 90, 110, 200 });

        /* Handle input */
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered >= 0) {
            selected = hovered;
        }
        /* Wheel */
        scroll -= (int)GetMouseWheelMove();
        if (scroll < 0) scroll = 0;
        if (scroll > n_models - 1) scroll = n_models - 1;
        if (scroll < 0) scroll = 0;

        EndDrawing();

        /* Confirm selection */
        if (selected >= 0 && (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER))) {
            char *result = (char*)malloc(strlen(models[selected].path) + 1);
            if (result) strcpy(result, models[selected].path);
            CloseWindow();
            return result;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            CloseWindow();
            return NULL;
        }
    }

    CloseWindow();
    return NULL;
}

static void state_color_rgba(AgentState state, float out[4]) {
    switch (state) {
        case AGENT_STATE_LISTENING: out[0]=0;  out[1]=212; out[2]=255; out[3]=255; break;
        case AGENT_STATE_THINKING:  out[0]=123; out[1]=47;  out[2]=190; out[3]=255; break;
        case AGENT_STATE_WRITING:   out[0]=0;   out[1]=200; out[2]=180; out[3]=255; break;
        case AGENT_STATE_ACTING:    out[0]=245; out[1]=158; out[2]=11;  out[3]=255; break;
    }
}

/* Update waveform ring buffer with current amplitude */
static void waveform_push(float amplitude) {
    s_waveform[s_wavePos] = amplitude;
    s_wavePos = (s_wavePos + 1) % WAVE_LEN;
}

/* Draw waveform at bottom of screen */
static void ui_draw_waveform(void) {
    int wy = s_height - 60;
    int wh = 40;
    int wx = 200;
    int ww = s_width - 400;

    DrawRectangleGradientV(wx, wy, ww, wh,
                           (Color){ 8, 12, 20, 0 },
                           (Color){ 0, 212, 255, 12 });
    DrawRectangleLines(wx, wy, ww, wh, (Color){ 0, 212, 255, 30 });

    for (int i = 1; i < ww; i++) {
        int idx0 = (s_wavePos + (i - 1) * WAVE_LEN / ww) % WAVE_LEN;
        int idx1 = (s_wavePos + i * WAVE_LEN / ww) % WAVE_LEN;
        int x0 = wx + i - 1;
        int x1 = wx + i;
        int y0 = wy + wh / 2 - (int)(s_waveform[idx0] * (wh / 2));
        int y1 = wy + wh / 2 - (int)(s_waveform[idx1] * (wh / 2));
        DrawLine(x0, y0, x1, y1, (Color){ 0, 212, 255, 100 });
    }
}

/* Action confirmation overlay */
bool ui_render_show_overlay(const char *title, const char *msg) {
    s_overlay_active = true;
    strncpy(s_overlay_title, title ? title : "Confirm", sizeof(s_overlay_title) - 1);
    strncpy(s_overlay_msg, msg ? msg : "", sizeof(s_overlay_msg) - 1);
    s_overlay_result = false;
    return s_overlay_result;
}

/* Blocking version — pumps raylib event loop until user answers */
static void ui_draw_overlay(void);
bool ui_render_confirm_blocking(const char *title, const char *msg) {
    s_overlay_active = true;
    strncpy(s_overlay_title, title ? title : "Confirm", sizeof(s_overlay_title) - 1);
    strncpy(s_overlay_msg, msg ? msg : "", sizeof(s_overlay_msg) - 1);
    s_overlay_result = false;

    while (s_overlay_active && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 8, 12, 20, 255 });
        ui_draw_overlay();
        EndDrawing();
    }
    return s_overlay_result;
}

static void ui_draw_overlay(void) {
    if (!s_overlay_active) return;

    /* Dim background */
    DrawRectangle(0, 0, s_width, s_height, (Color){ 0, 0, 0, 120 });

    /* Modal box */
    int mw = 400, mh = 150;
    int mx = (s_width - mw) / 2, my = (s_height - mh) / 2;
    DrawRectangle(mx, my, mw, mh, (Color){ 15, 25, 40, 230 });
    DrawRectangleLines(mx, my, mw, mh, (Color){ 0, 212, 255, 80 });

    DrawText(s_overlay_title, mx + 20, my + 15, 18, (Color){ 0, 212, 255, 255 });
    DrawText(s_overlay_msg, mx + 20, my + 45, 14, (Color){ 180, 190, 200, 255 });

    /* Yes button */
    Rectangle yBtn = { (float)(mx + 80), (float)(my + 100), 100, 30 };
    DrawRectangleRec(yBtn, (Color){ 0, 180, 80, 200 });
    DrawText("YES", (int)yBtn.x + 30, (int)yBtn.y + 6, 14, WHITE);

    /* No button */
    Rectangle nBtn = { (float)(mx + 220), (float)(my + 100), 100, 30 };
    DrawRectangleRec(nBtn, (Color){ 180, 40, 40, 200 });
    DrawText("NO", (int)nBtn.x + 32, (int)nBtn.y + 6, 14, WHITE);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Vector2 mp = GetMousePosition();
        if (CheckCollisionPointRec(mp, yBtn)) {
            s_overlay_result = true;
            s_overlay_active = false;
        }
        if (CheckCollisionPointRec(mp, nBtn)) {
            s_overlay_result = false;
            s_overlay_active = false;
        }
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

    /* Amplitude-driven morphing — orb breathes with audio/mock RMS */
    float breath  = 0.7f + 0.3f * amplitude;
    int glowAlpha = 15 + (int)(amplitude * 25.0f);
    int wireAlpha = 30 + (int)(amplitude * 40.0f);

    DrawSphere((Vector3){ 0, 0, 0 }, radius, col);
    DrawSphere((Vector3){ 0, 0, 0 }, radius * 1.5f * breath,
               (Color){ col.r, col.g, col.b, (unsigned char)glowAlpha });
    DrawSphereWires((Vector3){ 0, 0, 0 }, radius * 1.1f, 24, 16,
                    (Color){ 255, 255, 255, (unsigned char)wireAlpha });

    /* Rings pulse radius and brightness with amplitude */
    float r1 = radius * (1.6f + 0.4f * amplitude);
    float r2 = radius * (2.0f + 0.4f * amplitude);
    float r3 = radius * (1.3f + 0.3f * amplitude);
    int rAlpha = 60 + (int)(amplitude * 60.0f);

    DrawCircle3D((Vector3){ 0, 0, 0 }, r1, (Vector3){ 0, 1, 0 }, 0.0f,
                 (Color){ col.r, col.g, col.b, (unsigned char)rAlpha });
    DrawCircle3D((Vector3){ 0, 0, 0 }, r2, (Vector3){ 1, 0, 0 },
                 s_orbTime * 30.0f, (Color){ col.r, col.g, col.b, (unsigned char)(rAlpha * 0.7f) });
    DrawCircle3D((Vector3){ 0, 0, 0 }, r3, (Vector3){ 1, 1, 0 },
                 -s_orbTime * 40.0f, (Color){ 255, 255, 255, (unsigned char)(rAlpha * 0.5f) });

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
    /* Task strip */
    if (s_task_strip[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled(" | ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.83f, 0.71f, 1.0f), "%s", s_task_strip);
    }
    ImGui::End();
}

void ui_render_set_task_strip(const char *tasks) {
    strncpy(s_task_strip, tasks ? tasks : "", sizeof(s_task_strip) - 1);
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
    float chatH = 240;
    ImGui::SetNextWindowPos(ImVec2(200, (float)s_height - chatH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)s_width - 400, chatH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::Begin("##chat", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    if (ImGui::SmallButton("Clear")) { s_chatCount = 0; s_chatHead = 0; }
    ImGui::SameLine();
    s_chatFilter.Draw("Filter", 180);

    ImGui::Separator();
    ImGui::BeginChild("##chatscroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4),
                      false, ImGuiWindowFlags_NoNavFocus);
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

    /* Text input bar */
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::InputText("##input", s_input_buf, sizeof(s_input_buf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        s_input_pending = true;
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void ui_render_get_text_input(char *out, int out_len) {
    if (out && out_len > 0) out[0] = '\0';
    if (s_input_pending && s_input_buf[0]) {
        strncpy(out, s_input_buf, (size_t)out_len - 1);
        out[out_len - 1] = '\0';
        s_input_buf[0] = '\0';
        s_input_pending = false;
    }
}

bool ui_render_has_text_input(void) {
    return s_input_pending && s_input_buf[0];
}

void ui_render_init(int width, int height, const char *title) {
    s_width = width;
    s_height = height;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);
    SetTargetFPS(WINALP_TARGET_FPS);

    /* Window icon */
    Image iconImg = LoadImage("assets/WinAlp.png");
    if (iconImg.data != NULL) {
        SetWindowIcon(iconImg);
        UnloadImage(iconImg);
    }

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

    waveform_push(amplitude);

    ImGui_ImplRaylib_NewFrame();

    BeginDrawing();
    ClearBackground((Color){ 8, 12, 20, 255 });

    ui_draw_orb(state, amplitude);
    ui_draw_waveform();
    ui_draw_top_strip();
    ui_draw_left_panel();
    ui_draw_right_panel();
    ui_draw_bottom_chat();
    ui_draw_overlay();

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
