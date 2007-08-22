
helperdir = $(datadir)/PackageKit/helpers

NULL =

if BACKEND_TYPE_YUM
dist_helper_DATA = 			\
	yum-search-name.py		\
	yum-search-details.py		\
	$(NULL)
endif	

if BACKEND_TYPE_YUM
install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py
endif	

clean-local :
	rm -f *~

