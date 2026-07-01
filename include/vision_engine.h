/* vision_engine.h — DXGI Desktop Duplication + OCR wrapper */
#ifndef VISION_ENGINE_H
#define VISION_ENGINE_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*OcrResultCallback)(const char *text, void *userdata);

bool vision_engine_init(void);
void vision_engine_start_capture(OcrResultCallback cb, void *ud);
void vision_engine_stop_capture(void);
void vision_engine_poll(void);
void vision_engine_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_ENGINE_H */
