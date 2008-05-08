
helperdir = $(datadir)/PackageKit/helpers/pisi

NULL =

dist_helper_DATA = 			\
	get-depends.py			\
	get-files.py			\
	get-details.py			\
	get-repo-list.py 		\
	get-requires.py			\
	get-updates.py			\
	install-files.py		\
	install-packages.py		\
	refresh-cache.py 		\
	remove-packages.py		\
	resolve.py			\
	repo-set-data.py		\
	search-details.py		\
	search-file.py			\
	search-group.py			\
	search-name.py			\
	update-packages.py		\
	update-system.py		\
	pisiBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
