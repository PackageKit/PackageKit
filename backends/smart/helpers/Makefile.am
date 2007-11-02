
helperdir = $(datadir)/PackageKit/helpers/smart

NULL =

dist_helper_DATA = 			\
	get-depends.py			\
	get-description.py		\
	get-files.py			\
	get-repo-list.py		\
	get-updates.py			\
	install-file.py			\
	install.py			\
	refresh-cache.py		\
	remove.py			\
	repo-enable.py			\
	resolve.py			\
	search-name.py			\
	smartBackend.py			\
	update.py			\
	update-system.py		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
