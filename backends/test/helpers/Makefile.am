
helperdir = $(datadir)/PackageKit/helpers/test_spawn

NULL =

dist_helper_DATA = 					\
	search-name.sh					\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
