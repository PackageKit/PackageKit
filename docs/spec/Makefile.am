IMAGE_FILES =				\
	pk-structure.png		\
	pk-structure.svg		\
	pk-transactions.svg		\
	pk-transactions-failure.png	\
	pk-transactions-sig-install.png	\
	pk-transactions-success.png	\
	pk-transactions-trusted.png

SPEC_XML_FILES =			\
	pk-backend-compiled.xml		\
	pk-backend-dbus.xml		\
	pk-backend-spawn.xml		\
	pk-concepts.xml			\
	pk-introduction.xml		\
	pk-methods.xml			\
	pk-reference.xml		\
	pk-signals.xml			\
	pk-reference.xml		\
	pk-introduction.xml

SPEC_HTML_FILES =			\
	pk-reference.html

clean-local:
	rm -f *~
	rm -f *.html

EXTRA_DIST = 				\
	config.xsl			\
	docbook.css			\
	$(SPEC_XML_FILES)		\
	$(IMAGE_FILES)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)
htmldoc_DATA = pk-reference.html

pk-reference.html : pk-introduction.xml
	$(XMLTO) html-nochunks -m config.xsl pk-reference.xml

EXTRA_DIST += $(SPEC_HTML_FILES)

endif #DOCBOOK_DOCS_ENABLED

