## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	tests

upgradescriptdir = $(datadir)/PackageKit
upgradescript_SCRIPTS = pk-upgrade-distro.sh

dbusdir = ${DBUS_SYS_DIR}
dist_dbus_DATA = 					\
	org.freedesktop.PackageKit.conf

%.service: %.service.in Makefile
	$(AM_V_GEN)sed -e 's|\@libexecdir\@|$(libexecdir)|' \
	               -e 's|\@PACKAGEKIT_USER\@|$(PACKAGEKIT_USER)|' $< > $@.tmp && mv $@.tmp $@

servicemaindir       = $(DBUS_SERVICES_DIR)
servicemain_in_files = org.freedesktop.PackageKit.service.in
servicemain_DATA     = $(servicemain_in_files:.service.in=.service)

systemdservice_in_files =				\
	packagekit.service.in				\
	packagekit-offline-update.service.in

if HAVE_SYSTEMD
systemdservicedir       = $(systemdsystemunitdir)
systemdservice_DATA     = $(systemdservice_in_files:.service.in=.service)
endif


databasedir = $(localstatedir)/lib/PackageKit
database_DATA =						\
	transactions.db

install-data-hook:
	@for file in $(database_DATA); do \
		echo "Remove global read access for: $(DESTDIR)$(localstatedir)/lib/PackageKit/$$file"; \
		chmod o-r $(DESTDIR)$(localstatedir)/lib/PackageKit/$$file; \
	done
if ENABLE_OFFLINE_UPDATE
	mkdir -p $(DESTDIR)$(systemdservicedir)/system-update.target.wants
	ln -sf ../packagekit-offline-update.service $(DESTDIR)$(systemdservicedir)/system-update.target.wants/packagekit-offline-update.service
endif

EXTRA_DIST =						\
	org.freedesktop.PackageKit.conf.in		\
	$(servicemain_in_files)				\
	$(servicetest_in_files)				\
	$(serviceapt_in_files)				\
	$(systemdservice_in_files)			\
	$(localcache_DATA)				\
	$(database_DATA)				\
	$(upgradescript_SCRIPTS)

clean-local:
	rm -f *~

DISTCLEANFILES =					\
	$(systemdservice_DATA)				\
	org.freedesktop.PackageKit.service

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
