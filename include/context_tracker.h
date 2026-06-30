/* context_tracker.h — Win32 active window / app tracker */
#ifndef CONTEXT_TRACKER_H
#define CONTEXT_TRACKER_H

void context_tracker_poll(char *out_label, int out_len);
/* e.g. out_label = "VSCode — main.c | WinAlp project" */

#endif /* CONTEXT_TRACKER_H */
