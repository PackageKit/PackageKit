plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_dnf.la
EXTRA_DIST =								\
	dnf-backend-vendor-fedora.c					\
	dnf-backend-vendor-mageia.c
libpk_backend_dnf_la_SOURCES =						\
	dnf-backend-vendor.h						\
	dnf-backend.c							\
	dnf-backend.h							\
	pk-backend-dnf.c
nodist_libpk_backend_dnf_la_SOURCES = dnf-backend-vendor-@DNF_VENDOR@.c
libpk_backend_dnf_la_LIBADD = $(PK_PLUGIN_LIBS) $(DNF_LIBS)
libpk_backend_dnf_la_CPPFLAGS =						\
	$(DNF_CFLAGS)							\
	-DG_LOG_DOMAIN=\"PackageKit-DNF\"
libpk_backend_dnf_la_LDFLAGS = -module -avoid-version
libpk_backend_dnf_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
