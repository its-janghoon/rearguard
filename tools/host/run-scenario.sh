#!/bin/bash
# Host side: deploy the build, run one scenario/mode on the guest, pull results.
#   run-scenario.sh <mode> <scenario>
MODE=${1:-split}; SC=${2:-night}
R=$HOME/workplace/rearguard/results
mkdir -p "$R"
C="python3 $HOME/qnx-setup/con.py"
$C 'sh /data/qc/qc-pull.sh >/dev/null' 300 >/dev/null || { echo pull failed; exit 1; }
$C "sudo -n sh /data/qc/qc-run.sh $MODE $SC >/dev/null 2>&1; echo done" 600 >/dev/null || { echo run failed; exit 1; }
$C "base64 /data/qc/results/$MODE-$SC.tgz" 600 > "$R/$MODE-$SC.b64" || { echo fetch failed; exit 1; }
base64 -d -i "$R/$MODE-$SC.b64" > "$R/$MODE-$SC.tgz" && rm "$R/$MODE-$SC.b64"
rm -rf "$R/$MODE-$SC"; tar -xzf "$R/$MODE-$SC.tgz" -C "$R" && rm "$R/$MODE-$SC.tgz"
for b in "$R/$MODE-$SC"/*.bmp; do [ -f "$b" ] && magick "$b" "${b%.bmp}.png" && rm "$b"; done
echo "$R/$MODE-$SC"
