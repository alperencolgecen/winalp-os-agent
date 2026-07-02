#include "../include/ui_render.h"

extern "C" {
#include "../include/ai_engine.h"
#include "../include/stt_engine.h"
#include "../include/audio_capture.h"
#include "../include/sys_monitor.h"
}

#include "../include/logger.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include "raylib.h"

/* Spinlock for thread-safe chat/transcript access (no windows.h needed) */
static std::atomic_flag s_ui_lock = ATOMIC_FLAG_INIT;

static void ui_lock(void) {
    while (s_ui_lock.test_and_set(std::memory_order_acquire))
        ;
}

static void ui_unlock(void) {
    s_ui_lock.clear(std::memory_order_release);
}

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

static float s_time;
static AgentState s_lastState = AGENT_STATE_LISTENING;
static float s_colorLerp[4] = {0, 212, 255, 255};
static float s_colorTarget[4] = {0, 212, 255, 255};
static Font s_font;
static bool s_font_loaded = false;
static bool s_octagon_held = false;
static float s_speedLerp = 1.0f;

static volatile char s_transcript[4096] = "";

/* Chat message buffer */
#define MAX_CHAT 64
#define CHAT_LEN 512
static char s_chat_role[MAX_CHAT][32];
static char s_chat_text[MAX_CHAT][CHAT_LEN];
static char s_chat_src[MAX_CHAT][16];
static int s_chat_count = 0;
static int s_chat_head = 0;

/* Notification queue for quick status cards */
#define MAX_NOTIFS 8
typedef struct {
    char text[256];
    Color color;
    double start_time;
    double duration;
} Notif;
static Notif s_notifs[MAX_NOTIFS];
static int s_notif_count = 0;
static int s_notif_head = 0;

void ui_render_push_notification(const char *text, int r, int g, int b, double duration_sec) {
    if (!text || !text[0]) return;
    int idx = (s_notif_head + s_notif_count) % MAX_NOTIFS;
    strncpy(s_notifs[idx].text, text, sizeof(s_notifs[idx].text) - 1);
    s_notifs[idx].color = (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, 220};
    s_notifs[idx].start_time = s_time;
    s_notifs[idx].duration = duration_sec;
    if (s_notif_count < MAX_NOTIFS) s_notif_count++;
    else s_notif_head = (s_notif_head + 1) % MAX_NOTIFS;
}

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
        Font f = LoadFontEx(paths[i], 64, s_font_cps, FONT_CP_CNT);
        if (f.glyphCount > 0) SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
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
    DrawCircleGradient(cx, cy, (float)radius, (Color){2,6,12,0}, (Color){2,6,12,240});
}

static void draw_scanlines(void) {
    for (int y = 0; y < s_height; y += 3) {
        DrawLine(0, y, s_width, y, (Color){0,0,0,25});
    }
}

static void draw_grid(void) {
    for (int x = 0; x < s_width; x += 30) {
        Color c = (x % 150 == 0) ? (Color){0,243,255,12} : (Color){0,243,255,4};
        DrawLine(x, 0, x, s_height, c);
    }
    for (int y = 0; y < s_height; y += 30) {
        Color c = (y % 150 == 0) ? (Color){0,243,255,12} : (Color){0,243,255,4};
        DrawLine(0, y, s_width, y, c);
    }
}

static void draw_panel_bg(int x, int y, int w, int h, Color accent) {
    /* Glass background — more transparent */
    DrawRectangle(x, y, w, h, (Color){0, 20, 50, 20});

    /* Subtle top highlight gradient */
    DrawRectangle(x, y, w, 2, alpha(accent, 30));
    DrawRectangle(x, y, w, 1, alpha(accent, 60));

    /* Bottom-right corner cut */
    Vector2 pts[6] = {
        {(float)x, (float)y},
        {(float)(x + w), (float)y},
        {(float)(x + w), (float)(y + h - 12)},
        {(float)(x + w - 12), (float)(y + h)},
        {(float)x, (float)(y + h)},
        {(float)x, (float)y}
    };
    DrawLineStrip(pts, 6, alpha(accent, 80));

    /* Corner glow dots */
    DrawCircle(x, y, 3, alpha(accent, 120));
    DrawCircle(x + w, y + h, 3, alpha(accent, 120));

    /* Top-left L-bracket */
    DrawLine(x + 2, y - 1, x + 18, y - 1, accent);
    DrawLine(x - 1, y + 2, x - 1, y + 18, accent);

    /* Bottom-right L-bracket */
    DrawLine(x + w - 2, y + h + 1, x + w - 18, y + h + 1, accent);
    DrawLine(x + w + 1, y + h - 2, x + w + 1, y + h - 18, accent);
}

/* Orbiting particle state */
#define N_PARTICLES 24
static struct { float angle, radius, speed, size; } s_parts[N_PARTICLES];
static bool s_parts_init;

static void init_particles(void) {
    if (s_parts_init) return;
    float radii[] = {150, 200, 260};
    for (int i = 0; i < N_PARTICLES; i++) {
        s_parts[i].angle  = (float)i * (3.14159f * 2.0f / (N_PARTICLES / 3));
        s_parts[i].radius = radii[i / 8];
        s_parts[i].speed  = (i % 8 < 4) ? 0.6f + (i % 8) * 0.1f : -(0.6f + (i % 8) * 0.1f);
        s_parts[i].size   = 2.0f + (i % 3) * 0.5f;
    }
    s_parts_init = true;
}

/* ── Jarvis Arc Reactor ── */
static void draw_arc_reactor(AgentState state, float amplitude) {
    int cx = s_width / 2;
    int cy = s_height / 2;
    float dt = GetFrameTime();
    s_time += dt;
    init_particles();

    if (state != s_lastState) {
        state_color_rgba(state, s_colorTarget);
        s_lastState = state;
    }
    float ease = 1.0f - powf(0.01f, dt * 8.0f);
    for (int i = 0; i < 4; i++)
        s_colorLerp[i] += (s_colorTarget[i] - s_colorLerp[i]) * ease;

    Color col = color_from_state(s_colorLerp);
    Color cyan = (Color){0, 243, 255, 255};

    float r1 = 300.0f;
    DrawCircleLines(cx, cy, r1, alpha(cyan, 30));
    for(int i=0; i<72; i++) {
        float a = s_time * 0.1f + i * (3.14159f * 2.0f / 72.0f);
        if (i % 3 != 0) {
            Vector2 p1 = {cx + cosf(a)*r1, cy + sinf(a)*r1};
            Vector2 p2 = {cx + cosf(a)*(r1-5), cy + sinf(a)*(r1-5)};
            DrawLineEx(p1, p2, 2.0f, alpha(cyan, 100));
        }
    }

    float r2 = 250.0f;
    DrawCircleLines(cx, cy, r2, alpha(cyan, 50));
    for(int i=0; i<12; i++) {
        float a = -s_time * 0.2f + i * (3.14159f * 2.0f / 12.0f);
        Vector2 p1 = {cx + cosf(a)*r2, cy + sinf(a)*r2};
        Vector2 p2 = {cx + cosf(a)*(r2-15), cy + sinf(a)*(r2-15)};
        DrawLineEx(p1, p2, 4.0f, cyan);
    }

    float r3 = 180.0f;
    DrawCircleLines(cx, cy, r3, alpha(col, 80));
    DrawCircleLines(cx, cy, r3 - 4, alpha(col, 40));
    
    float r_core = 90.0f + amplitude * 10.0f;
    DrawCircleGradient(cx, cy, r_core * 1.5f, alpha(col, 80), (Color){0,0,0,0});
    
    float speedTarget = s_octagon_held ? 4.0f : 1.0f;
    float speedEase = 1.0f - powf(0.01f, dt * 4.0f);
    s_speedLerp += (speedTarget - s_speedLerp) * speedEase;
    Color sqCol = s_octagon_held ? (Color){0,255,100,255} : cyan;
    float angVel[8] = {40.0f, 55.0f, 70.0f, 85.0f, 100.0f, 115.0f, 130.0f, 145.0f};
    float sizeFac[8] = {1.0f, 0.85f, 0.70f, 0.57f, 0.45f, 0.34f, 0.24f, 0.15f};
    for (int i = 0; i < 8; i++) {
        float rot = s_time * angVel[i] * s_speedLerp + i * 22.5f;
        float s = r_core * sizeFac[i];
        Vector2 c = {(float)cx, (float)cy};
        int baseAlpha = (i == 0) ? 80 + (int)(40 * sinf(s_time * 0.5f + i)) : (120 - i * 12 > 40 ? 120 - i * 12 : 40);
        Color fill = (i == 3) ? alpha(s_octagon_held ? (Color){0,255,100,80} : col, 40) : BLANK;
        for (int g = 6; g >= 0; g--) {
            float gr = rot - g * dt * angVel[i] * s_speedLerp;
            int ga = baseAlpha * (7 - g) / 8;
            if (g == 0) {
                if (fill.a > 0) DrawPoly(c, 4, s, gr, fill);
                DrawPolyLines(c, 4, s, gr, alpha(sqCol, ga));
            } else {
                DrawPolyLines(c, 4, s, gr, alpha(sqCol, ga / 2));
            }
        }
    }

    /* Orbiting particles */
    float partSpeedMul = (state == AGENT_STATE_THINKING) ? 2.0f :
                         (state == AGENT_STATE_ACTING)  ? 3.0f : 1.0f;
    for (int i = 0; i < N_PARTICLES; i++) {
        s_parts[i].angle += s_parts[i].speed * dt * partSpeedMul;
        float nx = cx + cosf(s_parts[i].angle) * s_parts[i].radius;
        float ny = cy + sinf(s_parts[i].angle) * s_parts[i].radius;
        float dist = sqrtf((nx - cx)*(nx - cx) + (ny - cy)*(ny - cy));
        int pa = (int)(120 + 80 * sinf(dist * 0.02f + s_time));
        DrawCircle((int)nx, (int)ny, s_parts[i].size, alpha(col, pa));
    }

    /* Data flow lines — left and right panels to core */
    float flowPhase = fmodf(s_time * 0.5f, 1.0f);
    Vector2 centers[] = {
        {(float)(40 + 300), 170.0f},
        {(float)(s_width - 40 - 300), 170.0f}
    };
    for (int side = 0; side < 2; side++) {
        Vector2 from = centers[side];
        Vector2 to   = {(float)cx, (float)cy};
        /* Dashed line */
        for (float t = 0; t < 1.0f; t += 0.03f) {
            float phase = fmodf(t + flowPhase, 0.06f);
            if (phase < 0.04f) {
                float lx = from.x + (to.x - from.x) * t;
                float ly = from.y + (to.y - from.y) * t;
                DrawPixel((int)lx, (int)ly, alpha(col, 40));
            }
        }
        /* Traveling dot */
        float dotT = fmodf(s_time * 0.3f + side * 0.5f, 1.0f);
        float dx = from.x + (to.x - from.x) * dotT;
        float dy = from.y + (to.y - from.y) * dotT;
        DrawCircle((int)dx, (int)dy, 3, alpha(col, 180 - (int)(80 * dotT)));
    }

    DrawCircle(cx, cy, 30.0f + amplitude*5.0f, alpha(WHITE, 200));
    DrawCircleGradient(cx, cy, 60.0f, WHITE, alpha(cyan, 0));

    char pct[16]; snprintf(pct, sizeof(pct), "%.0f%%", s_sys.cpu_percent);
    DrawTextEx(s_font, pct, (Vector2){(float)(cx - MeasureTextEx(s_font, pct, 16, 1).x/2), (float)(cy - 8)}, 16, 1, BLACK);

    float ret = 350.0f;
    DrawLine(cx - ret, cy - ret, cx - ret + 20, cy - ret, cyan);
    DrawLine(cx - ret, cy - ret, cx - ret, cy - ret + 20, cyan);
    
    DrawLine(cx + ret, cy - ret, cx + ret - 20, cy - ret, cyan);
    DrawLine(cx + ret, cy - ret, cx + ret, cy - ret + 20, cyan);
    
    DrawLine(cx - ret, cy + ret, cx - ret + 20, cy + ret, cyan);
    DrawLine(cx - ret, cy + ret, cx - ret, cy + ret - 20, cyan);
    
    DrawLine(cx + ret, cy + ret, cx + ret - 20, cy + ret, cyan);
    DrawLine(cx + ret, cy + ret, cx + ret, cy + ret - 20, cyan);

    /* Octagon push-to-talk detection */
    float core_r = 90.0f + amplitude * 10.0f;
    Vector2 mp = GetMousePosition();
    bool over_core = CheckCollisionPointCircle(mp, (Vector2){(float)cx, (float)cy}, core_r + 20);

    if (over_core && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        s_octagon_held = true;
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        s_octagon_held = false;
    }

    if (s_octagon_held) {
        DrawCircleLines(cx, cy, core_r + 35, alpha((Color){0,255,100,255}, 80 + (int)(sinf(s_time * 8) * 50)));
    }
}

/* ── Left Panel ── */
static void draw_left_panel(void) {
    int px = 40, py = 120, pw = 300;
    Color cyan_c = (Color){0,243,255,255};
    Color txt = (Color){180,220,255,200};
    int y = py;

    draw_panel_bg(px, y, pw, 100, cyan_c);
    DrawTextEx(s_font, "CPU #1", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    char cpu_txt[32]; snprintf(cpu_txt, sizeof(cpu_txt), "%.0f%%", s_sys.cpu_percent);
    DrawTextEx(s_font, cpu_txt, (Vector2){(float)(px + pw - 40), (float)(y + 5)}, 12, 1, cyan_c);
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));
    
    for(int i=0; i<20; i++) {
        float h = (sinf(s_time*5.0f + i) * 0.5f + 0.5f) * 60.0f;
        if(i%3==0) h = s_sys.cpu_percent / 100.0f * 60.0f + (rand()%10);
        DrawRectangle(px + 10 + i*14, y + 90 - (int)h, 10, (int)h, alpha(cyan_c, 150));
    }
    y += 120;

    draw_panel_bg(px, y, pw, 100, cyan_c);
    DrawTextEx(s_font, "CENTRAL AUDIO UNIT", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawTextEx(s_font, "FREQ: 48kHz", (Vector2){(float)(px + pw - 80), (float)(y + 5)}, 10, 1, txt);
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));
    
    for (int i = 0; i < pw - 20; i++) {
        int idx = (s_wavePos + i * WAVE_LEN / (pw - 20)) % WAVE_LEN;
        int x0 = px + 10 + i;
        int h = (int)(s_waveform[idx] * 35);
        DrawLine(x0, y + 60 - h, x0, y + 60 + h, alpha(cyan_c, 60 + (int)(s_waveform[idx] * 120)));
    }
    y += 120;

    draw_panel_bg(px, y, pw, 120, cyan_c);
    DrawTextEx(s_font, "MEMORY ALLOCATION", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawTextEx(s_font, "RAM", (Vector2){(float)(px + pw - 30), (float)(y + 5)}, 10, 1, cyan_c);
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));

    char ram_str[256];
    snprintf(ram_str, sizeof(ram_str), "TOTAL: %llu MB\nUSAGE: %llu MB\nFREE: %llu MB", 
             s_sys.ram_total_mb, s_sys.ram_used_mb, s_sys.ram_total_mb - s_sys.ram_used_mb);
    DrawTextEx(s_font, ram_str, (Vector2){(float)(px + 10), (float)(y + 30)}, 10, 1.5f, txt);
    
    float ram_pct = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
    DrawRectangle(px + 10, y + 100, pw - 20, 6, (Color){10,20,40,200});
    DrawRectangle(px + 10, y + 100, (int)((pw - 20) * ram_pct), 6, cyan_c);
}

/* ── Right Panel ── */
static void draw_right_panel(void) {
    int pw = 300;
    int px = s_width - pw - 40, py = 120;
    Color cyan_c = (Color){0,243,255,255};
    Color txt = (Color){180,220,255,200};
    int y = py;

    DrawTextEx(s_font, "30°C", (Vector2){(float)(px + pw - MeasureTextEx(s_font, "30°C", 36, 1).x), (float)y}, 36, 1, WHITE);
    y += 40;
    const char* loc = "MALIBU, CA";
    DrawTextEx(s_font, loc, (Vector2){(float)(px + pw - MeasureTextEx(s_font, loc, 12, 1).x), (float)y}, 12, 1, cyan_c);
    y += 20;
    
    const char* w_details = "Humidity: 14%\nPrecipitation: 0%\nWind: 8 km/h (E)\nVisibility: 16.1 km";
    Vector2 w_sz = MeasureTextEx(s_font, w_details, 10, 1.5f);
    DrawTextEx(s_font, w_details, (Vector2){(float)(px + pw - w_sz.x), (float)y}, 10, 1.5f, txt);
    y += 80;

    draw_panel_bg(px, y, pw, 100, cyan_c);
    DrawTextEx(s_font, "SATELLITE UPLINK", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawTextEx(s_font, s_sys.time_str, (Vector2){(float)(px + pw - MeasureTextEx(s_font, s_sys.time_str, 12, 1).x - 10), (float)(y + 5)}, 12, 1, txt);
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));
    
    const char* sat_str = "LAT: 34.0259 N\nLON: 118.7798 W\nALT: 215 M\nSTATUS: ENCRYPTED";
    DrawTextEx(s_font, sat_str, (Vector2){(float)(px + 10), (float)(y + 30)}, 10, 1.5f, txt);
    y += 120;

    draw_panel_bg(px, y, pw, 100, cyan_c);
    DrawTextEx(s_font, "SYSTEM DIAGNOSTICS", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawTextEx(s_font, "NORMAL", (Vector2){(float)(px + pw - MeasureTextEx(s_font, "NORMAL", 12, 1).x - 10), (float)(y + 5)}, 12, 1, (Color){0,255,100,255});
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));
    
    for(int i=0; i<15; i++) {
        float h = (cosf(s_time*2.0f + i) * 0.5f + 0.5f) * 60.0f;
        DrawRectangle(px + 10 + i*18, y + 90 - (int)h, 12, (int)h, alpha(cyan_c, 100));
    }
    y += 120;

    draw_panel_bg(px, y, pw, 90, cyan_c);
    DrawTextEx(s_font, "POWER CORE", (Vector2){(float)(px + 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawTextEx(s_font, "STABLE", (Vector2){(float)(px + pw - MeasureTextEx(s_font, "STABLE", 12, 1).x - 10), (float)(y + 5)}, 12, 1, cyan_c);
    DrawLine(px + 10, y + 20, px + pw - 10, y + 20, alpha(cyan_c, 50));
    const char* pwr_str = "OUTPUT: 3.4 GW\nTEMP: 3500 K\nCONTAINMENT: 99.9%";
    DrawTextEx(s_font, pwr_str, (Vector2){(float)(px + 10), (float)(y + 30)}, 10, 1.5f, txt);
}

/* ── Top Bar ── */
static void draw_top_bar(void) {
    Color cyan_c = (Color){0,243,255,255};
    Color txt = (Color){180,220,255,200};

    const char *stt_st = stt_engine_is_loaded() ? "STT: READY" : "STT: MISSING";
    const char *ai_st  = ai_engine_is_loaded()  ? "AI:  READY" : "AI:  OFFLINE";
    Color stt_c = stt_engine_is_loaded() ? cyan_c : (Color){255, 80, 80, 220};
    Color ai_c  = ai_engine_is_loaded()  ? cyan_c : (Color){255, 80, 80, 220};

    DrawRectangleLines(40, 20, 150, 20, alpha(stt_c, 150));
    DrawRectangle(40, 20, 5, 5, stt_c);
    DrawTextEx(s_font, stt_st, (Vector2){50, 24}, 10, 1, stt_c);

    DrawRectangleLines(200, 20, 150, 20, alpha(ai_c, 150));
    DrawRectangle(200, 20, 5, 5, ai_c);
    DrawTextEx(s_font, ai_st, (Vector2){210, 24}, 10, 1, ai_c);

    int rx = s_width - 190;
    DrawRectangleLines(rx, 20, 150, 20, alpha(cyan_c, 100));
    DrawRectangle(rx, 20, 5, 5, cyan_c);
    DrawTextEx(s_font, "NETWORK: SECURE", (Vector2){(float)(rx + 10), 24}, 10, 1, cyan_c);

    rx -= 160;
    DrawRectangleLines(rx, 20, 150, 20, alpha(cyan_c, 100));
    DrawRectangle(rx, 20, 5, 5, cyan_c);
    DrawTextEx(s_font, "SYSTEM: ONLINE", (Vector2){(float)(rx + 10), 24}, 10, 1, cyan_c);

    int cx = s_width / 2;
    int p_w = 200, p_h = 50;
    int px = cx - p_w/2;
    draw_panel_bg(px, 10, p_w, p_h, cyan_c);
    DrawCircleLines(px + 25, 10 + p_h/2, 15, cyan_c);
    DrawTextEx(s_font, "JARVIS OS", (Vector2){(float)(px + 50), 15}, 14, 2, WHITE);
    DrawTextEx(s_font, "Ver: 1.2.5  |  User: Master", (Vector2){(float)(px + 50), 35}, 9, 1, txt);
}

/* ── Chat Panel ── */
static void draw_chat_panel(void) {
    /* Snapshot last messages under lock */
    ui_lock();
    int cc = s_chat_count, ch = s_chat_head;
    if (cc == 0) { ui_unlock(); return; }
    int show = cc > 6 ? 6 : cc;
    int start = show >= cc ? ch : (ch + cc - show) % MAX_CHAT;
    char snap_role[6][32];
    char snap_text[6][512];
    char snap_src[6][16];
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % MAX_CHAT;
        strncpy(snap_role[i], s_chat_role[idx], sizeof(snap_role[i]) - 1);
        strncpy(snap_text[i], s_chat_text[idx], sizeof(snap_text[i]) - 1);
        strncpy(snap_src[i],  s_chat_src[idx],  sizeof(snap_src[i]) - 1);
    }
    ui_unlock();

    int pw = 500, px = (s_width - pw) / 2, py = s_height - 220, ph = show * 24 + 20;
    DrawRectangle(px, py - ph, pw, ph, (Color){8, 12, 20, 180});
    DrawRectangleLines(px, py - ph, pw, ph, (Color){0, 212, 255, 40});

    int y = py - ph + 12;
    for (int i = 0; i < show; i++) {
        bool is_user = (strcmp(snap_role[i], "user") == 0);
        Color role_col = is_user ? (Color){0, 212, 255, 220} : (Color){0, 255, 100, 220};
        char line[520];
        snprintf(line, sizeof(line), "%s %s", snap_src[i], snap_text[i]);
        DrawTextEx(s_font, line, (Vector2){(float)(px + 12), (float)y}, 11, 1, role_col);
        y += 24;
    }
}
static void draw_footer(void) {
    const char *label = "COLGECEN TECHNOLOGIES";
    Vector2 sz = MeasureTextEx(s_font, label, 24, 8);
    
    int fy = s_height - 60;
    int fx = (s_width - (int)sz.x) / 2;
    
    DrawTextEx(s_font, label, (Vector2){(float)fx, (float)fy}, 24, 8, (Color){255,255,255,200});
    DrawTextEx(s_font, label, (Vector2){(float)(fx-1), (float)fy}, 24, 8, alpha((Color){0,243,255,255}, 100));
    DrawTextEx(s_font, label, (Vector2){(float)(fx+1), (float)fy}, 24, 8, alpha((Color){0,243,255,255}, 100));
}

/* ── Quick Status Cards ── */
static void draw_quick_status(void) {
    /* System warning cards (always shown when triggered) */
    int card_y = 160;

    /* CPU high warning */
    if (s_sys.cpu_percent > 80.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "CPU: %.0f%%", s_sys.cpu_percent);
        int cw = 200, ch = 28;
        int cx = s_width - cw - 40;
        DrawRectangle(cx, card_y, cw, ch, (Color){80, 20, 10, 180});
        DrawRectangle(cx, card_y, 3, ch, (Color){245, 158, 11, 220});
        DrawTextEx(s_font, buf, (Vector2){(float)(cx + 12), (float)(card_y + 6)}, 12, 1, (Color){245, 158, 11, 220});
        card_y += 34;
    }

    /* RAM high warning */
    float ram_pct = s_sys.ram_total_mb ? (float)s_sys.ram_used_mb / (float)s_sys.ram_total_mb : 0;
    if (ram_pct > 0.85f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "RAM: %llu/%llu MB", s_sys.ram_used_mb, s_sys.ram_total_mb);
        int cw = 240, ch = 28;
        int cx = s_width - cw - 40;
        DrawRectangle(cx, card_y, cw, ch, (Color){80, 20, 10, 180});
        DrawRectangle(cx, card_y, 3, ch, (Color){220, 40, 40, 220});
        DrawTextEx(s_font, buf, (Vector2){(float)(cx + 12), (float)(card_y + 6)}, 12, 1, (Color){220, 40, 40, 220});
        card_y += 34;
    }

    /* Battery warning */
    if (s_sys.battery_percent >= 0 && s_sys.battery_percent <= 20 && !s_sys.ac_power) {
        char buf[64];
        snprintf(buf, sizeof(buf), "BATTERY: %d%%", s_sys.battery_percent);
        int cw = 180, ch = 28;
        int cx = s_width - cw - 40;
        Color bc = s_sys.battery_percent <= 10 ? (Color){220, 40, 40, 220} : (Color){245, 158, 11, 220};
        DrawRectangle(cx, card_y, cw, ch, (Color){80, 20, 10, 180});
        DrawRectangle(cx, card_y, 3, ch, bc);
        DrawTextEx(s_font, buf, (Vector2){(float)(cx + 12), (float)(card_y + 6)}, 12, 1, bc);
        card_y += 34;
    }

    /* Transient notification cards from queue */
    for (int i = 0; i < s_notif_count; i++) {
        int idx = (s_notif_head + i) % MAX_NOTIFS;
        double elapsed = s_time - s_notifs[idx].start_time;
        if (elapsed > s_notifs[idx].duration) continue;

        float fade = 1.0f;
        if (elapsed > s_notifs[idx].duration - 1.0f)
            fade = (float)(s_notifs[idx].duration - elapsed);

        int cw = 260, ch = 32;
        int cx = s_width - cw - 40;
        int ny = s_height - 120 - (s_notif_count - i) * 38;

        Color bg = (Color){0, 20, 50, (unsigned char)(160 * fade)};
        DrawRectangle(cx, ny, cw, ch, bg);
        DrawRectangle(cx, ny, 3, ch, alpha(s_notifs[idx].color, (unsigned char)(200 * fade)));
        DrawTextEx(s_font, s_notifs[idx].text,
                   (Vector2){(float)(cx + 12), (float)(ny + 8)},
                   11, 1, alpha(s_notifs[idx].color, (unsigned char)(220 * fade)));
    }
}

/* Emergency flash state */
static bool s_emergency_active = false;
static double s_emergency_start = 0;
static double s_emergency_duration = 0;

void ui_render_trigger_emergency(double duration_sec) {
    s_emergency_active = true;
    s_emergency_start = s_time;
    s_emergency_duration = duration_sec;
}

static void draw_emergency(void) {
    if (!s_emergency_active) return;
    double elapsed = s_time - s_emergency_start;
    if (elapsed > s_emergency_duration) {
        s_emergency_active = false;
        return;
    }
    float intensity = (float)(1.0 - elapsed / s_emergency_duration);
    int alpha_v = (int)(120 * intensity * (0.5f + 0.5f * sinf((float)elapsed * 12.0f)));
    if (alpha_v < 5) return;
    DrawRectangle(0, 0, s_width, s_height, (Color){245, 50, 10, (unsigned char)alpha_v});
    /* Border flash */
    int bw = 4;
    DrawRectangle(0, 0, s_width, bw, (Color){245, 158, 11, (unsigned char)(180 * intensity)});
    DrawRectangle(0, s_height - bw, s_width, bw, (Color){245, 158, 11, (unsigned char)(180 * intensity)});
}

/* ── Overlay ── */
static void draw_transcript(void) {
    char txt[4096];
    ui_lock();
    strncpy(txt, (const char*)s_transcript, sizeof(txt) - 1);
    ui_unlock();
    if (!txt[0]) return;

    bool listening = (strcmp(txt, "Listening...") == 0);
    int pulse = listening ? (int)(80 + 60 * sinf(s_time * 4.0f)) : 220;
    Color col = listening ? (Color){0, 243, 255, pulse} : (Color){0, 255, 100, 220};
    int fs = listening ? 18 : 20;

    Vector2 sz = MeasureTextEx(s_font, txt, fs, 1);
    int px = (s_width - (int)sz.x) / 2;
    int py = s_height - 80;
    int pad = 14;
    DrawRectangle(px - pad, py - 6, (int)sz.x + pad * 2, (int)sz.y + 12,
                  (Color){0, 0, 0, 160});
    DrawRectangleLines(px - pad, py - 6, (int)sz.x + pad * 2, (int)sz.y + 12,
                       alpha(col, 60));
    DrawTextEx(s_font, txt, (Vector2){(float)px, (float)py}, fs, 1, col);
}

static void draw_overlay(void) {
    if (!s_overlay_active) return;
    DrawRectangle(0, 0, s_width, s_height, (Color){0,0,0,180});

    int mw = 400, mh = 150;
    int mx = (s_width - mw) / 2, my = (s_height - mh) / 2;
    draw_panel_bg(mx, my, mw, mh, (Color){0,243,255,255});

    DrawTextEx(s_font, s_overlay_title, (Vector2){(float)(mx + 20), (float)(my + 15)}, 18, 1, (Color){0,243,255,255});
    DrawTextEx(s_font, s_overlay_msg, (Vector2){(float)(mx + 20), (float)(my + 45)}, 14, 1, (Color){180,220,255,255});

    Rectangle yBtn = {(float)(mx + 70), (float)(my + 100), 110, 30};
    DrawRectangleLinesEx(yBtn, 1, (Color){0,243,255,100});
    DrawRectangleRec(yBtn, alpha((Color){0,243,255,255}, 30));
    Vector2 ys = MeasureTextEx(s_font, "YES", 14, 1);
    DrawTextEx(s_font, "YES", (Vector2){yBtn.x + (yBtn.width - ys.x) / 2, yBtn.y + 6}, 14, 1, WHITE);

    Rectangle nBtn = {(float)(mx + 220), (float)(my + 100), 110, 30};
    DrawRectangleLinesEx(nBtn, 1, (Color){255,40,40,100});
    DrawRectangleRec(nBtn, alpha((Color){255,40,40,255}, 30));
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

        DrawTextEx(f, "Double-click to select  |  Enter to confirm  |  ESC to exit",
                   (Vector2){40, (float)(mh - 30)}, 12, 1, (Color){80,90,110,200});

        bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
        if (clicked && hovered >= 0) {
            if (selected == hovered) {
                /* Second click on same item → confirm */
                char *result = (char*)malloc(strlen(models[selected].path) + 1);
                if (result) strcpy(result, models[selected].path);
                CloseWindow();
                return result;
            }
            selected = hovered;
        }

        scroll -= (int)GetMouseWheelMove();
        if (scroll < 0) scroll = 0;
        if (scroll > n_models - 1) scroll = n_models - 1;
        if (scroll < 0) scroll = 0;

        EndDrawing();

        /* Enter key also confirms */
        if (selected >= 0 && IsKeyPressed(KEY_ENTER)) {
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

bool ui_render_is_octagon_held(void) {
    return s_octagon_held;
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
    draw_chat_panel();
    draw_footer();
    draw_emergency();
    draw_transcript();
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
    if (!role || !text) return;
    ui_lock();
    int idx = (s_chat_head + s_chat_count) % MAX_CHAT;
    strncpy(s_chat_role[idx], role, sizeof(s_chat_role[0]) - 1);
    strncpy(s_chat_text[idx], text, sizeof(s_chat_text[0]) - 1);
    strncpy(s_chat_src[idx], source_icon ? source_icon : "", sizeof(s_chat_src[0]) - 1);
    if (s_chat_count < MAX_CHAT) s_chat_count++;
    else s_chat_head = (s_chat_head + 1) % MAX_CHAT;
    ui_unlock();
}

void ui_render_set_context_label(const char *label) {
    strncpy(s_contextLabel, label, sizeof(s_contextLabel) - 1);
}

void ui_render_set_profile_label(const char *label) {
    strncpy(s_profileLabel, label, sizeof(s_profileLabel) - 1);
}

void ui_render_set_transcript(const char *text) {
    ui_lock();
    strncpy((char*)s_transcript, text ? text : "", sizeof(s_transcript) - 1);
    ui_unlock();
}

void ui_render_set_task_strip(const char *tasks) {
    (void)tasks;
}

void ui_render_set_theme_float(const char *key, float value) {
    (void)key; (void)value;
}
