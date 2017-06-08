extern "C"
{

#include "katja-pkgtools.h"

}

G_DEFINE_INTERFACE(KatjaPkgtools, katja_pkgtools, G_TYPE_OBJECT)

static void
katja_pkgtools_default_init(KatjaPkgtoolsInterface *iface)
{
	GParamSpec *spec;

	spec = g_param_spec_string("name",
	                           "Name",
	                           "Repository name",
	                           NULL,
	                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_string("mirror",
	                           "Mirror",
	                           "Repository mirror",
	                           NULL,
	                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_uint("order",
	                         "Order",
	                         "Repository order",
	                         0,
	                         G_MAXUSHORT,
	                         0,
	                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_interface_install_property(iface, spec);

	spec = g_param_spec_boxed("blacklist",
	                          "Blacklist",
	                          "Repository blacklist",
	                          G_TYPE_REGEX,
	                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_interface_install_property(iface, spec);
}

/**
 * katja_pkgtools_collect_cache_info:
 * @pkgtools: a #KatjaPkgtools.
 * @tmpl:     temporary directory for downloading the files.
 *
 * Download files needed to get the information like the list of packages
 * in available repositories, updates, package descriptions and so on.
 *
 * Returns: list of files needed for building the cache.
 **/
GSList *
katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl)
{
	KatjaPkgtoolsInterface *iface;

	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), NULL);

	iface = KATJA_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_val_if_fail(iface->collect_cache_info != NULL, NULL);

	return iface->collect_cache_info(pkgtools, tmpl);
}

/**
 * katja_pkgtools_generate_cache:
 * @pkgtools: a #KatjaPkgtools.
 * @job:      a #PkBackendJob.
 * @tmpl:     temporary directory with the downloaded files.
 *
 * Generate package cache information and store it in the database.
 **/
void
katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools,
                              PkBackendJob *job,
                              const gchar *tmpl)
{
	KatjaPkgtoolsInterface *iface;

	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));

	iface = KATJA_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_if_fail(iface->generate_cache != NULL);

	iface->generate_cache(pkgtools, job, tmpl);
}

/**
 * katja_pkgools_download:
 * @pkgtools:      a #KatjaPkgtools.
 * @job:           a #PkBackendJob.
 * @dest_dir_name: destination directory.
 * @pkg_name:      package name.
 *
 * Download a package.
 *
 * Returns: TRUE on success, FALSE otherwise.
 **/
gboolean
katja_pkgtools_download(KatjaPkgtools *pkgtools,
                        PkBackendJob *job,
                        gchar *dest_dir_name,
                        gchar *pkg_name)
{
	KatjaPkgtoolsInterface *iface;

	g_return_val_if_fail(KATJA_IS_PKGTOOLS(pkgtools), FALSE);

	iface = KATJA_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_val_if_fail(iface->download != NULL, FALSE);

	return iface->download(pkgtools, job, dest_dir_name, pkg_name);
}

/**
 * katja_pkgtools_install:
 * @pkgtools: a #KatjaPkgtools.
 * @job:      a #PkBackendJob.
 * @pkg_name: package name.
 *
 * Install a package.
 **/
void
katja_pkgtools_install(KatjaPkgtools *pkgtools,
                       PkBackendJob *job,
                       gchar *pkg_name)
{
	KatjaPkgtoolsInterface *iface;

	g_return_if_fail(KATJA_IS_PKGTOOLS(pkgtools));

	iface = KATJA_PKGTOOLS_GET_IFACE(pkgtools);
	g_return_if_fail(iface->install != NULL);

	iface->install(pkgtools, job, pkg_name);
}
