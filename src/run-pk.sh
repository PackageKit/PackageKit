#!/bin/sh

if [ "$1x" = "x" ]; then
    BACKEND=dummy
else
    BACKEND=$1
fi
export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --disable-timer --backend=$BACKEND | tee debug.log

