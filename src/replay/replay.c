/* replay (host, Linux): re-render the QNX cluster from a recorded events.log.
 *
 * Uses the same cluster.c UI code with LVGL drawing into memory. For each
 * output frame time it applies the last "vbusd state" line at or before that
 * time and writes one raw 1920x720 BGRA frame to stdout.
 *
 * usage: replay events.log fps t_from_s t_to_s > frames.raw
 *        (times are seconds after the first line of the log)
 */
#include "../common/vbus.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cluster_build(void);
void cluster_set(const vbus_state_t *s);

#define W 1920
#define H 720

static uint32_t now_ms;
static uint32_t ticks(void) { return now_ms; }
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

typedef struct { double t; vbus_state_t s; } ev_t;

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: replay events.log fps from_s to_s\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    double fps = atof(argv[2]), from = atof(argv[3]), to = atof(argv[4]);

    static ev_t ev[200000];
    int n = 0;
    char line[600];
    double t0 = -1;
    while (fgets(line, sizeof line, f) && n < 200000) {
        unsigned long long ns;
        char proc[32], what[32];
        if (sscanf(line, "%llu %31s %31s", &ns, proc, what) != 3) continue;
        double t = ns / 1e9;
        if (t0 < 0) t0 = t;
        if (strcmp(proc, "vbusd") || strcmp(what, "state")) continue;
        vbus_state_t s;
        memset(&s, 0, sizeof s);
        if (sscanf(line, "%*u %*s %*s seq=%u gear=%d speed=%d steer=%d awake=%d charge=%d soc=%d range=%d belt=%d brake=%d ai=%d park=%d",
                   &s.seq, &s.gear, &s.speed_x10, &s.steer_deg, &s.awake, &s.charge_w, &s.soc, &s.range_km,
                   &s.seatbelt, &s.brake_fault, &s.ai_on, &s.park_dist_cm) == 12) {
            ev[n].t = t - t0; ev[n].s = s; n++;
        }
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "no vbusd state lines\n"); return 1; }

    lv_init();
    lv_tick_set_cb(ticks);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    static uint8_t buf[W * H * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);
    cluster_build();

    /* initial state before the first log line: vbusd defaults */
    vbus_state_t cur = ev[0].s;
    int k = 0;
    for (double t = from; t < to; t += 1.0 / fps) {
        while (k < n && ev[k].t <= t) cur = ev[k++].s;
        now_ms = (uint32_t)(t * 1000);
        cluster_set(&cur);
        lv_refr_now(d);
        fwrite(buf, 1, sizeof buf, stdout);
    }
    return 0;
}
