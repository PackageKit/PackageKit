
helperdir = $(datadir)/PackageKit/helpers/yum

NULL =

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =					\
	yumDBUSBackend.py				\
	$(NULL)

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
	repo-set-data.py		\
	get-update-detail.py		\
	install.py			\
	remove.py			\
	resolve.py			\
	update.py			\
	install-file.py			\
	refresh-cache.py		\
	update-system.py		\
	yumBackend.py			\
	$(NULL)

EXTRA_DIST =						\
	$(dbusinstance_DATA)				\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *.pyc
	rm -f *~

