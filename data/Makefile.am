## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	tests						\
	$(NULL)

cronfiledir = ${SYSCONFDIR}/cron.daily
cronfile_SCRIPTS = packagekit-background.cron

crondatadir = ${SYSCONFDIR}/sysconfig
crondata_SCRIPTS = packagekit-background

pmutilsdir = $(libdir)/pm-utils/sleep.d
pmutils_DATA = 95packagekit

upgradescriptdir = $(datadir)/PackageKit
upgradescript_SCRIPTS = pk-upgrade-distro.sh

if BACKEND_TYPE_APT
aptconfdir = ${SYSCONFDIR}/apt/apt.conf.d
aptconf_DATA = 20packagekit
endif

@INTLTOOL_XML_RULE@
mimedir = $(datadir)/mime/packages
mime_in_files = 					\
	packagekit-catalog.xml.in			\
	packagekit-package-list.xml.in			\
	packagekit-servicepack.xml.in			\
	$(NULL)						
mime_DATA = $(mime_in_files:.xml.in=.xml)

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = 					\
	org.freedesktop.PackageKit.conf			\
	$(NULL)

servicemaindir       = $(DBUS_SERVICES_DIR)
servicemain_in_files = org.freedesktop.PackageKit.service.in
servicemain_DATA     = $(servicemain_in_files:.service.in=.service)
$(servicemain_DATA): $(servicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(sbindir)|" -e "s|\@PACKAGEKIT_USER\@|$(PACKAGEKIT_USER)|" $< > $@

databasedir = $(PK_DB_DIR)
database_DATA =						\
	transactions.db					\
	$(NULL)

EXTRA_DIST =						\
	95packagekit					\
	packagekit-background				\
	packagekit-background.cron			\
	org.freedesktop.PackageKit.conf.in		\
	20packagekit					\
	$(servicemain_in_files)				\
	$(servicetest_in_files)				\
	$(serviceapt_in_files)				\
	$(mime_in_files)				\
	$(localcache_DATA)				\
	$(database_DATA)				\
	$(upgradescript_SCRIPTS)			\
	$(NULL)

clean-local:
	rm -f *~

install-data-hook:
	chmod a+x $(DESTDIR)$(libdir)/pm-utils/sleep.d/95packagekit;

DISTCLEANFILES =					\
	$(mime_DATA)					\
	org.freedesktop.PackageKit.service		\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

