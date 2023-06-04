#!/bin/bash

vcpu_spid=$1



for ((i = vcpu_spid, j = 5; i <= vcpu_spid + 3; i++, j++)); do
    taskset -cp $j $i
    echo "----------"
done
