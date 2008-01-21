## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	tests						\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = packagekit.pc

pmutilsdir = $(libdir)/pm-utils/sleep.d
pmutils_DATA = 95packagekit

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = org.freedesktop.PackageKit.conf org.freedesktop.PackageKit.yumBackend.conf

servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = org.freedesktop.PackageKit.service.in \
	org.freedesktop.PackageKit.yumBackend.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(sbindir)|" $< > $@

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
	org.freedesktop.PackageKit.conf.in		\
	org.freedesktop.PackageKit.yumBackend.conf.in	\
	packagekit.pc.in				\
	$(service_in_files)				\
	$(localcache_DATA)				\
	$(database_DATA)				\
	$(NULL)

clean-local:
	rm -f *~

DISTCLEANFILES =					\
	org.freedesktop.PackageKit.service		\
	org.freedesktop.PackageKit.yumBackend.service

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

