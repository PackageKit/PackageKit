BUILT_SOURCES = enums.py

enums.py: $(top_srcdir)/lib/python/enum-convertor.py $(top_srcdir)/lib/packagekit-glib2/pk-enum.c
	python $(top_srcdir)/lib/python/enum-convertor.py $(top_srcdir)/lib/packagekit-glib2/pk-enum.c > enums.py

if HAVE_PYTHON_BACKEND
packagekitpythondir = ${PYTHON_PACKAGE_DIR}
packagekitpython_PYTHON =				\
	__init__.py					\
	backend.py					\
	enums.py					\
	progress.py					\
	package.py					\
	filter.py					\
	misc.py
endif

MAINTAINERCLEANFILES =					\
	*.pyc

clean-local :
	rm -f *.pyc
	rm -f *~

-include $(top_srcdir)/git.mk
