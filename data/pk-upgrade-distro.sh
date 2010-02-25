#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# This file is designed to be run by a not privileged user, NOT ROOT.
# The tool which is invoked will have to use consolehelper or PolicyKit
# if privileged changes are required.
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

DISTRO=""
if [ -e /usr/bin/lsb_release ]; then
	DISTRO=$(/usr/bin/lsb_release -is)
fi

# Fedora uses preupgrade
if [ -e /etc/fedora-release ]; then
	if [ -e /usr/bin/preupgrade ]; then
		/usr/bin/preupgrade
	else
		xdg-open http://fedoraproject.org/en/get-fedora
	fi
elif [ "$DISTRO" = "Ubuntu" ]; then
	if [ -e /usr/bin/do-release-upgrade ]; then
		if [ "$DESKTOP" = "kde" ]; then
			PATH=`kde4-config --path exe` kdesu -- "/usr/bin/do-release-upgrade -d -m desktop -f kde -p"
		else
			gksu "/usr/bin/do-release-upgrade -m desktop -f gtk -p"
		fi
	elif [ "$DESKTOP" = "kde" ]; then
		xdg-open http://www.kubuntu.org/getkubuntu
	else
		xdg-open http://www.ubuntu.com/getubuntu
	fi
elif [ -e /etc/SuSE-release ] && [ -x /usr/sbin/wagon ]; then
	xdg-su -c /usr/sbin/wagon
else
	TITLE="System is not recognised"
	TEXT="Your distribution was not recognised by the upgrade script.\nPlease file a but in your distribution bug tracker under the component PackageKit."
	if [ "$DESKTOP" = "kde" ]; then
		PATH=`kde4-config --path exe` kdialog --title "$TITLE" --sorry "$TEXT"
	# do not dep on zenity in build scripts
	elif [ "`which zenity 2> /dev/null > /dev/null; echo $?`" -eq 0 ]; then
		zenity --warning --title $TITLE --text $TEXT
	else
		xmessage $TEXT
	fi
fi

