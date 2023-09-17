#!/bin/bash

NR_VCPU=4

NR_CPU=$(cat /proc/cpuinfo | grep processor | wc -l)

CSD_CPU0=$(lscpu | grep "Core(s) per socket" | awk '{print $4}')

GRUB_PARAM=""

ISOL_CPU="0-$((NR_VCPU-1)),$CSD_CPU0-12"

if [ "$(cat /sys/devices/system/cpu/isolated)" != $ISOL_CPU ]; then
  GRUB_PARAM+="isolcpus=${ISOL_CPU} nohz=on nohz_full=${ISOL_CPU}"
fi

if [ $(lscpu | grep -E "Meltdown|Spectre" | wc -l) -eq 0 ]; then
  GRUB_PARAM+=" mitigations=off"
fi

if [ $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver) != "acpi-cpufreq" ]; then
  GRUB_PARAM+=" intel_pstate=disable"
fi

if [ $(cat /sys/module/intel_idle/parameters/max_cstate) -ne 0 ]; then
  GRUB_PARAM+=" intel_idle.max_cstate=0"
fi

if [ -n "$GRUB_PARAM" ]; then
  sudo echo "GRUB_CMDLINE_LINUX_DEFAULT=\"$GRUB_PARAM\"" >> /etc/default/grub
  sudo update-grub
  echo "sudo reboot to enable grub parameters"
else
  echo "all grub configs are updated"
fi

# verify commands
# cat /proc/cmdline

echo "disable smt"
echo "off" | sudo tee "/sys/devices/system/cpu/smt/control" > /dev/null
