/* One-line event log shared by all processes: "<t_ns> <proc> <event> [args]". */
#ifndef EVLOG_H
#define EVLOG_H
#include <stdint.h>

#define EVLOG_PATH "/data/qc/run/events.log"

uint64_t now_ns(void);
void evlog(const char *proc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Set SCHED_FIFO priority of the calling thread (QNX: 1..255). */
void set_prio(int prio);

#endif
