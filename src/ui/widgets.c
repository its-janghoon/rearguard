#include "widgets.h"

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *arc(lv_obj_t *parent, int cx, int cy, int r, int start, int end, int width, uint32_t color)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, 2 * r, 2 * r);
    lv_obj_set_pos(a, cx - r, cy - r);
    lv_arc_set_bg_angles(a, start, end);
    lv_arc_set_angles(a, start, start);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

lv_obj_t *line(lv_obj_t *parent, const lv_point_precise_t *pts, int n, int width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

void set_color(lv_obj_t *o, uint32_t color)
{
    if (lv_obj_check_type(o, &lv_label_class)) lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    else if (lv_obj_check_type(o, &lv_arc_class)) lv_obj_set_style_arc_color(o, lv_color_hex(color), LV_PART_MAIN);
    else if (lv_obj_check_type(o, &lv_line_class)) lv_obj_set_style_line_color(o, lv_color_hex(color), 0);
    else lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
}
