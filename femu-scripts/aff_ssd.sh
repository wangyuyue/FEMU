runtime_id=$(grep "runtime thread" log | awk '{print $NF}')
ftl_id=$(grep "ftl thread" log | awk '{print $NF}')
poller_ids=$(grep -E "poller [0-9]+ thread" log | awk '{print $NF}' | paste -sd ',')
echo $runtime_id
echo $ftl_id
echo $poller_ids
taskset -cp 0-4 $runtime_id
taskset -cp 0-4 $ftl_id
taskset -cp 0-4 $poller_ids