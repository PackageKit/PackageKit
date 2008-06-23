
if ENABLE_GTK_DOC

all : pk-interface.ref.xml pk-interface-transaction.ref.xml

pk-interface.ref.xml : $(top_srcdir)/src/pk-interface.xml $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

pk-interface-transaction.ref.xml : $(top_srcdir)/src/pk-interface-transaction.xml $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) $(top_srcdir)/docs/api/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

#$(top_srcdir)/docs/api/dbus/spec-to-docbook $(top_srcdir)/src/pk-interface.xml

endif

EXTRA_DIST = spec-to-docbook.xsl dbus-introspect-docs.dtd

clean-local :
	rm -f *~ *.ref.xml
