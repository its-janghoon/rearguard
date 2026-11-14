#!/usr/bin/env python3
"""Build the demo video from recorded QNX runs (results/<mode>-<scenario>/events.log).

Every pixel of the car screens comes from the logs:
  - cluster: build/host/replay re-renders cluster.c from the logged bus states
  - camera:  shown exactly where the display probe saw camera frames on screen
             (probe cam_visible / cam_gone), frame number -> camsim zoom, guide
             lines from the logged steering angle (same formula as rvc.c)
  - stopwatch, PASS/FAIL, timeline: from the same log lines
Layout follows mockups/d-mix.html (left: camera inside infotainment, right: QNX split).

usage: make_video.py out.mp4
"""
import math, os, re, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")
REPLAY = os.path.join(ROOT, "build/host/replay")
FONT_DIR = os.path.join(ROOT, "third_party/fonts/p/public/static")
FPS = 30
W, H = 1920, 1080
CW, CH = 1920, 720
CAM_X, CAM_Y, CAM_W, CAM_H = 520, 86, 880, 486


def font(weight, size):
    return ImageFont.truetype(os.path.join(FONT_DIR, f"Pretendard-{weight}.otf"), size)


F = {k: font(*v) for k, v in {
    "kicker": ("SemiBold", 22), "h1": ("Bold", 44), "tag": ("Bold", 28), "dim": ("Regular", 22),
    "num": ("ExtraBold", 64), "lab": ("Medium", 18), "lane": ("Medium", 20), "seg": ("Bold", 16),
    "big": ("ExtraBold", 120), "card": ("Bold", 56), "cardsub": ("Regular", 30), "nosig": ("Bold", 34),
}.items()}

C = dict(bg=(5, 7, 10), text=(232, 237, 242), dim=(125, 136, 148), ok=(61, 220, 132), bad=(255, 77, 79),
         warn=(255, 176, 32), blue=(74, 163, 255), ai=(139, 123, 255), panel=(12, 16, 21), line=(31, 38, 46))


# ---------------------------------------------------------------- log parsing
class Run:
    def __init__(self, name):
        self.name = name
        self.lines = []
        with open(os.path.join(RES, name, "events.log")) as f:
            for ln in f:
                p = ln.split(" ", 2)
                if len(p) < 3:
                    continue
                self.lines.append((int(p[0]) / 1e9, p[1], p[2].strip()))
        self.first = self.lines[0][0]
        self.t0 = next(t for t, pr, m in self.lines if pr == "vsim" and m.startswith("start"))

    def ev(self, proc=None, prefix=""):
        return [(t - self.t0, m) for t, pr, m in self.lines if (proc is None or pr == proc) and m.startswith(prefix)]

    def states(self):
        out = []
        for t, m in self.ev("vbusd", "state"):
            d = dict(kv.split("=") for kv in m.split()[1:])
            out.append((t, {k: int(v) for k, v in d.items()}))
        return out

    def state_at(self, t):
        cur = None
        for tt, s in self._st:
            if tt <= t:
                cur = s
            else:
                break
        return cur

    def cam_intervals(self):
        out, start = [], None
        for t, m in self.ev("probe"):
            if m.startswith("cam_visible"):
                start = (t, int(re.search(r"seq=(\d+)", m).group(1)))
            elif m.startswith("cam_gone") and start:
                out.append((start[0], t, start[1]))
                start = None
        if start:
            out.append((start[0], 1e9, start[1]))
        return out

    def prep(self):
        self._st = self.states()
        self._cam = self.cam_intervals()
        self._r = []          # backing events (R start, R end)
        cur = None
        for t, s in self._st:
            if s["gear"] == 1 and cur is None:
                cur = t
            elif s["gear"] != 1 and cur is not None:
                self._r.append((cur, t)); cur = None
        if cur is not None:
            self._r.append((cur, 1e9))
        return self

    def cam_seq_at(self, t):
        for a, b, seq in self._cam:
            if a <= t < b:
                return seq + int((t - a) * 30)
        return None


# ---------------------------------------------------------------- camera layer
SRC = Image.open(os.path.join(ROOT, "assets/cam.jpg")).convert("RGB").resize((1280, 720))


def cam_frame(seq, steer):
    z = 1.0 + 0.15 * ((seq % 240) / 240.0)
    sw, sh = int(1280 / z), int(720 / z)
    sx0, sy0 = (1280 - sw) // 2, (720 - sh) // 2 + int(10 * (z - 1.0))
    img = SRC.crop((sx0, sy0, sx0 + sw, sy0 + sh)).resize((CAM_W, CAM_H), Image.NEAREST)
    d = ImageDraw.Draw(img)
    bend = steer / 40.0 * 120.0
    for side in (-1, 1):
        pts = []
        for i in range(0, 201, 4):
            t = i / 200.0
            pts.append((CAM_W / 2 + side * (290 - t * 210) + bend * t * t, CAM_H - t * (CAM_H - 236)))
        d.line(pts, fill=(255, 210, 63), width=7)
        for k, (t, c) in enumerate([(0.30, (255, 77, 79)), (0.62, (255, 176, 32)), (0.92, (61, 220, 132))]):
            x = CAM_W / 2 + side * (290 - t * 210) + bend * t * t
            y = CAM_H - t * (CAM_H - 236)
            d.line([(x, y), (x - side * (34 - 10 * k), y)], fill=c, width=7 - 2 * (k > 1))
    return img


# ---------------------------------------------------------------- cluster replay
class Replay:
    def __init__(self, run, t_from, t_to):
        off = run.t0 - run.first
        self.p = subprocess.Popen([REPLAY, os.path.join(RES, run.name, "events.log"), str(FPS),
                                   f"{t_from + off:.4f}", f"{t_to + off:.4f}"], stdout=subprocess.PIPE)

    def next(self):
        raw = self.p.stdout.read(CW * CH * 4)
        if len(raw) < CW * CH * 4:
            return None
        a = np.frombuffer(raw, np.uint8).reshape(CH, CW, 4)[:, :, [2, 1, 0]]
        return Image.fromarray(a.copy(), "RGB")


def car_screen(run, rp, t, label_nosig=True):
    img = rp.next()
    s = run.state_at(t) or {}
    seq = run.cam_seq_at(t)
    if seq is not None:
        img.paste(cam_frame(seq, s.get("steer", 0)), (CAM_X, CAM_Y))
    elif s.get("gear") == 1 and label_nosig:
        # video annotation only (not on the car screen): there is no image here
        d = ImageDraw.Draw(img)
        noise = Image.effect_noise((CAM_W // 4, CAM_H // 4), 60).resize((CAM_W, CAM_H)).convert("RGB")
        img.paste(Image.blend(Image.new("RGB", (CAM_W, CAM_H), (0, 0, 0)), noise, 0.18), (CAM_X, CAM_Y))
        d.text((CAM_X + CAM_W / 2, CAM_Y + CAM_H / 2 - 18), "NO CAMERA IMAGE", font=F["nosig"], fill=C["bad"], anchor="mm")
        d.text((CAM_X + CAM_W / 2, CAM_Y + CAM_H / 2 + 24), "infotainment is not running", font=F["dim"], fill=C["dim"], anchor="mm")
    return img


# ---------------------------------------------------------------- overlay pieces
def rounded_mask(size, r):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], r, fill=255)
    return m


_glow_cache = {}


def glow(canvas, box, color, r=40):
    pad = 60
    x0, y0, x1, y1 = box
    key = (x1 - x0, y1 - y0, color)
    if key not in _glow_cache:
        g = Image.new("RGBA", (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad), (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle([pad, pad, pad + x1 - x0, pad + y1 - y0], r, outline=color + (150,), width=10)
        _glow_cache[key] = g.filter(ImageFilter.GaussianBlur(18))
    canvas.alpha_composite(_glow_cache[key], (x0 - pad, y0 - pad))


def backing_status(run, t):
    """(elapsed_s, state) for the current/last backing event at time t."""
    ev = [b for b in run._r if b[0] <= t]
    if not ev:
        return None
    r0, r1 = ev[-1]
    if t > r1 + 1.0:
        return None
    shown = [a for a, b, _ in run._cam if r0 <= a <= r1 + 0.05]
    lost = [b for a, b, _ in run._cam if a <= r0 + 3 and r0 < b < r1 - 0.05]
    if shown and shown[0] <= t:
        dt = shown[0] - r0
        if lost and lost[0] <= t:
            return dt, "lost"
        return dt, "pass" if dt <= 2.0 else "late"
    if t > r1:
        return r1 - r0, "never"
    return t - r0, "waiting"


def stopwatch(d, cx, cy, st):
    if st is None:
        val, col, word, frac = "-.-- s", C["dim"], "", 0
    else:
        dt, state = st
        col = C["ok"] if state == "pass" else C["dim"] if state == "waiting" and dt <= 2.0 else C["bad"]
        word = {"pass": "PASS", "late": "LATE", "never": "NONE", "lost": "LOST", "waiting": ""}[state]
        val = f"{dt:.2f} s" if dt < 10 else f"{dt:.1f} s"
        if state == "never":
            val = "never"
        frac = min(dt / 2.0, 1.0)
    d.rounded_rectangle([cx - 230, cy - 62, cx + 230, cy + 62], 62, fill=(8, 11, 15), outline=(40, 48, 56), width=2)
    d.ellipse([cx - 210, cy - 44, cx - 122, cy + 44], outline=(29, 36, 44), width=8)
    if frac > 0:
        d.arc([cx - 210, cy - 44, cx - 122, cy + 44], -90, -90 + 360 * frac, fill=col, width=8)
    d.text((cx - 166, cy), word, font=F["seg"], fill=col, anchor="mm")
    d.text((cx - 100, cy - 8), val, font=F["num"], fill=col, anchor="lm")
    d.text((cx - 98, cy + 40), "R \u2192 CAMERA  \u00b7  LIMIT 2.00 s", font=F["lab"], fill=C["dim"], anchor="lm")


def lane_bar(d, x, y, w, t0, t1, segs, t_now):
    d.rounded_rectangle([x, y, x + w, y + 22], 6, fill=(20, 25, 31))
    for a, b, col, txt in segs:
        xa = x + w * max(0, (a - t0) / (t1 - t0)); xb = x + w * min(1, (b - t0) / (t1 - t0))
        if xb <= xa:
            continue
        d.rounded_rectangle([xa, y, max(xb, xa + 6), y + 22], 6, fill=col)
        if txt and xb - xa > 60:
            d.text((xa + 8, y + 11), txt, font=F["seg"], fill=(7, 16, 9) if col != C["bad"] else (255, 255, 255), anchor="lm")
    px = x + w * min(max((t_now - t0) / (t1 - t0), 0), 1)
    d.line([(px, y - 8), (px, y + 30)], fill=(255, 255, 255), width=2)


# ---------------------------------------------------------------- scenes
def card(frames, title, sub, foot=None):
    for i in range(frames):
        im = Image.new("RGB", (W, H), C["bg"])
        d = ImageDraw.Draw(im)
        a = min(1, i / 12)
        col = tuple(int(c * a) for c in C["text"])
        d.text((W / 2, H / 2 - 60), title, font=F["card"], fill=col, anchor="mm")
        d.text((W / 2, H / 2 + 20), sub, font=F["cardsub"], fill=tuple(int(c * a) for c in C["dim"]), anchor="mm")
        if foot:
            d.text((W / 2, H - 80), foot, font=F["lab"], fill=C["dim"], anchor="mm")
        yield im


def compare(left, right, t_from, t_to, kicker, title, lanes, speed=1.0):
    rl, rr = Replay(left, t_from, t_to + 1), Replay(right, t_from, t_to + 1)
    n = int((t_to - t_from) * FPS / speed)
    pw, ph = 880, 330
    for i in range(n):
        t = t_from + i * speed / FPS
        # replay emits 1 frame per 1/FPS of log time; skip frames when sped up
        for _ in range(int(speed) - 1):
            rl.next(); rr.next()
        L = car_screen(left, rl, t); R = car_screen(right, rr, t)
        im = Image.new("RGBA", (W, H), C["bg"] + (255,))
        d = ImageDraw.Draw(im)
        d.text((64, 52), kicker, font=F["kicker"], fill=C["dim"])
        d.text((64, 86), title, font=F["h1"], fill=C["text"])
        d.text((W - 64, 60), f"t = {t:6.2f} s", font=F["tag"], fill=C["dim"], anchor="ra")
        for k, (run, scr, x, name, note) in enumerate([(left, L, 64, "Camera inside infotainment", "how recalled cars are built"),
                                                       (right, R, 976, "Camera as its own QNX process", "this project")]):
            st = backing_status(run, t)
            ok = st is not None and st[1] == "pass"
            badst = st is not None and st[1] in ("late", "never", "lost") or (st and st[1] == "waiting" and st[0] > 2)
            d.text((x + 6, 176), name, font=F["tag"], fill=C["text"])
            d.text((x + pw - 6, 182), note, font=F["dim"], fill=C["dim"], anchor="ra")
            if ok or badst:
                glow(im, [x - 4, 220 - 4, x + pw + 4, 220 + ph + 4], C["ok"] if ok else C["bad"])
            small = scr.resize((pw, ph), Image.LANCZOS)
            im.paste(small, (x, 220), rounded_mask((pw, ph), 26))
            d = ImageDraw.Draw(im)
            stopwatch(d, x + pw // 2, 220 + ph + 70, st)
        # timeline
        y0 = 760
        d.rounded_rectangle([64, y0, W - 64, H - 40], 22, fill=C["panel"])
        for j, (name, segs) in enumerate(lanes):
            d.text((92, y0 + 34 + j * 52), name, font=F["lane"], fill=C["dim"], anchor="lm")
            lane_bar(d, 300, y0 + 23 + j * 52, W - 420, t_from, t_to, segs, t)
        yield im.convert("RGB")


def lanes_for(run, label):
    """Timeline lanes from a run's log: gear R, camera on screen, infotainment up."""
    r = [(a, b, C["warn"], "R") for a, b in run._r]
    cam = [(a, b, C["ok"], "camera") for a, b, _ in run._cam]
    up, boot, start = [], None, None
    for t, m in run.ev():
        if m.startswith("start slot="):
            boot = t
        elif m.startswith("ready slot=") and boot is not None:
            up.append((boot, t, C["warn"], "starting")); boot = None; start = t
        elif m.startswith("crash") and boot is not None:
            up.append((boot, t, C["bad"], "crash")); boot = None
        elif (m.startswith("exited") or m.startswith("sleep_stop")) and start is not None:
            up.append((start, t, C["blue"], "running")); start = None
    if start is not None:
        up.append((start, 1e9, C["blue"], "running"))
    if boot is not None:
        up.append((boot, 1e9, C["warn"], "starting"))
    return [(f"Gear R", r), (f"{label}: camera", cam), (f"{label}: infotainment", up)]


def charge_lane(run):
    """Charging session from secc: green while requests arrive on time; red marks for late ones."""
    segs = [(0, 1e9, C["ok"], "charging  ·  " + charge_note(run))]
    for t, m in run.ev("secc", "late"):
        segs.append((t - 0.2, t + 0.2, C["bad"], ""))
    return ("Charging session", segs)


def charge_note(run):
    late = len(run.ev("secc", "late"))
    stats = run.ev("secc", "stats")
    total = stats[-1][1].split()[1].split("=")[1] if stats else "?"
    return f"{late} late of {total} requests"


def main(out):
    mono_c, split_c = Run("mono-cold").prep(), Run("split-cold").prep()
    mono_n, split_n = Run("mono-night").prep(), Run("split-night").prep()

    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", out],
                          stdin=subprocess.PIPE)

    def emit(gen):
        for im in gen:
            ff.stdin.write(im.tobytes())

    emit(card(FPS * 4, "21,000,000 cars recalled for backup cameras", "The camera works. The computer in the middle does not.",
              "Bloomberg analysis of NHTSA data, Aug 2026"))
    emit(card(FPS * 3, "FMVSS 111: image within 2.0 s of R", "Test starts with everything powered off."))
    lc = lanes_for(mono_c, "left"); rc = lanes_for(split_c, "right")
    emit(compare(mono_c, split_c, 0.0, 9.0, "SCENE 1  \u00b7  COLD START (FMVSS 111 S14.2 SHAPE)",
                 "Door opens, car software starts from off, R at 4.0 s.", [lc[0], lc[1], rc[1], lc[2]]))
    ln = lanes_for(mono_n, "left"); rn = lanes_for(split_n, "right")
    emit(compare(mono_n, split_n, 2.0, 20.0, "SCENE 2  \u00b7  OVERNIGHT UPDATE, 2x SPEED",
                 "A bad update crashes infotainment 3 times. Slot A comes back.", [ln[2], rn[2], charge_lane(split_n)], speed=2.0))
    emit(compare(mono_n, split_n, 69.5, 76.0, "SCENE 3  \u00b7  WAKE UP, THEN REVERSE",
                 "Car wakes, infotainment restarts, driver shifts to R 0.6 s later.", [ln[0], ln[1], rn[1], ln[2]]))
    emit(compare(mono_n, split_n, 75.5, 93.5, "SCENE 4  \u00b7  DRIVE WITH AI, REVERSE, INFOTAINMENT KILLED",
                 "AI uses every CPU. Then infotainment is killed with kill -9 while in R.", [ln[0], ln[1], rn[1], ln[2]]))
    ff.stdin.close()
    ff.wait()
    print(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RES, "demo.mp4"))
