EXTRA_DIST =				\
	pkcon.xml			\
	pkmon.xml

if HAVE_DOCBOOK2MAN
man_MANS =				\
	pkcon.1				\
	pkmon.1
endif

if HAVE_DOCBOOK2MAN
pkcon.1: pkcon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? > /dev/null

pkmon.1: pkmon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

