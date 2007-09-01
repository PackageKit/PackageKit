
helperdir = $(datadir)/PackageKit/helpers

NULL =

dist_helper_DATA = 			\
	search-name.py			\
	search-details.py		\
	search-group.py			\
	search-file.py			\
	get-depends.py			\
	get-updates.py			\
	get-description.py		\
	install.py			\
	remove.py			\
	update.py			\
	refresh-cache.py		\
	update-system.py		\
	yumBackend.py			\
	packagekit.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

