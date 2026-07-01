#include "../include/thread_mutex.h"

void thread_mutex_init(ThreadMutex *m) {
    InitializeCriticalSection(&m->cs);
}

void thread_mutex_lock(ThreadMutex *m) {
    EnterCriticalSection(&m->cs);
}

void thread_mutex_unlock(ThreadMutex *m) {
    LeaveCriticalSection(&m->cs);
}

void thread_mutex_destroy(ThreadMutex *m) {
    DeleteCriticalSection(&m->cs);
}
