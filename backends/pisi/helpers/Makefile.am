
helperdir = $(datadir)/PackageKit/helpers/pisi

NULL =

dist_helper_DATA = 			\
	resolve.py			\
	remove.py			\
	refresh-cache.py 		\
	get-updates.py			\
	get-repo-list.py 		\
	install.py			\
	update.py			\
	pisiBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
