#!/bin/bash
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

trap trap_quit QUIT

trap_quit ()
{
	echo "Unlocking!"
	exit;
}

time=0.30

echo "Locking!"
echo -e "percentage\t0" > /dev/stderr
sleep ${time}
echo -e "percentage\t10" > /dev/stderr
sleep ${time}
echo -e "percentage\t20" > /dev/stderr
sleep ${time}
echo -e "percentage\t30" > /dev/stderr
sleep ${time}
echo -e "percentage\t40" > /dev/stderr
sleep ${time}
echo -e "percentage\t50" > /dev/stderr
sleep ${time}
echo -e "percentage\t60" > /dev/stderr
sleep ${time}
echo -e "percentage\t70" > /dev/stderr
sleep ${time}
echo -e "percentage\t80" > /dev/stderr
sleep ${time}
echo -e "percentage\t90" > /dev/stderr
sleep ${time}
echo -e "percentage\t100" > /dev/stderr
echo "Unlocking!"

