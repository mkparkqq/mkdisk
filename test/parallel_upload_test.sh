#!/bin/bash

if [ $# -ne 3 ]; then
	echo "usage: ./parallel_upload_test.sh [file to upload] [nexe] [count]"
	exit 1
fi

# target파일을 이름을 바꿔가며 nexe 개의 클라이언트에서 동시에 count번 업로드

sip="0"
sport="23455"
target="$1"
nexe=$2
count=$3

for i in $(seq 1 $nexe); do
	cp $target "$target-p$i"
done

for i in $(seq 1 $nexe); do
	./upload_test.sh "$target-p$i" "$count" &
done

wait

rm $target-p*
