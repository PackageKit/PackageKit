AC_PREREQ(2.52)

AC_INIT(PackageKit, 0.0.1)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

# libtool versioning - this applies to libpackagekit
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
LT_CURRENT=1
LT_REVISION=0
LT_AGE=0
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
    CPPFLAGS="$CPPFLAGS -Werror -Wcast-align -Wno-uninitialized"
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
DBUS_REQUIRED=1.1.1
DBUS_GLIB_REQUIRED=0.74
LIBNM_REQUIRED=0.6.4
POLKIT_DBUS_REQUIRED=0.5
POLKIT_GRANT_REQUIRED=0.5

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

PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_GLIB_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(LIBNM, \
 libnm_glib >= $LIBNM_REQUIRED)
AC_SUBST(LIBNM_CFLAGS)
AC_SUBST(LIBNM_LIBS)

PKG_CHECK_MODULES(POLKIT, \
 polkit-dbus >= $POLKIT_DBUS_REQUIRED \
 polkit-grant >= $POLKIT_GRANT_REQUIRED)

AC_CHECK_PROG([POLKIT_POLICY_FILE_VALIDATE],
              [polkit-policy-file-validate], [polkit-policy-file-validate])
if test -z "$POLKIT_POLICY_FILE_VALIDATE"; then
   AC_MSG_ERROR([polkit-policy-file-validate not found])
fi

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
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)

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
AC_ARG_ENABLE(tests, [  --enable-tests          Build self tests],enable_tests=$enableval,enable_tests=no)
AC_MSG_CHECKING([whether to support tests])
have_tests=no
if test x$enable_tests = xyes ; then
    have_tests=yes
	AC_MSG_RESULT([yes])
    AC_DEFINE(HAVE_TESTS, 1, [Define if we want to use the self tests])
else
	AC_MSG_RESULT([no])
fi
AM_CONDITIONAL([HAVE_TESTS], [test $have_tests = yes])

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
dnl - Compile time choice of backend
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([backend],
	    AS_HELP_STRING([--with-backend=<option>],
			   [Backend to use dummy,apt,yum,conary (dummy)]))
# default to a sane option for the installed tool
if test x$with_backend = x; then
    if test -f /usr/bin/yum ; then
        with_backend=yum
    elif test -f /usr/bin/apt-get ; then
        with_backend=apt
    elif test -f /usr/bin/conary ; then
        with_backend=conary
    elif test -f /usr/bin/box-repos ; then
        with_backend=box
    else
        with_backend=dummy
    fi
fi

# we need this for the spawn code
if test x$with_backend = xyum; then
    AC_DEFINE(BACKEND_PREFIX, "yum", [backend prefix])
elif test x$with_backend = xapt; then
    AC_DEFINE(BACKEND_PREFIX, "apt", [backend prefix])
elif test x$with_backend = xconary; then
    AC_DEFINE(BACKEND_PREFIX, "conary", [backend prefix])
elif test x$with_backend = xbox; then
    AC_DEFINE(BACKEND_PREFIX, "box", [backend prefix])
else
    AC_DEFINE(IGNORE_POLKIT, "err...", [if we should disable polkit])
    AC_DEFINE(BACKEND_PREFIX, "dummy", [backend prefix])
fi

AC_DEFUN([APT_BACKEND],
[
  if test "$APT_PKG_TYPE" == "" ; then
	   AC_LANG_PUSH(C++)
		_libaptpkg_save_cppflags=$CPPFLAGS
	   CPPFLAGS="$APT_CFLAGS $CPPFLAGS"
	   _APT_save_libs=$LIBS
	   LIBS="$APT_LIBS $LIBS"
	   
	   AC_MSG_CHECKING([for apt support for $1 packages])
		AC_RUN_IFELSE(AC_LANG_PROGRAM([
		#include <apt-pkg/configuration.h>
		#include <apt-pkg/pkgsystem.h>
		#include <apt-pkg/init.h>
		#include <stdio.h>
	   ],[
		if (pkgInitConfig(*_config) == false)
		{
			fprintf(stderr,"pkginitconfig was false");
			return -1;
		}
		if (pkgInitSystem(*_config, _system) == false)
		{
			fprintf(stderr,"pkginitsystem was false");
			return -1;
		}
		if (_system->ArchiveSupported("$1"))
			return 0;
		else
			return 1;
		]),[
			APT_PKG_TYPE=$1
			AC_MSG_RESULT([yes])
			AC_DEFINE(APT_PKG_$2,1,[apt-pkg support files of type $1])
		],)
		AC_LANG_POP(C++)
	   CPPFLAGS=$_libaptpkg_save_cppflags
	   LIBS=$_libaptpkg_save_libs
	   unset _libaptpkg_save_cppflags
	   unset _libaptpkg_save_libs
 	fi
])

if test x$with_backend = xapt; then
   
   AC_ARG_WITH(libapt-pkg-lib,
     AC_HELP_STRING([--with-libapt-pkg-lib=DIR],[look for the libapt-pkg library in DIR]),
     [_libaptpkg_with_lib=$withval],[_libaptpkg_with_lib=no])
  	if test "$_libaptpkg_with_lib" == "no" ; then
		APT_LIBS="-lapt-pkg"
    else
        APT_LIBS="-L$withval -lapt-pkg"
	fi	

   AC_ARG_WITH(libapt-pkg-includes,
     AC_HELP_STRING([--with-libapt-pkg-includes=DIR],[look for the libapt-pkg includes in DIR]),
     [_libaptpkg_with_inc=$withval],[_libaptpkg_with_inc=no])
  	if test "$_libaptpkg_with_inc" == "no" ; then
		APT_CFLAGS="-I/usr/include/apt-pkg"
    else
		APT_CFLAGS="-I$withval"
	fi	

	AC_CACHE_CHECK([whether libapt-pkg is usable],
	   [libaptpkg_usable],
	   [
	   _libaptpkg_save_cppflags=$CPPFLAGS
	   CPPFLAGS="$APT_CFLAGS $CPPFLAGS"
	   _APT_save_libs=$LIBS
	   LIBS="$APT_LIBS $LIBS"

	   AC_LANG_PUSH(C++)
	   AC_LINK_IFELSE(AC_LANG_PROGRAM([
		#include <apt-pkg/configuration.h>
	   	#include <apt-pkg/pkgsystem.h>
	   	#include <apt-pkg/init.h>
		#include <stdio.h>
	   ],[
		if (pkgInitConfig(*_config) == false)
		{
			fprintf(stderr,"pkginitconfig was false");
			return -1;
		}
		if (pkgInitSystem(*_config, _system) == false)
		{
			fprintf(stderr,"pkginitsystem was false");
			return -1;
		}
		return 0;
]),libaptpkg_usable=yes,AC_MSG_ERROR([libapt-pkg not found]))

	   CPPFLAGS=$_libaptpkg_save_cppflags
	   LIBS=$_libaptpkg_save_libs
	   unset _libaptpkg_save_cppflags
	   unset _libaptpkg_save_libs
	   ])
    AC_LANG_POP(C++)

	APT_BACKEND(deb,DEB)
	APT_BACKEND(rpm,RPM)
  	if test "$APT_PKG_TYPE" == "" ; then
		AC_MSG_ERROR([Couldn't find support for any type of packages that we know about for Apt!])
	fi
		
    AC_SUBST(APT_CFLAGS)
    AC_SUBST(APT_LIBS)
    AC_SUBST(APT_PKG_TYPE)
fi

if test x$with_backend = xbox; then
	PKG_CHECK_MODULES(BOX, libbox)
	AC_SUBST(BOX_CFLAGS)
	AC_SUBST(BOX_LIBS)
fi

AC_SUBST(PK_CONF_DIR, "\$(sysconfdir)/PackageKit")
AC_SUBST(PK_PLUGIN_DIR, "\$(libdir)/packagekit-backend")
AC_SUBST(PK_PLUGIN_CFLAGS, "-I\$(top_srcdir)/src -I\$(top_srcdir)/libpackagekit $GLIB_CFLAGS $DBUS_CFLAGS $GMODULE_CFLAGS")
AC_SUBST(PK_PLUGIN_LIBS, "$GLIB_LIBS $DBUS_LIBS $GMODULE_LIBS")

AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$with_backend = xdummy], [using dummy backend])
AM_CONDITIONAL(BACKEND_TYPE_APT, [test x$with_backend = xapt], [using APT backend])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$with_backend = xyum], [using YUM backend])
AM_CONDITIONAL(BACKEND_TYPE_CONARY, [test x$with_backend = xconary], [using CONARY backend])
AM_CONDITIONAL(BACKEND_TYPE_BOX, [test x$with_backend = xbox], [using BOX backend])


dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_OUTPUT([
packagekit.pc
Makefile
etc/Makefile
man/Makefile
backends/Makefile
backends/apt/Makefile
backends/box/Makefile
backends/box/helpers/Makefile
backends/conary/Makefile
backends/conary/helpers/Makefile
backends/dummy/Makefile
backends/test/Makefile
backends/yum/Makefile
backends/yum/helpers/Makefile
data/Makefile
libselftest/Makefile
libgbus/Makefile
libpackagekit/Makefile
policy/Makefile
src/Makefile
client/Makefile
PackageKit.conf
])

dnl ==========================================================================
echo "
                    PackageKit $VERSION
                  ====================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        Backend:                   ${with_backend}
"

