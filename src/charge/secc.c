/* secc: charger side of the charging session (stands in for the charge post).
 * The car (evcc) must send a CurrentDemand request at least every
 * TIMEOUT_MS or the charger ends the session. We log every late request.
 * Message passing: name_attach("secc"), MsgReceive / MsgReply.
 */
#include "../common/evlog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>

#define TIMEOUT_MS 250   /* our session timeout; set, not taken from ISO 15118 text */

typedef struct { uint16_t type; uint16_t sub; uint32_t seq; int32_t amps; } demand_t;

int main(void)
{
    name_attach_t *att = name_attach(NULL, "secc", 0);
    if (!att) { perror("name_attach"); return 1; }
    evlog("secc", "ready timeout_ms=%d", TIMEOUT_MS);
    uint64_t last = 0;
    unsigned late = 0, total = 0;
    double worst = 0;
    for (;;) {
        demand_t m;
        int rcvid = MsgReceive(att->chid, &m, sizeof m, NULL);
        if (rcvid == 0) continue;                 /* pulse */
        if (rcvid < 0) continue;
        if (m.type != 0x5001) { MsgReply(rcvid, EOK, NULL, 0); continue; }  /* connect etc. */
        uint64_t t = now_ns();
        if (last) {
            double gap = (t - last) / 1e6;
            if (gap > worst) worst = gap;
            if (gap > 2000) evlog("secc", "session_restart gap_ms=%.0f", gap);   /* car drove off and came back */
            else if (gap > TIMEOUT_MS) { late++; evlog("secc", "late gap_ms=%.1f seq=%u", gap, m.seq); }
        }
        last = t;
        total++;
        if (total % 100 == 0) { evlog("secc", "stats total=%u late=%u worst_gap_ms=%.1f", total, late, worst); worst = 0; }
        int32_t granted = m.amps;
        MsgReply(rcvid, EOK, &granted, sizeof granted);
    }
}
