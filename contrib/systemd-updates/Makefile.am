INCLUDES =						\
	$(GLIB_CFLAGS)					\
	-DPK_COMPILATION				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)/lib

libexec_PROGRAMS =					\
	pk-clear-offline-update				\
	pk-offline-update				\
	pk-trigger-offline-update

pk_trigger_offline_update_SOURCES =				\
	pk-trigger-offline-update.c
pk_trigger_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)

pk_clear_offline_update_SOURCES =				\
	pk-clear-offline-update.c
pk_clear_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

pk_offline_update_SOURCES =				\
	pk-offline-update.c

pk_offline_update_LDADD =				\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(PK_GLIB2_LIBS)

pk_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)

systemdservicedir = $(systemdsystemunitdir)
systemdservice_in_files = packagekit-offline-update.service.in
systemdservice_DATA = $(systemdservice_in_files:.service.in=.service)

$(systemdservice_DATA): $(systemdservice_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(systemdservice_in_files)

MAINTAINERCLEANFILES =					\
	$(systemdservice_DATA)

-include $(top_srcdir)/git.mk
