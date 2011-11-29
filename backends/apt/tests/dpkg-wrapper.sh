#!/bin/sh
echo "yeah"
exec /usr/bin/fakeroot /usr/bin/dpkg $*
