
helperdir = $(datadir)/PackageKit/helpers/pisi

NULL =

dist_helper_DATA = 			\
	get-depends.py			\
	get-description.py		\
	get-repo-list.py 		\
	get-requires.py			\
	get-updates.py			\
	install-file.py			\
	install.py			\
	refresh-cache.py 		\
	remove.py			\
	resolve.py			\
	search-name.py			\
	update.py			\
	update-system.py		\
	pisiBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
