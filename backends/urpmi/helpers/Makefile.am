SUBDIRS = perl_packagekit urpmi_backend

helperdir = $(datadir)/PackageKit/helpers/urpmi

NULL =

dist_helper_DATA = 						\
	search-name.pl							\
	get-details.pl							\
	get-depends.pl							\
	get-files.pl								\
	get-updates.pl							\
	get-update-detail.pl				\
	refresh-cache.pl						\
	install-packages.pl					\
	remove-packages.pl					\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.pl

clean-local :
	rm -f *~

