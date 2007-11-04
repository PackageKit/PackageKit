
helperdir = $(datadir)/PackageKit/helpers/box

NULL =

dist_helper_DATA = 			\
	install-file.sh			\
	install-package.sh		\
	update-package.sh		\
	update-system.sh		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

