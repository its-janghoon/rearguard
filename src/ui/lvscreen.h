#ifndef LVSCREEN_H
#define LVSCREEN_H
#include "lvgl.h"

typedef void (*lvs_frame_cb)(void);

/* Create a Screen window at (x,y) of size w*h with z-order z and an LVGL display on it. */
lv_display_t *lvs_create(int x, int y, int w, int h, int z);
void lvs_set_visible(int v);
/* Called after each frame is posted (for frame timing). */
void lvs_on_frame(lvs_frame_cb cb);
/* Run LVGL timers forever. */
void lvs_run(void);

#endif
