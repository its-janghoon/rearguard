#!/bin/sh
# Run one scenario in one mode and pack the results (run as root on the guest).
#   qc-run.sh <mode> <scenario>     e.g. qc-run.sh split night
MODE=${1:-split}
SC=${2:-night}
QC=/data/qc
if [ "$SC" = cold ]; then
    # cold start: the scenario clock starts together with the car software
    sh $QC/qc-start.sh stop
    rm -f $QC/run/events.log
    $QC/bin/vsim $QC/scenarios/$SC.txt &
    QC_NOSLAY=1 sh $QC/qc-start.sh $MODE
    sleep 14                   # cold.txt ends at 11 s
else
    sh $QC/qc-start.sh $MODE
    sleep 10                   # let infotainment finish its first start
    $QC/bin/vsim $QC/scenarios/$SC.txt
fi
sleep 4
OUT=$QC/results/$MODE-$SC
rm -rf $OUT; mkdir -p $OUT
cp $QC/run/events.log $OUT/
for f in $QC/run/*.bmp; do [ -f "$f" ] && mv "$f" $OUT/; done
cd $QC/results && tar -czf $MODE-$SC.tgz $MODE-$SC && echo "result $QC/results/$MODE-$SC.tgz"
