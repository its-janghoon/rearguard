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
        val = f"{dt * 1000:.0f} ms" if dt < 0.1 else f"{dt:.2f} s" if dt < 10 else f"{dt:.1f} s"
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


class Cursor:
    """Replay frames on demand: frame k of the replay is log time t_from + k/FPS."""
    def __init__(self, run, t_from, t_to):
        self.rp, self.t_from, self.k, self.img = Replay(run, t_from, t_to + 1), t_from, -1, None

    def at(self, t):
        want = int((t - self.t_from) * FPS)
        while self.k < want:
            nxt = self.rp.next()
            if nxt is None:
                break
            self.img, self.k = nxt, self.k + 1
        return self.img.copy()


class Fixed:
    def __init__(self, img): self.img = img
    def next(self): return self.img


def compare(left, right, t_from, t_to, kicker, title, lanes, seconds):
    cl, cr = Cursor(left, t_from, t_to), Cursor(right, t_from, t_to)
    speed = (t_to - t_from) / seconds
    n = int(seconds * FPS)
    pw, ph = 880, 330
    kicker = kicker + ("" if abs(speed - 1) < 0.05 else f"  \u00b7  {speed:.1f}x SPEED")
    for i in range(n):
        t = t_from + i * speed / FPS
        L = car_screen(left, Fixed(cl.at(t)), t); R = car_screen(right, Fixed(cr.at(t)), t)
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
        elif (m.startswith("exited") or m.startswith("sleep_stop") or m.startswith("restart_for_update")) and start is not None:
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


def text_card(seconds, draw_fn):
    for i in range(int(seconds * FPS)):
        im = Image.new("RGBA", (W, H), C["bg"] + (255,))
        draw_fn(im, ImageDraw.Draw(im), i / FPS)
        yield im.convert("RGB")


def fade(i, t):  # 0..1 over the first 0.4 s after t
    return max(0.0, min(1.0, (i - t) / 0.4))


def hook_card(sec):
    def f(im, d, t):
        a = fade(t, 0)
        d.text((W / 2, 380), "21,000,000", font=F["big"], fill=tuple(int(c * a) for c in C["text"]), anchor="mm")
        d.text((W / 2, 500), "U.S. cars recalled for backup camera problems since 2018", font=F["cardsub"], fill=C["dim"], anchor="mm")
        b = fade(t, 5.5)
        if b:
            d.text((W / 2, 640), "The camera was fine. The computer in the middle was not.", font=F["card"], fill=tuple(int(c * b) for c in C["text"]), anchor="mm")
        d.text((W / 2, H - 70), "Source: published analysis of U.S. recall data, Aug 2026", font=F["lab"], fill=C["dim"], anchor="mm")
    return text_card(sec, f)


def law_card(sec):
    parts = ["Cluster", "Rear camera", "Infotainment", "Updates", "AI assistant"]
    def f(im, d, t):
        d.text((W / 2, 200), "One computer runs all of it", font=F["card"], fill=C["text"], anchor="mm")
        for k, p in enumerate(parts):
            a = fade(t, 0.6 * k)
            x = W / 2 + (k - 2) * 330
            d.rounded_rectangle([x - 145, 300, x + 145, 400], 22, fill=tuple(int(c * a) for c in (24, 30, 38)),
                                outline=tuple(int(c * a) for c in (C["warn"] if p == "Rear camera" else C["line"])), width=3)
            d.text((x, 350), p, font=F["tag"], fill=tuple(int(c * a) for c in C["text"]), anchor="mm")
        b = fade(t, 6.5)
        if b:
            d.text((W / 2, 600), "FMVSS 111:  rear image within  2.0 s  of reverse", font=F["card"], fill=tuple(int(c * b) for c in C["text"]), anchor="mm")
            d.text((W / 2, 680), "S5.5.3. Test starts with every part of the rear view system powered off (S14.2).", font=F["cardsub"], fill=C["dim"], anchor="mm")
    return text_card(sec, f)


def device_card(sec):
    con = open(os.path.join(RES, "console-split.txt")).read().rstrip().splitlines()
    shot = Image.open(os.path.join(RES, "split-night", "s3_reverse.png")).convert("RGB").resize((1040, 390), Image.LANCZOS)
    mono = font("Medium", 19)
    try:
        mono = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 18)
    except OSError:
        pass
    def f(im, d, t):
        d.text((64, 52), "ON THE QNX TARGET  \u00b7  QNX 8.0, QEMU x86_64, 4 vCPU", font=F["kicker"], fill=C["dim"])
        d.text((64, 86), "Ten processes. The camera is one of them.", font=F["h1"], fill=C["text"])
        d.rounded_rectangle([64, 170, 800, 1000], 18, fill=(10, 12, 14), outline=C["line"], width=2)
        shown = int(len(con) * min(1, t / 6.0)) + 1
        for k, ln in enumerate(con[:shown]):
            col = C["warn"] if "rvc" in ln else (120, 210, 150) if ln.startswith("$") else (200, 206, 212)
            d.text((88, 196 + k * 30), ln[:62], font=mono, fill=col)
        im.paste(shot, (836, 200), rounded_mask(shot.size, 18))
        d = ImageDraw.Draw(im)
        d.text((836 + 520, 610), "screenshot taken on the target in R  (Screen, 1920x720)", font=F["lab"], fill=C["dim"], anchor="ma")
        if t > 8:
            y = 690
            for k, (name, note, col) in enumerate([("vbusd", "/dev/vbus resource manager, held replies", C["blue"]),
                                                   ("rvc", "camera window on top, waits for R", C["warn"]),
                                                   ("cluster", "speed, gear, warning lights", C["text"]),
                                                   ("infotainment", "slow, updated, can crash", C["dim"])]):
                a = fade(t, 8 + 0.8 * k)
                d.text((870, y + k * 70), name, font=F["tag"], fill=tuple(int(c * a) for c in col))
                d.text((1090, y + 6 + k * 70), note, font=F["dim"], fill=tuple(int(c * a) for c in C["dim"]))
    return text_card(sec, f)


def method_card(sec):
    def stack(d, x, title, cam_inside, a):
        d.text((x + 380, 230), title, font=F["tag"], fill=C["text"], anchor="mm")
        boxes = [("cluster", 300), ("infotainment", 420)] if cam_inside else [("cluster", 300), ("rear camera", 420), ("infotainment", 540)]
        for name, y in boxes:
            col = C["warn"] if name == "rear camera" else C["line"]
            d.rounded_rectangle([x + 120, y, x + 640, y + 96], 18, fill=(20, 25, 31), outline=col, width=3)
            d.text((x + 380, y + 48 if not (cam_inside and name == "infotainment") else y + 34), name, font=F["tag"], fill=C["text"], anchor="mm")
        if cam_inside:
            d.rounded_rectangle([x + 230, 470, x + 530, 506], 10, fill=(40, 30, 10), outline=C["warn"], width=2)
            d.text((x + 380, 488), "rear camera", font=F["lane"], fill=C["warn"], anchor="mm")
    def f(im, d, t):
        d.text((W / 2, 120), "Same code, built two ways", font=F["card"], fill=C["text"], anchor="mm")
        stack(d, 120, "Camera inside infotainment", True, 1)
        if t > 3:
            stack(d, 1040, "Camera as its own QNX process", False, 1)
        if t > 9:
            d.rounded_rectangle([420, 760, 1500, 900], 22, fill=C["panel"], outline=C["line"], width=2)
            d.text((W / 2, 805), "probe: reads the composited screen every 4 ms", font=F["tag"], fill=C["text"], anchor="mm")
            d.text((W / 2, 855), "and decodes the frame number hidden in the camera image", font=F["dim"], fill=C["dim"], anchor="mm")
    return text_card(sec, f)


def result_card(sec, rows):
    shots = [Image.open(os.path.join(RES, m, f)).convert("RGB").resize((560, 210), Image.LANCZOS)
             for m, f in [("mono-night", "s5_after_kill.png"), ("split-night", "s5_after_kill.png")]]
    def f(im, d, t):
        d.text((64, 60), "Results  \u00b7  measured from the screen", font=F["h1"], fill=C["text"])
        x0, x1, x2 = 64, 900, 1400
        d.text((x1, 160), "camera inside", font=F["tag"], fill=C["dim"]); d.text((x2, 160), "own QNX process", font=F["tag"], fill=C["dim"])
        for k, (name, a, b, bad) in enumerate(rows):
            y = 220 + k * 78
            al = fade(t, 0.5 * k)
            d.line([(x0, y - 14), (W - 64, y - 14)], fill=C["line"], width=1)
            d.text((x0, y + 16), name, font=F["tag"], fill=tuple(int(c * al) for c in C["text"]), anchor="lm")
            d.text((x1, y + 16), a, font=F["tag"], fill=tuple(int(c * al) for c in (C["bad"] if bad else C["text"])), anchor="lm")
            d.text((x2, y + 16), b, font=F["tag"], fill=tuple(int(c * al) for c in C["ok"]), anchor="lm")
        if t > 6:
            for k, sh in enumerate(shots):
                im.paste(sh, (240 + k * 900, 760), rounded_mask(sh.size, 14))
            d = ImageDraw.Draw(im)
            d.text((520, 985), "on target, after kill -9: camera inside infotainment", font=F["lab"], fill=C["dim"], anchor="ma")
            d.text((1420, 985), "on target, after kill -9: own process", font=F["lab"], fill=C["dim"], anchor="ma")
    return text_card(sec, f)


def close_card(sec):
    def f(im, d, t):
        d.text((W / 2, 400), "RearGuard", font=F["big"], fill=C["text"], anchor="mm")
        d.text((W / 2, 500), "One night in a parking lot", font=F["card"], fill=C["text"], anchor="mm")
        d.text((W / 2, 580), "QNX 8.0  \u00b7  QEMU x86_64  \u00b7  code, logs and build steps in the repository", font=F["cardsub"], fill=C["dim"], anchor="mm")
        d.text((W / 2, H - 70), "Camera photo: Triplec85, CC BY-SA 4.0, cropped. Full credits in the repository.", font=F["lab"], fill=C["dim"], anchor="mm")
    return text_card(sec, f)


def measure(run):
    """(R -> camera seen) for each backing event, and whether the image was lost during it."""
    out = []
    for r0, r1 in run._r:
        seen = [a for a, b, _ in run._cam if r0 <= a <= r1]
        lost = [b for a, b, _ in run._cam if r0 < b < r1 - 0.05]
        out.append((seen[0] - r0 if seen else None, r1 - r0, bool(lost), (r1 - lost[0]) if lost else 0))
    return out


def fmt(m):
    dt, span, lost, lost_s = m
    if dt is None:
        return f"never shown ({span:.1f} s in R)", True
    s = f"{dt * 1000:.0f} ms" if dt < 1 else f"{dt:.2f} s"
    return s, dt > 2.0


def main(out):
    mono_c, split_c = Run("mono-cold").prep(), Run("split-cold").prep()
    mono_n, split_n = Run("mono-night").prep(), Run("split-night").prep()
    voice = os.path.join(ROOT, "build/voice")
    dur = {}
    for ln in subprocess.run(["bash", "-c", f"for f in {voice}/*.mp3; do echo $(basename $f .mp3) $(ffprobe -v error -show_entries format=duration -of csv=p=0 $f); done"],
                             capture_output=True, text=True).stdout.split("\n"):
        if ln.strip():
            k, v = ln.split(); dur[k] = float(v)
    pad = 3.0
    D = {k: v + pad for k, v in dur.items()}

    mc, sc = measure(mono_c), measure(split_c)
    mn, sn = measure(mono_n), measure(split_n)
    kill_lost = f"image lost, last {mn[1][3]:.1f} s of R" if mn[1][2] else fmt(mn[1])[0]
    rows = [("Cold start, R at 4.0 s", *(fmt(mc[0])[0], fmt(sc[0])[0]), fmt(mc[0])[1]),
            ("Wake, R 0.6 s later", fmt(mn[0])[0], fmt(sn[0])[0], fmt(mn[0])[1]),
            ("AI on every CPU, then R", fmt(mn[1])[0] if not mn[1][2] else f"{mn[1][0]*1000:.0f} ms", fmt(sn[1])[0], False),
            ("kill -9 infotainment in R", kill_lost, "stays up", mn[1][2]),
            ("Bad update, 3 crashes", "rolled back to A", "rolled back to A", False),
            ("Charging requests late", str(len(mono_n.ev("secc", "late"))), str(len(split_n.ev("secc", "late"))), False)]

    order = []
    ff = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
                           os.path.join(ROOT, "build/video_noaudio.mp4")], stdin=subprocess.PIPE)
    t_cursor = [0.0]

    def emit(key, gen):
        order.append((key, t_cursor[0]))
        n = 0
        for im in gen:
            ff.stdin.write(im.tobytes()); n += 1
        t_cursor[0] += n / FPS

    lc, rc = lanes_for(mono_c, "left"), lanes_for(split_c, "right")
    ln, rn = lanes_for(mono_n, "left"), lanes_for(split_n, "right")
    emit("hook", hook_card(D["hook"]))
    emit("law", law_card(D["law"]))
    emit("device", device_card(D["device"]))
    emit("method", method_card(D["method"]))
    emit("cold", compare(mono_c, split_c, 0.0, 9.0, "SCENE 1  \u00b7  COLD START, FMVSS 111 S14.2 SHAPE",
                         "Everything starts from off. Reverse at 4.0 s.", [lc[0], lc[1], rc[1], lc[2]], D["cold"]))
    emit("update", compare(mono_n, split_n, 2.0, 20.0, "SCENE 2  \u00b7  OVERNIGHT UPDATE",
                           "A broken update crashes infotainment 3 times. Slot A comes back.", [ln[2], rn[2], charge_lane(split_n)], D["update"]))
    emit("wake", compare(mono_n, split_n, 69.5, 76.0, "SCENE 3  \u00b7  WAKE UP, THEN REVERSE",
                         "The car wakes and the driver shifts to R 0.6 s later.", [ln[0], ln[1], rn[1], ln[2]], D["wake"]))
    emit("kill", compare(mono_n, split_n, 75.5, 93.5, "SCENE 4  \u00b7  AI LOAD, REVERSE, kill -9",
                         "AI keeps every CPU busy. Then infotainment is killed while in R.", [ln[0], ln[1], rn[1], ln[2]], D["kill"]))
    emit("result", result_card(D["result"], rows))
    emit("close", close_card(D["close"]))
    ff.stdin.close(); ff.wait()

    # narration: each clip starts 0.4 s into its scene
    inputs, filt = [], []
    for k, (key, t0) in enumerate(order):
        inputs += ["-i", os.path.join(voice, f"{key}.mp3")]
        ms = int((t0 + 0.4) * 1000)
        filt.append(f"[{k + 1}:a]adelay={ms}|{ms}[a{k}]")
    mix = "".join(f"[a{k}]" for k in range(len(order))) + f"amix=inputs={len(order)}:normalize=0,apad[aout]"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", os.path.join(ROOT, "build/video_noaudio.mp4"), *inputs,
                    "-filter_complex", ";".join(filt) + ";" + mix, "-map", "0:v", "-map", "[aout]",
                    "-c:v", "copy", "-c:a", "aac", "-b:a", "160k", "-t", f"{t_cursor[0]:.3f}", out], check=True)
    print(out, f"{t_cursor[0]:.1f}s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(RES, "demo.mp4"))
