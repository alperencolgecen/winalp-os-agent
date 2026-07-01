#ifndef THREAD_MUTEX_H
#define THREAD_MUTEX_H

#include <windows.h>

typedef struct {
    CRITICAL_SECTION cs;
} ThreadMutex;

void thread_mutex_init(ThreadMutex *m);
void thread_mutex_lock(ThreadMutex *m);
void thread_mutex_unlock(ThreadMutex *m);
void thread_mutex_destroy(ThreadMutex *m);

#endif
