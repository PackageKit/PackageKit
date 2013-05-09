#!/bin/sh
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
echo -e "percentage\t0"
sleep ${time}
echo -e "percentage\t10"
sleep ${time}
echo -e "percentage\t20"
sleep ${time}
echo -e "percentage\t30"
sleep ${time}
echo -e "percentage\t40"
sleep ${time}
echo -e "percentage\t50"
sleep ${time}
echo -e "percentage\t60"
sleep ${time}
echo -e "percentage\t70"
sleep ${time}
echo -e "percentage\t80"
sleep ${time}
echo -e "percentage\t90"
sleep ${time}
echo -e "percentage\t100"
echo "Unlocking!"

