#!/bin/sh
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

time=0.5

echo -e "percentage\t0"
echo -e "percentage\t10"
sleep ${time}
echo -e "percentage\t20"
sleep ${time}
echo -e "percentage\t30"
sleep ${time}
echo -e "percentage\t40"
sleep ${time}
echo -e "package\tavailable\tpolkit;0.0.1;i386;data\tPolicyKit daemon"
echo -e "package\tinstalled\tpolkit-gnome;0.0.1;i386;data\tPolicyKit helper (•) for GNOME"
sleep ${time}
printf "package\tavailable\tConsoleKit"
sleep ${time}
echo -e "\tSystem console checker"
echo -e "percentage\t50"
sleep ${time}
echo -e "percentage\t60"
sleep ${time}
echo -e "percentage\t70"
sleep ${time}
echo -e "percentage\t80"
sleep ${time}
echo -e "percentage\t90"
echo -e "package\tinstalled\tgnome-power-manager;0.0.1;i386;data\tMore useless software"
sleep ${time}
echo -e "percentage\t100"

