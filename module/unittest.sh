#!/bin/bash

# Source - exec
declare -A files
files=( ["queue.c"]="queue.unittest" \
	["list.c"]="list.unittest"\
	["hashmap.c"]="hashmap.unittest"\
)

COLUMN=48
DIV_LINE=$(printf '=%.0s' $(seq 1 $COLUMN))

# Compile and run each unit test.
for src in "${!files[@]}"; do
    out=${files[$src]}
    gcc -g -D_UNIT_TEST_ -pthread -lpthread -o "$out" "$src" > /dev/null
    if [ $? -eq 0 ]; then
		echo "$DIV_LINE"
        echo "$out"
        ./"$out" $1
    else
        echo "Failed to compile $src"
    fi
done

echo

# rm *.unittest
