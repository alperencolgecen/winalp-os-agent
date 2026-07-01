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
#include <cstdlib>
#include "raylib.h"

static int s_width = 1280;
static int s_height = 720;
static bool s_shouldClose = false;

static char s_contextLabel[256] = "";
static char s_profileLabel[512] = "";
static char s_input_buf[512] = "";
static bool s_input_pending;
static char s_windows_user[64] = "";

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
static Font s_font;
static bool s_font_loaded = false;

/* Turkish character codepoints (UTF-16) */
#define FONT_CP_CNT (256 + 12)
static int s_font_cps[FONT_CP_CNT];
static bool s_font_cps_built = false;

static void build_font_cps(void) {
    if (s_font_cps_built) return;
    int idx = 0;
    for (int i = 32; i < 32 + 256; i++) s_font_cps[idx++] = i;
    s_font_cps[idx++] = 0x00C7; s_font_cps[idx++] = 0x00E7;
    s_font_cps[idx++] = 0x011E; s_font_cps[idx++] = 0x011F;
    s_font_cps[idx++] = 0x0130; s_font_cps[idx++] = 0x0131;
    s_font_cps[idx++] = 0x00D6; s_font_cps[idx++] = 0x00F6;
    s_font_cps[idx++] = 0x015E; s_font_cps[idx++] = 0x015F;
    s_font_cps[idx++] = 0x00DC; s_font_cps[idx++] = 0x00FC;
    s_font_cps_built = true;
}

static void load_ui_font(void) {
    build_font_cps();
    const char *paths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
    };
    for (int i = 0; i < 3; i++) {
        Font f = LoadFontEx(paths[i], 14, s_font_cps, FONT_CP_CNT);
        if (f.glyphCount > 0) { s_font = f; s_font_loaded = true; return; }
    }
    s_font = GetFontDefault();
    s_font_loaded = false;
}

static void state_color_rgba(AgentState state, float out[4]) {
    switch (state) {
        case AGENT_STATE_LISTENING: out[0]=0;  out[1]=212; out[2]=255; out[3]=255; break;
        case AGENT_STATE_THINKING:  out[0]=123; out[1]=47;  out[2]=190; out[3]=255; break;
        case AGENT_STATE_WRITING:   out[0]=0;   out[1]=200; out[2]=180; out[3]=255; break;
        case AGENT_STATE_ACTING:    out[0]=245; out[1]=158; out[2]=11;  out[3]=255; break;
    }
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

static void waveform_push(float amplitude) {
    s_waveform[s_wavePos] = amplitude;
    s_wavePos = (s_wavePos + 1) % WAVE_LEN;
}

static void draw_vignette(void) {
    int cx = s_width / 2, cy = s_height / 2;
    int radius = (s_width > s_height ? s_width : s_height);
    DrawCircleGradient(cx, cy, (float)radius, (Color){8,12,20,0}, (Color){8,12,20,220});
}

static void draw_scanlines(void) {
    for (int y = 0; y < s_height; y += 4) {
        DrawLine(0, y, s_width, y, (Color){0,0,0,18});
    }
}

static void draw_grid(void) {
    for (int x = 0; x < s_width; x += 40) {
        Color c = (x % 160 == 0) ? (Color){0,212,255,8} : (Color){0,212,255,4};
        DrawLine(x, 0, x, s_height, c);
    }
    for (int y = 0; y < s_height; y += 40) {
        Color c = (y % 160 == 0) ? (Color){0,212,255,8} : (Color){0,212,255,4};
        DrawLine(0, y, s_width, y, c);
    }
}

static void draw_panel_bg(int x, int y, int w, int h, Color accent) {
    DrawRectangle(x, y, w, h, (Color){6,10,18,220});
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 1, (Color){0,212,255,18});
    DrawLine(x + 2, y + 2, x + 20, y + 2, accent);
    DrawLine(x + 2, y + 2, x + 2, y + 20, accent);
    DrawLine(x + w - 3, y + 2, x + w - 21, y + 2, accent);
    DrawLine(x + 2, y + h - 3, x + 2, y + h - 21, accent);
    DrawRectangle(x, y, w, 1, (Color){0,212,255,30});
}

/* ── Jarvis Arc Reactor ── */
static void draw_arc_reactor(AgentState state, float amplitude) {
    int cx = s_width / 2;
    int cy = 175;
    float dt = GetFrameTime();
    s_time += dt;

    if (state != s_lastState) {
        state_color_rgba(state, s_colorTarget);
        s_lastState = state;
    }
    float ease = 1.0f - powf(0.01f, dt * 8.0f);
    for (int i = 0; i < 4; i++)
        s_colorLerp[i] += (s_colorTarget[i] - s_colorLerp[i]) * ease;

    Color col = color_from_state(s_colorLerp);

    /* Outer glow sphere */
    DrawCircleGradient(cx, cy, 220, alpha(col, 6), (Color){0,0,0,0});

    /* Concentric energy rings (arc reactor style) */
    float ring_radii[5] = {195, 165, 80, 55, 30};
    for (int ri = 0; ri < 5; ri++) {
        float r = ring_radii[ri];
        float speed = 0.3f + ri * 0.1f;
        float rot = s_time * speed + ri * 0.8f;
        DrawCircleLines(cx, cy, r, alpha(col, 12 + ri * 4));
        int segments = 24 - ri * 2;
        for (int si = 0; si < segments; si++) {
            float a = rot + (float)si * 3.14159f * 2.0f / (float)segments;
            float dx = cosf(a) * r;
            float dy = sinf(a) * r;
            float brightness = (ri == 2) ? 120 : 40 + ri * 12;
            DrawCircle(cx + (int)dx, cy + (int)dy, 1.5f, alpha(col, (int)brightness));
        }
    }

    /* Energy arcs connecting rings */
    for (int ai = 0; ai < 4; ai++) {
        float a = s_time * 0.5f + (float)ai * 3.14159f / 2.0f;
        float r1 = 80.0f, r2 = 165.0f;
        float x1 = cx + cosf(a) * r1, y1 = cy + sinf(a) * r1;
        float x2 = cx + cosf(a) * r2, y2 = cy + sinf(a) * r2;
        DrawLineEx((Vector2){x1,y1}, (Vector2){x2,y2}, 1.0f, alpha(col, 25 + (int)(sinf(a + s_time) * 15)));
    }

    /* 8 outer indicator dots */
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
            float a = (float)i * 3.14159f / 4.0f + s_time * 0.02f;
            float dx = cosf(a) * 210.0f;
            float dy = sinf(a) * 210.0f;
            float v = vals[i];
            Color dc;
            if (v > 0.7f) dc = (Color){0,220,80,220};
            else if (v > 0.3f) dc = (Color){240,200,0,220};
            else dc = (Color){220,40,40,220};
            int r = 4 + (int)(v * 6);
            DrawCircle(cx + (int)dx, cy + (int)dy, (float)r, dc);
            DrawCircleLines(cx + (int)dx, cy + (int)dy, (float)(r + 2), alpha(dc, 50));
            DrawTextEx(s_font, labels[i], (Vector2){(float)(cx + (int)dx - 8), (float)(cy + (int)dy + 10)}, 8, 1, alpha(col, 140));
        }
    }

    /* Rotary menu ring */
    DrawCircleLines(cx, cy, 140.0f, alpha(col, 25));

    /* 6 rotary buttons */
    Vector2 mp = GetMousePosition();
    s_rotary_hover = -1;
    for (int i = 0; i < 6; i++) {
        float a = (float)i * 3.14159f * 2.0f / 6.0f - 3.14159f / 2.0f;
        float bx = cx + cosf(a) * 140.0f;
        float by = cy + sinf(a) * 140.0f;
        bool hover = CheckCollisionPointCircle(mp, (Vector2){bx, by}, 20.0f);
        if (hover) s_rotary_hover = i;

        Color bc = (i == s_rotary_selected) ? col : hover ? alpha(col, 180) : alpha(col, 50);
        DrawCircle((int)bx, (int)by, hover ? 18.0f : 15.0f, hover ? alpha(col, 35) : (Color){10,16,26,180});
        DrawCircleLines((int)bx, (int)by, hover ? 18.0f : 15.0f, bc);
        DrawTextEx(s_font, s_rotary_labels[i], (Vector2){(float)((int)bx - 16), (float)((int)by - 6)}, 9, 1, bc);
    }

    /* ── Core Octagon (Arc Reactor) ── */
    float oct_r = 55.0f + amplitude * 3.0f;

    /* Outer glow circle */
    DrawCircleGradient(cx, cy, oct_r + 20, alpha(col, 30), (Color){0,0,0,0});

    /* Octagon */
    DrawPoly((Vector2){(float)cx, (float)cy}, 8, oct_r, -90.0f, alpha(col, 70));
    DrawPolyLines((Vector2){(float)cx, (float)cy}, 8, oct_r, -90.0f, alpha(col, 200));

    /* Inner octagon (rotated 22.5 deg) */
    DrawPolyLines((Vector2){(float)cx, (float)cy}, 8, oct_r * 0.7f, -90.0f + 22.5f, alpha(col, 100));

    /* Innermost octagon */
    DrawPolyLines((Vector2){(float)cx, (float)cy}, 8, oct_r * 0.45f, -90.0f, alpha(col, 140));

    /* White-hot center */
    DrawCircleGradient(cx, cy, oct_r * 0.3f, (Color){255,255,255,220}, alpha(col, 30));
    DrawCircle(cx, cy, 5.0f, (Color){255,255,255,240});

    /* Cross-hair lines through center */
    DrawLine(cx - (int)(oct_r * 0.6f), cy, cx + (int)(oct_r * 0.6f), cy, alpha(col, 60));
    DrawLine(cx, cy - (int)(oct_r * 0.6f), cx, cy + (int)(oct_r * 0.6f), alpha(col, 60));

    /* Pulse ring */
    float pulse_r = oct_r + 25.0f + amplitude * 12.0f;
    DrawCircleLines(cx, cy, pulse_r, alpha(col, 12 + (int)(amplitude * 25)));

    if (s_rotary_hover >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        s_rotary_selected = s_rotary_hover;
}

/* ── Left Panel ── */
static void draw_left_panel(void) {
    int px = 8, py = 38, pw = 200, ph = s_height - py - 8;
    draw_panel_bg(px, py, pw, ph, (Color){0,212,255,60});

    int y = py + 14;
    Color txt = (Color){160,175,190,255};
    Color cyan_c = (Color){0,212,255,255};

    DrawTextEx(s_font, "AUDIO", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    y += 18;
    for (int i = 0; i < pw - 36; i++) {
        int idx = (s_wavePos + i * WAVE_LEN / (pw - 36)) % WAVE_LEN;
        int x0 = px + 14 + i;
        int h = (int)(s_waveform[idx] * 28);
        DrawLine(x0, y + 18 - h, x0, y + 18 + h, alpha(cyan_c, 40 + (int)(s_waveform[idx] * 80)));
    }
    y += 54;

    DrawTextEx(s_font, "CPU", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    float cpu = s_sys.cpu_percent;
    DrawRectangle(px + 12, y + 14, pw - 24, 8, (Color){20,30,50,200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * cpu / 100.0f), 8,
                  cpu > 80 ? (Color){220,40,40,220} : (Color){0,212,255,220});
    char cpu_txt[32]; snprintf(cpu_txt, sizeof(cpu_txt), "%.0f%%", cpu);
    DrawTextEx(s_font, cpu_txt, (Vector2){(float)(px + pw - 36), (float)y}, 10, 1, txt);
    y += 32;

    DrawTextEx(s_font, "RAM", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    float ram_pct = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
    DrawRectangle(px + 12, y + 14, pw - 24, 8, (Color){20,30,50,200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * ram_pct), 8,
                  ram_pct > 0.85f ? (Color){220,40,40,220} : (Color){123,47,190,220});
    char ram_txt[64]; snprintf(ram_txt, sizeof(ram_txt), "%llu / %llu MB", s_sys.ram_used_mb, s_sys.ram_total_mb);
    DrawTextEx(s_font, ram_txt, (Vector2){(float)(px + 12), (float)(y + 26)}, 8, 1, txt);
    y += 46;

    DrawTextEx(s_font, "DRIVE", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    float disk_pct = s_sys.disk_total_mb ? 1.0f - (float)s_sys.disk_free_mb / (float)s_sys.disk_total_mb : 0;
    DrawRectangle(px + 12, y + 14, pw - 24, 8, (Color){20,30,50,200});
    DrawRectangle(px + 12, y + 14, (int)((pw - 24) * disk_pct), 8,
                  disk_pct > 0.9f ? (Color){220,40,40,220} : (Color){0,200,180,220});
    char disk_txt[64]; snprintf(disk_txt, sizeof(disk_txt), "%llu MB free", s_sys.disk_free_mb);
    DrawTextEx(s_font, disk_txt, (Vector2){(float)(px + 12), (float)(y + 26)}, 8, 1, txt);
    y += 46;

    DrawTextEx(s_font, "POWER", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    y += 16;
    if (s_sys.battery_percent >= 0) {
        Color bc = s_sys.battery_percent > 20 ? (Color){0,212,255,220} : (Color){220,40,40,220};
        DrawRectangle(px + 12, y, 56, 18, (Color){30,40,60,200});
        DrawRectangleLines(px + 12, y, 56, 18, bc);
        DrawRectangle(px + 68, y + 5, 4, 8, bc);
        int fw = (int)(52 * s_sys.battery_percent / 100.0f);
        DrawRectangle(px + 14, y + 2, fw, 14, alpha(bc, 160));
        char bt[16]; snprintf(bt, sizeof(bt), "%d%%", s_sys.battery_percent);
        DrawTextEx(s_font, bt, (Vector2){(float)(px + 22), (float)(y + 2)}, 10, 1, WHITE);
        DrawTextEx(s_font, s_sys.ac_power ? "AC" : "BAT", (Vector2){(float)(px + 80), (float)(y + 2)}, 9, 1, txt);
    } else {
        DrawTextEx(s_font, "Desktop", (Vector2){(float)(px + 12), (float)y}, 9, 1, txt);
    }
    y += 30;

    DrawTextEx(s_font, "NETWORK", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    DrawTextEx(s_font, s_sys.ip_addr[0] ? s_sys.ip_addr : "No IP", (Vector2){(float)(px + 12), (float)(y + 14)}, 9, 1, txt);
}

/* ── Right Panel ── */
static void draw_right_panel(void) {
    int pw = 200;
    int px = s_width - pw - 8, py = 38, ph = s_height - py - 8;
    draw_panel_bg(px, py, pw, ph, (Color){0,212,255,60});

    int y = py + 14;
    Color txt = (Color){160,175,190,255};
    Color cyan_c = (Color){0,212,255,255};

    DrawTextEx(s_font, s_sys.time_str, (Vector2){(float)(px + 12), (float)y}, 30, 1, cyan_c);
    y += 38;
    DrawTextEx(s_font, s_sys.date_str, (Vector2){(float)(px + 12), (float)y}, 10, 1, txt);
    y += 22;

    DrawTextEx(s_font, "UPTIME", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    unsigned long long up = s_sys.uptime_sec;
    int d = (int)(up / 86400); up %= 86400;
    int h = (int)(up / 3600); up %= 3600;
    int m = (int)(up / 60); int s = (int)(up % 60);
    char ut[64]; snprintf(ut, sizeof(ut), "%dd %02d:%02d:%02d", d, h, m, s);
    DrawTextEx(s_font, ut, (Vector2){(float)(px + 12), (float)(y + 14)}, 9, 1, txt);
    y += 36;

    DrawTextEx(s_font, "RECYCLE", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    char rc[32]; snprintf(rc, sizeof(rc), "%d items", s_sys.recycle_count);
    DrawTextEx(s_font, rc, (Vector2){(float)(px + 12), (float)(y + 14)}, 9, 1, txt);
    y += 36;

    DrawTextEx(s_font, "LLM ENGINE", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    y += 16;
    if (ai_engine_is_loaded()) {
        float tps = ai_engine_tokens_per_sec();
        int ctx = ai_engine_context_usage();
        char lt[128];
        snprintf(lt, sizeof(lt), "Token/s: %.1f", tps);
        DrawTextEx(s_font, lt, (Vector2){(float)(px + 12), (float)y}, 9, 1, txt);
        y += 14;
        snprintf(lt, sizeof(lt), "Context: %d%%", ctx);
        DrawTextEx(s_font, lt, (Vector2){(float)(px + 12), (float)y}, 9, 1, ctx > 80 ? (Color){220,40,40,255} : txt);
        y += 14;
        DrawTextEx(s_font, "Ready", (Vector2){(float)(px + 12), (float)y}, 9, 1, (Color){0,220,80,255});
    } else {
        DrawTextEx(s_font, "Not loaded", (Vector2){(float)(px + 12), (float)y}, 9, 1, (Color){220,40,40,255});
    }
    y += 30;

    DrawTextEx(s_font, "CONTEXT", (Vector2){(float)(px + 12), (float)y}, 10, 1, cyan_c);
    y += 14;
    if (s_contextLabel[0]) {
        const char *p = s_contextLabel;
        char ln[128];
        while (*p) {
            int i = 0;
            while (*p && i < (int)sizeof(ln) - 1 && *p != '\n') ln[i++] = *p++;
            ln[i] = '\0';
            if (*p == '\n') p++;
            DrawTextEx(s_font, ln, (Vector2){(float)(px + 12), (float)y}, 9, 1, txt);
            y += 12;
            if (y > py + ph - 20) break;
        }
    } else {
        DrawTextEx(s_font, "(none)", (Vector2){(float)(px + 12), (float)y}, 9, 1, txt);
    }
}

/* ── Top Bar ── */
static void draw_top_bar(void) {
    DrawRectangle(0, 0, s_width, 34, (Color){6,10,18,240});
    DrawLine(0, 34, s_width, 34, (Color){0,212,255,20});
    DrawLine(0, 35, s_width, 35, (Color){0,212,255,6});

    DrawTextEx(s_font, "WINALP OS  v1.0.0", (Vector2){14, 10}, 14, 1, (Color){0,212,255,255});
    DrawTextEx(s_font, "User:", (Vector2){(float)(s_width - 220), 11}, 11, 1, (Color){160,175,190,255});
    DrawTextEx(s_font, s_profileLabel[0] ? s_profileLabel : s_windows_user,
               (Vector2){(float)(s_width - 170), 11}, 11, 1, (Color){0,212,255,255});
}

/* ── Footer ── */
static void draw_footer(void) {
    int fy = s_height - 28;
    DrawRectangle(0, fy, s_width, 28, (Color){6,10,18,240});
    DrawLine(0, fy, s_width, fy, (Color){0,212,255,10});

    const char *label = "WINALP  |  COLGECEN TECHNOLOGIES";
    Vector2 sz = MeasureTextEx(s_font, label, 12, 1);
    DrawTextEx(s_font, label, (Vector2){(s_width - sz.x) / 2, (float)(fy + 7)}, 12, 1, (Color){0,212,255,80});
    DrawTextEx(s_font, "v1.0.0", (Vector2){14, (float)(fy + 7)}, 10, 1, (Color){0,212,255,35});
}

/* ── Quick Status ── */
static void draw_quick_status(void) {
    char buf[128];
    snprintf(buf, sizeof(buf), "FPS: %d  |  CPU: %.0f%%  |  RAM: %llu MB",
             GetFPS(), s_sys.cpu_percent, s_sys.ram_used_mb);
    Vector2 sz = MeasureTextEx(s_font, buf, 10, 1);
    DrawTextEx(s_font, buf, (Vector2){(s_width - sz.x) / 2, (float)(s_height - 48)}, 10, 1,
               (Color){150,160,170,160});
}

/* ── Overlay ── */
static void draw_overlay(void) {
    if (!s_overlay_active) return;
    DrawRectangle(0, 0, s_width, s_height, (Color){0,0,0,140});

    int mw = 400, mh = 150;
    int mx = (s_width - mw) / 2, my = (s_height - mh) / 2;
    DrawRectangle(mx, my, mw, mh, (Color){8,16,30,240});
    DrawRectangleLinesEx((Rectangle){(float)mx, (float)my, (float)mw, (float)mh}, 1, (Color){0,212,255,50});
    DrawLine(mx + 2, my + 2, mx + 20, my + 2, (Color){0,212,255,100});
    DrawLine(mx + 2, my + 2, mx + 2, my + 20, (Color){0,212,255,100});

    DrawTextEx(s_font, s_overlay_title, (Vector2){(float)(mx + 20), (float)(my + 15)}, 18, 1, (Color){0,212,255,255});
    DrawTextEx(s_font, s_overlay_msg, (Vector2){(float)(mx + 20), (float)(my + 45)}, 14, 1, (Color){160,175,190,255});

    Rectangle yBtn = {(float)(mx + 70), (float)(my + 100), 110, 30};
    DrawRectangleRounded(yBtn, 0.15f, 4, (Color){0,160,70,220});
    DrawRectangleLinesEx(yBtn, 1, (Color){0,212,80,100});
    Vector2 ys = MeasureTextEx(s_font, "YES", 14, 1);
    DrawTextEx(s_font, "YES", (Vector2){yBtn.x + (yBtn.width - ys.x) / 2, yBtn.y + 6}, 14, 1, WHITE);

    Rectangle nBtn = {(float)(mx + 220), (float)(my + 100), 110, 30};
    DrawRectangleRounded(nBtn, 0.15f, 4, (Color){160,40,40,220});
    DrawRectangleLinesEx(nBtn, 1, (Color){220,60,60,100});
    Vector2 ns = MeasureTextEx(s_font, "NO", 14, 1);
    DrawTextEx(s_font, "NO", (Vector2){nBtn.x + (nBtn.width - ns.x) / 2, nBtn.y + 6}, 14, 1, WHITE);

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
    Font f = GetFontDefault();

    while (!WindowShouldClose()) {
        int mw = GetScreenWidth();
        int mh = GetScreenHeight();
        Vector2 mp = GetMousePosition();
        hovered = -1;

        BeginDrawing();
        ClearBackground((Color){8, 12, 20, 255});

        DrawTextEx(f, "SELECT MODEL", (Vector2){40, 20}, 28, 1, (Color){0, 212, 255, 255});
        DrawTextEx(f, "Choose a brain model to start WinAlp",
                   (Vector2){40, 55}, 14, 1, (Color){128, 140, 160, 255});

        int x = 40, y = 90, item_h = 48;
        for (int i = 0; i < n_models; i++) {
            int cy = y + (i - scroll) * item_h;
            if (cy + item_h < 90 || cy > mh - 20) continue;

            Rectangle r = {(float)x, (float)cy, (float)(mw - 80), (float)item_h};
            bool inside = CheckCollisionPointRec(mp, r);
            if (inside) hovered = i;

            Color bg = (i == selected) ? (Color){0,100,180,100} :
                       inside ? (Color){40,50,70,100} : (Color){12,18,28,80};
            DrawRectangleRec(r, bg);
            DrawRectangleLinesEx(r, 1, inside ? (Color){0,212,255,60} : (Color){0,212,255,10});

            Color cc;
            switch (models[i].compat) {
                case 3: cc = (Color){0,200,80,255}; break;
                case 2: cc = (Color){240,200,0,255}; break;
                case 1: cc = (Color){220,40,40,255}; break;
                default: cc = (Color){80,80,80,255}; break;
            }
            DrawCircle(x + 14, cy + item_h / 2, 5, cc);

            char label[512];
            const char *vt = (models[i].flags & 1) ? " [VLM]" : "";
            snprintf(label, sizeof(label), "%s%s  (%llu MB, %s)",
                     models[i].label, vt, models[i].size_mb, models[i].arch);
            DrawTextEx(f, label, (Vector2){(float)(x + 32), (float)(cy + 7)},
                       15, 1, (Color){220,230,240,255});

            const char *ts = (models[i].tier == 0) ? "Hafif" :
                             (models[i].tier == 1) ? "Orta" : "Guclu";
            const char *cs = (models[i].compat == 3) ? "Uyumlu" :
                             (models[i].compat == 2) ? "Sinir" : "Yetersiz";
            char rl[128];
            snprintf(rl, sizeof(rl), "%s | %s", ts, cs);
            DrawTextEx(f, rl, (Vector2){(float)(mw - 180), (float)(cy + 9)}, 12, 1, cc);
        }

        DrawTextEx(f, "Click a model to select  |  ESC to exit",
                   (Vector2){40, (float)(mh - 30)}, 12, 1, (Color){80,90,110,200});

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

    const char *env_user = std::getenv("USERNAME");
    if (env_user && env_user[0]) {
        strncpy(s_windows_user, env_user, sizeof(s_windows_user) - 1);
    } else {
        strncpy(s_windows_user, "User", sizeof(s_windows_user) - 1);
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(width, height, title);
    SetTargetFPS(WINALP_TARGET_FPS);

    load_ui_font();

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
    if (s_font_loaded) UnloadFont(s_font);
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

void ui_render_set_task_strip(const char *tasks) {
    (void)tasks;
}

void ui_render_set_theme_float(const char *key, float value) {
    (void)key; (void)value;
}
