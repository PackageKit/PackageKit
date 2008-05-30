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
	search-group.pl							\
	get-packages.pl							\
	get-requires.pl							\
	search-details.pl						\
	search-file.pl							\
	resolve.pl									\
	update-packages.pl					\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.pl

clean-local :
	rm -f *~

