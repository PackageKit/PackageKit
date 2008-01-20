
helperdir = $(datadir)/PackageKit/helpers/test_spawn

NULL =

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =					\
	PackageKitDbusTest.py				\
	$(NULL)

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = org.freedesktop.PackageKitDbus.conf

servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = org.freedesktop.PackageKitDbus.service.in
service_DATA     = $(service_in_files:.service.in=.service)

dist_helper_DATA = 			\
	search-name.sh			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.sh

clean-local :
	rm -f *~

