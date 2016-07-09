#include "katja-pkgtools.h"

G_DEFINE_INTERFACE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT);

/**
 * katja_pkgtools_get_name:
 **/
gchar *katja_pkgtools_get_name(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->get_name(pkgtools);
}

/**
 * katja_pkgtools_get_mirror:
 **/
gchar *katja_pkgtools_get_mirror(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->get_mirror(pkgtools);
}

/**
 * katja_pkgtools_get_order:
 **/
gushort katja_pkgtools_get_order(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), 0);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->get_order(pkgtools);
}

/**
 * katja_pkgtools_get_blacklist:
 **/
GRegex *katja_pkgtools_get_blacklist(KatjaPkgtools *pkgtools) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->get_blacklist(pkgtools);
}

/**
 * katja_pkgtools_collect_cache_info:
 **/
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_IFACE(pkgtools)->collect_cache_info != NULL, NULL);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->collect_cache_info(pkgtools, tmpl);
}

/**
 * katja_pkgtools_generate_cache:
 **/
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_IFACE(pkgtools)->generate_cache != NULL);

	KATJA_PKGTOOLS_GET_IFACE(pkgtools)->generate_cache(pkgtools, job, tmpl);
}

/**
 * katja_pkgtools_download:
 **/
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), FALSE);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_IFACE(pkgtools)->download != NULL, FALSE);

	return KATJA_PKGTOOLS_GET_IFACE(pkgtools)->download(pkgtools, job, dest_dir_name, pkg_name);
}

/**
 * katja_pkgtools_install:
 **/
void katja_pkgtools_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_IFACE(pkgtools)->install != NULL);

	KATJA_PKGTOOLS_GET_IFACE(pkgtools)->install(pkgtools, job, pkg_name);
}

/**
 * katja_pkgtools_default_init:
 **/
static void katja_pkgtools_default_init(KatjaPkgtoolsInterface *iface) {
	iface->get_name = NULL;
	iface->get_mirror = NULL;
	iface->get_order = NULL;
	iface->get_blacklist = NULL;
	iface->collect_cache_info = NULL;
	iface->generate_cache = NULL;
	iface->download = NULL;
	iface->install = NULL;
}
