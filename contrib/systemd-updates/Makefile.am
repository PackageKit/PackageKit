AM_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(LIBSYSTEMD_JOURNAL_CFLAGS)			\
	-DPK_COMPILATION				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/lib

libexec_PROGRAMS =					\
	pk-clear-offline-update				\
	pk-trigger-offline-update

pk_trigger_offline_update_SOURCES =				\
	pk-trigger-offline-update.c
pk_trigger_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)
pk_trigger_offline_update_LDFLAGS =				\
	$(PIE_LDFLAGS)

pk_clear_offline_update_SOURCES =				\
	pk-clear-offline-update.c
pk_clear_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)
pk_clear_offline_update_LDFLAGS =				\
	$(PIE_LDFLAGS)

-include $(top_srcdir)/git.mk
