#!/bin/bash
# Waits for Dota 2 to start and injects libDota2Helper.so once per game process.
here="$(cd "$(dirname "$0")" && pwd)"
done_pid=""

while true; do
    pid=$(pidof dota2)
    if [ -n "$pid" ] && [ "$pid" != "$done_pid" ]; then
        done_pid="$pid"
        "$here/inject.sh"
    fi
    sleep 2
done
