#!/usr/bin/env bash

cd $(dirname $0)

./demo
unzip -t test-output.zip

if [ $? -ne 0 ]; then
    echo "-- FAIL --"
    exit 1
else
    echo "-- PASS --"
    exit 0
fi
