
helperdir = $(datadir)/PackageKit/helpers/yum2

NULL =

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =					\
	yumDBUSBackend.py				\
	$(NULL)

EXTRA_DIST =						\
	$(dbusinstance_DATA)				\
	$(NULL)

install-data-hook:
	chmod ug+rx $(DESTDIR)$(libexecdir)/*.py

clean-local :
	rm -f *.pyc
	rm -f *~

