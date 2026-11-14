#!/usr/bin/env python3
"""Write results/demo.en.srt and results/demo.ko.srt from the narration and the voice lengths,
using the same scene timing as make_video.py (each scene = voice + 3 s, voice starts 0.4 s in)."""
import json, os, re, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDER = ["hook", "law", "device", "method", "cold", "update", "wake", "kill", "result", "close"]
KO = {
    "hook": ["2018년부터 미국에서 약 2,100만 대가 후방카메라 문제로 리콜됐습니다.", "대부분 카메라는 멀쩡했습니다.", "문제는 차 한가운데 있는 컴퓨터였습니다."],
    "law": ["요즘은 컴퓨터 한 대가 계기판, 후방카메라, 인포테인먼트, 업데이트, 이제는 AI 비서까지 돌립니다.", "미국 안전 기준은 후진에 넣고 2초 안에 후방 화면이 떠야 한다고 정합니다."],
    "device": ["이게 우리 자동차 컴퓨터입니다. QEMU에서 QNX 8.0으로 돕니다.", "프로세스 10개가 마이크로커널로 서로 떨어져 있습니다.", "후방카메라는 자기 프로세스와 자기 창을 맨 위에 갖습니다.", "차량 신호 버스는 QNX 리소스 매니저이고, 기어가 바뀌는 순간 답을 받습니다."],
    "method": ["같은 코드를 두 가지로 만들었습니다.", "왼쪽은 리콜된 차들처럼 카메라가 인포테인먼트 안에 있습니다.", "오른쪽은 카메라가 따로 도는 QNX 프로세스입니다.", "따로 둔 측정기가 4ms마다 화면을 읽습니다. 사람이 화면을 찍는 것과 같습니다."],
    "cold": ["장면 1은 연방 시험 방식을 따릅니다.", "모두 꺼진 상태에서 시작하고, 4초에 후진에 넣습니다.", "왼쪽은 인포테인먼트가 켜질 때까지 카메라가 기다려야 합니다.", "2.36초.", "실패입니다.", "오른쪽은 약 5ms 만에 화면이 뜹니다."],
    "update": ["장면 2.", "밤에 충전하는 동안 망가진 인포테인먼트 업데이트가 깔립니다.", "세 번 죽자 감시자가 옛 버전으로 되돌립니다.", "충전 요청은 한 번도 놓치지 않습니다."],
    "wake": ["장면 3.", "아침에 차가 깨어나고, 운전자가 바로 후진에 넣습니다.", "왼쪽은 후진하는 내내 카메라가 뜨지 않습니다.", "오른쪽은 3ms 만에 떠 있습니다."],
    "kill": ["장면 4.", "AI 비서가 모든 CPU를 바쁘게 해도, 후진은 양쪽 다 됩니다.", "그다음 인포테인먼트를 kill -9로 죽입니다.", "왼쪽은 카메라가 꺼집니다.", "오른쪽은 인포테인먼트만 다시 켜집니다.", "카메라와 계기판은 계속 돕니다."],
    "result": ["같은 차, 같은 CPU, 같은 코드.", "다른 건 카메라가 어디 있느냐뿐입니다.", "QNX 프로세스 분리로, 후방 화면은 더 이상 차에서 가장 크고, 느리고, 자주 바뀌는 프로그램에 기대지 않습니다."],
    "close": ["RearGuard.", "보신 것은 모두 QEMU 위 QNX 8.0에서 돌았습니다.", "코드, 기록, 빌드 방법은 저장소에 있습니다."],
}


def dur(p):
    return float(subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", p]))


def ts(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def main():
    en = {x["id"]: x["text"] for x in json.load(open(f"{ROOT}/tools/narration.json"))}
    out = {"en": [], "ko": []}
    t0 = 0.0
    for key in ORDER:
        L = dur(f"{ROOT}/build/voice/{key}.mp3")
        print(f"{key:8s} starts {int(t0 // 60)}:{t0 % 60:05.2f}")
        for lang, parts in (("en", [p for p in re.split(r"(?<=[.!?])\s+", en[key]) if p]), ("ko", KO[key])):
            total = sum(len(p) for p in parts)
            t = t0 + 0.4
            for p in parts:
                d = L * len(p) / total
                out[lang].append(f"{len(out[lang]) + 1}\n{ts(t)} --> {ts(t + d - 0.05)}\n{p}\n")
                t += d
        t0 += L + 3.0
    for lang, cues in out.items():
        open(f"{ROOT}/results/demo.{lang}.srt", "w").write("\n".join(cues))
    print("end", f"{t0:.1f}")


main()
