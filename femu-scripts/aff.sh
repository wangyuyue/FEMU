#!/bin/bash

spid=$1

for ((i = spid, j = 32; i <= spid + 16; i++, j++)); do
    taskset -cp $j $i
    echo "----------"
done
