
helperdir = $(datadir)/PackageKit/helpers

NULL =

dist_helper_DATA = 			\
	install-package.sh		\
	refresh-cache.sh		\
	remove-package.sh		\
	update-package.sh		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

