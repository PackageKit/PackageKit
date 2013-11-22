# Copyright (C) 2007-2012 Richard Hughes <richard@hughsie.com>

m4_define([pk_major_version], [0])
m4_define([pk_minor_version], [8])
m4_define([pk_micro_version], [14])
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
LT_CURRENT=17
LT_REVISION=0
LT_AGE=1
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

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

PYTHON_PACKAGE_DIR=${pythondir}/packagekit
AC_SUBST(PYTHON_PACKAGE_DIR)

dnl ---------------------------------------------------------------------------
dnl - Use strict options (default enabled for devs, disabled in releases)
dnl ---------------------------------------------------------------------------
dnl if .git directory is present, considering we are working in the repository
if test -d ".git"; then
	default_strict=yes
else
	default_strict=no
fi

AC_ARG_ENABLE(strict, AS_HELP_STRING([--enable-strict],[Enable strict compilation options]), enable_strict=$enableval,
							enable_strict=$default_strict)
if test x$enable_strict != xno; then
	if test "$GCC" = "yes"; then
		WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Werror"
	fi
fi

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
#	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wempty-body"
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
GLIB_REQUIRED=2.32.0
GIO_REQUIRED=2.16.1
NETWORK_MANAGER_REQUIRED=0.6.4
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

dnl ---------------------------------------------------------------------------
dnl - Service Packs (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(service-packs, AS_HELP_STRING([--disable-service-packs],[Disable Service Packs support]), enable_servicepacks=$enableval)
if test x$enable_servicepacks != xno; then
	AC_CHECK_HEADERS(archive.h,
			 HAVE_ARCHIVE_H="yes",
			 HAVE_ARCHIVE_H="no")
	if test "x$HAVE_ARCHIVE_H" = "xyes"; then
			AC_DEFINE(HAVE_ARCHIVE_H)
			ARCHIVE_LIBS=-larchive
			AC_SUBST(ARCHIVE_LIBS)
	else
		if test x$enable_servicepacks = xyes; then
			AC_MSG_ERROR([Service Packs support need libarchive])
		fi
	fi
else
	HAVE_ARCHIVE_H="no"
fi

AM_CONDITIONAL(HAVE_ARCHIVE_H, test x$HAVE_ARCHIVE_H = xyes)

dnl ---------------------------------------------------------------------------
dnl - FreeBSD compatibility
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS([execinfo.h])
AC_CHECK_FUNCS(clearenv)
AC_PATH_PROG(GMSGFMT, msgfmt, msgfmt)

# unix networking stack always available
networking_apis="unix"

dnl ---------------------------------------------------------------------------
dnl - Haiku compatibility
dnl ---------------------------------------------------------------------------
AC_SEARCH_LIBS([daemon], [bsd])
AC_CHECK_FUNCS(setpriority)

dnl ---------------------------------------------------------------------------
dnl - NetworkManager (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(networkmanager, AS_HELP_STRING([--disable-networkmanager],[Disable NetworkManager support]),
		enable_networkmanager=$enableval)
if test x$enable_networkmanager != xno; then
	PKG_CHECK_MODULES(NETWORK_MANAGER, NetworkManager >= $NETWORK_MANAGER_REQUIRED, PK_BUILD_NETWORKMANAGER="yes", PK_BUILD_NETWORKMANAGER="no")
	if test "x$PK_BUILD_NETWORKMANAGER" = "xyes"; then
		networking_apis="${networking_apis},NetworkManager"
		AC_DEFINE(PK_BUILD_NETWORKMANAGER, 1, [define if NetworkManager is installed])
	else
		if test x$enable_networkmanager = xyes; then
			AC_MSG_ERROR([NetworkManager enabled but not found])
		fi
	fi
else
	PK_BUILD_NETWORKMANAGER=no
fi

AM_CONDITIONAL(PK_BUILD_NETWORKMANAGER, test x$PK_BUILD_NETWORKMANAGER = xyes)

dnl ---------------------------------------------------------------------------
dnl - ConnMan (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(connman, AS_HELP_STRING([--disable-connman],[Disable ConnMan support]), enable_connman=$enableval)
if test x$enable_connman != xno; then
	PKG_CHECK_MODULES(CONNMAN, connman, PK_BUILD_CONNMAN="yes", PK_BUILD_CONNMAN="no")
	if test "x$PK_BUILD_CONNMAN" = "xyes"; then
		networking_apis="${networking_apis},ConnMan"
		AC_DEFINE(PK_BUILD_CONNMAN, 1, [define if Connection Manager is installed])
	else
		if test x$enable_connman = xyes; then
			AC_MSG_ERROR([Connman enabled but not found])
		fi
	fi
else
	PK_BUILD_CONNMAN=no
fi

AM_CONDITIONAL(PK_BUILD_CONNMAN, test x$PK_BUILD_CONNMAN = xyes)

dnl ---------------------------------------------------------------------------
dnl - Use systemd and logind rather than ConsoleKit
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(systemd, AS_HELP_STRING([--enable-systemd],[enable systemd and logind code]),
	      enable_systemd=$enableval,enable_systemd=yes)
AM_CONDITIONAL(PK_BUILD_SYSTEMD, test x$enable_systemd = xyes)
if test x$enable_systemd = xyes; then
	PKG_CHECK_MODULES(SYSTEMD, libsystemd-login)
	AC_ARG_WITH([systemdsystemunitdir],
		    AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
		    [],
		    [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
	AC_DEFINE(PK_BUILD_SYSTEMD,1,[Build systemd code])
	AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir"])

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
dnl - Build self tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests],[enable unit test code]),
	      enable_tests=$enableval,enable_tests=yes)
AM_CONDITIONAL(EGG_BUILD_TESTS, test x$enable_tests = xyes)
if test x$enable_tests = xyes; then
	AC_DEFINE(EGG_BUILD_TESTS,1,[Build test code])
fi

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
dnl - Able to build browser plugin
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(browser_plugin, AS_HELP_STRING([--enable-browser-plugin],[Build browser plugin functionality]),
	      enable_browser_plugin=$enableval,enable_browser_plugin=yes)
if test x$enable_browser_plugin = xyes; then
	PKG_CHECK_MODULES(PK_BROWSER_PLUGIN,	\
			  mozilla-plugin >= 8.0	\
			  gio-unix-2.0		\
			  nspr >= 4.8		\
			  cairo			\
			  pango			\
			  gtk+-2.0 >= 2.14.0,
			  build_browser_plugin=yes, build_browser_plugin=no)
	if test $build_browser_plugin = "yes"; then

		# check we have xulrunner headers
		packagekit_save_CPPFLAGS="$CPPFLAGS"
		packagekit_save_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $PK_BROWSER_PLUGIN_CFLAGS"
		LDFLAGS="$CPPFLAGS $PK_BROWSER_PLUGIN_LIBS"
		AC_CHECK_HEADER(npfunctions.h, have_xulrunner_npfunctions=yes, have_xulrunner_npfunctions=no)
		CPPFLAGS="$packagekit_save_CPPFLAGS"
		LDFLAGS="$packagekit_save_LDFLAGS"

		# Are we specifying a different mozilla plugin directory?
		AC_ARG_WITH(mozilla-plugin-dir, [AS_HELP_STRING([--with-mozilla-plugin-dir=<dir>],[where to put the mozilla plugin])])
		if ! test -z "$with_mozilla_plugin_dir" ; then
			MOZILLA_PLUGIN_DIR="$with_mozilla_plugin_dir"
		else
			MOZILLA_PLUGIN_DIR="$LIBDIR/mozilla/plugins"
		fi
		AC_SUBST(MOZILLA_PLUGIN_DIR)

		# not got headers
		if test $have_xulrunner_npfunctions = "no"; then
			build_browser_plugin=no
		fi
	fi
else
	build_browser_plugin=no
fi
AM_CONDITIONAL(PK_BUILD_BROWSER_PLUGIN, test $build_browser_plugin = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build gstreamer plugin
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gstreamer_plugin, AS_HELP_STRING([--enable-gstreamer-plugin],[Build gstreamer plugin functionality]),
	      enable_gstreamer_plugin=$enableval,enable_gstreamer_plugin=yes)
if test x$enable_gstreamer_plugin = xyes; then
	PKG_CHECK_MODULES(PK_GSTREAMER_PLUGIN, gstreamer-0.10 gstreamer-plugins-base-0.10 glib-2.0 gio-2.0,
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

dnl ---------------------------------------------------------------------------
dnl - Able to build debuginfo-install functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(debuginfo-install, AS_HELP_STRING([--enable-debuginfo-install],[Build debuginfo-install functionality]),
	      build_debuginfo_install=$enableval,build_debuginfo_install=yes)
AM_CONDITIONAL(PK_BUILD_DEBUGINFO_INSTALL, test $build_debuginfo_install = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to use device rebind functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(device-rebind, AS_HELP_STRING([--enable-device-rebind],[Build device rebind functionality]),
	      build_device_rebind=$enableval,build_device_rebind=yes)
if test x$build_device_rebind = xyes; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0,
			  build_device_rebind=yes, build_device_rebind=no)
else
	build_device_rebind=no
fi
AM_CONDITIONAL(PK_BUILD_DEVICE_REBIND, test $build_device_rebind = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build pm-utils script functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(pm_utils, AS_HELP_STRING([--enable-pm-utils],[Build pm-utils script functionality]),
	      build_pm_utils=$enableval,build_pm_utils=yes)
if test x$build_pm_utils = xyes; then
	PKG_CHECK_MODULES(PM_UTILS, pm-utils,
	                  build_pm_utils=yes, build_pm_utils=no)
else
	build_pm_utils=no
fi
AM_CONDITIONAL(PK_BUILD_PM_UTILS, test $build_pm_utils = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to use systemd updates functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(systemd-updates, AS_HELP_STRING([--enable-systemd-updates],[Build systemd updates functionality]),
	      build_systemd_updates=$enableval,build_systemd_updates=yes)
AM_CONDITIONAL(PK_BUILD_SYSTEMD_UPDATES, test $build_systemd_updates = "yes")
if test x$build_systemd_updates = xyes; then
	PKG_CHECK_MODULES(LIBSYSTEMD_JOURNAL, libsystemd-journal)
	AC_DEFINE(PK_HAS_OFFLINE_UPDATES,1,[Has offline updates])
fi

# backends
AC_ARG_ENABLE(alpm, AS_HELP_STRING([--enable-alpm],[use the ALPM backend]),enable_alpm=$enableval,enable_alpm=no)
AC_ARG_ENABLE(apt, AS_HELP_STRING([--enable-apt],[use the APT backend]),enable_apt=$enableval,enable_apt=no)
AC_ARG_ENABLE(aptcc, AS_HELP_STRING([--enable-aptcc],[use the APTcc backend]),enable_aptcc=$enableval,enable_aptcc=no)
AC_ARG_ENABLE(box, AS_HELP_STRING([--enable-box],[use the BOX backend]),enable_box=$enableval,enable_box=no)
AC_ARG_ENABLE(conary, AS_HELP_STRING([--enable-conary],[use the CONARY backend]),enable_conary=$enableval,enable_conary=no)
AC_ARG_ENABLE(dummy, AS_HELP_STRING([--enable-dummy],[use the dummy backend]),enable_dummy=$enableval,enable_dummy=yes)
AC_ARG_ENABLE(entropy, AS_HELP_STRING([--enable-entropy],[use the entropy backend]),enable_entropy=$enableval,enable_entropy=no)
AC_ARG_ENABLE(hawkey, AS_HELP_STRING([--enable-hawkey],[use the hawkey backend]),enable_hawkey=$enableval,enable_hawkey=no)
AC_ARG_ENABLE(opkg, AS_HELP_STRING([--enable-opkg],[use the OPKG backend]),enable_opkg=$enableval,enable_opkg=no)
AC_ARG_ENABLE(pisi, AS_HELP_STRING([--enable-pisi],[use the PiSi backend]),enable_pisi=$enableval,enable_pisi=no)
AC_ARG_ENABLE(poldek, AS_HELP_STRING([--enable-poldek],[use the poldek backend]),enable_poldek=$enableval,enable_poldek=no)
AC_ARG_ENABLE(portage, AS_HELP_STRING([--enable-portage],[use the portage backend]),enable_portage=$enableval,enable_portage=no)
AC_ARG_ENABLE(ports, AS_HELP_STRING([--enable-ports],[use the ports backend]),enable_ports=$enableval,enable_ports=no)
AC_ARG_ENABLE(slapt, AS_HELP_STRING([--enable-slapt],[use the slapt backend]),enable_slapt=$enableval,enable_slapt=no)
AC_ARG_ENABLE(smart, AS_HELP_STRING([--enable-smart],[use the SMART backend]),enable_smart=$enableval,enable_smart=no)
AC_ARG_ENABLE(urpmi, AS_HELP_STRING([--enable-urpmi],[use the URPMI backend]),enable_urpmi=$enableval,enable_urpmi=no)
AC_ARG_ENABLE(yum, AS_HELP_STRING([--enable-yum],[use the YUM backend]),enable_yum=$enableval,enable_yum=no)
AC_ARG_ENABLE(zypp, AS_HELP_STRING([--enable-zypp],[use the Zypp backend]),enable_zypp=$enableval,enable_zypp=no)

# export to Makefile.am's
AM_CONDITIONAL(BACKEND_TYPE_ALPM, [test x$enable_alpm = xyes])
AM_CONDITIONAL(BACKEND_TYPE_APT, [test x$enable_apt = xyes])
AM_CONDITIONAL(BACKEND_TYPE_APTCC, [test x$enable_aptcc = xyes])
AM_CONDITIONAL(BACKEND_TYPE_BOX, [test x$enable_box = xyes])
AM_CONDITIONAL(BACKEND_TYPE_CONARY, [test x$enable_conary = xyes])
AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$enable_dummy = xyes])
AM_CONDITIONAL(BACKEND_TYPE_ENTROPY, [test x$enable_entropy = xyes])
AM_CONDITIONAL(BACKEND_TYPE_HAWKEY, [test x$enable_hawkey = xyes])
AM_CONDITIONAL(BACKEND_TYPE_OPKG, [test x$enable_opkg = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PISI, [test x$enable_pisi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_POLDEK, [test x$enable_poldek = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PORTAGE, [test x$enable_portage = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PORTS, [test x$enable_ports = xyes])
AM_CONDITIONAL(BACKEND_TYPE_SLAPT, [test x$enable_slapt = xyes])
AM_CONDITIONAL(BACKEND_TYPE_SMART, [test x$enable_smart = xyes])
AM_CONDITIONAL(BACKEND_TYPE_URPMI, [test x$enable_urpmi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$enable_yum = xyes])
AM_CONDITIONAL(BACKEND_TYPE_ZYPP, [test x$enable_zypp = xyes])

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

dnl ---------------------------------------------------------------------------
dnl - Compile time default choice of security framework
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([security_framework],
	    AS_HELP_STRING([--with-security-framework=<option>],
			   [Default security framework to use polkit,dummy]))
# try and guess this if nothing is listed
if test x$with_security_framework = x; then
	with_security_framework=polkit
fi

AC_DEFINE_UNQUOTED(security_framework, "$with_security_framework", [default security framework])
AC_SUBST(security_framework, "$with_security_framework")

if test x$with_security_framework = xpolkit; then
	PKG_CHECK_MODULES(POLKIT, \
			  polkit-gobject-1 >= $POLKIT_GOBJECT_REQUIRED)
	AC_DEFINE(USE_SECURITY_POLKIT, 1, [if we should use PolicyKit])
elif test x$with_security_framework = xdummy; then
	AC_DEFINE(USE_SECURITY_DUMMY, 1, [if we should use a dummy security framework])
else
	AC_MSG_ERROR([No valid security framework specified])
fi

AM_CONDITIONAL(SECURITY_TYPE_DUMMY, [test x$with_security_framework = xdummy])
AM_CONDITIONAL(SECURITY_TYPE_POLKIT, [test x$with_security_framework = xpolkit])

dnl ---------------------------------------------------------------------------
dnl - Compile time default choice of backend
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([default_backend],
	    AS_HELP_STRING([--with-default-backend=<option>],
			   [Default backend to use
                           auto,alpm,apt,aptcc,box,conary,dummy,entropy,hawkey,opkg,pisi,portage,ports,slapt,smart,urpmi,yum,zypp (dummy)]))
# default to a sane option for the installed tool
if test x$with_default_backend = x; then
	if test -f /usr/bin/yum ; then
		with_default_backend=yum
	elif test -f /usr/lib/libalpm.so; then
		with_default_backend=alpm
	elif test -f /usr/bin/apt-get ; then
		with_default_backend=aptcc
	elif test -f /usr/bin/conary ; then
		with_default_backend=conary
	elif test -f /usr/bin/box-repos ; then
		with_default_backend=box
	elif test -f /usr/sbin/slapt-get ; then
		with_default_backend=slapt
	elif test -f /usr/bin/smart ; then
		with_default_backend=smart
	elif test -f /usr/bin/pisi ; then
		with_default_backend=pisi
	elif test -f /usr/bin/poldek ; then
		with_default_backend=poldek
	elif test -f /usr/bin/urpmq ; then
		with_default_backend=urpmi
	elif test -f /usr/bin/zypper ; then
		with_default_backend=zypp
	elif test -d /usr/lib/entropy ; then
		with_default_backend=entropy
	elif test -f /usr/bin/emerge ; then
		with_default_backend=portage
	elif test -f /usr/local/sbin/portupgrade ; then
		with_default_backend=ports
	else
		with_default_backend=dummy
	fi
fi

AC_DEFINE_UNQUOTED(DEFAULT_BACKEND, "$with_default_backend", [default backend prefix])
AC_SUBST(DEFAULT_BACKEND, "$with_default_backend")

build_hawkey=no
if test x$enable_hawkey = xyes; then
	AC_ARG_ENABLE(hawkey, AS_HELP_STRING([--enable-hawkey],[Build Hawkey experimental code]),
		      enable_hawkey=$enableval,enable_hawkey=yes)
	if test x$enable_hawkey = xyes; then
		PKG_CHECK_MODULES(HAWKEY, hawkey >= 0.4.5 librepo rpm >= 4.11.0,
			          build_hawkey=yes, build_hawkey=no)
	fi
	if test "x$build_hawkey" = "xyes"; then
		AC_DEFINE([HAVE_HAWKEY], [1], [If Hawkey support should be enabled])
	fi
fi
AM_CONDITIONAL(PK_BUILD_HAWKEY, test $build_hawkey = "yes")

if test x$enable_apt = xyes; then
	PY_CHECK_MOD([apt_pkg],,,AC_MSG_ERROR([Apt backend needs python-apt]))
	PY_CHECK_MOD([UpdateManager.Core.MetaRelease],,
		     AC_DEFINE(HAVE_PYTHON_META_RELEASE, 1,
			       "Meta release is available"),
		     AC_MSG_WARN([Distro upgrade notification not supported]))
	PY_CHECK_MOD([softwareproperties.SoftwareProperties],,
		     AC_DEFINE(HAVE_PYTHON_SOFTWARE_PROPERTIES, 1,
			       "Python software properties is available"),
		     AC_MSG_WARN([Repository handling not supported]))
fi

if test x$enable_aptcc = xyes; then
	dnl Use C++
	AC_LANG([C++])

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

	PKG_CHECK_MODULES(GSTREAMER, gstreamer-0.10 gstreamer-base-0.10 gstreamer-plugins-base-0.10,,
					[AC_MSG_ERROR([Can't find the gstreamer libraries -- please install libgstreamermm-0.10-dev])])
	AC_SUBST(GSTREAMER_CFLAGS)
	AC_SUBST(GSTREAMER_LIBS)

	AC_MSG_CHECKING([whether apt includes the automatic dependency removal patch (required)])

	AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <apt-pkg/depcache.h>]],
					  [[extern pkgDepCache::ActionGroup d;]]),
			  AC_MSG_RESULT([yes]),
			  AC_MSG_FAILURE([need libapt-pkg 0.7 or later]))

	AC_MSG_CHECKING([whether apt supports ddtp])

	AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <apt-pkg/pkgcache.h>]],
					  [[pkgCache::DescIterator d;]]),
			  AC_MSG_RESULT([yes])
			  AC_DEFINE([HAVE_DDTP], [], [Set to true if apt is DDTP-enabled]),
			  AC_MSG_RESULT([no]))

	CPPFLAGS="$save_CPPFLAGS"
	LDFLAGS="$save_LDFLAGS"
	CFLAGS="$save_CFLAGS"
	LIBS="$save_LIBS"
fi

if test x$enable_box = xyes; then
	PKG_CHECK_MODULES(BOX, libbox >= 0.1.4 )
fi

if test x$enable_opkg = xyes; then
	PKG_CHECK_MODULES(OPKG, libopkg >= 0.1.5)
fi

if test x$enable_alpm = xyes; then
	PKG_CHECK_MODULES(ALPM, libalpm >= 4.1.0)
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

if test x$enable_slapt = xyes; then
	with_default_backend=dummy
	AC_CHECK_HEADER([slapt.h],
			[with_default_backend=slapt],
			[AC_MSG_WARN([No slapt headers found - falling back to dummy backend])])
	AC_CHECK_LIB(curl, curl_version, [
	    CURL_CFLAGS="`curl-config --cflags`"
	    CURL_LIBS="`curl-config --libs`"
	    ], [AC_MSG_ERROR([Can't find curl])])
	AC_CHECK_LIB(gpgme, gpgme_check_version, [
		AC_DEFINE(SLAPT_HAS_GPGME, 1, [Define to 1 if GPGME is available to Slapt])
	    GPGME_CFLAGS="`gpgme-config --cflags`"
	    GPGME_LIBS="`gpgme-config --libs`"
	    ], [AC_MSG_WARN([Can't find gpgme])])
	SLAPT_CFLAGS="$CURL_CFLAGS $GPGME_CFLAGS"
	SLAPT_LIBS="$CURL_LIBS $GPGME_LIBS -lslapt"
	AC_SUBST(SLAPT_CFLAGS)
	AC_SUBST(SLAPT_LIBS)
fi

AC_SUBST(PK_CONF_DIR, "\$(sysconfdir)/PackageKit")
AC_SUBST(PK_YUM_PLUGIN_DIR, "\$(prefix)/lib/yum-plugins")
AC_SUBST(PK_DB_DIR, "\$(localstatedir)/lib/PackageKit")
AC_SUBST(PK_LOG_DIR, "\$(localstatedir)/log")
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
contrib/browser-plugin/Makefile
contrib/gstreamer-plugin/Makefile
contrib/gtk-module/Makefile
contrib/gtk-module/gtk2/Makefile
contrib/yum-packagekit/Makefile
contrib/command-not-found/Makefile
contrib/cron/Makefile
contrib/debuginfo-install/Makefile
contrib/device-rebind/Makefile
contrib/pm-utils/Makefile
contrib/systemd-updates/Makefile
backends/Makefile
backends/alpm/Makefile
backends/apt/Makefile
backends/aptcc/Makefile
backends/box/Makefile
backends/conary/Makefile
backends/dummy/Makefile
backends/entropy/Makefile
backends/hawkey/Makefile
backends/opkg/Makefile
backends/slapt/Makefile
backends/smart/Makefile
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
src/plugins/Makefile
src/plugins/packagekit-plugin.pc
client/Makefile
tools/Makefile
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
        Building unit tests:       ${enable_tests}
        Able to run locally:       ${enable_local}
        Use systemd:               ${enable_systemd}
        Security framework:        ${with_security_framework}
        Networking stacks:         ${networking_apis}
        Browser plugin:            ${build_browser_plugin}
        GStreamer plugin:          ${build_gstreamer_plugin}
        Pango module (GTK+3):      ${build_gtk_module}
        gobject-introspection:     ${found_introspection}
        BASH Command not found:    ${build_command_not_found}
        Cron scripts:              ${build_cron}
        debuginfo-install:         ${build_debuginfo_install}
        device-rebind:             ${build_device_rebind}
        systemd-updates:           ${build_systemd_updates}
        pm-utils scripts:          ${build_pm_utils}
        python3:                   ${enable_python3}
        systemd system unit dir:   ${with_systemdsystemunitdir}

        Backends:
        ALPM backend:              ${enable_alpm}
        APT backend:               ${enable_apt}
        APTcc backend:             ${enable_aptcc}
        BOX backend:               ${enable_box}
        CONARY backend:            ${enable_conary}
        dummy backend:             ${enable_dummy}
        Entropy backend:           ${enable_entropy}
        Hawkey backend:            ${enable_hawkey}
        OPKG backend:              ${enable_opkg}
        PiSi backend:              ${enable_pisi}
        poldek backend:            ${enable_poldek}
        Portage backend:           ${enable_portage}
        Ports backend:             ${enable_ports}
        Slapt backend:             ${enable_slapt}
        SMART backend:             ${enable_smart}
        URPMI backend:             ${enable_urpmi}
        YUM backend:               ${enable_yum}
        Zypp backend:              ${enable_zypp}
        Default backend:           ${with_default_backend}
"

# warn that dummy is basically broken
if test x$with_security_framework = xdummy; then
	echo "*******************************************************************"
	echo "** YOU ARE NOT USING A SECURE DAEMON. ALL USERS CAN DO ANYTHING! **"
	echo "*******************************************************************"
fi

