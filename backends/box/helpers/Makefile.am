
helperdir = $(datadir)/PackageKit/helpers

NULL =

dist_helper_DATA = 			\
	refresh-cache.sh		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

