EXTRA_DIST =				\
	pkcon.xml			\
	pkmon.xml			\
	pkgenpack.xml

man_MANS =				\
	pkcon.1				\
	pkmon.1				\
	pkgenpack.1

pkcon.1: pkcon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkmon.1: pkmon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkgenpack.1: pkgenpack.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

