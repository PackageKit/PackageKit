## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	tests						\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = packagekit.pc

cronfiledir = ${SYSCONFDIR}/cron.daily
cronfile_SCRIPTS = packagekit-background.cron

crondatadir = ${SYSCONFDIR}/sysconfig
crondata_SCRIPTS = packagekit-background

pmutilsdir = $(libdir)/pm-utils/sleep.d
pmutils_DATA = 95packagekit

if BACKEND_TYPE_APT
aptconfdir = ${SYSCONFDIR}/apt.conf.d
aptconf_DATA = 20packagekit
endif

@INTLTOOL_XML_RULE@
mimedir = $(datadir)/mime/packages
mime_in_files = 					\
	packagekit-catalog.xml.in			\
	packagekit-pack.xml.in				\
	$(NULL)						
mime_DATA = $(mime_in_files:.xml.in=.xml)

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = 					\
	org.freedesktop.PackageKit.conf			\
	org.freedesktop.PackageKitTestBackend.conf	\
	org.freedesktop.PackageKitAptBackend.conf	\
	$(NULL)

servicemaindir       = $(DBUS_SERVICES_DIR)
servicemain_in_files = org.freedesktop.PackageKit.service.in
servicemain_DATA     = $(servicemain_in_files:.service.in=.service)
$(servicemain_DATA): $(servicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(sbindir)|" -e "s|\@PACKAGEKIT_USER\@|$(PACKAGEKIT_USER)|" $< > $@

servicetestdir       = $(DBUS_SERVICES_DIR)
servicetest_in_files = org.freedesktop.PackageKitTestBackend.service.in
servicetest_DATA     = $(servicetest_in_files:.service.in=.service)
$(servicetest_DATA): $(servicetest_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" -e "s|\@PK_BACKEND_USER\@|$(PK_BACKEND_USER)|" $< > $@

serviceaptdir       = $(DBUS_SERVICES_DIR)
serviceapt_in_files = org.freedesktop.PackageKitAptBackend.service.in
serviceapt_DATA     = $(serviceapt_in_files:.service.in=.service)
$(serviceapt_DATA): $(serviceapt_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" -e "s|\@PK_BACKEND_USER\@|$(PK_BACKEND_USER)|" $< > $@

localcachedir = $(localstatedir)/run/PackageKit
localcache_DATA =					\
	job_count.dat					\
	$(NULL)

databasedir = $(PK_DB_DIR)
database_DATA =						\
	transactions.db					\
	$(NULL)

EXTRA_DIST =						\
	95packagekit					\
	packagekit-background				\
	packagekit-background.cron			\
	org.freedesktop.PackageKit.conf.in		\
	org.freedesktop.PackageKitTestBackend.conf.in	\
	org.freedesktop.PackageKitAptBackend.conf.in	\
	packagekit.pc.in				\
	20packagekit					\
	$(servicemain_in_files)				\
	$(servicetest_in_files)				\
	$(serviceapt_in_files)				\
	$(mime_in_files)				\
	$(localcache_DATA)				\
	$(database_DATA)				\
	$(NULL)

clean-local:
	rm -f *~

install-data-hook:
	chmod a+x $(DESTDIR)$(libdir)/pm-utils/sleep.d/95packagekit;

DISTCLEANFILES =					\
	$(mime_DATA)					\
	org.freedesktop.PackageKit.service		\
	org.freedesktop.PackageKitTestBackend.service	\
	org.freedesktop.PackageKitAptBackend.service	\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

