
helperdir = $(datadir)/PackageKit/helpers/ports

NULL =

dist_helper_DATA = 					\
	default-dialog					\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/default-dialog

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
