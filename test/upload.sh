#!/bin/bash

# usage: ./load_test.sh [file path] [iteration]"

iter=20
target="data/data13mb.jpg"

if [ $# -eq 1 ]; then
	target="$1"
elif [ $# -eq 2 ]; then
	target="$1"
	iter=$2
fi


for i in $(seq 1 $iter); do
    mv "$target" "$target-$i" && 
		./client.test 0 23455 "$target-$i" $(($i % 2)) > /dev/null
	if [ $? -eq 0 ]; then
		printf "[upload] $target-%-3s\t\033[32m%6s\033[0m\n" "$i" "[PASS]"
	else
		printf "[upload] $target-%-3s\t\033[31m%6s\033[0m\n" "$i" "[FAIL]"
	fi
    mv "$target-$i" "$target"
done

