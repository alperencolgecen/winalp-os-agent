/* ============================================================
 * WinAlp — Thread-Safe Logger
 * File   : include/logger.h
 * ============================================================ */
#ifndef WINALP_LOGGER_H
#define WINALP_LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

void winalp_log(LogLevel level, const char *fmt, ...);
void winalp_log_init(const char *log_file_path); /* NULL = stderr only */
void winalp_log_shutdown(void);

#endif /* WINALP_LOGGER_H */
