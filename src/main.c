/* ============================================================
 * WinAlp — Main Entry Point
 * File   : src/main.c
 * Role   : Application bootstrap, thread orchestration, main loop
 * ============================================================ */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/winalp.h"
#include "../include/logger.h"

int main(void) {
    winalp_log(LOG_INFO, "WinAlp starting...");
    /* TODO: Stage 39 — model selector screen */
    /* TODO: Stage 41 — spin up LLM thread    */
    /* TODO: Stage 31 — spin up audio thread  */
    /* TODO: Stage 34 — spin up STT thread    */
    /* TODO: Stage 6  — Raylib/ImGui loop     */
    return 0;
}
