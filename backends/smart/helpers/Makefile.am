
helperdir = $(datadir)/PackageKit/helpers/smart

NULL =

dist_helper_DATA = 			\
	remove.py			\
	resolve.py			\
	smartBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
