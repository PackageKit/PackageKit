
helperdir = $(datadir)/PackageKit/helpers

NULL =

if BACKEND_TYPE_YUM
dist_helper_DATA = 			\
	yum-search-name.py		\
	yum-search-details.py		\
	$(NULL)
endif	

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

