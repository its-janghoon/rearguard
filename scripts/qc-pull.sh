#!/bin/sh
# Fetch binaries, assets and scenarios from the host (QEMU user net: host = 10.0.2.2).
H=${1:-http://10.0.2.2:8765}
QC=/data/qc
mkdir -p $QC/bin $QC/assets $QC/scenarios $QC/run
for b in vbusd vsim camsim rvc cluster infotainment supervisor copilot otad secc evcc probe; do
    # download beside, then rename: a running binary cannot be overwritten in place
    curl -sf -o $QC/bin/$b.new $H/build/x86_64/$b && chmod 755 $QC/bin/$b.new && mv -f $QC/bin/$b.new $QC/bin/$b || echo "missing $b"
done
curl -sf -o $QC/assets/cam.bgra $H/assets/cam.bgra
for s in $(curl -sf $H/scenarios/list.txt); do curl -sf -o $QC/scenarios/$s $H/scenarios/$s; done
for s in qc-start.sh qc-pull.sh qc-run.sh; do curl -sf -o $QC/$s $H/scripts/$s; chmod 755 $QC/$s; done
echo pulled
