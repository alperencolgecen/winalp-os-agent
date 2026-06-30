#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../include/winalp.h"
#include "../include/logger.h"
#include "../include/ui_render.h"

int main(void) {
    winalp_log_init("winalp.log");
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    ui_render_init(1280, 720, "WinAlp AI Assistant");

    double mockTime = 0.0;
    AgentState state = AGENT_STATE_LISTENING;

    while (!ui_render_should_close()) {
        mockTime += 1.0 / 60.0;

        /* Mock breathing amplitude (replaced by real mic RMS in Stage 31) */
        float amplitude = 0.5f + 0.5f * (float)sin(mockTime * 2.5f);

        /* Cycle through states for visual testing (remove in production) */
        /* int s = ((int)(mockTime * 0.25)) % 4;
        if (s == 0) state = AGENT_STATE_LISTENING;
        else if (s == 1) state = AGENT_STATE_THINKING;
        else if (s == 2) state = AGENT_STATE_WRITING;
        else state = AGENT_STATE_ACTING; */

        ui_render_frame(state, amplitude);
    }

    ui_render_shutdown();
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
