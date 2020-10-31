#!/bin/bash

players=$1

actual=$(timeout 10 ltrace -e pthread_join hippos $players 1000 5 2>&1 | grep -c pthread_join)

expected=$(($players + 1))

if [ $actual -eq $expected ]
then
    echo Test Passed: all the expected pthread_join calls were made
else
    echo Test Failed: Expected $expected threads to exit. 
    echo There are probably some threads stuck waiting on a condition variable or lock
fi
