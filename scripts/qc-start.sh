#!/bin/sh
# Start the whole car on the QNX guest (run as root).
#   qc-start.sh split   camera is its own process (this project)
#   qc-start.sh mono    camera lives inside infotainment (like the recalled cars)
#   qc-start.sh fixed   split, but vbusd channel without priority inheritance
MODE=${1:-split}
QC=/data/qc
B=$QC/bin

if [ "$QC_NOSLAY" != 1 ]; then
    slay -f -Q vsim otad probe copilot evcc secc supervisor infotainment cluster rvc camsim vbusd >/dev/null 2>&1
    sleep 1
fi
[ "$MODE" = stop ] && exit 0
mkdir -p $QC/run $QC/info/a
[ "$QC_NOSLAY" != 1 ] && rm -f $QC/run/events.log
rm -rf $QC/info/b
cp $B/infotainment $QC/info/a/infotainment
echo a > $QC/info/next

if [ ! -e /dev/screen/.attach ]; then
    sed "s/1280 x 768/1920 x 720/" /system/share/screen/graphics-virtual-display.conf > $QC/run/screen.conf
    screen -c $QC/run/screen.conf
    waitfor /dev/screen 10
fi

if [ "$MODE" = fixed ]; then $B/vbusd --fixed-prio & else $B/vbusd & fi
waitfor /dev/vbus 5
$B/camsim $QC/assets/cam.bgra &
$B/probe &
[ "$MODE" != mono ] && $B/rvc &
$B/cluster &
$B/secc &
$B/evcc &
$B/copilot 4 &
if [ "$MODE" = mono ]; then $B/supervisor --camera-in-info & else $B/supervisor & fi
echo "qc started mode=$MODE"
