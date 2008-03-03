#!/bin/bash
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

time=0.01

for i in `seq 1 100`
do
	echo -e "percentage\t$i" > /dev/stderr
	echo -e "package:installed\tpackage\tpolkit\tPolicyKit daemon"
	echo -e "package:available\tpackage\tpolkit-gnome\tPolicyKit helper for GNOME"
	sleep ${time}
done

