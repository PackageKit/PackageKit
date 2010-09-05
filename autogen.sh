#!/bin/sh
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Run this to generate all the initial makefiles, etc.
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(test -f $srcdir/configure.ac) || {
    echo -n "**Error**: Directory \"\'$srcdir\'\" does not look like the"
    echo " top-level package directory"
    exit 1
}

if ([ -z "$*" ] && [ "x$NOCONFIGURE" = "x" ]) ; then
  echo "**Warning**: I am going to run 'configure' with no arguments."
  echo "If you wish to pass any to it, please specify them on the"
  echo "'$0' command line."
  echo
fi

# check for gobject-introspection-devel
(which g-ir-scanner &> /dev/null) || {
    echo "**Error**: you don't have gobject-introspection installed"
    exit 1
}

(cd $srcdir && gtkdocize) || exit 1
(cd $srcdir && autoreconf --force --install) || exit 1
(cd $srcdir && intltoolize) || exit 1

conf_flags="--enable-gtk-doc"

if test x$NOCONFIGURE = x; then
  echo Running $srcdir/configure $conf_flags "$@" ...
  $srcdir/configure $conf_flags "$@" \
  && echo Now type \`make\' to compile. || exit 1
else
  echo Skipping configure process.
fi
