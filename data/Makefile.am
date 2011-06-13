## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	tests						\
	$(NULL)

upgradescriptdir = $(datadir)/PackageKit
upgradescript_SCRIPTS = pk-upgrade-distro.sh

@INTLTOOL_XML_RULE@
mimedir = $(datadir)/mime/packages
mime_in_files = 					\
	packagekit-catalog.xml.in			\
	packagekit-package-list.xml.in			\
	packagekit-servicepack.xml.in			\
	$(NULL)						
mime_DATA = $(mime_in_files:.xml.in=.xml)

dbusdir = ${DBUS_SYS_DIR}
dist_dbus_DATA = 					\
	org.freedesktop.PackageKit.conf			\
	$(NULL)

servicemaindir       = $(DBUS_SERVICES_DIR)
servicemain_in_files = org.freedesktop.PackageKit.service.in
servicemain_DATA     = $(servicemain_in_files:.service.in=.service)
$(servicemain_DATA): $(servicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" -e "s|\@PACKAGEKIT_USER\@|$(PACKAGEKIT_USER)|" $< > $@

databasedir = $(PK_DB_DIR)
database_DATA =						\
	transactions.db					\
	$(NULL)

install-data-hook:
	@for file in $(database_DATA); do \
		echo "Remove global read access for: $(DESTDIR)$(PK_DB_DIR)/$$file"; \
		chmod o-r $(DESTDIR)$(PK_DB_DIR)/$$file; \
	done

EXTRA_DIST =						\
	org.freedesktop.PackageKit.conf.in		\
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

DISTCLEANFILES =					\
	$(mime_DATA)					\
	org.freedesktop.PackageKit.service		\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)


-include $(top_srcdir)/git.mk
