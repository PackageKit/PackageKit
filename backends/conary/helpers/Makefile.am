
helperdir = $(datadir)/PackageKit/helpers/conary

NULL =

dist_helper_DATA = 			\
	search-name.py			\
	search-details.py		\
	get-depends.py			\
	get-requires.py			\
	get-updates.py			\
	get-update-detail.py		\
	install.py			\
	refresh-cache.py		\
	remove.py			\
	resolve.py			\
	update-system.py		\
	get-description.py		\
	get-files.py			\
	conaryBackend.py		\
	conaryCallback.py		\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

