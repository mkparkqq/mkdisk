#!/bin/bash

# usage: ./fncpy.sh [file path] [n]

if [ $# -ne 2 ]; then
	echo "usage: ./fncpy.sh [file path] [n]"
fi

target=$1
iter=$2

for i in $(seq 1 $iter); do
	cp $target "$target-$i"
done


