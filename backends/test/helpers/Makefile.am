
helperdir = $(datadir)/PackageKit/helpers/test_spawn

NULL =

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =					\
	PackageKitDbusTest.py				\
	$(NULL)

dist_helper_DATA = 					\
	search-name.sh					\
	$(NULL)

EXTRA_DIST =						\
	$(dbusinstance_DATA)				\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

