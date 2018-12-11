# Copyright (C) 2007-2012 Richard Hughes <richard@hughsie.com>

m4_define([pk_major_version], [1])
m4_define([pk_minor_version], [1])
m4_define([pk_micro_version], [13])
m4_define([pk_version],
          [pk_major_version.pk_minor_version.pk_micro_version])

AC_PREREQ(2.63)
AC_INIT([PackageKit],
        [pk_version],
        [richard@hughsie.com],
        [PackageKit],
        [http://www.packagekit.org/])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11 -Wno-portability no-dist-gzip dist-xz tar-ustar])
AM_MAINTAINER_MODE([enable])

# enable nice build output on automake1.11
AM_SILENT_RULES([yes])

GOBJECT_INTROSPECTION_CHECK([0.9.9])

# Vala bindings
VAPIGEN_CHECK([0.16])

PK_MAJOR_VERSION=pk_major_version
PK_MINOR_VERSION=pk_minor_version
PK_MICRO_VERSION=pk_micro_version
PK_VERSION=pk_version
AC_SUBST(PK_MAJOR_VERSION)
AC_SUBST(PK_MINOR_VERSION)
AC_SUBST(PK_MICRO_VERSION)
AC_SUBST(PK_VERSION)

# libtool versioning - this applies to libpackagekit
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# - If interfaces have been changed or added, but binary compatibility
#   has been preserved, change:
#      CURRENT += 1
#      REVISION = 0
#      AGE += 1
# - If binary compatibility has been broken (eg removed or changed
#   interfaces), change:
#      CURRENT += 1
#      REVISION = 0
#      AGE = 0
# - If the interface is the same as the previous version, but bugs are
#   fixed, change:
#      REVISION += 1
LT_CURRENT=19
LT_REVISION=3
LT_AGE=1
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AS_ALL_LINGUAS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_SEARCH_LIBS([strerror],[cposix])
AC_HEADER_STDC
AM_PROG_CC_C_O

# Initialize libtool
LT_INIT

# Internationalisation
IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=PackageKit
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# set up gtk-doc
GTK_DOC_CHECK([1.11],[--flavour no-tmpl])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)

# Python2/3 stuff
AC_ARG_ENABLE(python3, AS_HELP_STRING([--enable-python3],[Use Python3 for PackageKit]), enable_python3=$enableval,
							enable_python3=$default_strict)
if test x$enable_python3 = xyes; then
	AM_PATH_PYTHON([3.2])
else
	AM_PATH_PYTHON([2.7])
fi
AC_ARG_WITH([python_package_dir],
	    AS_HELP_STRING([--with-python-package-dir=<location>],
			   [Location for python modules]))
if test -z "$with_python_package_dir" ; then
	PYTHON_PACKAGE_DIR=${pythondir}/packagekit
else
	PYTHON_PACKAGE_DIR=${with_python_package_dir}/packagekit
fi
AC_SUBST(PYTHON_PACKAGE_DIR)

if test "$GCC" = "yes"; then
	# work both in C and C++
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wall"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wmissing-declarations"
#	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wredundant-decls"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wpointer-arith"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wcast-align"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wwrite-strings"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Winit-self"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wreturn-type"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wformat-nonliteral"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wformat-security"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wmissing-include-dirs"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wmissing-format-attribute"
#	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wclobbered"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wempty-body"
#	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wignored-qualifiers"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wsign-compare"
#	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wtype-limits"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wuninitialized"

	# work only in C
	WARNINGFLAGS_C="$WARNINGFLAGS_CPP"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
#	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-parameter-type"
#	WARNINGFLAGS_C="$WARNINGFLAGS_C -Woverride-init"
else
	WARNINGFLAGS_C=""
	WARNINGFLAGS_CPP=""
fi
AC_SUBST(WARNINGFLAGS_C)
AC_SUBST(WARNINGFLAGS_CPP)

# check for PIE (position independent executable) support
AX_CHECK_COMPILE_FLAG([-fPIE],
		      [AX_CHECK_LINK_FLAG([-fPIE -pie],
					  [PIE_CFLAGS="-fPIE" PIE_LDFLAGS="-pie"])])
AC_SUBST(PIE_CFLAGS)
AC_SUBST(PIE_LDFLAGS)

# check for full RELRO (relocation read-only) support
AX_CHECK_LINK_FLAG([-Wl,-z,relro,-z,now],
		   [RELRO_LDFLAGS="-Wl,-z,relro,-z,now"])
AC_SUBST([RELRO_LDFLAGS])

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
GETTEXT_PACKAGE=PackageKit
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT

dnl ---------------------------------------------------------------------------
dnl - Library dependencies
dnl ---------------------------------------------------------------------------
GLIB_REQUIRED=2.54.0
GIO_REQUIRED=2.16.1
POLKIT_GOBJECT_REQUIRED=0.98

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0)
PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(GIO, \
 gio-2.0 >= $GIO_REQUIRED \
 gio-unix-2.0 >= $GIO_REQUIRED)
PKG_CHECK_MODULES(POLKIT, \
		  polkit-gobject-1 >= $POLKIT_GOBJECT_REQUIRED)

# 0.114 introduced autocleanup functions for its types.
PKG_CHECK_MODULES([POLKIT_0_114], [polkit-gobject-1 >= 0.114],
                  [have_polkit_0_114=yes], [have_polkit_0_114=no])
AS_IF([test "$have_polkit_0_114" = "yes"], [
	AC_DEFINE([HAVE_POLKIT_0_114],[1],
	          [Define as 1 if you have polkit >= 0.114])
])

# Avoid g_simple_async_result deprecation warnings in glib 2.46+
AC_DEFINE(GLIB_VERSION_MIN_REQUIRED, GLIB_VERSION_2_44, [minimum glib version])

dnl ---------------------------------------------------------------------------
dnl - FreeBSD compatibility
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS([execinfo.h])
AC_CHECK_FUNCS(clearenv)
AC_PATH_PROG(GMSGFMT, msgfmt, msgfmt)

dnl ---------------------------------------------------------------------------
dnl - Haiku compatibility
dnl ---------------------------------------------------------------------------
AC_CHECK_FUNCS(setpriority)

dnl ---------------------------------------------------------------------------
dnl - Use systemd and logind rather than ConsoleKit
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(systemd, AS_HELP_STRING([--enable-systemd],[enable systemd and logind code]),
	      enable_systemd=$enableval,enable_systemd=yes)
if test x$enable_systemd = xyes; then
	PKG_CHECK_MODULES(SYSTEMD, libsystemd >= 213)
	AC_ARG_WITH([systemdsystemunitdir],
		    AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
		    [has_systemdsystemunitdir=$with_systemdsystemunitdir],
		    [has_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
	AC_SUBST([systemdsystemunitdir], [$has_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$has_systemdsystemunitdir"])

dnl ---------------------------------------------------------------------------
dnl - Use systemd offline update
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(offline-update, AS_HELP_STRING([--enable-offline-update],[enable offline update via systemd]),
	      enable_offline_update=$enableval,enable_offline_update=yes)
AM_CONDITIONAL(ENABLE_OFFLINE_UPDATE, [test x$enable_systemd = xyes -a x$enable_offline_update = xyes])

dnl ---------------------------------------------------------------------------
dnl - Use elogind instead of systemd-login
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(elogind, AS_HELP_STRING([--enable-elogind],[enable elogind session tracker]),
	      enable_elogind=$enableval,enable_elogind=no)
if test x$enable_elogind = xyes; then
	PKG_CHECK_MODULES(ELOGIND, [libelogind >= 229.4], [have_elogind=yes], [have_elogind=no])
else
	have_elogind=no
fi

dnl ---------------------------------------------------------------------------
dnl --- Is systemd/sd-login.h, either from systemd or elogind, available?
dnl ---------------------------------------------------------------------------
if test -n "$has_systemdsystemunitdir" -o "x$have_elogind" != "xno" ; then
		AC_DEFINE(HAVE_SYSTEMD_SD_LOGIN_H,1,[Build systemd-login code])
fi

dnl ---------------------------------------------------------------------------
dnl - Generate man pages ? (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(man_pages, AS_HELP_STRING([--disable-man-pages],[Disable man pages generation]), enable_man_pages=$enableval)
if test x$enable_man_pages != xno; then
	AC_PATH_PROG(XSLTPROC, xsltproc, no)
	if test x$enable_man_pages = xyes; then
		if test "$XSLTPROC" = "no" ; then
			AC_MSG_ERROR([xsltproc not found, it is needed to build man pages])
		fi
	fi
else
	XSLTPROC=no
fi
AM_CONDITIONAL(HAVE_XSLTPROC, [test "$XSLTPROC" != "no"])

dnl **** Bash completion ****
AC_ARG_ENABLE(bash_completion, AS_HELP_STRING([--enable-bash-completion],[Enable bash completion]),
	      enable_bash_completion=$enableval, enable_bash_completion=yes)
if test x$enable_bash_completion != xno; then
	PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0])
	BASH_COMPLETIONS_DIR="`pkg-config --variable=completionsdir bash-completion`"
	AC_SUBST([BASH_COMPLETIONS_DIR])
fi
AM_CONDITIONAL([HAVE_BASH_COMPLETION],[test "x$enable_bash_completion" = "xyes"])

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)
AC_SUBST(LIBDIR, $libdir)
AC_SUBST(LIBEXECDIR, $libexecdir)
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)
AC_SUBST(LOCALSTATEDIR, $localstatedir)

AC_ARG_WITH([packagekit_user],
	    AS_HELP_STRING([--with-packagekit-user=<user>],
			   [User for running the PackageKit daemon (root)]))
if test -z "$with_packagekit_user" ; then
	PACKAGEKIT_USER=root
else
	PACKAGEKIT_USER=$with_packagekit_user
fi
AC_SUBST(PACKAGEKIT_USER)
AC_DEFINE_UNQUOTED(PACKAGEKIT_USER,"$PACKAGEKIT_USER", [User for running the PackageKit daemon])

dnl ---------------------------------------------------------------------------
dnl - Able to run from a checkout?
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(local, AS_HELP_STRING([--enable-local],[enable running in local checkout]),
	      enable_local=$enableval,enable_local=no)
AM_CONDITIONAL(PK_BUILD_LOCAL, test x$enable_local = xyes)
if test x$enable_local = xyes; then
	AC_DEFINE(PK_BUILD_LOCAL,1,[Build local code])
fi

dnl ---------------------------------------------------------------------------
dnl - Able to build gstreamer plugin
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gstreamer_plugin, AS_HELP_STRING([--enable-gstreamer-plugin],[Build gstreamer plugin functionality]),
	      enable_gstreamer_plugin=$enableval,enable_gstreamer_plugin=yes)
if test x$enable_gstreamer_plugin = xyes; then
	PKG_CHECK_MODULES(PK_GSTREAMER_PLUGIN, gstreamer-1.0 gstreamer-plugins-base-1.0 glib-2.0 gio-2.0,
	                  build_gstreamer_plugin=yes, build_gstreamer_plugin=no)
else
	build_gstreamer_plugin=no
fi
AM_CONDITIONAL(PK_BUILD_GSTREAMER_PLUGIN, test $build_gstreamer_plugin = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build GTK module
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gtk_module, AS_HELP_STRING([--enable-gtk-module],[Build GTK+-3 module functionality]),
	      enable_gtk_module=$enableval,enable_gtk_module=yes)
if test x$enable_gtk_module = xyes; then
	PKG_CHECK_MODULES(PK_GTK_MODULE, gtk+-3.0 >= 3.0.0 pangoft2 fontconfig gio-2.0,
	                  build_gtk_module=yes, build_gtk_module=no)
else
	build_gtk_module=no
fi
if test x$build_gtk_module = xyes; then
	# this is needed to avoid the GTK2 warning
	PKG_CHECK_MODULES(PK_GTK_MODULE_GTK2, gtk+-2.0 gio-2.0)
	GLIB_GSETTINGS
fi
AM_CONDITIONAL(PK_BUILD_GTK_MODULE, test $build_gtk_module = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build BASH command-not-found functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(command_not_found, AS_HELP_STRING([--enable-command-not-found],[Build BASH command-not-found functionality]),
	      build_command_not_found=$enableval,build_command_not_found=yes)
AM_CONDITIONAL(PK_BUILD_COMMAND_NOT_FOUND, test $build_command_not_found = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build cron background functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(cron, AS_HELP_STRING([--enable-cron],[Build cron background functionality]),
	      build_cron=$enableval,build_cron=yes)
AM_CONDITIONAL(PK_BUILD_CRON, test $build_cron = "yes")

# don't do daemon checks when running make distcheck
AC_ARG_ENABLE(daemon-tests, AS_HELP_STRING([--enable-daemon-tests],
	      [Test the daemon using the dummy backend]),
	      enable_daemon_tests=$enableval,enable_daemon_tests=yes)
if test x$enable_daemon_tests = xyes; then
	AC_DEFINE(PK_ENABLE_DAEMON_TESTS,1,[Enable the daemon tests])
fi

# backends
AC_ARG_ENABLE(alpm, AS_HELP_STRING([--enable-alpm],[use the ALPM backend]),enable_alpm=$enableval,enable_alpm=no)
AC_ARG_ENABLE(aptcc, AS_HELP_STRING([--enable-aptcc],[use the APTcc backend]),enable_aptcc=$enableval,enable_aptcc=no)
AC_ARG_ENABLE(dnf, AS_HELP_STRING([--enable-dnf],[use the DNF backend]),enable_dnf=$enableval,enable_dnf=no)
AC_ARG_ENABLE(dummy, AS_HELP_STRING([--enable-dummy],[use the dummy backend]),enable_dummy=$enableval,enable_dummy=yes)
AC_ARG_ENABLE(entropy, AS_HELP_STRING([--enable-entropy],[use the entropy backend]),enable_entropy=$enableval,enable_entropy=no)
AC_ARG_ENABLE(pisi, AS_HELP_STRING([--enable-pisi],[use the PiSi backend]),enable_pisi=$enableval,enable_pisi=no)
AC_ARG_ENABLE(poldek, AS_HELP_STRING([--enable-poldek],[use the poldek backend]),enable_poldek=$enableval,enable_poldek=no)
AC_ARG_ENABLE(portage, AS_HELP_STRING([--enable-portage],[use the portage backend]),enable_portage=$enableval,enable_portage=no)
AC_ARG_ENABLE(ports, AS_HELP_STRING([--enable-ports],[use the ports backend]),enable_ports=$enableval,enable_ports=no)
AC_ARG_ENABLE(slack, AS_HELP_STRING([--enable-slack],[use the Slackware backend]),enable_slack=$enableval,enable_slack=no)
AC_ARG_ENABLE(urpmi, AS_HELP_STRING([--enable-urpmi],[use the URPMI backend]),enable_urpmi=$enableval,enable_urpmi=no)
AC_ARG_ENABLE(yum, AS_HELP_STRING([--enable-yum],[use the YUM backend]),enable_yum=$enableval,enable_yum=no)
AC_ARG_ENABLE(zypp, AS_HELP_STRING([--enable-zypp],[use the Zypp backend]),enable_zypp=$enableval,enable_zypp=no)
AC_ARG_ENABLE(nix, AS_HELP_STRING([--enable-nix],[use the Nix backend]),enable_nix=$enableval,enable_nix=no)

# export to Makefile.am's
AM_CONDITIONAL(BACKEND_TYPE_ALPM, [test x$enable_alpm = xyes])
AM_CONDITIONAL(BACKEND_TYPE_APTCC, [test x$enable_aptcc = xyes])
AM_CONDITIONAL(BACKEND_TYPE_DNF, [test x$enable_dnf = xyes])
AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$enable_dummy = xyes])
AM_CONDITIONAL(BACKEND_TYPE_ENTROPY, [test x$enable_entropy = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PISI, [test x$enable_pisi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_POLDEK, [test x$enable_poldek = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PORTAGE, [test x$enable_portage = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PORTS, [test x$enable_ports = xyes])
AM_CONDITIONAL(BACKEND_TYPE_SLACK, [test x$enable_slack = xyes])
AM_CONDITIONAL(BACKEND_TYPE_URPMI, [test x$enable_urpmi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$enable_yum = xyes])
AM_CONDITIONAL(BACKEND_TYPE_ZYPP, [test x$enable_zypp = xyes])
AM_CONDITIONAL(BACKEND_TYPE_NIX, [test x$enable_nix = xyes])

dnl ---------------------------------------------------------------------------
dnl - Are we specifying a different dbus root ?
dnl ---------------------------------------------------------------------------
AC_ARG_WITH(dbus-sys,
	      [AS_HELP_STRING([--with-dbus-sys=<dir>],[where D-BUS system.d directory is])])
AC_ARG_WITH(dbus-services,
	      [AS_HELP_STRING([--with-dbus-services=<dir>],[where D-BUS system-services directory is])])
if ! test -z "$with_dbus_sys" ; then
	DBUS_SYS_DIR="$with_dbus_sys"
else
	DBUS_SYS_DIR="$SYSCONFDIR/dbus-1/system.d"
fi
if ! test -z "$with_dbus_services" ; then
	DBUS_SERVICES_DIR="$with_dbus_services"
else
	DBUS_SERVICES_DIR="$DATADIR/dbus-1/system-services"
fi
AC_SUBST(DBUS_SYS_DIR)
AC_SUBST(DBUS_SERVICES_DIR)

if test x$enable_dnf = xyes; then
	PKG_CHECK_MODULES(DNF, appstream-glib libdnf >= 0.22.0 rpm)
	AC_ARG_WITH(dnf-vendor,
			[AS_HELP_STRING([--with-dnf-vendor=<vendor>],[select a vendor configuration (fedora, mageia, openmandriva; default is fedora)])])
	if test "$with_dnf_vendor" = "fedora" -o "$with_dnf_vendor" = "mageia" -o "$with_dnf_vendor" = "openmandriva"; then
		with_dnf_vendor="$with_dnf_vendor"
	else
		with_dnf_vendor="fedora"
	fi
	AC_SUBST(DNF_VENDOR, [$with_dnf_vendor])
fi

have_python_backend="no"
if test x$enable_entropy = xyes -o x$enable_pisi = xyes -o x$enable_portage = xyes -o x$enable_yum = xyes; then
	have_python_backend="yes"
fi
AM_CONDITIONAL(HAVE_PYTHON_BACKEND, test x$have_python_backend = xyes)

if test x$enable_aptcc = xyes; then
	dnl Use C++
	AC_LANG([C++])

	# Require C++11 support
	AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])

	# check we have apt and gstreamer headers
	save_CPPFLAGS="$CPPFLAGS"
	save_LDFLAGS="$LDFLAGS"
	save_CFLAGS="$CFLAGS"
	save_LIBS="$LIBS"

	AC_CHECK_LIB(apt-pkg, main, , [AC_MSG_ERROR([Can't find the APT libraries -- please install libapt-pkg-dev])])
	AC_CHECK_HEADER(apt-pkg/init.h, , [AC_MSG_ERROR([Can't find the APT header files -- please install libapt-pkg-dev])])

	AC_SUBST(APTCC_CFLAGS)
	AC_SUBST(APTCC_LIBS)

	AC_CHECK_LIB(apt-inst, main, , [AC_MSG_ERROR([Can't find the APT libraries -- please install libapt-pkg-dev])])
	AC_SUBST(APTCC_CFLAGS)
	AC_SUBST(APTCC_LIBS)

	PKG_CHECK_MODULES(GSTREAMER, gstreamer-1.0 gstreamer-base-1.0 gstreamer-plugins-base-1.0,,
					[AC_MSG_ERROR([Can't find the gstreamer libraries -- please install libgstreamer-plugins-base1.0-dev])])
	AC_SUBST(GSTREAMER_CFLAGS)
	AC_SUBST(GSTREAMER_LIBS)

	PKG_CHECK_MODULES(APPSTREAM, appstream >= 0.11)

	AC_MSG_CHECKING([whether apt is at least at version 1.1])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <apt-pkg/pkgsystem.h>]],
					  [[_system->ArchitecturesSupported();]])],
			  AC_MSG_RESULT([yes]),
			  AC_MSG_FAILURE([need libapt-pkg 1.1 or later]))

	AC_MSG_CHECKING([whether apt supports ddtp])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <apt-pkg/pkgcache.h>]],
					  [[pkgCache::DescIterator d;]])],
			  AC_MSG_RESULT([yes])
			  AC_DEFINE([HAVE_DDTP], [], [Set to true if apt is DDTP-enabled]),
			  AC_MSG_RESULT([no]))

	CPPFLAGS="$save_CPPFLAGS"
	LDFLAGS="$save_LDFLAGS"
	CFLAGS="$save_CFLAGS"
	LIBS="$save_LIBS"
fi

if test x$enable_alpm = xyes; then
	PKG_CHECK_MODULES(ALPM, libalpm >= 10.0.0)
fi

if test x$enable_poldek = xyes; then
	POLDEK_CFLAGS="-I/usr/include/poldek"
	POLDEK_LIBS="-lpoclidek -lpoldek"
	AC_SUBST(POLDEK_CFLAGS)
	AC_SUBST(POLDEK_LIBS)
fi

if test x$enable_zypp = xyes; then
	PKG_CHECK_MODULES(ZYPP, libzypp >= 6.16.0)
	PKG_CHECK_EXISTS(libzypp >= 11.4.0, [ ZYPP_RETURN_BYTES="yes" ], [ ZYPP_RETURN_BYTES="no" ])
	if test "x$ZYPP_RETURN_BYTES" = "xyes"; then
	    AC_DEFINE(ZYPP_RETURN_BYTES, 1, [define if libzypp returns package size in bytes])
	fi
fi

if test x$enable_slack = xyes; then
	AC_LANG([C++])
	AX_CXX_COMPILE_STDCXX_14([ext], [mandatory])

	AC_CHECK_LIB(curl, curl_version, [
	    CURL_CFLAGS="`curl-config --cflags`"
	    CURL_LIBS="`curl-config --libs`"
	    ], [AC_MSG_ERROR([Cant find curl])])
	case "`uname -m`" in
		x86-64|x86_64|X86-64|X86_64)
			SLACK_PKGMAIN="slackware64"
			;;
		*)
			SLACK_PKGMAIN="slackware"
	esac

	SLACK_CFLAGS="$CURL_CFLAGS"
	SLACK_LIBS="$CURL_LIBS"

	AC_SUBST(SLACK_PKGMAIN)
	AC_SUBST(SLACK_CFLAGS)
	AC_SUBST(SLACK_LIBS)
fi

if test x$enable_nix = xyes; then
	dnl Use C++
	AC_LANG([C++])

	# Require C++11 support
	AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])

	PKG_CHECK_MODULES(NIX, nix-expr >= 1.12)
	PKG_CHECK_MODULES(NIX, nix-main >= 1.12)
	PKG_CHECK_MODULES(NIX, nix-store >= 1.12)
fi

AC_SUBST(PK_PLUGIN_DIR, "\$(libdir)/packagekit-backend")
AC_SUBST(PK_PLUGIN_CFLAGS, "-I\$(top_srcdir)/src -I\$(top_srcdir)/lib -DPK_COMPILATION $GLIB_CFLAGS $GMODULE_CFLAGS $GIO_CFLAGS")
AC_SUBST(PK_PLUGIN_LIBS, "$GLIB_LIBS $GMODULE_LIBS $GIO_LIBS")

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
etc/Makefile
docs/Makefile
docs/api/Makefile
docs/api/dbus/Makefile
docs/api/version.xml
contrib/Makefile
contrib/gstreamer-plugin/Makefile
contrib/gtk-module/Makefile
contrib/gtk-module/gtk2/Makefile
contrib/yum-packagekit/Makefile
contrib/command-not-found/Makefile
contrib/cron/Makefile
backends/Makefile
backends/alpm/Makefile
backends/aptcc/Makefile
backends/dnf/Makefile
backends/dummy/Makefile
backends/entropy/Makefile
backends/slack/Makefile
backends/slack/tests/Makefile
backends/test/Makefile
backends/test/helpers/Makefile
backends/urpmi/Makefile
backends/urpmi/helpers/Makefile
backends/urpmi/helpers/perl_packagekit/Makefile
backends/urpmi/helpers/urpmi_backend/Makefile
backends/yum/Makefile
backends/pisi/Makefile
backends/poldek/Makefile
backends/portage/Makefile
backends/ports/Makefile
backends/ports/ruby_packagekit/Makefile
backends/zypp/Makefile
backends/nix/Makefile
data/Makefile
data/org.freedesktop.PackageKit.conf
data/tests/Makefile
data/tests/pk-client-helper-test.py
data/tests/pk-spawn-dispatcher.py
data/tests/pk-spawn-test-sigquit.py
lib/Makefile
lib/packagekit-glib2/pk-version.h
lib/packagekit-glib2/Makefile
lib/packagekit-glib2/packagekit-glib2.pc
lib/python/Makefile
lib/python/packagekit/Makefile
policy/Makefile
src/Makefile
client/Makefile
po/Makefile.in
],[
chmod +x data/tests/pk-client-helper-test.py
chmod +x data/tests/pk-spawn-dispatcher.py
chmod +x data/tests/pk-spawn-test-sigquit.py
])
AC_OUTPUT

dnl ==========================================================================
echo "
                    PackageKit $VERSION
                  ====================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
        Able to run locally:       ${enable_local}
        Use elogind:               ${enable_elogind}
        Use systemd:               ${enable_systemd}
        Enable offline update:     ${enable_offline_update}
        GStreamer plugin:          ${build_gstreamer_plugin}
        Pango module (GTK+3):      ${build_gtk_module}
        gobject-introspection:     ${found_introspection}
        BASH Command not found:    ${build_command_not_found}
        Cron scripts:              ${build_cron}
        python3:                   ${enable_python3}
        systemd system unit dir:   ${has_systemdsystemunitdir}
        Installing python backend: ${have_python_backend}
        python package dir:        ${with_python_package_dir}

        Backends:
        ALPM backend:              ${enable_alpm}
        APTcc backend:             ${enable_aptcc}
        DNF backend:               ${enable_dnf}
        DNF backend vendor:        ${with_dnf_vendor}
        dummy backend:             ${enable_dummy}
        Entropy backend:           ${enable_entropy}
        PiSi backend:              ${enable_pisi}
        poldek backend:            ${enable_poldek}
        Portage backend:           ${enable_portage}
        Ports backend:             ${enable_ports}
        Slackware backend:         ${enable_slack}
        URPMI backend:             ${enable_urpmi}
        YUM backend:               ${enable_yum}
        Zypp backend:              ${enable_zypp}
        Nix backend:               ${enable_nix}
"
