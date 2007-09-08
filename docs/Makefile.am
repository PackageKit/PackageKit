IMAGE_FILES =				\
	pk-structure.png		\
	pk-structure.svg

SPEC_XML_FILES =			\
	reference.xml			\
	introduction.xml		\
	backends.xml

SPEC_HTML_FILES =			\
	reference.html

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
htmldoc_DATA = reference.html

reference.html : introduction.xml
	$(XMLTO) html-nochunks -m config.xsl reference.xml

EXTRA_DIST += $(SPEC_HTML_FILES)

endif #DOCBOOK_DOCS_ENABLED
