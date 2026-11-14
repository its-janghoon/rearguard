#include "evlog.h"
#include "vbus.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void evlog(const char *proc, const char *fmt, ...)
{
    static int fd = -1;
    if (fd < 0) fd = open(EVLOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    char line[512];
    int n = snprintf(line, sizeof line, "%llu %s ", (unsigned long long)now_ns(), proc);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = sizeof line - 2;
    line[n++] = '\n';
    if (fd >= 0) (void)!write(fd, line, n); /* one write = one atomic line */
}

void set_prio(int prio)
{
    struct sched_param p = { .sched_priority = prio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
}

#ifdef __QNXNTO__
int vbus_open(void)
{
    for (int i = 0; i < 200; i++) { /* vbusd may still be starting */
        int fd = open(VBUS_PATH, O_RDWR);
        if (fd >= 0) return fd;
        usleep(10000);
    }
    return -1;
}

int vbus_get(int fd, vbus_state_t *s)
{
    return devctl(fd, VB_GET, s, sizeof *s, NULL);
}

int vbus_put(int fd, uint32_t mask, const vbus_state_t *s)
{
    vbus_put_t p = { .mask = mask, .s = *s };
    return devctl(fd, VB_PUT, &p, sizeof p, NULL);
}

int vbus_wait(int fd, uint32_t seen_seq, vbus_state_t *s)
{
    union { vbus_wait_t w; vbus_state_t s; } u;
    memset(&u, 0, sizeof u);
    u.w.seen_seq = seen_seq;
    int rc = devctl(fd, VB_WAIT, &u, sizeof u, NULL);
    if (rc == EOK) *s = u.s;
    return rc;
}
#endif
