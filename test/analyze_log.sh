#!/bin/bash

if [ $# -ne 1 ]; then
	echo "usage: ./analyze_log.sh [test file size]"
	exit 1
fi

LOG_FILE="server_test.log"

start_time=$(awk 'NR==5 {print $2}' $LOG_FILE)
sh=$(echo "$start_time" | awk -F ':' '{print $1}')
sm=$(echo "$start_time" | awk -F ':' '{print $2}')
ss=$(echo "$start_time" | awk -F ':' '{print $3}')
sms=$(echo "$start_time" | awk -F ':' '{print $4}')
st_ms=$(($sh*60*60*1000 + $sm*60*1000 + $ss*1000 + $sms))

end_time=$(awk 'END {print $2}' $LOG_FILE)
eh=$(echo "$end_time" | awk -F ':' '{print $1}')
em=$(echo "$end_time" | awk -F ':' '{print $2}')
es=$(echo "$end_time" | awk -F ':' '{print $3}')
ems=$(echo "$end_time" | awk -F ':' '{print $4}')
et_ms=$(($eh*60*60*1000 + $em*60*1000 + $es*1000 + $ems))

elapsed_time=$((et_ms - st_ms))

connections=$(grep -c 'new connection' $LOG_FILE)
# failed=$(grep -c 'FAIL' client_test.log)
successed=$(grep -c 'Upload complete' $LOG_FILE)
requests=$(($failed + $successed))

max_sessions=$(grep 'MAX_SESSIONS' ../server.h | awk '{print $3}')
worker_num=$(grep 'SESSION_WORKER_NUM' ../server.h | awk '{print $3}')

# max_sessions worker_num elapsed_time requests successed file_size
printf "%d %d %s %s %d %d %d %d\n" \
		   $max_sessions $worker_num \
		   "$start_time" "$end_time" \
		   $elapsed_time $requests $successed $1 >> ./test_report.txt
