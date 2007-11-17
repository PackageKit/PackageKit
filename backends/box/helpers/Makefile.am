
helperdir = $(datadir)/PackageKit/helpers/box

NULL =

dist_helper_DATA = 			\
	update-system.sh		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

