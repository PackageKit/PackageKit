#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir
PACKAGE="packagekit-plugin"

have_libtool=false
have_autoconf=false
have_automake=false
need_configure_in=false

if libtool --version < /dev/null > /dev/null 2>&1 ; then
	libtool_version=`libtoolize --version | sed 's/^[^0-9]*\([0-9.][0-9.]*\).*/\1/'`
	have_libtool=true
	case $libtool_version in
	    1.3*)
		need_configure_in=true
		;;
	esac
fi

if autoconf --version < /dev/null > /dev/null 2>&1 ; then
	autoconf_version=`autoconf --version | sed 's/^[^0-9]*\([0-9.][0-9.]*\).*/\1/'`
	have_autoconf=true
	case $autoconf_version in
	    2.13)
		need_configure_in=true
		;;
	esac
fi

if $have_libtool ; then : ; else
	echo;
	echo "You must have libtool >= 1.3 installed to compile $PACKAGE";
	echo;
	exit;
fi

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo;
	echo "You must have automake installed to compile $PACKAGE";
	echo;
	exit;
}

(intltoolize --version) < /dev/null > /dev/null 2>&1 || {
	echo;
	echo "You must have intltool installed to compile $PACKAGE";
	echo;
	exit;
}

echo "Generating configuration files for $PACKAGE, please wait...."
echo;

if $need_configure_in ; then
    if test ! -f configure.in ; then
	echo "Creating symlink from configure.in to configure.ac..."
	echo
	ln -s configure.ac configure.in
    fi
fi

aclocal $ACLOCAL_FLAGS
libtoolize --force
autoheader
automake --add-missing -Woverride
autoconf
intltoolize

cd $ORIGDIR || exit $?

$srcdir/configure $@ --enable-maintainer-mode --enable-compile-warnings

