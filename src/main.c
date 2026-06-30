#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/winalp.h"
#include "../include/logger.h"
#include "../include/ui_render.h"

int main(void) {
    winalp_log_init("winalp.log");
    winalp_log(WINALP_LOG_INFO, "WinAlp v%d.%d.%d starting...",
               WINALP_VERSION_MAJOR, WINALP_VERSION_MINOR, WINALP_VERSION_PATCH);

    ui_render_init(1280, 720, "WinAlp");

    while (!ui_render_should_close()) {
        ui_render_frame(AGENT_STATE_LISTENING, 0.0f);
    }

    ui_render_shutdown();
    winalp_log(WINALP_LOG_INFO, "WinAlp shutdown complete");
    winalp_log_shutdown();
    return 0;
}
