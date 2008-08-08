
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
	get-details.py			\
	get-repo-list.py		\
	get-packages.py			\
	repo-enable.py			\
	repo-set-data.py		\
	get-update-detail.py		\
	install-packages.py		\
	remove-packages.py		\
	resolve.py			\
	update-packages.py		\
	install-files.py		\
	install-signature.py		\
	refresh-cache.py		\
	what-provides.py		\
	update-system.py		\
	download-packages.py		\
	yumBackend.py			\
	yumDirect.py			\
	yumComps.py			\
	yumFilter.py			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *.pyc
	rm -f *~

