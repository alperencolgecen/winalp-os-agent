#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../include/winalp.h"
#include "../include/logger.h"
#include "../include/ui_render.h"

/* VAD states for mock speech cycle demo */
enum VadState {
    VAD_IDLE,
    VAD_IN_SPEECH,
    VAD_PROCESSING,
    VAD_RESPONDING
};

int main(void) {
    winalp_log_init("winalp.log");
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    ui_render_init(1280, 720, "WinAlp AI Assistant");

    double mockTime = 0.0;

    AgentState   state     = AGENT_STATE_LISTENING;
    enum VadState vad       = VAD_IDLE;
    double       speechEnd = 0.0;
    double       thinkEnd  = 0.0;

    while (!ui_render_should_close()) {
        double dt  = 1.0 / 60.0;
        mockTime  += dt;

        /* Mock audio RMS envelope (replaced by mic in Stage 31) */
        float amplitude = 0.5f + 0.5f * (float)sin(mockTime * 2.5f);

        /* ---------- VAD state machine ---------- */
        switch (vad) {

        case VAD_IDLE:
            state = AGENT_STATE_LISTENING;
            if (amplitude > 0.60f) {
                /* Speech burst detected */
                vad       = VAD_IN_SPEECH;
                speechEnd = mockTime + 1.5;
            }
            break;

        case VAD_IN_SPEECH:
            state = AGENT_STATE_LISTENING;
            if (mockTime >= speechEnd) {
                /* Speech ended → start thinking */
                vad      = VAD_PROCESSING;
                thinkEnd = mockTime + 2.0;
            }
            break;

        case VAD_PROCESSING:
            state = AGENT_STATE_THINKING;
            if (mockTime >= thinkEnd)
                vad = VAD_RESPONDING;  /* → writing */
            break;

        case VAD_RESPONDING:
            state = AGENT_STATE_WRITING;
            if (amplitude < 0.40f)
                vad = VAD_IDLE;  /* back to listen */
            break;
        }

        ui_render_frame(state, amplitude);
    }

    ui_render_shutdown();
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
