#!/bin/sh
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

time=0.01

for i in `seq 1 100`
do
	echo -e "percentage\t$i"
	echo -e "package\tavailable\tpolkit;0.0.1;i386;data\tPolicyKit daemon"
	echo -e "package\tinstalled\tpolkit-gnome;0.0.1;i386;data\tPolicyKit helper for GNOME"
	sleep ${time}
done

