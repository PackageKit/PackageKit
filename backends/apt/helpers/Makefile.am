
helperdir = $(datadir)/PackageKit/helpers/apt

NULL =

dist_helper_DATA = 			\
	install-file.py			\
	search-name.py			\
	search-details.py		\
	search-group.py			\
	search-file.py			\
	get-depends.py			\
	get-description.py		\
	get-repo-list.py		\
	get-requires.py			\
	get-update-detail.py		\
	get-updates.py			\
	refresh-cache.py		\
	repo-enable.py			\
	resolve.py			\
	aptBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

