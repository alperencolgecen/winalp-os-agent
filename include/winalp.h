/* ============================================================
 * WinAlp — Core Type Definitions & Global Declarations
 * File   : include/winalp.h
 * ============================================================ */
#ifndef WINALP_H
#define WINALP_H

#include <stdint.h>
#include <stdbool.h>

/* Version */
#define WINALP_VERSION_MAJOR 0
#define WINALP_VERSION_MINOR 1
#define WINALP_VERSION_PATCH 0

/* Target frame rate */
#define WINALP_TARGET_FPS    60

/* ── Agent state (drives holographic UI) ─────────────────── */
typedef enum {
    AGENT_STATE_LISTENING  = 0,  /* Cyan breathing orb           */
    AGENT_STATE_THINKING   = 1,  /* Indigo orbital rings         */
    AGENT_STATE_WRITING    = 2,  /* Teal token flash             */
    AGENT_STATE_ACTING     = 3   /* Amber hexagon grid           */
} AgentState;

/* ── Thread-safe message types ──────────────────────────────*/
typedef enum {
    MSG_STT_RESULT    = 0,
    MSG_LLM_TOKEN     = 1,
    MSG_LLM_ACTION    = 2,
    MSG_OCR_RESULT    = 3,
    MSG_PROFILE_UPDATE= 4
} MessageType;

typedef struct {
    MessageType type;
    char        payload[4096];
} WinAlpMessage;

#endif /* WINALP_H */
