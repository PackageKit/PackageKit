AC_PREREQ(2.52)

AC_INIT(PackageKit, 0.4.6)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR([m4])

# Should we enable extra stuff automatically?
# set no for release builds, yes for development builds
DEVELOPMENT_RELEASE=yes

# libtool versioning - this applies to libpackagekit
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# increment;
# CURRENT	If the API or ABI interface has changed (reset REVISION to 0)
# REVISION	If the API and ABI remains the same, but bugs are fixed.
# AGE		If libpackagekit can be linked into executables which can be
# 		built with previous versions of this library. Don't use.
LT_CURRENT=11
LT_REVISION=4
LT_AGE=0
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL
AM_PROG_CC_C_O

# Internationalisation
IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=PackageKit
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# set up gtk-doc
GTK_DOC_CHECK(1.9)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AM_PATH_PYTHON
PYTHON_PACKAGE_DIR=${pythondir}/packagekit
AC_SUBST(PYTHON_PACKAGE_DIR)


dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
CPPFLAGS="$CPPFLAGS -DG_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_SINGLE_INCLUDES"
CPPFLAGS="$CPPFLAGS -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED"
CPPFLAGS="$CPPFLAGS -DGDK_MULTIHEAD_SAFE -DGTK_MULTIHEAD_SAFE"

if test "$GCC" = "yes"; then
	# work both in C and C++
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wall"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Werror"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wmissing-declarations"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wredundant-decls"
	WARNINGFLAGS_CPP="$WARNINGFLAGS_CPP -Wmissing-noreturn"
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
GLIB_REQUIRED=2.14.0
GIO_REQUIRED=2.16.1
DBUS_REQUIRED=1.1.1
DBUS_GLIB_REQUIRED=0.74
LIBNM_GLIB_REQUIRED=0.6.4
POLKIT_DBUS_REQUIRED=0.8
POLKIT_GRANT_REQUIRED=0.8
QTCORE_REQUIRED=4.4.0
QTDBUS_REQUIRED=4.4.0
QTGUI_REQUIRED=4.4.0
QTSQL_REQUIRED=4.4.0
QTXML_REQUIRED=4.4.0

dnl ---------------------------------------------------------------------------
dnl - Make above strings available for packaging files (e.g. rpm spec files)
dnl ---------------------------------------------------------------------------
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(DBUS_REQUIRED)
AC_SUBST(DBUS_GLIB_REQUIRED)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

PKG_CHECK_MODULES(SQLITE, sqlite3)
AC_SUBST(SQLITE_CFLAGS)
AC_SUBST(SQLITE_LIBS)

PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_GLIB_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Build PackageKit-Qt
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(qt, AS_HELP_STRING([--enable-qt],[Build PackageKit-Qt]),
	      enable_qt=$enableval,enable_qt=yes)
if test x$enable_qt = xyes; then
	PKG_CHECK_MODULES(QTCORE, QtCore >= $QTCORE_REQUIRED)
	AC_SUBST(QTCORE_CFLAGS)
	AC_SUBST(QTCORE_LIBS)

	PKG_CHECK_MODULES(QTDBUS, QtDBus >= $QTDBUS_REQUIRED)
	AC_SUBST(QTDBUS_CFLAGS)
	AC_SUBST(QTDBUS_LIBS)

	PKG_CHECK_MODULES(QTGUI, QtGui >= $QTGUI_REQUIRED)
	AC_SUBST(QTGUI_CFLAGS)
	AC_SUBST(QTGUI_LIBS)

	PKG_CHECK_MODULES(QTSQL, QtSql >= $QTSQL_REQUIRED)
	AC_SUBST(QTSQL_CFLAGS)
	AC_SUBST(QTSQL_LIBS)

	PKG_CHECK_MODULES(QTXML, QtXml >= $QTXML_REQUIRED)
	AC_SUBST(QTXML_CFLAGS)
	AC_SUBST(QTXML_LIBS)

	PKG_CHECK_MODULES(CPPUNIT, cppunit)
	AC_SUBST(CPPUNIT_CFLAGS)
	AC_SUBST(CPPUNIT_LIBS)
	build_qt=yes
else
	build_qt=no
fi
AM_CONDITIONAL(PK_BUILD_QT, test $build_qt = "yes")

dnl ---------------------------------------------------------------------------
dnl - Build packageKit-sharp
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(managed, AC_HELP_STRING([--enable-managed],[Build PackageKit-Sharp]),
	      enable_managed=$enableval,enable_managed=false)
if test x$enable_managed = xyes; then
	PKG_CHECK_MODULES(GLIBSHARP, glib-sharp-2.0)
	AC_SUBST(GLIBSHARP_CFLAGS)
	AC_SUBST(GLIBSHARP_LIBS)

	AC_PATH_PROG(MCS, mcs)
	CS="C#"
	if test ! -x "$MCS"; then
		AC_PATH_PROG(MCS, gmcs)
		if test ! -x "$MCS"; then
			AC_MSG_ERROR([No $CS compiler found])
		fi
	fi

	build_managed=yes
else
	build_managed=no
fi
AM_CONDITIONAL(PK_BUILD_MANAGED, test $build_managed = "yes")

dnl ---------------------------------------------------------------------------
dnl - libarchive
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS(archive.h,
		 HAVE_ARCHIVE_H="yes",
		 HAVE_ARCHIVE_H="no")
if test "x$HAVE_ARCHIVE_H" = "xyes"; then
		AC_DEFINE(HAVE_ARCHIVE_H)
		ARCHIVE_LIBS=-larchive
		AC_SUBST(ARCHIVE_LIBS)
else
		AC_MSG_WARN([Cannot find archive.h. Service packs cannot be created or checked.])
fi
AM_CONDITIONAL(HAVE_ARCHIVE_H, test x$HAVE_ARCHIVE_H = xyes)

dnl ---------------------------------------------------------------------------
dnl - xsltproc
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([XSLTPROC], [xsltproc])

dnl ---------------------------------------------------------------------------
dnl - moc (for packagekit-qt)
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([MOC], [moc-qt4])

dnl ---------------------------------------------------------------------------
dnl - Is GIO available?
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GIO, gio-2.0 >= $GIO_REQUIRED gio-unix-2.0 >= $GIO_REQUIRED, PK_BUILD_GIO="yes", PK_BUILD_GIO="no")
if test "x$PK_BUILD_GIO" = "xyes"; then
	with_gio="yes"
	AC_DEFINE(PK_BUILD_GIO, 1, [define if GIO is installed])
else
	with_gio="no"
	PK_BUILD_GIO=no
fi

AM_CONDITIONAL(PK_BUILD_GIO, test x$PK_BUILD_GIO = xyes)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Is NetworkManager available?
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(LIBNM, libnm_glib >= $LIBNM_GLIB_REQUIRED, PK_BUILD_NETWORKMANAGER="yes", PK_BUILD_NETWORKMANAGER="no")
if test "x$PK_BUILD_NETWORKMANAGER" = "xyes"; then
	with_networking_stack="NetworkManager"
	AC_DEFINE(PK_BUILD_NETWORKMANAGER, 1, [define if NetworkManager is installed])
else
	with_networking_stack="dummy"
	PK_BUILD_NETWORKMANAGER=no
fi

AM_CONDITIONAL(PK_BUILD_NETWORKMANAGER, test x$PK_BUILD_NETWORKMANAGER = xyes)
AC_SUBST(LIBNM_CFLAGS)
AC_SUBST(LIBNM_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Is docbook2man available?
dnl ---------------------------------------------------------------------------
AC_PATH_PROG(DOCBOOK2MAN, docbook2man, no)
if test "$DOCBOOK2MAN" = "no" ; then
	AC_MSG_WARN([docbook2man not found, will not be able to build man documentation])
	fi
AM_CONDITIONAL(HAVE_DOCBOOK2MAN, [test "$DOCBOOK2MAN" != "no"])

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

AC_ARG_WITH([backend_user],
	    AS_HELP_STRING([--with-backend-user=<user>],
			   [Alternative user for running the PackageKit backend]))
if test -z "$with_backend_user" ; then
	PK_BACKEND_USER=$PACKAGEKIT_USER
else
	PK_BACKEND_USER=$with_backend_user
fi
AC_SUBST(PK_BACKEND_USER)
AC_DEFINE_UNQUOTED(PK_BACKEND_USER,"$PK_BACKEND_USER", [Alternative user for running the PackageKit backend])

dnl ---------------------------------------------------------------------------
dnl - Build self tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests],[enable unit test code]),
	      enable_tests=$enableval,enable_tests=$DEVELOPMENT_RELEASE)
AM_CONDITIONAL(EGG_BUILD_TESTS, test x$enable_tests = xyes)
if test x$enable_tests = xyes; then
	AC_DEFINE(EGG_BUILD_TESTS,1,[Build test code])
fi

dnl ---------------------------------------------------------------------------
dnl - Display DAEMON messages?
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(developer, AS_HELP_STRING([--enable-developer],[enable daemon debug messages]),
	      enable_developer=$enableval,enable_developer=$DEVELOPMENT_RELEASE)
AM_CONDITIONAL(PK_IS_DEVELOPER, test x$enable_developer = xyes)
if test x$enable_developer = xyes; then
	AC_DEFINE(PK_IS_DEVELOPER,1,[Build developer code])
fi

dnl ---------------------------------------------------------------------------
dnl - Able to run from a checkout?
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(local, AS_HELP_STRING([--enable-local],[enable running in local checkout]),
	      enable_local=$enableval,enable_local=$DEVELOPMENT_RELEASE)
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
	PKG_CHECK_MODULES(PK_BROWSER_PLUGIN, mozilla-plugin gio-unix-2.0 cairo pango gtk+-2.0 dbus-glib-1,
	                  build_browser_plugin=yes, build_browser_plugin=no)
	AC_SUBST(PK_BROWSER_PLUGIN_CFLAGS)
	AC_SUBST(PK_BROWSER_PLUGIN_LIBS)
	if test $build_browser_plugin = "yes"; then
		# Check if we have GdkAppLaunchContext (new in GTK+-2.14)

		packagekit_save_CPPFLAGS="$CPPFLAGS"
		packagekit_save_LDFLAGS="$LDFLAGS"

		CPPFLAGS="$CPPFLAGS $PK_BROWSER_PLUGIN_CFLAGS"
		LDFLAGS="$CPPFLAGS $PK_BROWSER_PLUGIN_LIBS"

		AC_CHECK_FUNCS(gdk_app_launch_context_new)
		AC_CHECK_HEADER(npfunctions.h, xulrunner_plugin_new=yes, xulrunner_plugin_new=no)

		CPPFLAGS="$packagekit_save_CPPFLAGS"
		LDFLAGS="$packagekit_save_LDFLAGS"

		# Are we specifying a different mozilla plugin directory?
		AC_ARG_WITH(mozilla-plugin-dir, [AC_HELP_STRING([--with-mozilla-plugin-dir=<dir>], [where to put the mozilla plugin])])
		if ! test -z "$with_mozilla_plugin_dir" ; then
			MOZILLA_PLUGIN_DIR="$with_mozilla_plugin_dir"
		else
			MOZILLA_PLUGIN_DIR="$LIBDIR/mozilla/plugins"
		fi
		AC_SUBST(MOZILLA_PLUGIN_DIR)
	fi
else
	build_browser_plugin=no
fi
AM_CONDITIONAL(PK_BUILD_BROWSER_PLUGIN, test $build_browser_plugin = "yes")
AM_CONDITIONAL(PK_XULRUNNER_PLUGIN_NEW, test $xulrunner_plugin_new = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build gstreamer plugin
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gstreamer_plugin, AS_HELP_STRING([--enable-gstreamer-plugin],[Build gstreamer plugin functionality]),
	      enable_gstreamer_plugin=$enableval,enable_gstreamer_plugin=yes)
if test x$enable_gstreamer_plugin = xyes; then
	PKG_CHECK_MODULES(PK_GSTREAMER_PLUGIN, gstreamer-0.10 gstreamer-plugins-base-0.10 glib-2.0 dbus-glib-1,
	                  build_gstreamer_plugin=yes, build_gstreamer_plugin=no)
	AC_SUBST(PK_GSTREAMER_PLUGIN_CFLAGS)
	AC_SUBST(PK_GSTREAMER_PLUGIN_LIBS)
else
	build_gstreamer_plugin=no
fi
AM_CONDITIONAL(PK_BUILD_GSTREAMER_PLUGIN, test $build_gstreamer_plugin = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build GTK module
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gtk_module, AS_HELP_STRING([--enable-gtk-module],[Build GTK module functionality]),
	      enable_gtk_module=$enableval,enable_gtk_module=yes)
if test x$enable_gtk_module = xyes; then
	PKG_CHECK_MODULES(PK_GTK_MODULE, gtk+-2.0 pangoft2 fontconfig dbus-glib-1,
	                  build_gtk_module=yes, build_gtk_module=no)
	AC_SUBST(PK_GTK_MODULE_CFLAGS)
	AC_SUBST(PK_GTK_MODULE_LIBS)
else
	build_gtk_module=no
fi
AM_CONDITIONAL(PK_BUILD_GTK_MODULE, test $build_gtk_module = "yes")

dnl ---------------------------------------------------------------------------
dnl - Able to build BASH command-not-found functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(command_not_found, AS_HELP_STRING([--enable-command-not-found],[Build BASH command-not-found functionality]),
	      build_command_not_found=$enableval,build_command_not_found=yes)
AM_CONDITIONAL(PK_BUILD_COMMAND_NOT_FOUND, test $build_command_not_found = "yes")

dnl ---------------------------------------------------------------------------
dnl - Build ruck (a rug-like interface for PackageKit)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(ruck, AS_HELP_STRING([--enable-ruck],[Build ruck client]),
	      build_ruck=$enableval,build_ruck=yes)
AM_CONDITIONAL(PK_BUILD_RUCK, test $build_ruck = "yes")

dnl ---------------------------------------------------------------------------
dnl - Other tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gcov, AS_HELP_STRING([--enable-gcov],[compile with coverage profiling instrumentation (gcc only)]),
	      enable_gcov=$enableval,enable_gcov=no)
AC_ARG_ENABLE(gprof, AS_HELP_STRING([--enable-gprof],[compile with gprof support (gcc only)]),
	      enable_gprof=$enableval,enable_gprof=no)

# backends
AC_ARG_ENABLE(alpm, AS_HELP_STRING([--enable-alpm],[use the ALPM backend]),enable_alpm=$enableval,enable_alpm=no)
AC_ARG_ENABLE(apt, AS_HELP_STRING([--enable-apt],[use the APT backend]),enable_apt=$enableval,enable_apt=no)
AC_ARG_ENABLE(aptcc, AS_HELP_STRING([--enable-aptcc],[use the APTcc backend]),enable_aptcc=$enableval,enable_aptcc=no)
AC_ARG_ENABLE(box, AS_HELP_STRING([--enable-box],[use the BOX backend]),enable_box=$enableval,enable_box=no)
AC_ARG_ENABLE(conary, AS_HELP_STRING([--enable-conary],[use the CONARY backend]),enable_conary=$enableval,enable_conary=no)
AC_ARG_ENABLE(dummy, AS_HELP_STRING([--enable-dummy],[use the dummy backend]),enable_dummy=$enableval,enable_dummy=yes)
AC_ARG_ENABLE(opkg, AS_HELP_STRING([--enable-opkg],[use the OPKG backend]),enable_opkg=$enableval,enable_opkg=no)
AC_ARG_ENABLE(pisi, AS_HELP_STRING([--enable-pisi],[use the PiSi backend]),enable_pisi=$enableval,enable_pisi=no)
AC_ARG_ENABLE(poldek, AS_HELP_STRING([--enable-poldek],[use the poldek backend]),enable_poldek=$enableval,enable_poldek=no)
AC_ARG_ENABLE(razor, AS_HELP_STRING([--enable-razor],[use the razor backend]),enable_razor=$enableval,enable_razor=no)
AC_ARG_ENABLE(smart, AS_HELP_STRING([--enable-smart],[use the SMART backend]),enable_smart=$enableval,enable_smart=no)
AC_ARG_ENABLE(urpmi, AS_HELP_STRING([--enable-urpmi],[use the URPMI backend]),enable_urpmi=$enableval,enable_urpmi=no)
AC_ARG_ENABLE(yum, AS_HELP_STRING([--enable-yum],[use the YUM backend]),enable_yum=$enableval,enable_yum=no)
AC_ARG_ENABLE(zypp, AS_HELP_STRING([--enable-zypp],[use the Zypp backend]),enable_zypp=$enableval,enable_zypp=no)

# export to Makefile.am's
AM_CONDITIONAL(BACKEND_TYPE_ALPM, [test x$enable_alpm = xyes])
AM_CONDITIONAL(BACKEND_TYPE_APT, [test x$enable_apt = xyes])
AM_CONDITIONAL(BACKEND_TYPE_BOX, [test x$enable_box = xyes])
AM_CONDITIONAL(BACKEND_TYPE_CONARY, [test x$enable_conary = xyes])
AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$enable_dummy = xyes])
AM_CONDITIONAL(BACKEND_TYPE_OPKG, [test x$enable_opkg = xyes])
AM_CONDITIONAL(BACKEND_TYPE_PISI, [test x$enable_pisi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_POLDEK, [test x$enable_poldek = xyes])
AM_CONDITIONAL(BACKEND_TYPE_RAZOR, [test x$enable_razor = xyes])
AM_CONDITIONAL(BACKEND_TYPE_SMART, [test x$enable_smart = xyes])
AM_CONDITIONAL(BACKEND_TYPE_URPMI, [test x$enable_urpmi = xyes])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$enable_yum = xyes])
AM_CONDITIONAL(BACKEND_TYPE_ZYPP, [test x$enable_zypp = xyes])

if test x$enable_gcov = xyes; then
	## so that config.h changes when you toggle gcov support
	AC_DEFINE_UNQUOTED(PK_BUILD_GCOV, 1, [Defined if gcov is enabled to force a rebuild due to config.h changing])

	AC_MSG_CHECKING([for gcc 3.3 version of gcov file format])
	have_gcc33_gcov=no
	AC_RUN_IFELSE( [AC_LANG_PROGRAM( , [[ if (__GNUC__ >=3 && __GNUC_MINOR__ >= 3) exit (0); else exit (1); ]])],
			have_gcc33_gcov=yes)
	if test x$have_gcc33_gcov = xyes ; then
		AC_DEFINE_UNQUOTED(PK_HAVE_GCC33_GCOV, 1, [Defined if we have gcc 3.3 and thus the new gcov format])
	fi
	CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	AC_MSG_RESULT($have_gcc33_gcov)
fi
AM_CONDITIONAL(PK_BUILD_GCOV, test x$enable_gcov = xyes)

if test x$enable_gprof = xyes; then
	## so that config.h changes when you toggle gprof support
	AC_DEFINE_UNQUOTED(PK_BUILD_GPROF, 1, [Defined if gprof is enabled to force a rebuild due to config.h changing])
	CPPFLAGS="$CPPFLAGS -pg"
	LDFLAGS="$LDFLAGS -pg"
	CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	AC_MSG_RESULT($enable_gprof)
fi
AM_CONDITIONAL(PK_BUILD_GPROF, test x$enable_gprof = xyes)

dnl ---------------------------------------------------------------------------
dnl - Are we specifying a different dbus root ?
dnl ---------------------------------------------------------------------------
AC_ARG_WITH(dbus-sys,
	      [AC_HELP_STRING([--with-dbus-sys=<dir>],
	      [where D-BUS system.d directory is])])
AC_ARG_WITH(dbus-services,
	      [AC_HELP_STRING([--with-dbus-services=<dir>],
	      [where D-BUS system-services directory is])])
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
dnl - DocBook Documentation
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(docbook-docs, [  --enable-docbook-docs   build documentation (requires xmlto)],enable_docbook_docs=$enableval,enable_docbook_docs=auto)
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
	have_docbook=no
else
	have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
	if test x$have_docbook = xno ; then
		enable_docbook_docs=no
	else
		enable_docbook_docs=yes
	fi
fi
if test x$enable_docbook_docs = xyes; then
	if test x$have_docbook = xno; then
		AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
	fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT(yes)

dnl ---------------------------------------------------------------------------
dnl - Compile time default choice of security framework
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([security_framework],
	    AS_HELP_STRING([--with-security-framework=<option>],
			   [Default security framework to use polkit,dummy]))
# try and guess this if nothing is listed
if test x$with_security_framework = x; then
	if test -f /usr/bin/polkit-action ; then
		with_security_framework=polkit
	else
		AC_MSG_ERROR([--with-security-framework explicitly required when not using PolicyKit or RBAC])
	fi
fi

AC_DEFINE_UNQUOTED(security_framework, "$with_security_framework", [default security framework])
AC_SUBST(security_framework, "$with_security_framework")

if test x$with_security_framework = xpolkit; then
	PKG_CHECK_MODULES(POLKIT, \
			  polkit-dbus >= $POLKIT_DBUS_REQUIRED \
			  polkit-grant >= $POLKIT_GRANT_REQUIRED)
	AC_SUBST(POLKIT_CFLAGS)
	AC_SUBST(POLKIT_LIBS)
	AC_CHECK_PROG([POLKIT_POLICY_FILE_VALIDATE],
		      [polkit-policy-file-validate], [polkit-policy-file-validate])
	if test -z "$POLKIT_POLICY_FILE_VALIDATE"; then
	   AC_MSG_ERROR([polkit-policy-file-validate not found])
	fi
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
                           alpm,apt,aptcc,box,conary,dummy,razor,smart,urpmi,yum,pisi,zypp,opkg (dummy)]))
# default to a sane option for the installed tool
if test x$with_default_backend = x; then
	if test -f /usr/bin/yum ; then
		with_default_backend=yum
	elif test -f /usr/lib/libalpm.so; then
		with_default_backend=alpm
	elif test -f /usr/bin/apt-get ; then
		with_default_backend=apt
	elif test -f /usr/bin/conary ; then
		with_default_backend=conary
	elif test -f /usr/bin/box-repos ; then
		with_default_backend=box
	elif test -f /usr/bin/smart ; then
		with_default_backend=smart
	elif test -f /usr/bin/pisi ; then
		with_default_backend=pisi
	elif test -f /usr/bin/razor ; then
		with_default_backend=razor
	elif test -f /usr/bin/poldek ; then
		with_default_backend=poldek
	elif test -f /usr/bin/urpmq ; then
		with_default_backend=urpmi
	elif test -f /usr/bin/zypper ; then
		with_default_backend=zypp
	else
		with_default_backend=dummy
	fi
fi

AC_DEFINE_UNQUOTED(DEFAULT_BACKEND, "$with_default_backend", [default backend prefix])
AC_SUBST(DEFAULT_BACKEND, "$with_default_backend")

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

	AC_CHECK_LIB(apt-pkg, main, , [AC_MSG_ERROR([Can't find the APT libraries -- please install libapt-pkg-dev])])
	AC_CHECK_HEADER(apt-pkg/init.h, , [AC_MSG_ERROR([Can't find the APT header files -- please install libapt-pkg-dev])])
	AC_SUBST(APTCC_CFLAGS)
	AC_SUBST(APTCC_LIBS)

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
fi

if test x$enable_box = xyes; then
	PKG_CHECK_MODULES(BOX, libbox >= 0.1.4 )
	AC_SUBST(BOX_CFLAGS)
	AC_SUBST(BOX_LIBS)
fi

if test x$enable_opkg = xyes; then
	PKG_CHECK_MODULES(OPKG, libopkg = 0.1.5)
	AC_SUBST(OPKG_CFLAGS)
	AC_SUBST(OPKG_LIBS)
fi

if test x$enable_alpm = xyes; then
	with_default_backend=dummy
	AC_CHECK_HEADER([alpm.h],
			[with_default_backend=alpm],
			[AC_MSG_WARN([No alpm headers found - falling back to dummy backend])])
fi

if test x$enable_poldek = xyes; then
	POLDEK_CFLAGS="-I/usr/include/poldek"
	POLDEK_LIBS="-lpoclidek -lpoldek"
	AC_SUBST(POLDEK_CFLAGS)
	AC_SUBST(POLDEK_LIBS)
fi

if test x$enable_zypp = xyes; then
	PKG_CHECK_MODULES(ZYPP, libzypp >= 5.20.0)
	AC_SUBST(ZYPP_CFLAGS)
	AC_SUBST(ZYPP_LIBS)
fi

if test x$enable_razor = xyes; then
	PKG_CHECK_MODULES(RAZOR, razor >= 0.1)
	AC_SUBST(RAZOR_CFLAGS)
	AC_SUBST(RAZOR_LIBS)
fi

AC_SUBST(PK_CONF_DIR, "\$(sysconfdir)/PackageKit")
AC_SUBST(PK_YUM_PLUGIN_DIR, "\$(prefix)/lib/yum-plugins")
AC_SUBST(PK_DB_DIR, "\$(localstatedir)/lib/PackageKit")
AC_SUBST(PK_LOG_DIR, "\$(localstatedir)/log")
AC_SUBST(PK_PLUGIN_DIR, "\$(libdir)/packagekit-backend")
AC_SUBST(PK_PLUGIN_CFLAGS, "-I\$(top_srcdir)/src -I\$(top_srcdir)/lib $GLIB_CFLAGS $DBUS_CFLAGS $GMODULE_CFLAGS")
AC_SUBST(PK_PLUGIN_LIBS, "$GLIB_LIBS $DBUS_LIBS $GMODULE_LIBS")

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_OUTPUT([
Makefile
etc/Makefile
man/Makefile
docs/Makefile
docs/html/Makefile
docs/html/img/Makefile
docs/html/img/thumbnails/Makefile
docs/html/videos/Makefile
docs/api/Makefile
docs/api/dbus/Makefile
docs/api/version.xml
contrib/Makefile
contrib/browser-plugin/Makefile
contrib/gstreamer-plugin/Makefile
contrib/gtk-module/Makefile
contrib/yum-packagekit/Makefile
contrib/command-not-found/Makefile
contrib/ruck/Makefile
contrib/ruck/src/Makefile
backends/Makefile
backends/alpm/Makefile
backends/apt/Makefile
backends/aptcc/Makefile
backends/box/Makefile
backends/conary/Makefile
backends/dummy/Makefile
backends/opkg/Makefile
backends/smart/Makefile
backends/test/Makefile
backends/test/helpers/Makefile
backends/urpmi/Makefile
backends/urpmi/helpers/Makefile
backends/urpmi/helpers/perl_packagekit/Makefile
backends/urpmi/helpers/urpmi_backend/Makefile
backends/razor/Makefile
backends/yum/Makefile
backends/pisi/Makefile
backends/poldek/Makefile
backends/zypp/Makefile
data/Makefile
data/packagekit-glib.pc
data/packagekit-qt.pc
data/org.freedesktop.PackageKit.conf
data/org.freedesktop.PackageKitTestBackend.conf
data/org.freedesktop.PackageKitAptBackend.conf
data/tests/Makefile
lib/Makefile
lib/packagekit-glib/Makefile
lib/packagekit-qt/Makefile
lib/packagekit-qt/modules/Makefile
lib/packagekit-qt/src/Makefile
lib/packagekit-qt/test/Makefile
lib/packagekit-sharp/Makefile
lib/packagekit-sharp/samples/Makefile
lib/python/Makefile
lib/python/packagekit/Makefile
policy/Makefile
src/Makefile
client/Makefile
po/Makefile.in
])

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
        Developer warnings:        ${enable_developer}
        GCC coverage profiling:    ${enable_gcov}
        GCC time profiling:        ${enable_gprof}
        Security framework:        ${with_security_framework}
        Networking stack:          ${with_networking_stack}
        GIO support:               ${with_gio}
        Browser plugin:            ${build_browser_plugin}
        GStreamer plugin:          ${build_gstreamer_plugin}
        Application install:       ${enable_app_install}
        Pango module:              ${build_gtk_module}
        BASH Command not found:    ${build_command_not_found}
        RUCK client:               ${build_ruck}
        QT library:                ${build_qt}
	Managed bindings:          ${build_managed}

        Backends:
        ALPM backend:              ${enable_alpm}
        APT backend:               ${enable_apt}
        APTcc backend:             ${enable_aptcc}
        BOX backend:               ${enable_box}
        CONARY backend:            ${enable_conary}
        dummy backend:             ${enable_dummy}
        OPKG backend:              ${enable_opkg}
        Razor backend:             ${enable_razor}
        PiSi backend:              ${enable_pisi}
        poldek backend:            ${enable_poldek}
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

