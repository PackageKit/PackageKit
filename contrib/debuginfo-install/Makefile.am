## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

EXTRA_DIST =						\
	pk-debuginfo-install.xml			\
	$(NULL)

if HAVE_XSLTPROC
man_MANS =						\
	pk-debuginfo-install.1				\
	$(NULL)
endif

if HAVE_XSLTPROC
pk-debuginfo-install.1: pk-debuginfo-install.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/lib

PK_GLIB_LIBS = $(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la

bin_PROGRAMS = pk-debuginfo-install

pk_debuginfo_install_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-debuginfo-install.c				\
	pk-progress-bar.c				\
	pk-progress-bar.h				\
	$(NULL)

pk_debuginfo_install_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pk_debuginfo_install_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out

