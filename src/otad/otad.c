/* otad: over-the-air update agent for the infotainment slot.
 * Installs into slot b while the car charges (disk + CPU load on purpose),
 * then asks the supervisor to restart into b.
 * --bad installs a build that crashes on start (the Jeep 4xe case).
 */
#include "../common/evlog.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy(const char *from, const char *to)
{
    int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (in < 0 || out < 0) return -1;
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0) if (write(out, buf, n) != n) return -1;
    close(in); close(out);
    return 0;
}

int main(int argc, char **argv)
{
    int bad = argc > 1 && !strcmp(argv[1], "--bad");
    int mb = 300;
    set_prio(5);
    evlog("otad", "install_begin slot=b bad=%d size_mb=%d", bad, mb);
    mkdir("/data/qc/info/b", 0755);
    /* "download + unpack": write and checksum a large payload */
    static char blk[1 << 20];
    int fd = open("/data/qc/info/b/payload.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    unsigned sum = 0;
    for (int i = 0; i < mb; i++) {
        for (size_t j = 0; j < sizeof blk; j++) { blk[j] = (char)(i * 131 + j * 7); sum += (unsigned char)blk[j]; }
        if (fd >= 0) (void)!write(fd, blk, sizeof blk);
    }
    if (fd >= 0) { fsync(fd); close(fd); }
    if (copy("/data/qc/info/a/infotainment", "/data/qc/info/b/infotainment")) { evlog("otad", "error copy"); return 1; }
    if (bad) { int b = open("/data/qc/info/b/BAD", O_WRONLY | O_CREAT, 0644); close(b); }
    else unlink("/data/qc/info/b/BAD");
    FILE *f = fopen("/data/qc/info/next", "w");
    if (f) { fputs("b", f); fclose(f); }
    evlog("otad", "install_done sum=%u next=b", sum);

    f = fopen("/data/qc/run/supervisor.pid", "r");
    int pid = 0;
    if (f) { if (fscanf(f, "%d", &pid) != 1) pid = 0; fclose(f); }
    if (pid > 0) kill(pid, SIGUSR1);
    return 0;
}
