## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DG_LOG_DOMAIN=\"PackageKit\"			\
	-DPK_COMPILATION				\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-I$(top_srcdir)/lib

libexec_PROGRAMS = pk-command-not-found

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a	\
	$(NULL)

pk_command_not_found_SOURCES =				\
	pk-command-not-found.c				\
	$(NULL)

pk_command_not_found_LDADD =				\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pk_command_not_found_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

bashprofiledir = ${SYSCONFDIR}/profile.d
dist_bashprofile_DATA = PackageKit.sh

PackageKit.sh: PackageKit.sh.in Makefile
	sed -e s!\@LIBEXECDIR\@!$(libexecdir)! < $< > $@

confdir = $(PK_CONF_DIR)
dist_conf_DATA = CommandNotFound.conf

EXTRA_DIST = PackageKit.sh.in

CLEANFILES = PackageKit.sh

-include $(top_srcdir)/git.mk
