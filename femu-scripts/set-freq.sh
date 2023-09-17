#!/bin/bash

echo "use performance governor for all cpus"
sudo cpupower frequency-set --governor performance > /dev/null

CSD_CPUS="$1-$2"

CSD_FREQ="1.2GHz"
echo "limit CSD cpus [$CSD_CPUS] to $CSD_FREQ"
sudo cpupower -c $CSD_CPUS frequency-set -u $CSD_FREQ > /dev/null
