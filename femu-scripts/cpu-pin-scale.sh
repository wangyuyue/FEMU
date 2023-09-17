#!/bin/bash
# pin all vCPUs and the main threads

./pin.sh

echo "pin CSD threads"
pid_cmd='pidstat -t -G qemu-system-x86'

CSD_TIDS=($($pid_cmd | grep "FEMU" | awk '{ print $5 }'))

NR_CSD_CORE=${#CSD_TIDS[@]}

CSD_CORE0=$(lscpu | grep "per socket" | awk '{print $4}')

for i in ${!CSD_TIDS[@]}; do
  TID=${CSD_TIDS[$i]}
  taskset -cp $((CSD_CORE0 + i)) $TID
done

sudo ./set-freq.sh $CSD_CORE0 $((CSD_CORE0 + NR_CSD_CORE -1))
