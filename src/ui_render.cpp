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

static int s_width = 1280;
static int s_height = 720;
static bool s_shouldClose = false;

static char s_contextLabel[256] = "";
static char s_profileLabel[512] = "";
static char s_input_buf[512] = "";
static bool s_input_pending;

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

static void draw_vignette(void) {
    Color outer = (Color){8, 12, 20, 255};
    Color inner = (Color){8, 12, 20, 0};
    int cx = s_width / 2, cy = s_height / 2;
    int radius = (s_width > s_height ? s_width : s_height) / 2;
    DrawCircleGradient(cx, cy, (float)radius, inner, outer);
}

static void draw_scanlines(void) {
    for (int y = 0; y < s_height; y += 4) {
        DrawLine(0, y, s_width, y, (Color){0, 0, 0, 20});
    }
}

static void draw_grid(void) {
    Color grid_col = (Color){0, 212, 255, 5};
    for (int x = 0; x < s_width; x += 40)
        DrawLine(x, 0, x, s_height, grid_col);
    for (int y = 0; y < s_height; y += 40)
        DrawLine(0, y, s_width, y, grid_col);
    for (int x = 0; x < s_width; x += 160) {
        DrawLine(x, 0, x, s_height, (Color){0, 212, 255, 10});
    }
    for (int y = 0; y < s_height; y += 160) {
        DrawLine(0, y, s_width, y, (Color){0, 212, 255, 10});
    }
}

static void draw_panel_bg(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, (Color){8, 12, 20, 210});
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 1, (Color){0, 212, 255, 25});
    DrawLine(x + 2, y + 2, x + 16, y + 2, (Color){0, 212, 255, 60});
    DrawLine(x + 2, y + 2, x + 2, y + 16, (Color){0, 212, 255, 60});
    DrawLine(x + w - 3, y + 2, x + w - 17, y + 2, (Color){0, 212, 255, 60});
    DrawLine(x + 2, y + h - 3, x + 2, y + h - 17, (Color){0, 212, 255, 60});
}

/* ── Octagon Core + Rotary Menu + Outer Indicators ── */
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
    DrawCircleGradient(cx, cy, 200, alpha(ac_col, 10), (Color){0,0,0,0});

    /* Outer indicator ring — 8 dots */
    {
        float vals[8];
        vals[0] = GetFPS() / 60.0f;
        vals[1] = ai_engine_tokens_per_sec() / 50.0f;
        vals[2] = ai_engine_context_usage() / 100.0f;
        vals[3] = s_sys.cpu_percent / 100.0f;
        vals[4] = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
        vals[5] = s_sys.disk_total_mb ? 1.0f - (float)s_sys.disk_free_mb / (float)s_sys.disk_total_mb : 0;
        vals[6] = amplitude;
        vals[7] = s_sys.battery_percent >= 0 ? s_sys.battery_percent / 100.0f : 0.5f;

        const char *labels[8] = {"FPS", "T/S", "CTX", "CPU", "RAM", "DSK", "MIC", "BAT"};
        for (int i = 0; i < 8; i++) {
            float angle = (float)i * 3.14159f / 4.0f + s_time * 0.015f;
            float dx = cosf(angle) * 195.0f;
            float dy = sinf(angle) * 195.0f;
            float v = vals[i];
            Color dot_col;
            if (v > 0.7f) dot_col = (Color){0, 220, 80, 220};
            else if (v > 0.3f) dot_col = (Color){240, 200, 0, 220};
            else dot_col = (Color){220, 40, 40, 220};
            int r = 3 + (int)(v * 5);
            DrawCircle(cx + (int)dx, cy + (int)dy, (float)r, dot_col);
            DrawCircleLines(cx + (int)dx, cy + (int)dy, (float)(r + 2), alpha(dot_col, 60));
            DrawText(labels[i], cx + (int)dx - 10, cy + (int)dy + 8, 8, alpha(ac_col, 150));
        }
    }

    /* Outer rotating ring */
    float ring_angle = s_time * 0.4f;
    DrawCircleLines(cx, cy, 155.0f, alpha(ac_col, 20));
    for (int i = 0; i < 12; i++) {
        float a = ring_angle + (float)i * 3.14159f * 2.0f / 12.0f;
        float dx = cosf(a) * 155.0f;
        float dy = sinf(a) * 155.0f;
        DrawCircle(cx + (int)dx, cy + (int)dy, 2.0f, alpha(ac_col, 80));
    }

    /* Rotary menu ring */
    DrawCircleLines(cx, cy, 135.0f, alpha(ac_col, 35));

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

    /* Octagon core */
    Vector2 oct[8];
    float oct_r = 52.0f + amplitude * 4.0f;
    for (int i = 0; i < 8; i++) {
        float angle = (float)i * 3.14159f * 2.0f / 8.0f - 3.14159f / 2.0f;
        oct[i].x = cx + cosf(angle) * oct_r;
        oct[i].y = cy + sinf(angle) * oct_r;
    }
    DrawTriangleFan(oct, 8, alpha(ac_col, 45));
    DrawPoly((Vector2){(float)cx, (float)cy}, 8, oct_r - 4, -90.0f, alpha(ac_col, 90));
    DrawPolyLines((Vector2){(float)cx, (float)cy}, 8, oct_r - 4, -90.0f, alpha(ac_col, 200));

    /* Inner octagon glow */
    DrawCircleGradient(cx, cy, oct_r * 0.6f, (Color){255,255,255,180}, alpha(ac_col, 0));

    /* Inner ring */
    DrawCircleLines(cx, cy, oct_r * 0.4f, alpha(ac_col, 80));

    /* Center dot */
    DrawCircle(cx, cy, 6.0f, (Color){255,255,255,220});
    DrawCircle(cx, cy, 10.0f, alpha(ac_col, 40));

    /* Pulse ring around octagon */
    float pulse_r = oct_r + 20.0f + amplitude * 15.0f;
    DrawCircleLines(cx, cy, pulse_r, alpha(ac_col, 15 + (int)(amplitude * 30)));

    if (s_rotary_hover >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        s_rotary_selected = s_rotary_hover;
}

/* ── Left Panel ── */
static void draw_left_panel(void) {
    int px = 10, py = 38, pw = 200, ph = s_height - py - 10;
    draw_panel_bg(px, py, pw, ph);

    int y = py + 14;
    Color txt = (Color){180, 190, 200, 255};
    Color cyan_c = (Color){0, 212, 255, 255};

    DrawText("AUDIO", px + 12, y, 10, cyan_c);
    y += 16;
    for (int i = 0; i < pw - 36; i++) {
        int idx = (s_wavePos + i * WAVE_LEN / (pw - 36)) % WAVE_LEN;
        int x0 = px + 14 + i;
        int h = (int)(s_waveform[idx] * 28);
        DrawLine(x0, y + 18 - h, x0, y + 18 + h, alpha(cyan_c, 50 + (int)(s_waveform[idx] * 80)));
    }
    y += 52;

    DrawText("CPU #1", px + 12, y, 10, cyan_c);
    float cpu = s_sys.cpu_percent;
    DrawRectangle(px + 12, y + 14, pw - 24, 10, (Color){20, 30, 50, 200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * cpu / 100.0f), 10,
                  cpu > 80 ? (Color){220, 40, 40, 220} : (Color){0, 212, 255, 220});
    char cpu_txt[32]; snprintf(cpu_txt, sizeof(cpu_txt), "%.0f%%", cpu);
    DrawText(cpu_txt, px + pw - 40, y, 10, txt);
    y += 34;

    DrawText("RAM", px + 12, y, 10, cyan_c);
    float ram_pct = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
    DrawRectangle(px + 12, y + 14, pw - 24, 10, (Color){20, 30, 50, 200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * ram_pct), 10,
                  ram_pct > 0.85f ? (Color){220, 40, 40, 220} : (Color){123, 47, 190, 220});
    char ram_txt[64]; snprintf(ram_txt, sizeof(ram_txt), "%llu / %llu", s_sys.ram_used_mb, s_sys.ram_total_mb);
    DrawText(ram_txt, px + 12, y + 28, 9, txt);
    y += 48;

    DrawText("DRIVE #1", px + 12, y, 10, cyan_c);
    float disk_pct = s_sys.disk_total_mb ? 1.0f - (float)s_sys.disk_free_mb / (float)s_sys.disk_total_mb : 0;
    DrawRectangle(px + 12, y + 14, pw - 24, 10, (Color){20, 30, 50, 200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * disk_pct), 10,
                  disk_pct > 0.9f ? (Color){220, 40, 40, 220} : (Color){0, 200, 180, 220});
    char disk_txt[64]; snprintf(disk_txt, sizeof(disk_txt), "%llu MB free", s_sys.disk_free_mb);
    DrawText(disk_txt, px + 12, y + 28, 9, txt);
    y += 48;

    DrawText("POWER", px + 12, y, 10, cyan_c);
    y += 16;
    if (s_sys.battery_percent >= 0) {
        Color bat_col = s_sys.battery_percent > 20 ? (Color){0, 212, 255, 220} : (Color){220, 40, 40, 220};
        DrawRectangle(px + 12, y, 56, 18, (Color){30, 40, 60, 200});
        DrawRectangleLines(px + 12, y, 56, 18, bat_col);
        DrawRectangle(px + 68, y + 5, 4, 8, bat_col);
        int fill_w = (int)(52 * s_sys.battery_percent / 100.0f);
        DrawRectangle(px + 14, y + 2, fill_w, 14, alpha(bat_col, 160));
        char bat_txt[16]; snprintf(bat_txt, sizeof(bat_txt), "%d%%", s_sys.battery_percent);
        DrawText(bat_txt, px + 22, y + 2, 10, WHITE);
        DrawText(s_sys.ac_power ? "AC" : "BAT", px + 80, y + 2, 9, txt);
    } else {
        DrawText("Desktop", px + 12, y, 9, txt);
    }
    y += 30;

    DrawText("NETWORK", px + 12, y, 10, cyan_c);
    DrawText(s_sys.ip_addr[0] ? s_sys.ip_addr : "No IP", px + 12, y + 14, 9, txt);
}

/* ── Right Panel ── */
static void draw_right_panel(void) {
    int pw = 200;
    int px = s_width - pw - 10, py = 38, ph = s_height - py - 10;
    draw_panel_bg(px, py, pw, ph);

    int y = py + 14;
    Color txt = (Color){180, 190, 200, 255};
    Color cyan_c = (Color){0, 212, 255, 255};

    DrawText(s_sys.time_str, px + 12, y, 28, cyan_c);
    y += 36;
    DrawText(s_sys.date_str, px + 12, y, 10, txt);
    y += 22;

    DrawText("UPTIME", px + 12, y, 10, cyan_c);
    unsigned long long up = s_sys.uptime_sec;
    int d = (int)(up / 86400); up %= 86400;
    int h = (int)(up / 3600); up %= 3600;
    int m = (int)(up / 60); int sec = (int)(up % 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "%dd %02d:%02d:%02d", d, h, m, sec);
    DrawText(uptime_txt, px + 12, y + 14, 9, txt);
    y += 36;

    DrawText("RECYCLE", px + 12, y, 10, cyan_c);
    char recyc_txt[32]; snprintf(recyc_txt, sizeof(recyc_txt), "%d items", s_sys.recycle_count);
    DrawText(recyc_txt, px + 12, y + 14, 9, txt);
    y += 36;

    DrawText("LLM ENGINE", px + 12, y, 10, cyan_c);
    y += 16;
    if (ai_engine_is_loaded()) {
        float tps = ai_engine_tokens_per_sec();
        int ctx = ai_engine_context_usage();
        char llm_txt[128];
        snprintf(llm_txt, sizeof(llm_txt), "Token/s: %.1f", tps);
        DrawText(llm_txt, px + 12, y, 9, txt);
        y += 14;
        snprintf(llm_txt, sizeof(llm_txt), "Context: %d%%", ctx);
        DrawText(llm_txt, px + 12, y, 9, ctx > 80 ? (Color){220,40,40,255} : txt);
        y += 14;
        DrawText("Ready", px + 12, y, 9, (Color){0, 220, 80, 255});
    } else {
        DrawText("Not loaded", px + 12, y, 9, (Color){220, 40, 40, 255});
    }
    y += 30;

    DrawText("CONTEXT", px + 12, y, 10, cyan_c);
    y += 14;
    if (s_contextLabel[0]) {
        const char *p = s_contextLabel;
        char line[128];
        while (*p) {
            int i = 0;
            while (*p && i < (int)sizeof(line) - 1 && *p != '\n') line[i++] = *p++;
            line[i] = '\0';
            if (*p == '\n') p++;
            DrawText(line, px + 12, y, 9, txt);
            y += 12;
            if (y > py + ph - 20) break;
        }
    } else {
        DrawText("(none)", px + 12, y, 9, txt);
    }
}

/* ── Top Bar ── */
static void draw_top_bar(void) {
    Color cyan_c = (Color){0, 212, 255, 255};
    DrawRectangle(0, 0, s_width, 32, (Color){8, 12, 20, 235});
    DrawLine(0, 32, s_width, 32, (Color){0, 212, 255, 25});
    DrawLine(0, 33, s_width, 33, (Color){0, 212, 255, 8});

    DrawText("WINALP OS  v1.0.0", 14, 8, 14, cyan_c);
    DrawText(s_profileLabel[0] ? s_profileLabel : "Alperen Colgecen",
             s_width - 170, 9, 11, cyan_c);
    DrawText("User:", s_width - 220, 9, 11, (Color){180, 190, 200, 255});
}

/* ── Footer ── */
static void draw_footer(void) {
    Color cyan_c = (Color){0, 212, 255, 90};
    int fy = s_height - 28;
    DrawRectangle(0, fy, s_width, 28, (Color){8, 12, 20, 230});
    DrawLine(0, fy, s_width, fy, (Color){0, 212, 255, 15});

    const char *label = "WINALP  |  COLGECEN TECHNOLOGIES";
    int fw = MeasureText(label, 12);
    DrawText(label, (s_width - fw) / 2, fy + 7, 12, cyan_c);

    DrawText("v1.0.0", 14, fy + 7, 10, (Color){0, 212, 255, 40});
}

/* ── Quick Status (minimal center-bottom overlay) ── */
static void draw_quick_status(void) {
    Color txt = (Color){150, 160, 170, 180};
    char buf[128];
    snprintf(buf, sizeof(buf), "FPS: %d  |  CPU: %.0f%%  |  RAM: %llu MB",
             GetFPS(), s_sys.cpu_percent, s_sys.ram_used_mb);
    int fw = MeasureText(buf, 10);
    DrawText(buf, (s_width - fw) / 2, s_height - 48, 10, txt);
}

/* ── Overlay ── */
static void draw_overlay(void) {
    if (!s_overlay_active) return;
    DrawRectangle(0, 0, s_width, s_height, (Color){0, 0, 0, 140});

    int mw = 400, mh = 150;
    int mx = (s_width - mw) / 2, my = (s_height - mh) / 2;
    DrawRectangle(mx, my, mw, mh, (Color){10, 20, 35, 240});
    DrawRectangleLines(mx, my, mw, mh, (Color){0, 212, 255, 60});
    DrawLine(mx + 2, my + 2, mx + 16, my + 2, (Color){0, 212, 255, 100});
    DrawLine(mx + 2, my + 2, mx + 2, my + 16, (Color){0, 212, 255, 100});

    DrawText(s_overlay_title, mx + 20, my + 15, 18, (Color){0, 212, 255, 255});
    DrawText(s_overlay_msg, mx + 20, my + 45, 14, (Color){180, 190, 200, 255});

    Rectangle yBtn = {(float)(mx + 80), (float)(my + 100), 100, 30};
    DrawRectangleRounded(yBtn, 0.2f, 4, (Color){0, 180, 80, 220});
    DrawText("YES", (int)yBtn.x + 30, (int)yBtn.y + 6, 14, WHITE);

    Rectangle nBtn = {(float)(mx + 220), (float)(my + 100), 100, 30};
    DrawRectangleRounded(nBtn, 0.2f, 4, (Color){180, 40, 40, 220});
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
        draw_vignette();
        draw_grid();
        draw_scanlines();
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

    BeginDrawing();
    ClearBackground((Color){8, 12, 20, 255});

    draw_vignette();
    draw_grid();
    draw_scanlines();
    draw_left_panel();
    draw_right_panel();
    draw_arc_reactor(state, amplitude);
    draw_quick_status();
    draw_top_bar();
    draw_footer();
    draw_overlay();

    EndDrawing();
}

bool ui_render_should_close(void) {
    return s_shouldClose;
}

void ui_render_shutdown(void) {
    CloseWindow();
}

void ui_render_push_chat(const char *role, const char *text, const char *source_icon) {
    (void)role;
    (void)text;
    (void)source_icon;
}

void ui_render_set_context_label(const char *label) {
    strncpy(s_contextLabel, label, sizeof(s_contextLabel) - 1);
}

void ui_render_set_profile_label(const char *label) {
    strncpy(s_profileLabel, label, sizeof(s_profileLabel) - 1);
}

void ui_render_set_task_strip(const char *tasks) {
    (void)tasks;
}

void ui_render_set_theme_float(const char *key, float value) {
    (void)key; (void)value;
}
