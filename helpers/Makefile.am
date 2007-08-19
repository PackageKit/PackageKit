
helperdir = $(datadir)/PackageKit/helpers

NULL =

dist_helper_DATA = 			\
	yum-search-name.py		\
	yum-search-details.py		\
	$(NULL)

install-data-hook:
	chmod a+rx $(helperdir)/*.py

clean-local :
	rm -f *~

