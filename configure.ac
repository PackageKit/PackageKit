AC_PREREQ(2.52)

AC_INIT(PackageKit, 0.0.1)
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL

GNOME_COMPILE_WARNINGS
GNOME_DOC_INIT

AC_PATH_PROG(GLIB_GENMARSPACKAGEKIT, glib-genmarshal)

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
if test "$GCC" = "yes"; then
    CPPFLAGS="$CPPFLAGS -Werror -Wcast-align -Wno-uninitialized"
    CPPFLAGS="$CPPFLAGS -Wall"
fi

dnl ---------------------------------------------------------------------------
dnl - Library dependencies
dnl ---------------------------------------------------------------------------
GLIB_REQUIRED=2.6.0
DBUS_REQUIRED=0.70

dnl ---------------------------------------------------------------------------
dnl - Make above strings available for packaging files (e.g. rpm spec files)
dnl ---------------------------------------------------------------------------
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(DBUS_REQUIRED)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(DBUS, \
 dbus-glib-1 >= $DBUS_REQUIRED \
 dbus-1 >= $DBUS_REQUIRED \
 gthread-2.0)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)

dnl ---------------------------------------------------------------------------
dnl - PolicyKit support?
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(policykit, [  --enable-policykit      support PolicyKit permissions daemon (requires polkitd)],enable_polkit=$enableval,enable_polkit=no)
AC_MSG_CHECKING([whether to support PolicyKit])
have_polkit=no
if test x$enable_polkit = xyes ; then
    have_polkit=yes
    AC_DEFINE(HAVE_POLKIT, 1, [Define if we use PolicyKit])
fi
#AC_MSG_RESULT(yes)

AC_ARG_WITH([packagekit_user],
	    AS_HELP_STRING([--with-packagekit-user=<user>],
	                   [User for running the PacakgeKit daemon (root)]))
if test -z "$with_packagekit_user" ; then
    PACKAGEKIT_USER=root
else
    PACKAGEKIT_USER=$with_packagekit_user
fi
AC_SUBST(PACKAGEKIT_USER)
AC_DEFINE_UNQUOTED(PACKAGEKIT_USER,"$PACKAGEKIT_USER", [User for running the PacakgeKit daemon])

dnl ---------------------------------------------------------------------------
dnl - Build self tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(tests, [  --enable-tests          Build self tests],enable_tests=$enableval,enable_tests=no)
AC_MSG_CHECKING([whether to support tests])
have_tests=no
if test x$enable_tests = xyes ; then
    have_tests=yes
    AC_DEFINE(HAVE_TESTS, 1, [Define if we want to use the self tests])
fi
AM_CONDITIONAL([HAVE_TESTS], [test $have_tests = yes])


dnl ---------------------------------------------------------------------------
dnl - Compile time choice of backend
dnl ---------------------------------------------------------------------------
AC_ARG_WITH([backend],
	    AS_HELP_STRING([--with-backend=<option>],
			   [Backend to use dummy,apt,yum (dummy)]))
# default to the dummy backend
if test x$with_backend = x; then
    with_backend=dummy
fi
AM_CONDITIONAL(BACKEND_TYPE_DUMMY, [test x$with_backend = xdummy], [using APT])
AM_CONDITIONAL(BACKEND_TYPE_APT, [test x$with_backend = xapt], [using APT])
AM_CONDITIONAL(BACKEND_TYPE_YUM, [test x$with_backend = xdummy], [using APT])


dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_OUTPUT([
Makefile
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
        PolicyKit support:         ${have_polkit}
        Self test support:         ${have_tests}
"

