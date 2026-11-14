/* Vehicle signal bus shared by every process. Server: vbusd (/dev/vbus). */
#ifndef VBUS_H
#define VBUS_H
#include <stdint.h>

#define VBUS_PATH "/dev/vbus"

enum { GEAR_P = 0, GEAR_R = 1, GEAR_N = 2, GEAR_D = 3 };

typedef struct {
    uint32_t seq;          /* bumped on every change */
    int32_t gear;          /* GEAR_* */
    int32_t speed_x10;     /* km/h * 10 */
    int32_t steer_deg;     /* -40..40, used by the rear guide lines */
    int32_t awake;         /* 0 = vehicle asleep, 1 = awake */
    int32_t charge_w;      /* charging power in W, 0 = not charging */
    int32_t soc;           /* battery % */
    int32_t range_km;
    int32_t seatbelt;      /* 1 = buckled */
    int32_t brake_fault;   /* 1 = brake system warning */
    int32_t ai_on;         /* 1 = voice assistant busy */
    int32_t park_dist_cm;  /* rear obstacle distance, 0 = none */
    uint64_t t_gear_ns;    /* CLOCK_MONOTONIC when gear last changed */
} vbus_state_t;

/* Field mask for VB_PUT. */
enum {
    VF_GEAR = 1 << 0, VF_SPEED = 1 << 1, VF_STEER = 1 << 2, VF_AWAKE = 1 << 3,
    VF_CHARGE = 1 << 4, VF_SOC = 1 << 5, VF_RANGE = 1 << 6, VF_BELT = 1 << 7,
    VF_BRAKE = 1 << 8, VF_AI = 1 << 9, VF_PARK = 1 << 10,
};

typedef struct {
    uint32_t mask;
    vbus_state_t s;
} vbus_put_t;

typedef struct {
    uint32_t seen_seq;     /* reply as soon as seq != seen_seq */
} vbus_wait_t;

#ifdef __QNXNTO__
#include <devctl.h>
#define VB_GET  __DIOF(_DCMD_MISC, 1, vbus_state_t)
#define VB_PUT  __DIOT(_DCMD_MISC, 2, vbus_put_t)
#define VB_WAIT __DIOTF(_DCMD_MISC, 3, vbus_state_t)
#endif

/* Client helpers (vbus_client.c). */
int vbus_open(void);
int vbus_get(int fd, vbus_state_t *s);
int vbus_put(int fd, uint32_t mask, const vbus_state_t *s);
/* Blocks in vbusd (reply-blocked) until the state changes. */
int vbus_wait(int fd, uint32_t seen_seq, vbus_state_t *s);

#endif
