AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)

noinst_PROGRAMS =					\
	pk-refresh-licenses

pk_refresh_licenses_SOURCES =				\
	pk-refresh-licenses.c

pk_refresh_licenses_LDADD =				\
	-lm						\
	$(GLIB_LIBS)

pk_refresh_licenses_CFLAGS =				\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~
	rm -f *.tmp
	rm -f *.wiki

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

-include $(top_srcdir)/git.mk
