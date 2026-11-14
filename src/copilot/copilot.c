/* copilot: stands in for the on-device voice assistant (LLM inference).
 * While vbus ai_on == 1 it runs N worker threads of dense float math at a
 * low priority (10) and logs throughput, so we can show it was not starved.
 * usage: copilot [threads]
 */
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define N 96
static volatile int on;
static volatile unsigned long long ops;

static void *worker(void *arg)
{
    (void)arg;
    struct sched_param p = { .sched_priority = 10 };
    pthread_setschedparam(pthread_self(), SCHED_RR, &p);
    static __thread float a[N][N], b[N][N], c[N][N];
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { a[i][j] = (i + j) * 0.01f; b[i][j] = (i - j) * 0.02f; }
    for (;;) {
        if (!on) { usleep(20000); continue; }
        for (int i = 0; i < N; i++)
            for (int k = 0; k < N; k++) {
                float aik = a[i][k];
                for (int j = 0; j < N; j++) c[i][j] += aik * b[k][j];
            }
        a[0][0] = c[1][1] * 1e-9f;
        __sync_fetch_and_add(&ops, 1);
    }
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 4;
    for (int i = 0; i < n; i++) { pthread_t t; pthread_create(&t, NULL, worker, NULL); }
    int fd = vbus_open();
    vbus_state_t s;
    vbus_get(fd, &s);
    on = s.ai_on;
    evlog("copilot", "ready threads=%d", n);
    unsigned long long last = 0;
    uint64_t t_last = now_ns();
    for (;;) {
        /* poll the bus once a second; the wait would block the report */
        usleep(1000000);
        vbus_get(fd, &s);
        if (s.ai_on != on) evlog("copilot", "ai_on=%d", s.ai_on);
        on = s.ai_on;
        uint64_t t = now_ns();
        unsigned long long o = ops;
        if (on) evlog("copilot", "matmul_per_s=%.1f", (o - last) / ((t - t_last) / 1e9));
        last = o; t_last = t;
    }
}
