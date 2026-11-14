/* camsim: fake rear camera. 30 fps frames into shared memory /camsim.
 * Source: a 1280x720 BGRA still; we zoom in slowly so it looks like reversing.
 * Row 0 carries the frame number (CAM_TAG_BITS pixels) for the screen probe.
 */
#include "../common/camshm.h"
#include "../common/evlog.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SRC_W 1280
#define SRC_H 720

int main(int argc, char **argv)
{
    const char *src_path = argc > 1 ? argv[1] : "/data/qc/assets/cam.bgra";
    static uint32_t src[SRC_W * SRC_H];
    FILE *f = fopen(src_path, "rb");
    if (!f || fread(src, 4, SRC_W * SRC_H, f) != SRC_W * SRC_H) { perror(src_path); return 1; }
    fclose(f);

    int fd = shm_open(CAMSHM_NAME, O_RDWR | O_CREAT, 0666);
    if (fd < 0 || ftruncate(fd, sizeof(camshm_t)) < 0) { perror("shm"); return 1; }
    camshm_t *m = mmap(NULL, sizeof *m, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    m->w = CAM_W; m->h = CAM_H;

    set_prio(50);
    evlog("camsim", "ready");
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    uint32_t seq = m->seq;
    for (;;) {
        seq++;
        uint32_t b = 1 - m->idx;
        uint32_t *dst = m->px[b];
        /* zoom 1.00 -> 1.15 over 8 s, then start over */
        double z = 1.0 + 0.15 * ((seq % 240) / 240.0);
        int sw = (int)(SRC_W / z), sh = (int)(SRC_H / z);
        int sx0 = (SRC_W - sw) / 2, sy0 = (SRC_H - sh) / 2 + (int)(10 * (z - 1.0));
        for (int y = 0; y < CAM_H; y++) {
            const uint32_t *srow = src + (sy0 + y * sh / CAM_H) * SRC_W + sx0;
            uint32_t *drow = dst + y * CAM_W;
            for (int x = 0; x < CAM_W; x++) drow[x] = srow[x * sw / CAM_W] | 0xff000000u;
        }
        for (int i = 0; i < CAM_TAG_BITS; i++) dst[i] = (seq >> i & 1) ? 0xffffffffu : 0xff000000u;
        m->t_ns[b] = now_ns();
        __sync_synchronize();
        m->idx = b;
        m->seq = seq;

        next.tv_nsec += 33333333;
        if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
}
