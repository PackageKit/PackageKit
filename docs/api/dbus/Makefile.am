
if ENABLE_GTK_DOC

all : org.freedesktop.PackageKit.ref.xml org.freedesktop.PackageKit.Transaction.ref.xml

org.freedesktop.PackageKit.ref.xml : $(top_srcdir)/src/org.freedesktop.PackageKit.xml $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.PackageKit.Transaction.ref.xml : $(top_srcdir)/src/org.freedesktop.PackageKit.Transaction.xml $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

endif

EXTRA_DIST = spec-to-docbook.xsl dbus-introspect-docs.dtd

CLEANFILES =						\
	org.freedesktop.PackageKit.Transaction.ref.xml	\
	org.freedesktop.PackageKit.ref.xml

clean-local :
	rm -f *~ *.ref.xml

-include $(top_srcdir)/git.mk
