/* vsim: plays a scenario onto the vehicle bus.
 * Scenario lines: "<t_ms> <field> <value>"   or   "<t_ms> run <shell command>"
 * fields: gear P|R|N|D, speed (km/h, may be fractional), steer, awake,
 *         belt, brake, ai, park (cm), soc, range
 * Times are relative to start. Lines must be sorted by time.
 */
#include "../common/evlog.h"
#include "../common/vbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void sleep_until(uint64_t t)
{
    struct timespec ts = { .tv_sec = t / 1000000000ull, .tv_nsec = t % 1000000000ull };
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: vsim scenario.txt\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    set_prio(45);
    int fd = vbus_open();
    uint64_t t0 = now_ns();
    evlog("vsim", "start scenario=%s", argv[1]);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\n")] = 0;
        double t_ms;
        char field[32];
        int off = 0;
        if (sscanf(line, "%lf %31s %n", &t_ms, field, &off) < 2) continue;
        const char *val = line + off;
        sleep_until(t0 + (uint64_t)(t_ms * 1e6));

        if (!strcmp(field, "run")) {
            char cmd[600];
            snprintf(cmd, sizeof cmd, "%s &", val);
            evlog("vsim", "run %s", val);
            (void)!system(cmd);
            continue;
        }
        if (!strcmp(field, "mark")) { evlog("vsim", "mark %s", val); continue; }

        vbus_state_t s;
        memset(&s, 0, sizeof s);
        uint32_t m = 0;
        if (!strcmp(field, "gear")) { m = VF_GEAR; s.gear = strchr("PRND", val[0]) - "PRND"; }
        else if (!strcmp(field, "speed")) { m = VF_SPEED; s.speed_x10 = (int)(atof(val) * 10); }
        else if (!strcmp(field, "steer")) { m = VF_STEER; s.steer_deg = atoi(val); }
        else if (!strcmp(field, "awake")) { m = VF_AWAKE; s.awake = atoi(val); }
        else if (!strcmp(field, "belt")) { m = VF_BELT; s.seatbelt = atoi(val); }
        else if (!strcmp(field, "brake")) { m = VF_BRAKE; s.brake_fault = atoi(val); }
        else if (!strcmp(field, "ai")) { m = VF_AI; s.ai_on = atoi(val); }
        else if (!strcmp(field, "park")) { m = VF_PARK; s.park_dist_cm = atoi(val); }
        else if (!strcmp(field, "soc")) { m = VF_SOC; s.soc = atoi(val); }
        else if (!strcmp(field, "range")) { m = VF_RANGE; s.range_km = atoi(val); }
        else { evlog("vsim", "unknown_field %s", field); continue; }
        if (m != VF_SPEED && m != VF_STEER && m != VF_PARK) evlog("vsim", "set %s=%s", field, val);
        vbus_put(fd, m, &s);
    }
    evlog("vsim", "end");
    return 0;
}
