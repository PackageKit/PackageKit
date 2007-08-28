
helperdir = $(datadir)/PackageKit/helpers

NULL =

if BACKEND_TYPE_YUM
dist_helper_DATA = 			\
	yum-search-name.py		\
	yum-search-details.py		\
	yum-search-group.py		\
	yum-search-file.py		\
	yum-get-deps.py			\
	yum-get-updates.py		\
	yum-get-description.py		\
	yum-install.py			\
	yum-remove.py			\
	yum-update.py			\
	yum-refresh-cache.py		\
	yum-update-system.py		\
	yumBackend.py			\
	packagekit.py			\
	$(NULL)
endif	

if BACKEND_TYPE_CONARY
dist_helper_DATA = 			\
	conary-search-name.py		\
	conary-search-details.py	\
	conary-get-deps.py		\
	conary-get-updates.py		\
	conary-install.py		\
	conary-refresh-cache.py		\
	conary-remove.py		\
	conary-update-system.py		\
	conary-get-description.py	\
	conaryBackend.py		\
	packagekit.py			\
	$(NULL)
endif	

if BACKEND_TYPE_YUM
install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/yum*.py
endif	

if BACKEND_TYPE_CONARY
install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/conary*.py
endif	

clean-local :
	rm -f *~

