#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdbool.h>

bool thread_pool_start_all(void);
bool thread_pool_stop_all(void);
bool thread_pool_is_running(void);

/* Push text input (keyboard) to the AI processing pipeline */
void thread_pool_send_text(const char *text);

/* Message queues consumed by UI thread */
bool thread_pool_get_transcript(char *out, int out_len);
bool thread_pool_get_ai_response(char *out, int out_len);
bool thread_pool_get_vision_text(char *out, int out_len);

#endif
