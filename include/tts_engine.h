#ifndef TTS_ENGINE_H
#define TTS_ENGINE_H

#include <stdbool.h>

bool tts_engine_init(void);
void tts_engine_speak_async(const char *text);
bool tts_engine_is_speaking(void);
void tts_engine_stop(void);
void tts_engine_shutdown(void);

#endif
