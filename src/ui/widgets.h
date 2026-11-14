#ifndef WIDGETS_H
#define WIDGETS_H
#include "lvgl.h"

LV_FONT_DECLARE(f_speed)
LV_FONT_DECLARE(f_prnd)
LV_FONT_DECLARE(f_64)
LV_FONT_DECLARE(f_26)
LV_FONT_DECLARE(f_24)
LV_FONT_DECLARE(f_22)

#define C_TEXT   0xe8edf2
#define C_DIM    0x8a96a2
#define C_OFF    0x2e3740
#define C_RED    0xff4d4f
#define C_AMBER  0xffb020
#define C_GREEN  0x3ddc84
#define C_YELLOW 0xffd23f
#define C_AI     0x8b7bff

/* A plain box with no default theme styles. */
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
/* A stroked arc (no knob, no indicator). Angles in degrees, 0 = 3 o'clock, clockwise. */
lv_obj_t *arc(lv_obj_t *parent, int cx, int cy, int r, int start, int end, int width, uint32_t color);
/* A polyline; pts must stay valid for the object's life. */
lv_obj_t *line(lv_obj_t *parent, const lv_point_precise_t *pts, int n, int width, uint32_t color);
void set_color(lv_obj_t *o, uint32_t color);

#endif
