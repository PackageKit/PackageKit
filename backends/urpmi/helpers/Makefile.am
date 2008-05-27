
helperdir = $(datadir)/PackageKit/helpers/urpmi

NULL =

dist_helper_DATA = 			\
	search-name.pl			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.pl

clean-local :
	rm -f *~

