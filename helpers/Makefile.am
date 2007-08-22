
helperdir = $(datadir)/PackageKit/helpers

NULL =

if BACKEND_TYPE_YUM
dist_helper_DATA = 			\
	yum-search-name.py		\
	yum-search-details.py		\
	yum-description.py		\
	yum-get-deps.py			\
	yum-get-updates.py		\
	yum-install.py			\
	yum-refresh-cache.py		\
	yum-remove.py			\
	yum-update-system.py		\
	yum-get-description.py		\
	$(NULL)
endif	

if BACKEND_TYPE_YUM
install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py
endif	

clean-local :
	rm -f *~

