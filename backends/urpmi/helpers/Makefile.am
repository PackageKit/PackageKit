SUBDIRS = perl_packagekit urpmi_backend

helperdir = $(datadir)/PackageKit/helpers/urpmi

NULL =

dist_helper_DATA = 						\
	urpmi-dispatched-backend.pl					\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.pl

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
