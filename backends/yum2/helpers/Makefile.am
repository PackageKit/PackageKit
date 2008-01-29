
helperdir = $(datadir)/PackageKit/helpers/yum2

NULL =

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =					\
	yumDBUSBackend.py				\
	$(NULL)

EXTRA_DIST =						\
	$(dbusinstance_DATA)				\
	$(NULL)

clean-local :
	rm -f *.pyc
	rm -f *~

