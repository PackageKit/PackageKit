dnl Process this file with autoconf to produce a configure script.

AC_INIT(src/plugin.cpp)

AM_INIT_AUTOMAKE([packagekit-plugin], 0.1)
AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

AC_DISABLE_STATIC
AM_PROG_LIBTOOL

# Internationalisation
IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=packagekit-plugin
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

changequote(,)dnl
if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[\ \	]-Wall[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi
changequote([,])dnl

PKG_CHECK_MODULES(PLUGIN, mozilla-plugin cairo pango gtk+-2.0 packagekit gnome-vfs-2.0 gnome-desktop-2.0)

AC_SUBST(PLUGIN_CFLAGS)
AC_SUBST(PLUGIN_LIBS)

AC_OUTPUT([
Makefile
po/Makefile.in
])
