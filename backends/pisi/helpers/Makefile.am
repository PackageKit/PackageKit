
helperdir = $(datadir)/PackageKit/helpers/pisi

NULL =

dist_helper_DATA = 			\
	get-depends.py			\
	get-repo-list.py 		\
	get-requires.py			\
	get-updates.py			\
	install.py			\
	refresh-cache.py 		\
	remove.py			\
	resolve.py			\
	update.py			\
	pisiBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
