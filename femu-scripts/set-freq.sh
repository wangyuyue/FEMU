#!/bin/bash

cpupower() {
  echo "use performance governor for all cpus"
  sudo cpupower frequency-set --governor performance > /dev/null

  CSD_CPUS="$1-$2"

  CSD_FREQ="1.2GHz"
  echo "limit CSD cpus [$CSD_CPUS] to $CSD_FREQ"
  sudo cpupower -c $CSD_CPUS frequency-set -u $CSD_FREQ > /dev/null
}

manual() {
  echo "use performance governor for all cpus"
  for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > $governor
  done

  echo "limit CSD cpus [$CSD_CPUS] to $CSD_FREQ"
  for ((i=$1; i<=$2; i++)); do
    echo 1200000 | sudo tee /sys/devices/system/cpu/cpu$i/cpufreq/scaling_max_freq
  done
}
