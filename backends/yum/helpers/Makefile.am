
helperdir = $(datadir)/PackageKit/helpers/yum

NULL =

dist_helper_DATA = 			\
	search-name.py			\
	search-details.py		\
	search-group.py			\
	search-file.py			\
	get-depends.py			\
	get-requires.py			\
	get-files.py			\
	get-updates.py			\
	get-description.py		\
	get-repo-list.py		\
	repo-enable.py			\
	repo-set-data.py        \
	get-updates-detail.py   \
	install.py			\
	remove.py			\
	resolve.py			\
	update.py			\
	install-file.py     \
	refresh-cache.py		\
	update-system.py		\
	yumBackend.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

