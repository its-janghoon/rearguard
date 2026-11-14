/* probe: watches the composited display from the outside, like a person
 * filming the screen. Every 4 ms it reads the first row of the camera area
 * from the display and decodes the frame number camsim hid there.
 * It knows nothing about rvc; it logs when a camera frame becomes visible
 * on the display and when it disappears.
 * Needs a display-manager context (root).
 */
#include "../common/camshm.h"
#include "../common/evlog.h"

#include <pthread.h>
#include <screen/screen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CAM_X 520
#define CAM_Y 86

int main(void)
{
    screen_context_t ctx;
    if (screen_create_context(&ctx, SCREEN_DISPLAY_MANAGER_CONTEXT)) { perror("context"); return 1; }
    screen_display_t disp;
    screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)&disp);

    screen_pixmap_t pm;
    screen_create_pixmap(&pm, ctx);
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ, fmt = SCREEN_FORMAT_RGBA8888, size[2] = { CAM_TAG_BITS, 1 };
    screen_set_pixmap_property_iv(pm, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_pixmap_property_iv(pm, SCREEN_PROPERTY_FORMAT, &fmt);
    screen_set_pixmap_property_iv(pm, SCREEN_PROPERTY_BUFFER_SIZE, size);
    screen_create_pixmap_buffer(pm);
    screen_buffer_t buf;
    screen_get_pixmap_property_pv(pm, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf);
    uint32_t *px;
    screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void **)&px);

    struct sched_param sp = { .sched_priority = 60 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    evlog("probe", "ready");
    int rect[4] = { CAM_X, CAM_Y, CAM_TAG_BITS, 1 };
    uint32_t last = 0;
    int visible = 0;
    for (;;) {
        if (screen_read_display(disp, buf, 1, rect, 0) == 0) {
            uint32_t tag = cam_tag_read(px);
            /* a real frame has black/white pixels only; anything else is not the camera */
            int ok = 1;
            for (int i = 0; i < CAM_TAG_BITS; i++) {
                uint32_t c = px[i] & 0xffffff;
                if (c != 0 && c != 0xffffff) { ok = 0; break; }
            }
            ok = ok && tag != 0;
            if (ok && !visible) { evlog("probe", "cam_visible seq=%u", tag); visible = 1; }
            else if (!ok && visible) { evlog("probe", "cam_gone last_seq=%u", last); visible = 0; }
            if (ok) last = tag;
        }
        usleep(4000);
    }
}
