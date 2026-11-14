# One night in a parking lot

A car's central computer on **QNX 8.0**: instrument cluster, rear camera,
infotainment, an overnight A/B update, a charging session and an AI assistant
on the same CPUs. The question is the one behind 21 million backup-camera
recalls: when infotainment is slow, busy, crashed or freshly updated, does the
rear image still appear within **2.0 s of R** (FMVSS 111 S5.5.3)?

Everything here runs in QEMU (x86_64, TCG, no GPU, no KVM). Screen composites
in software on a 1920x720 virtual display, the size of a 12.3" cluster.

## Results (QEMU TCG, 4 vCPU)

"Seen" is measured from outside: `probe` reads the composited display every
4 ms and decodes the frame number camsim hides in the camera image's first row.
It starts from the moment vbusd records gear R.

| Scene | Camera inside infotainment | Camera as its own QNX process |
|---|---|---|
| Cold start, R at 4.0 s (S14.2 shape, car software only) | 2.36 s, **fails 2.0 s** | 5 ms |
| Wake, R 0.6 s later | **never shown** in a 4.4 s backing event | 3 ms |
| Driving with AI on all CPUs, then R | 7 ms | 4 ms |
| `kill -9` infotainment while in R | **image lost** for the last 3.0 s | stays up |
| Bad update, 3 crashed starts | rollback to slot A, both | rollback to slot A, both |
| Charging requests late (> 250 ms) all night | 0 | 0 |

Raw logs and in-guest screenshots: `results/<mode>-<scenario>/`.
Video (2:44, narrated) rebuilt from those logs: `results/demo.mp4`. Narration text: `tools/narration.json`, voice made with `tools/tts.sh`.

Priority-inheritance ablation (`qc-start.sh fixed`, channel created with
`_NTO_CHF_FIXED_PRIORITY`): R -> first post 2.1 / 1.3 ms vs 2.2 / 1.5 ms with
inheritance. No measurable difference under this load; the isolation of the
camera process is what matters here.

## Parts

| Process | Prio | Job | QNX features |
|---|---|---|---|
| `vbusd` | inherits | vehicle signal bus at `/dev/vbus` | resource manager, devctl, held replies (`_RESMGR_NOREPLY`), unblock handling |
| `rvc` | 55/54 | rear camera window on top; R -> show, R ends -> hide | Screen window z-order, reply-blocked wait on the bus |
| `camsim` | 50 | 30 fps fake camera into shared memory | `shm_open` |
| `cluster` | 40 | speed, PRND, telltales, park view (LVGL 9.5) | Screen |
| `infotainment` | 20 | slow start (6 s), one AI line; `--with-camera` = recalled-car layout | |
| `supervisor` | 30 | A/B slots, 3 failed starts -> rollback, sleep/wake | `posix_spawn` |
| `otad` | 5 | 300 MB install into slot b, `--bad` build | |
| `evcc` / `secc` | 45 | charging session, 100 ms requests, 250 ms timeout | `name_attach`, `MsgSend`/`MsgReceive`/`MsgReply` |
| `copilot` | 10 | AI load: 4 threads of float math | |
| `probe` | 60 | watches the display like a camera filming it | `screen_read_display` |
| `vsim` | 45 | plays `scenarios/*.txt` | |

## Build and run

```
scripts/fetch-deps.sh                     # LVGL 9.5, Pretendard
source ~/qnx800/qnxsdp-env.sh && make     # QNX binaries -> build/x86_64
make -f Makefile.host                     # host replay renderer
tools/host/run-qemu.sh std                # boot the QNX Quick Start image headless
python3 -m http.server 8765 --bind 127.0.0.1 &   # guest pulls from 10.0.2.2:8765
tools/host/run-scenario.sh split night    # also: mono night, split cold, mono cold
tools/make_video.py results/demo.mp4      # needs Pillow + NumPy
```

The guest is driven through the QEMU serial console (`tools/host/con.py`).

## Limits

- QEMU TCG timing is coarser and slower than hardware. Compare rows, not absolute numbers to a real car.
- "Cold start" restarts the car software from nothing. It does not include the OS boot (about 100 s under TCG).
- Screen itself is one process. If the compositor dies, every window goes. Real cars split this in display hardware layers.
- The camera is a still photo zoomed over time, not a sensor.
- `secc` runs on the same machine as the car and stands in for the charge post; the 250 ms timeout is ours, not a value from ISO 15118.

Camera photo and font credits: `assets/ATTRIBUTION.txt`.
