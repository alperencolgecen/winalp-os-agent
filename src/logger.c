/* logger.c — Thread-safe logger implementation */
#include "../include/logger.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static CRITICAL_SECTION s_cs;
static FILE             *s_file = NULL;
static int               s_initialized = 0;

static const char *level_str[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };
static const char *level_color[] = { "\033[90m", "\033[36m", "\033[33m", "\033[31m" };
#define COLOR_RESET "\033[0m"

void winalp_log_init(const char *log_file_path) {
    InitializeCriticalSection(&s_cs);
    if (log_file_path)
        s_file = fopen(log_file_path, "a");
    s_initialized = 1;
}

void winalp_log(LogLevel level, const char *fmt, ...) {
    if (!s_initialized) {
        InitializeCriticalSection(&s_cs);
        s_initialized = 1;
    }
    EnterCriticalSection(&s_cs);

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    va_list args;
    va_start(args, fmt);

    /* Colorised stderr */
    fprintf(stderr, "%s[%s][%s] ", level_color[level], time_buf, level_str[level]);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "%s\n", COLOR_RESET);

    /* Plain file log */
    if (s_file) {
        va_end(args);
        va_start(args, fmt);
        fprintf(s_file, "[%s][%s] ", time_buf, level_str[level]);
        vfprintf(s_file, fmt, args);
        fprintf(s_file, "\n");
        fflush(s_file);
    }

    va_end(args);
    LeaveCriticalSection(&s_cs);
}

void winalp_log_shutdown(void) {
    if (s_file) { fclose(s_file); s_file = NULL; }
    DeleteCriticalSection(&s_cs);
}
