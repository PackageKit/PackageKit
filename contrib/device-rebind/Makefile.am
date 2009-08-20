## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

EXTRA_DIST =						\
	pk-device-rebind.xml				\
	$(NULL)

if HAVE_XSLTPROC
man_MANS =						\
	pk-device-rebind.1				\
	$(NULL)
endif

if HAVE_XSLTPROC
pk-device-rebind.1: pk-device-rebind.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

INCLUDES = $(GLIB_CFLAGS)

sbin_PROGRAMS = pk-device-rebind

pk_device_rebind_SOURCES =				\
	pk-device-rebind.c				\
	$(NULL)

pk_device_rebind_LDADD =				\
	$(GLIB_LIBS)					\
	$(NULL)

pk_device_rebind_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out

