#!/bin/bash

if [ $# -ne 1 ]; then
	echo "usage: ./analyze_log.sh [test file size]"
	exit 1
fi

LOG_FILE="server_test.log"

start_time=$(awk 'NR==5 {print $2}' $LOG_FILE)
sh=$(echo "$start_time" | awk -F ':' '{print $1}' | sed 's/^0*//')
sm=$(echo "$start_time" | awk -F ':' '{print $2}' | sed 's/^0*//')
ss=$(echo "$start_time" | awk -F ':' '{print $3}' | sed 's/^0*//')
sms=$(echo "$start_time" | awk -F ':' '{print $4}' | sed 's/^0*//')
st_ms=$(($sh*60*60*1000 + $sm*60*1000 + $ss*1000 + $sms))

end_time=$(awk 'END {print $2}' $LOG_FILE)
eh=$(echo "$end_time" | awk -F ':' '{print $1}' | sed 's/^0*//')
em=$(echo "$end_time" | awk -F ':' '{print $2}' | sed 's/^0*//')
es=$(echo "$end_time" | awk -F ':' '{print $3}' | sed 's/^0*//')
ems=$(echo "$end_time" | awk -F ':' '{print $4}' | sed 's/^0*//')
et_ms=$(($eh*60*60*1000 + $em*60*1000 + $es*1000 + $ems))

elapsed_time=$((et_ms - st_ms))

connections=$(grep -c 'new connection' $LOG_FILE)
successed=$(grep -c 'Finished to create the file' $LOG_FILE)
transmissions=$(grep -c 'Transmission complete' $LOG_FILE)
max_sessions=$(grep 'MAX_SESSIONS' ../server.h | awk '{print $3}')
worker_num=$(grep 'SESSION_WORKER_NUM' ../server.h | awk '{print $3}')

# (request = max_sessions)
# max_sessions worker_num elapsed_time transmissions successed file_size
printf "%d %d %s %s %d %d %d %d\n" \
		   $max_sessions $worker_num \
		   "$start_time" "$end_time" \
		   $elapsed_time $transmissions $successed $1 >> ./test_report.txt

printf "%d %d %s %s %d %d %d %d\n" \
		   $max_sessions $worker_num \
		   "$start_time" "$end_time" \
		   $elapsed_time $transmissions $successed $1
