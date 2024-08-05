#!/bin/bash

iter=20

if [ $# -eq 1 ]; then
	iter=$1
fi

for i in {1..$iter}; do
    mv "data500mb" "data500mb-$i"
	./client.out 0 23455 data500mb-$i 0
    mv "data500mb-$i" "data500mb"
done
