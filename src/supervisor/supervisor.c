/* supervisor: keeps infotainment alive, handles A/B slots and rollback.
 *
 * - starts /data/qc/info/<slot>/infotainment, slot from /data/qc/info/next
 * - a crash within 10 s of start counts as a failed start; 3 failed starts
 *   on slot b -> write next=a and log a rollback
 * - vehicle asleep -> infotainment stopped (power save); awake -> started
 * - SIGUSR1 (from otad) -> restart into whatever `next` says now
 */
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static volatile sig_atomic_t restart_req;
static volatile int awake = 1;
static int camera_in_info;

static void on_usr1(int s) { (void)s; restart_req = 1; }

static void read_next(char *slot)
{
    FILE *f = fopen("/data/qc/info/next", "r");
    slot[0] = 'a'; slot[1] = 0;
    if (f) { int c = fgetc(f); if (c == 'a' || c == 'b') slot[0] = (char)c; fclose(f); }
}

static void write_next(const char *slot)
{
    FILE *f = fopen("/data/qc/info/next", "w");
    if (f) { fputs(slot, f); fclose(f); }
}

static void *watch_awake(void *arg)
{
    (void)arg;
    int fd = vbus_open();
    vbus_state_t s;
    vbus_get(fd, &s);
    for (;;) { awake = s.awake; if (vbus_wait(fd, s.seq, &s) != 0) usleep(5000); }
}

static pid_t start(const char *slot)
{
    char path[128];
    snprintf(path, sizeof path, "/data/qc/info/%s/infotainment", slot);
    char *argv[] = { path, "--slot", (char *)slot, camera_in_info ? "--with-camera" : NULL, NULL };
    pid_t pid;
    if (posix_spawn(&pid, path, NULL, NULL, argv, environ) != 0) { evlog("supervisor", "spawn_failed %s", path); return -1; }
    evlog("supervisor", "started slot=%s pid=%d", slot, pid);
    return pid;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--camera-in-info")) camera_in_info = 1;
    FILE *pf = fopen("/data/qc/run/supervisor.pid", "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }
    signal(SIGUSR1, on_usr1);
    set_prio(30);
    pthread_t th;
    pthread_create(&th, NULL, watch_awake, NULL);

    char slot[2];
    int fails_b = 0;
    pid_t child = -1;
    uint64_t t_start = 0;
    for (;;) {
        if (child < 0 && awake) {
            read_next(slot);
            child = start(slot);
            t_start = now_ns();
        }
        if (child > 0 && (!awake || restart_req)) {
            evlog("supervisor", restart_req ? "restart_for_update" : "sleep_stop");
            restart_req = 0;
            kill(child, SIGTERM);
            waitpid(child, NULL, 0);
            child = -1;
            continue;
        }
        if (child > 0) {
            int st;
            pid_t r = waitpid(child, &st, WNOHANG);
            if (r == child) {
                double up = (now_ns() - t_start) / 1e9;
                evlog("supervisor", "exited slot=%s up_s=%.1f signal=%d", slot, up, WIFSIGNALED(st) ? WTERMSIG(st) : 0);
                child = -1;
                if (up < 10.0 && slot[0] == 'b' && ++fails_b >= 3) {
                    write_next("a");
                    fails_b = 0;
                    evlog("supervisor", "rollback to=a after=3_failed_starts");
                }
                usleep(300000);
                continue;
            }
        }
        usleep(50000);
    }
}
