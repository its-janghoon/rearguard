#!/bin/bash
# Narration with Amazon Polly (generative engine) -> build/voice/<id>.mp3
#   AWS_PROFILE=<profile> tools/tts.sh
set -u
cd "$(dirname "$0")/.."
mkdir -p build/voice
VOICE=${VOICE:-Matthew}
python3 -c 'import json,sys; [print(s["id"]+"\t"+s["text"]) for s in json.load(open("tools/narration.json"))]' |
while IFS=$'\t' read -r id text; do
    aws polly synthesize-speech --region us-east-1 --engine generative --voice-id "$VOICE" \
        --output-format mp3 --sample-rate 24000 --text "$text" "build/voice/$id.mp3" >/dev/null || { echo "failed $id"; exit 1; }
    printf '%s %s\n' "$id" "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "build/voice/$id.mp3")"
done
