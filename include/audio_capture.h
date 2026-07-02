/* audio_capture.h — miniaudio.h based microphone capture */
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H
#include <stdbool.h>

bool  audio_capture_start(void);
bool  audio_capture_is_initialised(void);
void  audio_capture_stop(void);
float audio_capture_rms(void);           /* current amplitude for orb morph */
int   audio_capture_read(float *buf, int max_samples);

#endif /* AUDIO_CAPTURE_H */
