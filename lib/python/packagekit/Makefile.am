BUILT_SOURCES = enums.py

NULL =

enums.py: $(top_srcdir)/lib/python/enum-convertor.py $(top_srcdir)/lib/packagekit-glib2/pk-enum.c
	python $(top_srcdir)/lib/python/enum-convertor.py $(top_srcdir)/lib/packagekit-glib2/pk-enum.c > enums.py

packagekitpythondir = ${PYTHON_PACKAGE_DIR}
packagekitpython_PYTHON =				\
	__init__.py					\
	backend.py					\
	frontend.py					\
	enums.py					\
	pkexceptions.py					\
	pkdbus.py					\
	progress.py					\
	package.py					\
	client.py					\
	filter.py					\
	misc.py						\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*.pyc

clean-local :
	rm -f *.pyc
	rm -f *~


-include $(top_srcdir)/git.mk
