/* rvc: rear view camera presenter.
 *
 * Owns its own Screen window, top of the z-order, over the cluster's
 * camera area. Waits on /dev/vbus for gear R (reply-blocked in vbusd),
 * then copies camsim frames, draws guide lines and posts.
 * FMVSS 111: show within 2.0 s of R (S5.5.3), stay up while in R
 * (S5.5.5), take it down when R ends (S5.5.4). Nothing but guide lines
 * is drawn over the image.
 *
 * Built twice: as the `rvc` process, and linked into infotainment
 * (rvc_start) for the "camera inside infotainment" comparison.
 */
#include "rvc.h"
#include "../common/camshm.h"
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <screen/screen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define WIN_X 520
#define WIN_Y 86

static const char *who = "rvc";
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int want_show;          /* gear is R */
static int steer;
static uint64_t t_gear;        /* when vbusd saw the gear change */
static uint64_t t_seen;        /* when we got the reply */

static void put(uint32_t *px, int stride, int x, int y, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < CAM_W && yy >= 1 && yy < CAM_H) px[yy * stride + xx] = c;
        }
}

/* Guide lines: a pair of curves bent by the steering angle, with
 * red / amber / green distance marks (about 0.5 m / 1 m / 2 m). */
static void draw_guides(uint32_t *px, int stride, int steer_deg)
{
    const double bend = steer_deg / 40.0 * 120.0;
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i <= 200; i++) {
            double t = i / 200.0;                 /* 0 = bottom, 1 = far end */
            double y = CAM_H - t * (CAM_H - 236);
            double half = 290.0 - t * 210.0;      /* lines converge with distance */
            double x = CAM_W / 2.0 + side * half + bend * t * t;
            put(px, stride, (int)x, (int)y, 3, 0xffffd23fu);
        }
        const struct { double t; uint32_t c; } marks[] = { {0.30, 0xffff4d4fu}, {0.62, 0xffffb020u}, {0.92, 0xff3ddc84u} };
        for (int k = 0; k < 3; k++) {
            double t = marks[k].t, y = CAM_H - t * (CAM_H - 236), half = 290.0 - t * 210.0;
            double x = CAM_W / 2.0 + side * half + bend * t * t;
            for (int d = 0; d < 34 - 10 * k; d++) put(px, stride, (int)(x - side * d), (int)y, 3 - (k > 1), marks[k].c);
        }
    }
}

static void *waiter(void *arg)
{
    (void)arg;
    set_prio(55);
    int fd = vbus_open();
    if (fd < 0) { evlog(who, "error vbus_open"); return NULL; }
    vbus_state_t s;
    vbus_get(fd, &s);
    for (;;) {
        pthread_mutex_lock(&mu);
        int r = s.gear == GEAR_R;
        if (r && !want_show) { t_gear = s.t_gear_ns; t_seen = now_ns(); }
        want_show = r;
        steer = s.steer_deg;
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&mu);
        if (vbus_wait(fd, s.seq, &s) != 0) usleep(1000);
    }
}

static void *presenter(void *arg)
{
    (void)arg;
    set_prio(54);
    int fd = -1;
    camshm_t *m = MAP_FAILED;
    while (m == MAP_FAILED) {     /* camsim may start after us */
        if (fd < 0) fd = shm_open(CAMSHM_NAME, O_RDONLY, 0);
        if (fd >= 0) m = mmap(NULL, sizeof *m, PROT_READ, MAP_SHARED, fd, 0);
        if (m == MAP_FAILED) usleep(20000);
    }

    screen_context_t ctx;
    screen_window_t win;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) ||
        screen_create_window(&win, ctx)) { evlog(who, "error screen"); return NULL; }
    int pos[2] = { WIN_X, WIN_Y }, size[2] = { CAM_W, CAM_H };
    int usage = SCREEN_USAGE_WRITE, fmt = SCREEN_FORMAT_RGBA8888, z = 100, vis = 0;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &fmt);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);
    int tr = SCREEN_TRANSPARENCY_NONE;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_TRANSPARENCY, &tr);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &vis);
    screen_create_window_buffers(win, 2);
    screen_buffer_t bufs[2];
    screen_get_window_property_pv(win, SCREEN_PROPERTY_BUFFERS, (void **)bufs);
    int stride;
    screen_get_buffer_property_iv(bufs[0], SCREEN_PROPERTY_STRIDE, &stride);
    void *ptr[2];
    screen_get_buffer_property_pv(bufs[0], SCREEN_PROPERTY_POINTER, &ptr[0]);
    screen_get_buffer_property_pv(bufs[1], SCREEN_PROPERTY_POINTER, &ptr[1]);
    evlog(who, "ready");

    int shown = 0, bi = 0, frames = 0;
    uint32_t last_seq = 0;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!want_show && !shown) pthread_cond_wait(&cv, &mu);
        int show = want_show, st = steer;
        uint64_t tg = t_gear, ts = t_seen;
        pthread_mutex_unlock(&mu);

        if (!show) {               /* backing event over: take the image down */
            vis = 0;
            screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &vis);
            screen_flush_context(ctx, 0);
            evlog(who, "hide frames=%d", frames);
            shown = 0;
            continue;
        }
        uint32_t seq = m->seq;
        if (shown && seq == last_seq) { usleep(2000); continue; }
        uint32_t idx = m->idx;
        uint32_t *dst = ptr[bi];
        int spx = stride / 4;
        for (int y = 0; y < CAM_H; y++) memcpy(dst + y * spx, m->px[idx] + y * CAM_W, CAM_W * 4);
        draw_guides(dst, spx, st);
        int rect[4] = { 0, 0, CAM_W, CAM_H };
        screen_post_window(win, bufs[bi], 1, rect, 0);
        bi ^= 1;
        last_seq = seq;
        frames++;
        if (!shown) {
            vis = 1;
            screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &vis);
            screen_flush_context(ctx, 0);
            uint64_t tp = now_ns();
            evlog(who, "first_post gear_ns=%llu seen_ns=%llu cam_seq=%u r_to_post_ms=%.1f",
                  (unsigned long long)tg, (unsigned long long)ts, seq, (tp - tg) / 1e6);
            shown = 1;
            frames = 1;
        }
    }
}

int rvc_start(const char *name)
{
    who = name;
    pthread_t a, b;
    pthread_create(&a, NULL, waiter, NULL);
    pthread_create(&b, NULL, presenter, NULL);
    return 0;
}

#ifndef RVC_EMBED
int main(void)
{
    rvc_start("rvc");
    for (;;) pause();
}
#endif
