IMAGE_FILES =				\
	pk-structure.png		\
	pk-structure.svg

SPEC_XML_FILES =			\
	pk-reference.xml		\
	pk-introduction.xml		\
	pk-backends.xml

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
