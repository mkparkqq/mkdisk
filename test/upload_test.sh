#!/bin/bash

# usage: ./load_test.sh [file path] [iteration]"

iter=20
target="data500mb"

if [ $# -eq 1 ]; then
	target="$1"
elif [ $# -eq 2 ]; then
	target="$1"
	iter=$2
fi


for i in $(seq 1 $iter); do
	printf "upload $target-%-3s\t"  "$i"
    mv "$target" "$target-$i" && 
		./client.test 0 23455 "$target-$i" 0 > /dev/null
	if [ $? -eq 0 ]; then
		printf "\033[32m%6s\033[0m\n" "[PASS]"
	else
		printf "\033[31m%6s\033[0m\n" "[FAIL]"
	fi
    mv "$target-$i" "$target"
done
