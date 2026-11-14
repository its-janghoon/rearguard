/* infotainment: stands in for the big, slow, often-updated app.
 *
 * It takes a while to start (like a real head unit framework), shows one
 * line of assistant text while driving, and can be a broken build:
 * if /data/qc/info/<slot>/BAD exists it crashes during start-up.
 *
 * --with-camera links the rear camera into this process, the way the
 * recalled cars are built. Then the camera is only as alive as this app.
 */
#include "../common/evlog.h"
#include "../common/vbus.h"
#include "../rvc/rvc.h"
#include "../ui/lvscreen.h"
#include "../ui/widgets.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BOOT_MS 6000

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static vbus_state_t latest;
static int have;
static lv_obj_t *row, *text;

static void *reader(void *arg)
{
    (void)arg;
    set_prio(20);
    int fd = vbus_open();
    vbus_state_t s;
    vbus_get(fd, &s);
    for (;;) {
        pthread_mutex_lock(&mu);
        latest = s; have = 1;
        pthread_mutex_unlock(&mu);
        vbus_wait(fd, s.seq, &s);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    static uint32_t seen = ~0u;
    pthread_mutex_lock(&mu);
    vbus_state_t s = latest;
    int h = have;
    pthread_mutex_unlock(&mu);
    if (!h || s.seq == seen) return;
    seen = s.seq;
    int on = s.awake && s.gear == GEAR_D;
    lvs_set_visible(on);
    lv_label_set_text(text, s.ai_on ? "Planning your route..." : "Next charger 42 km ahead");
}

int main(int argc, char **argv)
{
    const char *slot = "a";
    int with_camera = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--slot") && i + 1 < argc) slot = argv[++i];
        else if (!strcmp(argv[i], "--with-camera")) with_camera = 1;
    }
    char bad[128];
    snprintf(bad, sizeof bad, "/data/qc/info/%s/BAD", slot);
    struct stat st;
    int broken = stat(bad, &st) == 0;
    evlog("infotainment", "start slot=%s camera=%d broken=%d", slot, with_camera, broken);

    /* framework start-up: busy, single core, like a heavy UI stack */
    uint64_t t_end = now_ns() + BOOT_MS * 1000000ull;
    volatile double x = 1.0;
    while (now_ns() < t_end) {
        for (int i = 0; i < 200000; i++) x = x * 1.0000001 + 0.000001;
        usleep(2000);
        if (broken && now_ns() > t_end - (BOOT_MS - 1500) * 1000000ull) {
            evlog("infotainment", "crash slot=%s", slot);
            abort();
        }
    }

    set_prio(20);
    lv_display_t *d = lvs_create(520, 588, 880, 40, 10);
    if (!d) return 1;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080b), 0);
    row = box(scr, 0, 0, 880, 40, 0x05080b, 0);
    lv_obj_t *dot = box(row, 0, 15, 10, 10, C_AI, 5);
    text = label(row, &f_24, 0xdfe6ec, "Next charger 42 km ahead");
    lv_obj_align(text, LV_ALIGN_CENTER, 12, 0);
    lv_obj_align_to(dot, text, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lvs_set_visible(0);

    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
    lv_timer_create(tick, 50, NULL);
    if (with_camera) rvc_start("infotainment-rvc");
    evlog("infotainment", "ready slot=%s", slot);
    lvs_run();
}
