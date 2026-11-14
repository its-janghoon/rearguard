/* cluster: 12.3" instrument cluster, 1920x720, z-order 0 (bottom).
 * Speed, PRND and telltales are always visible (FMVSS 101). The camera
 * image itself belongs to rvc, a separate process with its own window
 * on top; this process only draws the caution line under it.
 */
#include "../common/evlog.h"
#include "../common/vbus.h"
#include "../ui/lvscreen.h"
#include "../ui/widgets.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define W 1920
#define H 720

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static vbus_state_t latest;
static int have;

static void *reader(void *arg)
{
    (void)arg;
    set_prio(40);
    int fd = vbus_open();
    vbus_state_t s;
    memset(&s, 0, sizeof s);
    vbus_get(fd, &s);
    for (;;) {
        pthread_mutex_lock(&mu);
        latest = s; have = 1;
        pthread_mutex_unlock(&mu);
        vbus_wait(fd, s.seq, &s);
    }
}

static struct {
    lv_obj_t *all, *gear[4], *speed, *park, *park_rear, *range, *range_km, *range_bar, *range_note;
    lv_obj_t *road, *caution, *belt[3], *brake[3], *plug[3], *odo;
} ui;

static void make_telltales(lv_obj_t *p)
{
    int x0 = W / 2 - 44 / 2 - 84, y0 = 618;
    /* seatbelt: head, body, belt */
    ui.belt[0] = box(p, x0 + 16, y0 + 2, 12, 12, C_OFF, 6);
    static lv_point_precise_t body[] = { {14, 42}, {16, 20}, {22, 16}, {28, 20}, {30, 42} };
    static lv_point_precise_t strap[] = { {12, 18}, {32, 36} };
    ui.belt[1] = line(p, body, 5, 4, C_OFF); lv_obj_set_pos(ui.belt[1], x0, y0);
    ui.belt[2] = line(p, strap, 2, 4, C_OFF); lv_obj_set_pos(ui.belt[2], x0, y0);
    /* brake system: circle with ! and side brackets */
    int x1 = x0 + 84;
    ui.brake[0] = arc(p, x1 + 22, y0 + 22, 12, 0, 360, 4, C_OFF);
    ui.brake[1] = arc(p, x1 + 22, y0 + 22, 20, 130, 230, 4, C_OFF);
    ui.brake[2] = label(p, &f_22, C_OFF, "!");
    lv_obj_set_pos(ui.brake[2], x1 + 19, y0 + 9);
    lv_obj_t *b2 = arc(p, x1 + 22, y0 + 22, 20, 310, 50, 4, C_OFF);
    lv_obj_set_user_data(ui.brake[1], b2);
    /* charging plug */
    int x2 = x1 + 84;
    static lv_point_precise_t cell[] = { {10, 8}, {28, 8}, {28, 38}, {10, 38}, {10, 8} };
    static lv_point_precise_t bolt[] = { {21, 13}, {16, 24}, {22, 24}, {17, 33} };
    static lv_point_precise_t cord[] = { {28, 18}, {36, 18}, {36, 32} };
    ui.plug[0] = line(p, cell, 5, 4, C_OFF); lv_obj_set_pos(ui.plug[0], x2, y0);
    ui.plug[1] = line(p, bolt, 4, 3, C_OFF); lv_obj_set_pos(ui.plug[1], x2, y0);
    ui.plug[2] = line(p, cord, 3, 4, C_OFF); lv_obj_set_pos(ui.plug[2], x2, y0);
}

static void make_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    ui.all = box(scr, 0, 0, W, H, 0x05080b, 0);
    lv_obj_set_style_bg_grad_color(ui.all, lv_color_hex(0x10161d), 0);
    lv_obj_set_style_bg_grad_dir(ui.all, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(ui.all, 255, 0);
    lv_obj_set_style_bg_grad_stop(ui.all, 0, 0);
    lv_obj_set_style_radius(ui.all, 60, 0);
    lv_obj_t *p = ui.all;

    lv_obj_t *t = label(p, &f_26, 0x9aa6b2, "07:02"); lv_obj_set_pos(t, 120, 26);
    t = label(p, &f_26, 0x9aa6b2, "12\xc2\xb0" "C"); lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -120, 26);

    const char *g[4] = { "P", "R", "N", "D" };
    for (int i = 0; i < 4; i++) {
        ui.gear[i] = label(p, &f_prnd, C_OFF, g[i]);
        lv_obj_set_pos(ui.gear[i], W / 2 - 98 + i * 52, 18);
    }

    /* left: rear park view (R) */
    ui.park = box(p, 70, 100, 420, 510, 0, 0);
    lv_obj_set_style_bg_opa(ui.park, LV_OPA_TRANSP, 0);
    arc(ui.park, 210, 170, 130, 225, 315, 16, 0x59636d);
    box(ui.park, 160, 210, 100, 200, 0xa8b1b9, 38);
    box(ui.park, 176, 250, 68, 54, 0x323b43, 12);
    box(ui.park, 176, 350, 68, 34, 0x323b43, 10);
    ui.park_rear = arc(ui.park, 210, 300, 150, 60, 120, 16, 0x59636d);
    arc(ui.park, 210, 300, 180, 62, 118, 12, 0x3a434c);

    /* left: range (D) */
    ui.range = box(p, 70, 100, 420, 510, 0, 0);
    lv_obj_set_style_bg_opa(ui.range, LV_OPA_TRANSP, 0);
    t = label(ui.range, &f_22, C_DIM, "Range"); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 120);
    ui.range_km = label(ui.range, &f_64, 0xffffff, "300 km"); lv_obj_align(ui.range_km, LV_ALIGN_TOP_MID, 0, 150);
    box(ui.range, 100, 250, 220, 16, 0x1d252d, 8);
    ui.range_bar = box(ui.range, 100, 250, 132, 16, C_GREEN, 8);
    ui.range_note = label(ui.range, &f_22, C_DIM, ""); lv_obj_align(ui.range_note, LV_ALIGN_TOP_MID, 0, 285);

    /* center: road (D) */
    ui.road = box(p, 610, 145, 700, 420, 0, 0);
    lv_obj_set_style_bg_opa(ui.road, LV_OPA_TRANSP, 0);
    static lv_point_precise_t l1[] = { {318, 20}, {120, 400} }, l2[] = { {382, 20}, {580, 400} };
    line(ui.road, l1, 2, 5, 0x6b747c);
    line(ui.road, l2, 2, 5, 0x6b747c);
    box(ui.road, 300, 290, 100, 70, 0xa8b1b9, 22);
    box(ui.road, 318, 130, 64, 44, 0x59636d, 14);

    /* center: caution under the camera (R). The image area above is rvc's window. */
    ui.caution = box(p, 520, 584, 880, 40, 0, 0);
    lv_obj_set_style_bg_opa(ui.caution, LV_OPA_TRANSP, 0);
    static lv_point_precise_t tri[] = { {13, 0}, {26, 24}, {0, 24}, {13, 0} };
    lv_obj_t *tr = line(ui.caution, tri, 4, 4, C_AMBER); lv_obj_set_pos(tr, 270, 8);
    t = label(ui.caution, &f_22, 0xc9d2db, "Check surroundings for safety"); lv_obj_set_pos(t, 308, 6);

    /* right: speed */
    ui.speed = label(p, &f_speed, 0xffffff, "0");
    lv_obj_align(ui.speed, LV_ALIGN_TOP_MID, 1640 - W / 2, 150);
    t = label(p, &f_26, C_DIM, "km/h"); lv_obj_align(t, LV_ALIGN_TOP_MID, 1640 - W / 2, 330);
    lv_obj_t *lim = box(p, 1605, 395, 70, 70, 0xffffff, 35);
    lv_obj_set_style_border_color(lim, lv_color_hex(0xe23b3b), 0);
    lv_obj_set_style_border_width(lim, 7, 0);
    t = label(lim, &f_26, 0x111111, "30"); lv_obj_center(t);

    ui.odo = label(p, &f_24, 0x9aa6b2, "ODO 12,408 km"); lv_obj_set_pos(ui.odo, 150, 640);
    t = label(p, &f_24, 0x9aa6b2, "AUTO HOLD"); lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -150, 640);
    make_telltales(p);
}

static void show(lv_obj_t *o, int on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void tint(lv_obj_t **o, int n, uint32_t c)
{
    for (int i = 0; i < n; i++) {
        set_color(o[i], c);
        lv_obj_t *extra = lv_obj_get_user_data(o[i]);
        if (extra) set_color(extra, c);
    }
}

static vbus_state_t shown_s;
static int first = 1;

static void tick(lv_timer_t *tm)
{
    (void)tm;
    vbus_state_t s;
    pthread_mutex_lock(&mu);
    if (!have) { pthread_mutex_unlock(&mu); return; }
    s = latest;
    pthread_mutex_unlock(&mu);
    if (!first && s.seq == shown_s.seq) return;

    show(ui.all, s.awake);
    int r = s.gear == GEAR_R;
    for (int i = 0; i < 4; i++) set_color(ui.gear[i], i == s.gear ? 0xffffff : C_OFF);
    show(ui.park, r); show(ui.caution, r);
    show(ui.range, !r); show(ui.road, s.gear == GEAR_D);
    set_color(ui.park_rear, s.park_dist_cm == 0 ? 0x59636d : s.park_dist_cm < 50 ? C_RED : s.park_dist_cm < 100 ? C_AMBER : C_GREEN);
    lv_label_set_text_fmt(ui.speed, "%d", (s.speed_x10 + 5) / 10);
    lv_label_set_text_fmt(ui.range_km, "%d km", s.range_km);
    lv_obj_set_width(ui.range_bar, 220 * s.soc / 100);
    if (s.charge_w > 0) lv_label_set_text_fmt(ui.range_note, "Charging %d.%d kW  \xc2\xb7  %d%%", s.charge_w / 1000, s.charge_w / 100 % 10, s.soc);
    else lv_label_set_text_fmt(ui.range_note, "Battery %d%%", s.soc);
    tint(ui.belt, 3, s.seatbelt ? C_OFF : C_RED);
    tint(ui.brake, 3, s.brake_fault ? C_RED : C_OFF);
    tint(ui.plug, 3, s.charge_w > 0 ? C_GREEN : C_OFF);

    if (!first && r != (shown_s.gear == GEAR_R)) evlog("cluster", "gear_view r=%d", r);
    shown_s = s;
    first = 0;
}

/* frame timing: count frames that miss a 16.7 ms slot */
static uint64_t last_post, win_start;
static int frames, late;
static double worst_ms;

static void on_frame(void)
{
    uint64_t t = now_ns();
    if (last_post) {
        double dt = (t - last_post) / 1e6;
        if (dt > worst_ms) worst_ms = dt;
    }
    last_post = t;
    frames++;
    if (!win_start) win_start = t;
    if (t - win_start >= 1000000000ull) {
        evlog("cluster", "frames=%d worst_gap_ms=%.1f", frames, worst_ms);
        frames = 0; late = 0; worst_ms = 0; win_start = t;
    }
}

int main(void)
{
    set_prio(40);
    lv_display_t *d = lvs_create(0, 0, W, H, 0);
    if (!d) return 1;
    make_ui();
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
    lv_timer_create(tick, 16, NULL);
    lvs_on_frame(on_frame);
    evlog("cluster", "ready");
    lvs_run();
}
