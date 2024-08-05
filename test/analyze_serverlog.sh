#!/bin/bash

# LOG_FILE="server_test.log"
LOG_FILE="serverlog"

# 시간 추출 및 정렬
awk -F '[][]' '{print $2}' "$LOG_FILE" | sort | tee sorted_times.txt | {
    read -r earliest
    read -r latest
	printf "%-11s %s\n" "Test start" "$earliest"
	printf "%-11s %s\n" "Test end" "$latest"
}
