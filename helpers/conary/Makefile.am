
helperdir = $(datadir)/PackageKit/helpers

NULL =

dist_helper_DATA = 			\
	search-name.py			\
	search-details.py		\
	get-depends.py			\
	get-updates.py			\
	install.py			\
	refresh-cache.py		\
	remove.py			\
	update-system.py		\
	get-description.py		\
	conaryBackend.py		\
	packagekit.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

