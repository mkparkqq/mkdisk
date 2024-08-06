#!/bin/bash
server_pid=$(ps aux | grep server.out | awk 'NR==1 {print $2}')
sudo kill $server_pid

rm -rf ./server_test.log ./.downloads_test 218.*
