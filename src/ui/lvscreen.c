/* Minimal LVGL display on a QNX Screen window with position and z-order.
 * Based on lvgl/src/drivers/qnx/lv_qnx.c (MIT), which always puts the
 * window at 0,0 and has no z-order. LVGL renders straight into the
 * window's two buffers (full-frame mode) and we post the one it drew.
 */
#include "lvscreen.h"
#include "../common/evlog.h"

#include <screen/screen.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    screen_context_t ctx;
    screen_window_t win;
    screen_buffer_t bufs[2];
    void *ptr[2];
    int bi, stride;
    lvs_frame_cb frame_cb;
} lvs_t;

static lvs_t g;

static uint32_t ticks(void) { return (uint32_t)(now_ns() / 1000000ull); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (lv_display_flush_is_last(d)) {
        /* px points at the area's first pixel inside the buffer LVGL just drew */
        uint8_t *base = px - (size_t)a->y1 * g.stride - (size_t)a->x1 * 4;
        int i = base == (uint8_t *)g.ptr[0] ? 0 : 1;
        int pr = screen_post_window(g.win, g.bufs[i], 0, NULL, 0);
        static int dbg; if (dbg++ < 6) evlog("lvs", "post buf=%d rc=%d area=%d,%d-%d,%d", i, pr, (int)a->x1, (int)a->y1, (int)a->x2, (int)a->y2);
        if (g.frame_cb) g.frame_cb();
    }
    lv_display_flush_ready(d);
}

lv_display_t *lvs_create(int x, int y, int w, int h, int z)
{
    if (screen_create_context(&g.ctx, SCREEN_APPLICATION_CONTEXT) || screen_create_window(&g.win, g.ctx)) {
        perror("screen");
        return NULL;
    }
    int pos[2] = { x, y }, size[2] = { w, h };
    int usage = SCREEN_USAGE_WRITE, fmt = SCREEN_FORMAT_RGBA8888, vis = 1;
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_POSITION, pos);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_FORMAT, &fmt);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_ZORDER, &z);
    int tr = SCREEN_TRANSPARENCY_NONE;
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_TRANSPARENCY, &tr);
    screen_create_window_buffers(g.win, 2);
    screen_get_window_property_pv(g.win, SCREEN_PROPERTY_BUFFERS, (void **)g.bufs);
    int bufsize[2], stride;
    (void)bufsize;
    screen_get_buffer_property_iv(g.bufs[0], SCREEN_PROPERTY_BUFFER_SIZE, bufsize);
    screen_get_buffer_property_iv(g.bufs[0], SCREEN_PROPERTY_STRIDE, &stride);
    screen_get_buffer_property_pv(g.bufs[0], SCREEN_PROPERTY_POINTER, &g.ptr[0]);
    screen_get_buffer_property_pv(g.bufs[1], SCREEN_PROPERTY_POINTER, &g.ptr[1]);

    lv_init();
    lv_tick_set_cb(ticks);
    lv_display_t *d = lv_display_create(w, h);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers_with_stride(d, g.ptr[0], g.ptr[1], stride * h, stride, LV_DISPLAY_RENDER_MODE_DIRECT);
    g.stride = stride;
    lv_display_set_flush_cb(d, flush_cb);
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_VISIBLE, &vis);
    return d;
}

void lvs_set_visible(int v)
{
    screen_set_window_property_iv(g.win, SCREEN_PROPERTY_VISIBLE, &v);
    screen_flush_context(g.ctx, 0);
}

void lvs_on_frame(lvs_frame_cb cb) { g.frame_cb = cb; }

void lvs_run(void)
{
    for (;;) {
        uint32_t ms = lv_timer_handler();
        if (ms == LV_NO_TIMER_READY || ms > 16) ms = 16;
        usleep(ms * 1000);
    }
}
