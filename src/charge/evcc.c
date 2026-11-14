/* evcc: car side of the charging session. Every 100 ms, MsgSend a
 * CurrentDemand request to the charger (secc) and publish charging power
 * on the vehicle bus. Runs at prio 45 so an update install at prio 5 or
 * the assistant at prio 10 cannot make it late.
 */
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <time.h>
#include <unistd.h>

typedef struct { uint16_t type; uint16_t sub; uint32_t seq; int32_t amps; } demand_t;

int main(void)
{
    set_prio(45);
    int coid = -1;
    while ((coid = name_open("secc", 0)) == -1) usleep(50000);
    int fd = vbus_open();
    evlog("evcc", "ready");
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    uint32_t seq = 0;
    vbus_state_t s;
    memset(&s, 0, sizeof s);
    int soc = 60, published = -1;
    for (;;) {
        vbus_state_t now;
        vbus_get(fd, &now);
        if (now.gear != GEAR_P) {            /* unplugged to drive: no session */
            if (published != 0) { s.charge_w = 0; s.soc = soc; s.range_km = soc * 5; vbus_put(fd, VF_CHARGE, &s); published = 0; evlog("evcc", "session_end gear=%d", now.gear); }
            usleep(200000);
            clock_gettime(CLOCK_MONOTONIC, &next);
            continue;
        }
        demand_t m = { .type = 0x5001, .seq = ++seq, .amps = 32 };
        int32_t granted = 0;
        if (MsgSend(coid, &m, sizeof m, &granted, sizeof granted) == -1) {
            granted = 0;
            if (seq < 5) evlog("evcc", "send_error errno=%d", errno);
        }
        int watts = granted * 230;
        if (seq % 600 == 0 && soc < 100) soc++;   /* +1% a minute */
        if (watts != published || seq % 600 == 0) {
            s.charge_w = watts; s.soc = soc; s.range_km = soc * 5;
            vbus_put(fd, VF_CHARGE | VF_SOC | VF_RANGE, &s);
            published = watts;
        }
        next.tv_nsec += 100000000;
        if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
}
