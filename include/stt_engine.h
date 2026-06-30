/* stt_engine.h — whisper.cpp wrapper (audio → text) */
#ifndef STT_ENGINE_H
#define STT_ENGINE_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TranscriptCallback)(const char *text, void *userdata);

bool stt_engine_load(const char *model_path);
bool stt_engine_is_loaded(void);
void stt_engine_process(const float *pcm, int n_samples, TranscriptCallback cb, void *ud);
void stt_engine_unload(void);

#ifdef __cplusplus
}
#endif

#endif /* STT_ENGINE_H */
