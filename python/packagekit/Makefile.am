BUILT_SOURCES = enums.py

NULL =

enums.py: ../enum-convertor.py ../../libpackagekit/pk-enum.c
	(cd ..; python enum-convertor.py > packagekit/enums.py)

packagekitpythondir = ${PYTHON_PACKAGE_DIR}
packagekitpython_PYTHON =				\
	__init__.py					\
	backend.py					\
	frontend.py					\
	enums.py					\
	$(NULL)

clean-local :
	rm -f *.pyc
	rm -f *~

