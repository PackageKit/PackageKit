
helperdir = $(datadir)/PackageKit/helpers/smart

NULL =

dist_helper_DATA = 			\
	get-depends.py			\
	get-details.py			\
	get-files.py			\
	get-repo-list.py		\
	get-updates.py			\
	install-files.py		\
	install-packages.py		\
	refresh-cache.py		\
	remove-packages.py		\
	repo-enable.py			\
	resolve.py			\
	search-name.py			\
	search-details.py		\
	smartBackend.py			\
	update-packages.py		\
	update-system.py		\
	download-packages.py		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
