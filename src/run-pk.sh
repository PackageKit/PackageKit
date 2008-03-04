#!/bin/sh

if [ "$USER" != "root" ]; then
    echo "You are not running this script as root. Use sudo."
    exit 1
fi

if [ "$1x" = "x" ]; then
    BACKEND=dummy
else
    BACKEND=$1
fi
export G_DEBUG=fatal_criticals
killall packagekitd
./packagekitd --verbose --disable-timer --backend=$BACKEND

