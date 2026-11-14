/* Shared memory between camsim (writer) and the camera presenter (reader). */
#ifndef CAMSHM_H
#define CAMSHM_H
#include <stdint.h>

#define CAMSHM_NAME "/camsim"
#define CAM_W 880
#define CAM_H 486
#define CAM_TAG_BITS 24  /* frame number, 1 bit per pixel in row 0 */

typedef struct {
    uint32_t w, h;
    volatile uint32_t seq;   /* last complete frame */
    volatile uint32_t idx;   /* buffer holding frame `seq` */
    uint64_t t_ns[2];
    uint32_t px[2][CAM_W * CAM_H];  /* 0xAARRGGBB */
} camshm_t;

static inline uint32_t cam_tag_read(const uint32_t *row0)
{
    uint32_t v = 0;
    for (int i = 0; i < CAM_TAG_BITS; i++)
        if ((row0[i] & 0x00ff0000u) > 0x00800000u) v |= 1u << i;
    return v;
}

#endif
