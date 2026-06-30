/* ============================================================
 * WinAlp — Thread-Safe Logger
 * File   : include/logger.h
 * ============================================================ */
#ifndef WINALP_LOGGER_H
#define WINALP_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WINALP_LOG_DEBUG = 0,
    WINALP_LOG_INFO  = 1,
    WINALP_LOG_WARN  = 2,
    WINALP_LOG_ERROR = 3
} LogLevel;

void winalp_log(LogLevel level, const char *fmt, ...);
void winalp_log_init(const char *log_file_path); /* NULL = stderr only */
void winalp_log_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* WINALP_LOGGER_H */
