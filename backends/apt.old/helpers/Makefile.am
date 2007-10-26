
helperdir = $(datadir)/PackageKit/helpers/apt

NULL =

dist_helper_DATA = 			\
	refresh-cache.py		\
	aptBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

