/* vbusd: vehicle signal bus as a QNX resource manager at /dev/vbus.
 *
 * devctl VB_GET  -> current state
 * devctl VB_PUT  -> update fields by mask, wake every waiter
 * devctl VB_WAIT -> reply held (_RESMGR_NOREPLY) until the state changes.
 *
 * One receive thread. The kernel hands this thread the priority of the
 * highest-priority sender, so a camera waiter at prio 55 is not stuck
 * behind the AI process at prio 10.
 */
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>

#define MAX_WAITERS 64

static vbus_state_t state;
static struct { int rcvid; uint32_t seen; } waiters[MAX_WAITERS];
static int nwaiters;

static void reply_state(int rcvid)
{
    struct _io_devctl_reply o;
    memset(&o, 0, sizeof o);
    o.nbytes = sizeof state;
    iov_t iov[2];
    SETIOV(&iov[0], &o, sizeof o);
    SETIOV(&iov[1], &state, sizeof state);
    MsgReplyv(rcvid, EOK, iov, 2);
}

static void wake_waiters(void)
{
    int keep = 0;
    for (int i = 0; i < nwaiters; i++) {
        if (waiters[i].seen != state.seq) reply_state(waiters[i].rcvid);
        else waiters[keep++] = waiters[i];
    }
    nwaiters = keep;
}

static void apply_put(const vbus_put_t *p)
{
    uint32_t m = p->mask;
    const vbus_state_t *s = &p->s;
    if ((m & VF_GEAR) && s->gear != state.gear) { state.gear = s->gear; state.t_gear_ns = now_ns(); }
    if (m & VF_SPEED) state.speed_x10 = s->speed_x10;
    if (m & VF_STEER) state.steer_deg = s->steer_deg;
    if (m & VF_AWAKE) state.awake = s->awake;
    if (m & VF_CHARGE) state.charge_w = s->charge_w;
    if (m & VF_SOC) state.soc = s->soc;
    if (m & VF_RANGE) state.range_km = s->range_km;
    if (m & VF_BELT) state.seatbelt = s->seatbelt;
    if (m & VF_BRAKE) state.brake_fault = s->brake_fault;
    if (m & VF_AI) state.ai_on = s->ai_on;
    if (m & VF_PARK) state.park_dist_cm = s->park_dist_cm;
    state.seq++;
    evlog("vbusd", "state seq=%u gear=%d speed=%d steer=%d awake=%d charge=%d soc=%d range=%d belt=%d brake=%d ai=%d park=%d",
          state.seq, state.gear, state.speed_x10, state.steer_deg, state.awake, state.charge_w, state.soc,
          state.range_km, state.seatbelt, state.brake_fault, state.ai_on, state.park_dist_cm);
}

static int io_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb)
{
    int st = iofunc_devctl_default(ctp, msg, ocb);
    if (st != _RESMGR_DEFAULT) return st;

    void *data = _DEVCTL_DATA(msg->i);
    switch (msg->i.dcmd) {
    case VB_GET:
        memset(&msg->o, 0, sizeof msg->o);
        memcpy(data, &state, sizeof state);
        msg->o.nbytes = sizeof state;
        return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o + sizeof state);

    case VB_PUT:
        if (msg->i.nbytes < sizeof(vbus_put_t)) return EINVAL;
        apply_put((const vbus_put_t *)data);
        wake_waiters();
        memset(&msg->o, 0, sizeof msg->o);
        return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);

    case VB_WAIT: {
        const vbus_wait_t *w = (const vbus_wait_t *)data;
        if (w->seen_seq != state.seq) { reply_state(ctp->rcvid); return _RESMGR_NOREPLY; }
        if (nwaiters == MAX_WAITERS) return EAGAIN;
        waiters[nwaiters].rcvid = ctp->rcvid;
        waiters[nwaiters].seen = w->seen_seq;
        nwaiters++;
        return _RESMGR_NOREPLY;
    }
    default:
        return ENOSYS;
    }
}

/* A held waiter was signalled or killed: let it go. The unblock pulse
 * carries the blocked client's rcvid in its value. */
static int io_unblock(resmgr_context_t *ctp, io_pulse_t *msg, RESMGR_OCB_T *ocb)
{
    (void)ocb;
    int who = msg->value.sival_int;
    for (int i = 0; i < nwaiters; i++) {
        if (waiters[i].rcvid == who || waiters[i].rcvid == ctp->rcvid) {
            MsgError(waiters[i].rcvid, EINTR);
            waiters[i] = waiters[--nwaiters];
            break;
        }
    }
    return _RESMGR_NOREPLY;
}

int main(int argc, char **argv)
{
    int flags = 0;
    if (argc > 1 && strcmp(argv[1], "--fixed-prio") == 0) {
        flags = _NTO_CHF_FIXED_PRIORITY; /* ablation: no priority inheritance */
    }

    state.gear = GEAR_P; state.awake = 1; state.soc = 60; state.range_km = 300; state.seatbelt = 1;

    int chid = ChannelCreate(_NTO_CHF_UNBLOCK | _NTO_CHF_DISCONNECT | flags);
    if (chid == -1) { perror("ChannelCreate"); return 1; }
    dispatch_t *dpp = dispatch_create_channel(chid, DISPATCH_FLAG_NOLOCK);
    if (!dpp) { perror("dispatch_create_channel"); return 1; }

    static resmgr_connect_funcs_t cf;
    static resmgr_io_funcs_t iof;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cf, _RESMGR_IO_NFUNCS, &iof);
    iof.devctl = io_devctl;
    iof.unblock = io_unblock;

    static iofunc_attr_t attr;
    iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);

    resmgr_attr_t ra = { .nparts_max = 2, .msg_max_size = 2048 };
    if (resmgr_attach(dpp, &ra, VBUS_PATH, _FTYPE_ANY, 0, &cf, &iof, &attr) == -1) {
        perror("resmgr_attach");
        return 1;
    }

    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    evlog("vbusd", "ready flags=%d", flags);
    for (;;) {
        if ((ctp = dispatch_block(ctp)) == NULL) { perror("dispatch_block"); return 1; }
        dispatch_handler(ctp);
    }
}
