AC_PREREQ(2.52)

AC_INIT(PackageKit, 0.2.6)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

# Should we enable extra stuff automatically?
# set no for release builds, yes for development builds
DEVELOPMENT_RELEASE=no

# libtool versioning - this applies to libpackagekit
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# increment;
# CURRENT	If the API or ABI interface has changed (reset REVISION to 0)
# REVISION	If the API and ABI remains the same, but bugs are fixed.
# AGE		If libpackagekit can be linked into executables which can be
# 		built with previous versions of this library. Don't use.
LT_CURRENT=4
LT_REVISION=0
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
if test "$GCC" = "yes"; then
	CPPFLAGS="$CPPFLAGS -Werror -Wcast-align -Wno-uninitialized"
#	CPPFLAGS="$CPPFLAGS -Wall -Wformat-security"
	CPPFLAGS="$CPPFLAGS -Wall"
fi

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
DBUS_GLIB_REQUIRED=0.73
LIBNM_GLIB_REQUIRED=0.6.4
POLKIT_DBUS_REQUIRED=0.8
POLKIT_GRANT_REQUIRED=0.8

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
dnl - xsltproc
dnl ---------------------------------------------------------------------------
AC_PATH_PROG([XSLTPROC], [xsltproc])

dnl ---------------------------------------------------------------------------
dnl - Is GIO available?
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GIO, gio-2.0 >= $GIO_REQUIRED, PK_BUILD_GIO="yes", PK_BUILD_GIO="no")
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
AM_CONDITIONAL(PK_BUILD_TESTS, test x$enable_tests = xyes)
if test x$enable_tests = xyes; then
	AC_DEFINE(PK_BUILD_TESTS,1,[Build test code])
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
dnl - Other tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gcov, AS_HELP_STRING([--enable-gcov],[compile with coverage profiling instrumentation (gcc only)]),
	      enable_gcov=$enableval,enable_gcov=no)
AC_ARG_ENABLE(gprof, AS_HELP_STRING([--enable-gprof],[compile with gprof support (gcc only)]),
	      enable_gprof=$enableval,enable_gprof=no)

# backends
AC_ARG_ENABLE(alpm, AS_HELP_STRING([--enable-alpm],[use the ALPM backend]),enable_alpm=$enableval,enable_alpm=no)
AC_ARG_ENABLE(apt, AS_HELP_STRING([--enable-apt],[use the APT backend]),enable_apt=$enableval,enable_apt=no)
AC_ARG_ENABLE(box, AS_HELP_STRING([--enable-box],[use the BOX backend]),enable_box=$enableval,enable_box=no)
AC_ARG_ENABLE(conary, AS_HELP_STRING([--enable-conary],[use the CONARY backend]),enable_conary=$enableval,enable_conary=no)
AC_ARG_ENABLE(dummy, AS_HELP_STRING([--enable-dummy],[use the dummy backend]),enable_dummy=$enableval,enable_dummy=yes)
AC_ARG_ENABLE(opkg, AS_HELP_STRING([--enable-opkg],[use the OPKG backend]),enable_opkg=$enableval,enable_opkg=no)
AC_ARG_ENABLE(pisi, AS_HELP_STRING([--enable-pisi],[use the PiSi backend]),enable_pisi=$enableval,enable_pisi=no)
AC_ARG_ENABLE(poldek, AS_HELP_STRING([--enable-poldek],[use the poldek backend]),enable_poldek=$enableval,enable_poldek=no)
AC_ARG_ENABLE(smart, AS_HELP_STRING([--enable-smart],[use the SMART backend]),enable_smart=$enableval,enable_smart=no)
AC_ARG_ENABLE(urpmi, AS_HELP_STRING([--enable-urpmi],[use the URPMI backend]),enable_urpmi=$enableval,enable_urpmi=no)
AC_ARG_ENABLE(yum, AS_HELP_STRING([--enable-yum],[use the YUM backend]),enable_yum=$enableval,enable_yum=no)
AC_ARG_ENABLE(yum2, AS_HELP_STRING([--enable-yum2],[use the YUM DBUS backend]),enable_yum2=$enableval,enable_yum2=no)
AC_ARG_ENABLE(zypp, AS_HELP_STRING([--enable-zypp],[use the Zypp backend]),enable_zypp=$enableval,enable_zypp=no)

# export to Makefile.am's
AM_CONDITIONAL(BACKEND_TYPE_ALPM, [test x$enable_alpm = xyes], [using ALPM backend])
AM_CONDITIONAL(BACKEND_TYPE_APT, [test x$enable_apt = xyes], [using APT backend])
AM_CONDITIONAL(BACKEND_TYPE_BOX, [test x$enable_box = xyes], [using BOX backend])
AM_CONDITIONAL(BACKEND_TYPE_CONARY, [test x$enable_conary = xyes], [using CONARY backend])
AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$enable_dummy = xyes], [using dummy backend])
AM_CONDITIONAL(BACKEND_TYPE_OPKG, [test x$enable_opkg = xyes], [using OPKG backend])
AM_CONDITIONAL(BACKEND_TYPE_PISI, [test x$enable_pisi = xyes], [using PiSi backend])
AM_CONDITIONAL(BACKEND_TYPE_POLDEK, [test x$enable_poldek = xyes], [using poldek backend])
AM_CONDITIONAL(BACKEND_TYPE_SMART, [test x$enable_smart = xyes], [using SMART backend])
AM_CONDITIONAL(BACKEND_TYPE_URPMI, [test x$enable_urpmi = xyes], [using URPMI backend])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$enable_yum = xyes], [using YUM backend])
AM_CONDITIONAL(BACKEND_TYPE_YUM2, [test x$enable_yum2 = xyes], [using YUM DBUS backend])
AM_CONDITIONAL(BACKEND_TYPE_ZYPP, [test x$enable_zypp = xyes], [using Zypp backend])

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

AM_CONDITIONAL(SECURITY_TYPE_DUMMY, [test x$with_security_framework = xdummy], [using dummy security framework])
AM_CONDITIONAL(SECURITY_TYPE_POLKIT, [test x$with_security_framework = xpolkit], [using PolicyKit security framework])

dnl ---------------------------------------------------------------------------
dnl - Compile time default choice of backend
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([default_backend],
	    AS_HELP_STRING([--with-default-backend=<option>],
			   [Default backend to use
                           alpm,apt,box,conary,dummy,smart,urpmi,yum,pisi,zypp,opkg (dummy)]))
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
	PKG_CHECK_MODULES(ZYPP, libzypp >= 4.25.0)
	AC_SUBST(ZYPP_CFLAGS)
	AC_SUBST(ZYPP_LIBS)
fi

AC_SUBST(PK_CONF_DIR, "\$(sysconfdir)/PackageKit")
AC_SUBST(PK_YUM_PLUGIN_DIR, "\$(prefix)/lib/yum-plugins")
AC_SUBST(PK_DB_DIR, "\$(localstatedir)/lib/PackageKit")
AC_SUBST(PK_LOG_DIR, "\$(localstatedir)/log")
AC_SUBST(PK_PLUGIN_DIR, "\$(libdir)/packagekit-backend")
AC_SUBST(PK_PLUGIN_CFLAGS, "-I\$(top_srcdir)/src -I\$(top_srcdir)/libpackagekit $GLIB_CFLAGS $DBUS_CFLAGS $GMODULE_CFLAGS")
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
docs/api/Makefile
docs/api/dbus/Makefile
docs/api/version.xml
contrib/Makefile
contrib/udev/Makefile
contrib/yum-packagekit/Makefile
backends/Makefile
backends/alpm/Makefile
backends/apt/Makefile
backends/box/Makefile
backends/conary/Makefile
backends/conary/helpers/Makefile
backends/dummy/Makefile
backends/opkg/Makefile
backends/smart/Makefile
backends/smart/helpers/Makefile
backends/test/Makefile
backends/test/helpers/Makefile
backends/urpmi/Makefile
backends/urpmi/helpers/Makefile
backends/urpmi/helpers/perl_packagekit/Makefile
backends/urpmi/helpers/urpmi_backend/Makefile
backends/yum/Makefile
backends/yum/helpers/Makefile
backends/yum2/Makefile
backends/yum2/helpers/Makefile
backends/pisi/Makefile
backends/pisi/helpers/Makefile
backends/poldek/Makefile
backends/zypp/Makefile
data/Makefile
data/packagekit.pc
data/org.freedesktop.PackageKit.conf
data/org.freedesktop.PackageKitTestBackend.conf
data/org.freedesktop.PackageKitYumBackend.conf
data/org.freedesktop.PackageKitAptBackend.conf
data/tests/Makefile
libselftest/Makefile
libgbus/Makefile
libpackagekit/Makefile
policy/Makefile
src/Makefile
client/Makefile
python/Makefile
python/packagekit/Makefile
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
        Building unit tests:       ${enable_tests}
        Able to run locally:       ${enable_local}
        Developer warnings:        ${enable_developer}
        GCC coverage profiling:    ${enable_gcov}
        GCC time profiling:        ${enable_gprof}
        Security framework:        ${with_security_framework}
        Networking stack:          ${with_networking_stack}
        GIO support:               ${with_gio}

        Backends:
        ALPM backend:              ${enable_alpm}
        APT backend:               ${enable_apt}
        BOX backend:               ${enable_box}
        CONARY backend:            ${enable_conary}
        dummy backend:             ${enable_dummy}
        OPKG backend:              ${enable_opkg}
        PiSi backend:              ${enable_pisi}
        poldek backend:            ${enable_poldek}
        SMART backend:             ${enable_smart}
        URPMI backend:             ${enable_urpmi}
        YUM backend:               ${enable_yum}
        YUM2 backend:              ${enable_yum2}
        Zypp backend:              ${enable_zypp}
        Default backend:           ${with_default_backend}
"



# warn that dummy is basically broken
if test x$with_security_framework = xdummy; then
	echo "*******************************************************************"
	echo "** YOU ARE NOT USING A SECURE DAEMON. ALL USERS CAN DO ANYTHING! **"
	echo "*******************************************************************"
fi

