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
	docbook2man $? > /dev/null

pkmon.1: pkmon.xml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

