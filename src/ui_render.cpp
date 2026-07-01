#include "../include/ui_render.h"

extern "C" {
#include "../include/ai_engine.h"
#include "../include/audio_capture.h"
#include "../include/sys_monitor.h"
}

#include "../include/logger.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include "raylib.h"
#include "imgui.h"
#include "imgui_impl_raylib.h"

#define MAX_CHAT_LINES 256
#define MAX_CHAT_LINE_LEN 512

static int s_width = 1280;
static int s_height = 720;
static bool s_shouldClose = false;

static char s_contextLabel[256] = "";
static char s_profileLabel[512] = "";
static char s_task_strip[1024] = "";
static char s_input_buf[512] = "";
static bool s_input_pending;

static char s_chatBuffer[MAX_CHAT_LINES][MAX_CHAT_LINE_LEN];
static int s_chatCount = 0;
static int s_chatHead = 0;
static bool s_chatScrolled = false;
static ImGuiTextFilter s_chatFilter;

static bool s_overlay_active;
static char s_overlay_title[128];
static char s_overlay_msg[512];
static bool s_overlay_result;

static int s_wavePos;
#define WAVE_LEN 256
static float s_waveform[WAVE_LEN];

static SysMonitorData s_sys;

static int s_rotary_hover = -1;
static int s_rotary_selected = 0;
static const char *s_rotary_labels[6] = {"APPS", "DOCS", "CTRL", "SYS", "GAME", "DISK"};

static float s_time;
static AgentState s_lastState = AGENT_STATE_LISTENING;
static float s_colorLerp[4] = {0, 212, 255, 255};
static float s_colorTarget[4] = {0, 212, 255, 255};

static void state_color_rgba(AgentState state, float out[4]) {
    switch (state) {
        case AGENT_STATE_LISTENING: out[0]=0;  out[1]=212; out[2]=255; out[3]=255; break;
        case AGENT_STATE_THINKING:  out[0]=123; out[1]=47;  out[2]=190; out[3]=255; break;
        case AGENT_STATE_WRITING:   out[0]=0;   out[1]=200; out[2]=180; out[3]=255; break;
        case AGENT_STATE_ACTING:    out[0]=245; out[1]=158; out[2]=11;  out[3]=255; break;
    }
}

static void chat_push(const char *msg) {
    strncpy(s_chatBuffer[s_chatHead], msg, MAX_CHAT_LINE_LEN - 1);
    s_chatBuffer[s_chatHead][MAX_CHAT_LINE_LEN - 1] = '\0';
    s_chatHead = (s_chatHead + 1) % MAX_CHAT_LINES;
    if (s_chatCount < MAX_CHAT_LINES) s_chatCount++;
    s_chatScrolled = true;
}

static void waveform_push(float amplitude) {
    s_waveform[s_wavePos] = amplitude;
    s_wavePos = (s_wavePos + 1) % WAVE_LEN;
}

static Color color_from_state(float lerp[4]) {
    Color c;
    c.r = (unsigned char)lerp[0];
    c.g = (unsigned char)lerp[1];
    c.b = (unsigned char)lerp[2];
    c.a = (unsigned char)lerp[3];
    return c;
}

static Color alpha(Color c, int a) {
    c.a = (unsigned char)a;
    return c;
}

static void draw_grid(void) {
    Color grid_col = (Color){0, 212, 255, 6};
    for (int x = 0; x < s_width; x += 40)
        DrawLine(x, 0, x, s_height, grid_col);
    for (int y = 0; y < s_height; y += 40)
        DrawLine(0, y, s_width, y, grid_col);
}

/* ── Arc Reactor + Rotary Menu + Outer Indicators ── */
static void draw_arc_reactor(AgentState state, float amplitude) {
    int cx = s_width / 2;
    int cy = 190;
    float dt = GetFrameTime();
    s_time += dt;

    if (state != s_lastState) {
        state_color_rgba(state, s_colorTarget);
        s_lastState = state;
    }
    float ease = 1.0f - powf(0.01f, dt * 10.0f);
    for (int i = 0; i < 4; i++)
        s_colorLerp[i] += (s_colorTarget[i] - s_colorLerp[i]) * ease;

    Color ac_col = color_from_state(s_colorLerp);

    /* Outer glow */
    DrawCircleGradient(cx, cy, 180, alpha(ac_col, 8), (Color){0,0,0,0});

    /* Outer indicator ring — 8 dots for FPS, tokens/s, ctx%, CPU, RAM, Disk, Mic, Battery */
    {
        float vals[8];
        vals[0] = GetFPS() / 60.0f;
        vals[1] = ai_engine_tokens_per_sec() / 50.0f;
        vals[2] = ai_engine_context_usage();
        vals[3] = s_sys.cpu_percent / 100.0f;
        vals[4] = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
        vals[5] = s_sys.disk_total_mb ? 1.0f - (float)s_sys.disk_free_mb / (float)s_sys.disk_total_mb : 0;
        vals[6] = amplitude;
        vals[7] = s_sys.battery_percent >= 0 ? s_sys.battery_percent / 100.0f : 0.5f;

        const char *labels[8] = {"FPS", "T/S", "CTX", "CPU", "RAM", "DSK", "MIC", "BAT"};
        for (int i = 0; i < 8; i++) {
            float angle = (float)i * 3.14159f / 4.0f + s_time * 0.01f;
            float dx = cosf(angle) * 175.0f;
            float dy = sinf(angle) * 175.0f;
            float v = vals[i];
            Color dot_col;
            if (v > 0.7f) dot_col = (Color){0, 220, 80, 200};
            else if (v > 0.3f) dot_col = (Color){240, 200, 0, 200};
            else dot_col = (Color){220, 40, 40, 200};
            int r = 4 + (int)(v * 4);
            DrawCircle(cx + (int)dx, cy + (int)dy, (float)r, dot_col);
            DrawText(labels[i], cx + (int)dx - 10, cy + (int)dy + 8, 8, alpha(ac_col, 150));
        }
    }

    /* Rotary menu circle */
    DrawCircleLines(cx, cy, 135.0f, alpha(ac_col, 30));

    /* 6 rotary buttons */
    Vector2 mp = GetMousePosition();
    s_rotary_hover = -1;
    for (int i = 0; i < 6; i++) {
        float angle = (float)i * 3.14159f * 2.0f / 6.0f - 3.14159f / 2.0f;
        float bx = cx + cosf(angle) * 135.0f;
        float by = cy + sinf(angle) * 135.0f;
        bool hover = CheckCollisionPointCircle(mp, (Vector2){bx, by}, 18.0f);
        if (hover) s_rotary_hover = i;

        Color btn_col = (i == s_rotary_selected) ? ac_col :
                        hover ? alpha(ac_col, 200) : alpha(ac_col, 60);
        DrawCircle((int)bx, (int)by, 16.0f, hover ? alpha(ac_col, 40) : (Color){15,20,30,180});
        DrawCircleLines((int)bx, (int)by, 16.0f, btn_col);
        DrawText(s_rotary_labels[i], (int)bx - 16, (int)by - 5, 9, btn_col);
    }

    /* Inner glow ring */
    DrawCircleGradient(cx, cy, 115, alpha(ac_col, 20), (Color){0,0,0,0});

    /* Triangle core (inverted) */
    Vector2 t1 = {(float)cx, (float)(cy - 48)};
    Vector2 t2 = {(float)(cx - 44), (float)(cy + 34)};
    Vector2 t3 = {(float)(cx + 44), (float)(cy + 34)};
    DrawTriangle(t1, t2, t3, alpha(ac_col, 180));
    DrawTriangleLines(t1, t2, t3, alpha(ac_col, 220));

    /* Center glow */
    DrawCircleGradient(cx, cy, 22, (Color){255,255,255,200}, alpha(ac_col, 0));

    /* Amplitude pulse ring around triangle */
    float pulse_r = 60.0f + amplitude * 20.0f;
    DrawCircleLines(cx, cy, pulse_r, alpha(ac_col, 20 + (int)(amplitude * 40)));

    /* Rotary menu click */
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && s_rotary_hover >= 0)
        s_rotary_selected = s_rotary_hover;
}

/* ── Left Panel: CPU, RAM, Disk, Battery, Wave ── */
static void draw_left_panel(void) {
    int px = 0, py = 32, pw = 220;

    Color bg_col = (Color){8, 12, 20, 200};
    DrawRectangle(px, py, pw, s_height - py - 290, bg_col);
    DrawLine(px + pw, py, px + pw, s_height - 290, (Color){0, 212, 255, 30});

    int y = py + 8;
    Color txt = (Color){180, 190, 200, 255};
    Color cyan_c = (Color){0, 212, 255, 255};

    /* Waveform visual */
    DrawText("AUDIO", px + 10, y, 10, cyan_c);
    y += 14;
    for (int i = 0; i < pw - 20; i++) {
        int idx = (s_wavePos + i * WAVE_LEN / (pw - 20)) % WAVE_LEN;
        int x0 = px + 10 + i;
        int h = (int)(s_waveform[idx] * 30);
        DrawLine(x0, y + 20 - h, x0, y + 20 + h, alpha(cyan_c, 60 + (int)(s_waveform[idx] * 100)));
    }
    y += 50;

    /* CPU */
    DrawText("CPU #1", px + 10, y, 10, cyan_c);
    float cpu = s_sys.cpu_percent;
    DrawRectangle(px + 10, y + 14, pw - 20, 12, (Color){20, 30, 50, 200});
    DrawRectangle(px + 10, y + 14, (int)((pw - 20) * cpu / 100.0f), 12,
                  cpu > 80 ? (Color){220, 40, 40, 200} : (Color){0, 212, 255, 200});
    char cpu_txt[32]; snprintf(cpu_txt, sizeof(cpu_txt), "%.0f%%", cpu);
    DrawText(cpu_txt, px + pw - 50, y, 10, txt);
    y += 36;

    /* RAM */
    DrawText("RAM", px + 10, y, 10, cyan_c);
    float ram_pct = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
    DrawRectangle(px + 10, y + 14, pw - 20, 12, (Color){20, 30, 50, 200});
    DrawRectangle(px + 10, y + 14, (int)((pw - 20) * ram_pct), 12,
                  ram_pct > 0.85f ? (Color){220, 40, 40, 200} : (Color){123, 47, 190, 200});
    char ram_txt[64]; snprintf(ram_txt, sizeof(ram_txt), "%llu / %llu MB", s_sys.ram_used_mb, s_sys.ram_total_mb);
    DrawText(ram_txt, px + 10, y + 28, 9, txt);
    y += 50;

    /* Disk */
    DrawText("DRIVE #1 'C:'", px + 10, y, 10, cyan_c);
    float disk_pct = s_sys.disk_total_mb ? 1.0f - (float)s_sys.disk_free_mb / (float)s_sys.disk_total_mb : 0;
    DrawRectangle(px + 10, y + 14, pw - 20, 12, (Color){20, 30, 50, 200});
    DrawRectangle(px + 10, y + 14, (int)((pw - 20) * disk_pct), 12,
                  disk_pct > 0.9f ? (Color){220, 40, 40, 200} : (Color){0, 200, 180, 200});
    char disk_txt[64]; snprintf(disk_txt, sizeof(disk_txt), "%llu / %llu MB free",
                                s_sys.disk_free_mb, s_sys.disk_total_mb);
    DrawText(disk_txt, px + 10, y + 28, 9, txt);
    y += 50;

    /* Battery */
    DrawText("POWER", px + 10, y, 10, cyan_c);
    y += 16;
    if (s_sys.battery_percent >= 0) {
        Color bat_col = s_sys.battery_percent > 20 ? (Color){0, 212, 255, 200} : (Color){220, 40, 40, 200};
        DrawRectangle(px + 10, y, 60, 20, (Color){30, 40, 60, 200});
        DrawRectangleLines(px + 10, y, 60, 20, bat_col);
        DrawRectangle(px + 70, y + 6, 4, 8, bat_col);
        int fill_w = (int)(56 * s_sys.battery_percent / 100.0f);
        DrawRectangle(px + 12, y + 2, fill_w, 16, alpha(bat_col, 180));
        char bat_txt[16]; snprintf(bat_txt, sizeof(bat_txt), "%d%%", s_sys.battery_percent);
        DrawText(bat_txt, px + 24, y + 3, 10, WHITE);
        DrawText(s_sys.ac_power ? "AC" : "BAT", px + 80, y + 3, 9, txt);
    } else {
        DrawText("Desktop / No Battery", px + 10, y, 9, txt);
    }
    y += 30;

    /* Network */
    DrawText("NETWORK", px + 10, y, 10, cyan_c);
    DrawText(s_sys.ip_addr[0] ? s_sys.ip_addr : "No IP", px + 10, y + 14, 9, txt);
}

/* ── Right Panel: Time, Uptime, LLM, Context ── */
static void draw_right_panel(void) {
    int pw = 220;
    int px = s_width - pw, py = 32;

    Color bg_col = (Color){8, 12, 20, 200};
    DrawRectangle(px, py, pw, s_height - py - 290, bg_col);
    DrawLine(px, py, px, s_height - 290, (Color){0, 212, 255, 30});

    int y = py + 8;
    Color txt = (Color){180, 190, 200, 255};
    Color cyan_c = (Color){0, 212, 255, 255};

    /* Digital Clock */
    DrawText(s_sys.time_str, px + 10, y, 28, cyan_c);
    y += 36;
    DrawText(s_sys.date_str, px + 10, y, 10, txt);
    y += 22;

    /* Uptime */
    DrawText("UPTIME", px + 10, y, 10, cyan_c);
    unsigned long long up = s_sys.uptime_sec;
    int d = (int)(up / 86400); up %= 86400;
    int h = (int)(up / 3600); up %= 3600;
    int m = (int)(up / 60); int sec = (int)(up % 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "%dd %02d:%02d:%02d", d, h, m, sec);
    DrawText(uptime_txt, px + 10, y + 14, 9, txt);
    y += 36;

    /* Recycle Bin */
    DrawText("RECYCLE BIN", px + 10, y, 10, cyan_c);
    char recyc_txt[32]; snprintf(recyc_txt, sizeof(recyc_txt), "%d items", s_sys.recycle_count);
    DrawText(recyc_txt, px + 10, y + 14, 9, txt);
    y += 36;

    /* LLM Status */
    DrawText("LLM ENGINE", px + 10, y, 10, cyan_c);
    y += 16;
    if (ai_engine_is_loaded()) {
        float tps = ai_engine_tokens_per_sec();
        int ctx = ai_engine_context_usage();
        char llm_txt[128];
        snprintf(llm_txt, sizeof(llm_txt), "Token/s: %.1f", tps);
        DrawText(llm_txt, px + 10, y, 9, txt);
        y += 14;
        snprintf(llm_txt, sizeof(llm_txt), "Context: %d%%", ctx);
        DrawText(llm_txt, px + 10, y, 9, ctx > 80 ? (Color){220,40,40,255} : txt);
        y += 14;
        DrawText("Status: Ready", px + 10, y, 9, (Color){0, 220, 80, 255});
    } else {
        DrawText("Status: Not loaded", px + 10, y, 9, (Color){220, 40, 40, 255});
    }
    y += 30;

    /* Active context */
    DrawText("CONTEXT", px + 10, y, 10, cyan_c);
    y += 14;
    if (s_contextLabel[0]) {
        int ctx_y = y;
        const char *p = s_contextLabel;
        char line[128];
        while (*p) {
            int i = 0;
            while (*p && i < (int)sizeof(line) - 1 && *p != '\n') line[i++] = *p++;
            line[i] = '\0';
            if (*p == '\n') p++;
            DrawText(line, px + 10, ctx_y, 9, txt);
            ctx_y += 12;
        }
    } else {
        DrawText("(no active context)", px + 10, y, 9, txt);
    }
}

/* ── Top Bar ── */
static void draw_top_bar(void) {
    Color cyan_c = (Color){0, 212, 255, 255};
    Color txt = (Color){180, 190, 200, 255};

    DrawRectangle(0, 0, s_width, 30, (Color){8, 12, 20, 230});
    DrawLine(0, 30, s_width, 30, alpha(cyan_c, 30));

    DrawText("WINALP OS  v1.0.0", 10, 6, 14, cyan_c);

    if (s_profileLabel[0]) {
        int x = s_width - 20 - (int)strlen(s_profileLabel) * 8;
        DrawText(s_profileLabel, x > 10 ? x : 10, 8, 11, txt);
    }
    DrawText("User:", s_width - 220, 8, 11, txt);
    DrawText(s_profileLabel[0] ? s_profileLabel : "Alperen Colgecen",
             s_width - 170, 8, 11, cyan_c);
}

/* ── Footer ── */
static void draw_footer(void) {
    Color cyan_c = (Color){0, 212, 255, 100};
    int fy = s_height - 30;
    DrawRectangle(0, fy, s_width, 30, (Color){8, 12, 20, 230});
    DrawLine(0, fy, s_width, fy, alpha(cyan_c, 20));

    const char *label = "WINALP  |  STARK INDUSTRIES";
    int fw = MeasureText(label, 14);
    DrawText(label, (s_width - fw) / 2, fy + 7, 14, cyan_c);
}

/* ── Chat (ImGui) ── */
static void draw_chat(void) {
    int chat_y = s_height - 290;
    int chat_h = 260;

    ImGui::SetNextWindowPos(ImVec2(0, (float)chat_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)s_width, (float)chat_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
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
        ImGui::TextDisabled("No messages yet. Type below to chat.");
    }
    ImGui::EndChild();

    ImGui::PushItemWidth(-1.0f);
    if (ImGui::InputText("##input", s_input_buf, sizeof(s_input_buf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        s_input_pending = true;
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

/* ── Overlay ── */
static void draw_overlay(void) {
    if (!s_overlay_active) return;
    DrawRectangle(0, 0, s_width, s_height, (Color){0, 0, 0, 120});

    int mw = 400, mh = 150;
    int mx = (s_width - mw) / 2, my = (s_height - mh) / 2;
    DrawRectangle(mx, my, mw, mh, (Color){15, 25, 40, 230});
    DrawRectangleLines(mx, my, mw, mh, (Color){0, 212, 255, 80});

    DrawText(s_overlay_title, mx + 20, my + 15, 18, (Color){0, 212, 255, 255});
    DrawText(s_overlay_msg, mx + 20, my + 45, 14, (Color){180, 190, 200, 255});

    Rectangle yBtn = {(float)(mx + 80), (float)(my + 100), 100, 30};
    DrawRectangleRec(yBtn, (Color){0, 180, 80, 200});
    DrawText("YES", (int)yBtn.x + 30, (int)yBtn.y + 6, 14, WHITE);

    Rectangle nBtn = {(float)(mx + 220), (float)(my + 100), 100, 30};
    DrawRectangleRec(nBtn, (Color){180, 40, 40, 200});
    DrawText("NO", (int)nBtn.x + 32, (int)nBtn.y + 6, 14, WHITE);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Vector2 mp = GetMousePosition();
        if (CheckCollisionPointRec(mp, yBtn)) { s_overlay_result = true; s_overlay_active = false; }
        if (CheckCollisionPointRec(mp, nBtn)) { s_overlay_result = false; s_overlay_active = false; }
    }
}

/* ═══════════════════════════════════════════════
   PUBLIC API
   ═══════════════════════════════════════════════ */

char *ui_render_model_select(ModelEntry *models, int n_models) {
    if (n_models <= 0) return NULL;

    InitWindow(800, 500, "WinAlp — Model Selection");
    SetTargetFPS(60);

    int selected = -1;
    int scroll = 0;
    int hovered = -1;
    Font font = GetFontDefault();

    while (!WindowShouldClose()) {
        int mw = GetScreenWidth();
        int mh = GetScreenHeight();
        Vector2 mp = GetMousePosition();
        hovered = -1;

        BeginDrawing();
        ClearBackground((Color){8, 12, 20, 255});

        DrawTextEx(font, "SELECT MODEL", (Vector2){40, 20}, 28, 1, (Color){0, 212, 255, 255});
        DrawTextEx(font, "Choose a brain model to start WinAlp",
                   (Vector2){40, 55}, 14, 1, (Color){128, 140, 160, 255});

        int x = 40, y = 90, item_h = 48;
        for (int i = 0; i < n_models; i++) {
            int cy = y + (i - scroll) * item_h;
            if (cy + item_h < 90 || cy > mh - 20) continue;

            Rectangle rect = {(float)x, (float)cy, (float)(mw - 80), (float)item_h};
            bool inside = CheckCollisionPointRec(mp, rect);
            if (inside) hovered = i;

            Color bg = (i == selected) ? (Color){0, 100, 180, 100} :
                       inside ? (Color){40, 50, 70, 100} : (Color){15, 20, 30, 80};
            DrawRectangleRec(rect, bg);

            Color compat_col;
            switch (models[i].compat) {
                case 3: compat_col = (Color){0, 200, 80, 255}; break;
                case 2: compat_col = (Color){240, 200, 0, 255}; break;
                case 1: compat_col = (Color){220, 40, 40, 255}; break;
                default: compat_col = (Color){80, 80, 80, 255}; break;
            }
            DrawCircle(x + 12, cy + item_h / 2, 6, compat_col);

            char label[512];
            const char *vl_tag = (models[i].flags & 1) ? " [VLM]" : "";
            snprintf(label, sizeof(label), "%s%s  (%llu MB, %s)",
                     models[i].label, vl_tag, models[i].size_mb, models[i].arch);
            DrawTextEx(font, label, (Vector2){(float)(x + 30), (float)(cy + 6)},
                       16, 1, (Color){220, 230, 240, 255});

            const char *tier_str = (models[i].tier == 0) ? "Hafif" :
                                   (models[i].tier == 1) ? "Orta" : "Guclu";
            const char *compat_str = (models[i].compat == 3) ? "Uyumlu" :
                                     (models[i].compat == 2) ? "Sinir" : "Yetersiz Bellek";
            char right_label[128];
            snprintf(right_label, sizeof(right_label), "%s | %s", tier_str, compat_str);
            DrawTextEx(font, right_label,
                       (Vector2){(float)(mw - 180), (float)(cy + 8)}, 12, 1, compat_col);
        }

        DrawTextEx(font, "Click a model to select  |  ESC to exit",
                   (Vector2){40, (float)(mh - 30)}, 12, 1, (Color){80, 90, 110, 200});

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered >= 0)
            selected = hovered;

        scroll -= (int)GetMouseWheelMove();
        if (scroll < 0) scroll = 0;
        if (scroll > n_models - 1) scroll = n_models - 1;
        if (scroll < 0) scroll = 0;

        EndDrawing();

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

bool ui_render_confirm_blocking(const char *title, const char *msg) {
    s_overlay_active = true;
    strncpy(s_overlay_title, title ? title : "Confirm", sizeof(s_overlay_title) - 1);
    strncpy(s_overlay_msg, msg ? msg : "", sizeof(s_overlay_msg) - 1);
    s_overlay_result = false;

    while (s_overlay_active && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){8, 12, 20, 255});
        draw_overlay();
        EndDrawing();
    }
    return s_overlay_result;
}

bool ui_render_has_text_input(void) {
    return s_input_pending && s_input_buf[0];
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

void ui_render_init(int width, int height, const char *title) {
    s_width = width;
    s_height = height;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);
    SetTargetFPS(WINALP_TARGET_FPS);

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
    sys_monitor_poll(&s_sys);

    ImGui_ImplRaylib_NewFrame();

    BeginDrawing();
    ClearBackground((Color){8, 12, 20, 255});

    draw_grid();
    draw_left_panel();
    draw_right_panel();
    draw_arc_reactor(state, amplitude);
    draw_chat();
    draw_top_bar();
    draw_footer();
    draw_overlay();

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

void ui_render_set_task_strip(const char *tasks) {
    strncpy(s_task_strip, tasks, sizeof(s_task_strip) - 1);
}

void ui_render_set_theme_float(const char *key, float value) {
    (void)key; (void)value;
}
