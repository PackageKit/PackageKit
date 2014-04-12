#include "katja-pkgtools.h"

G_DEFINE_TYPE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT);

/* Static public members */
sqlite3 *katja_pkgtools_db = NULL;


/**
 * katja_pkgtools_collect_cache_info:
 **/
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);
	g_return_val_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info != NULL, NULL);

	return KATJA_PKGTOOLS_GET_CLASS(pkgtools)->collect_cache_info(pkgtools, tmpl);
}

/**
 * katja_pkgtools_generate_cache:
 **/
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->generate_cache(pkgtools, tmpl);
}

/**
 * katja_pkgtools_download:
 **/
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->download(pkgtools, dest_dir_name, pkg_name);
}

/**
 * katja_pkgtools_install:
 **/
void katja_pkgtools_install(KatjaPkgtools *pkgtools, gchar *pkg_name) {
	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));
	g_return_if_fail(KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install != NULL);

	KATJA_PKGTOOLS_GET_CLASS(pkgtools)->install(pkgtools, pkg_name);
}

/**
 * katja_pkgtools_finalize:
 **/
static void katja_pkgtools_finalize(GObject *object) {
	KatjaPkgtools *pkgtools;

	g_return_if_fail(KATJA_IS_PKGTOOLS(object));

	pkgtools = KATJA_PKGTOOLS(object);
	if (pkgtools->name)
		g_string_free(pkgtools->name, TRUE);
	if (pkgtools->mirror)
		g_string_free(pkgtools->mirror, TRUE);
	if (pkgtools->blacklist)
		g_object_unref(pkgtools->blacklist);

	G_OBJECT_CLASS(katja_pkgtools_parent_class)->finalize(object);
}

/**
 * katja_pkgtools_class_init:
 **/
static void katja_pkgtools_class_init(KatjaPkgtoolsClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = katja_pkgtools_finalize;

	klass->collect_cache_info = NULL;
	klass->generate_cache = NULL;
	klass->download = NULL;
	klass->install = NULL;
}

/**
 * katja_pkgtools_init:
 **/
static void katja_pkgtools_init(KatjaPkgtools *pkgtools) {
	pkgtools->name = NULL;
	pkgtools->mirror = NULL;
	pkgtools->blacklist = NULL;
	pkgtools->order = 0;
}
