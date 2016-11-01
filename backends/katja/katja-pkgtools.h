#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <errno.h>
#include <sqlite3.h>
#include "katja-utils.h"

G_BEGIN_DECLS

#define KATJA_TYPE_PKGTOOLS (katja_pkgtools_get_type())
#define KATJA_PKGTOOLS(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_PKGTOOLS, KatjaPkgtools))
#define KATJA_PKGTOOLS_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_PKGTOOLS, KatjaPkgtoolsClass))
#define KATJA_IS_PKGTOOLS(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_PKGTOOLS))
#define KATJA_IS_PKGTOOLS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_PKGTOOLS))
#define KATJA_PKGTOOLS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_PKGTOOLS, KatjaPkgtoolsClass))

#define KATJA_PKGTOOLS_MAX_BUF_SIZE 8192

typedef struct
{
	GObject parent;

	/* protected */
	gchar *name;
	gchar *mirror;
	gushort order;
	GRegex *blacklist;
} KatjaPkgtools;

typedef struct {
	GObjectClass parent_class;

	sqlite3 *db;
	CURL *curl;
	GSList *(*collect_cache_info) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);
} PkBackendKatjaJobData;

typedef struct {
	GObjectClass parent_class;

	gchar *(*get_name) (KatjaPkgtools *pkgtools);
	gchar *(*get_mirror) (KatjaPkgtools *pkgtools);
	gushort (*get_order) (KatjaPkgtools *pkgtools);
	GRegex *(*get_blacklist) (KatjaPkgtools *pkgtools);
	GSList *(*collect_cache_info) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);
	gboolean (*download) (KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
	void (*install) (KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name);
} KatjaPkgtoolsClass;

GType katja_pkgtools_get_type(void);

/* Virtual public methods */
gchar *katja_pkgtools_get_name(KatjaPkgtools *pkgtools);
gchar *katja_pkgtools_get_mirror(KatjaPkgtools *pkgtools);
gushort katja_pkgtools_get_order(KatjaPkgtools *pkgtools);
GRegex *katja_pkgtools_get_blacklist(KatjaPkgtools *pkgtools);
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl);
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
void katja_pkgtools_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name);

/* Implementations */
gchar *katja_pkgtools_real_get_name(KatjaPkgtools *pkgtools);
gchar *katja_pkgtools_real_get_mirror(KatjaPkgtools *pkgtools);
gushort katja_pkgtools_real_get_order(KatjaPkgtools *pkgtools);
GRegex *katja_pkgtools_real_get_blacklist(KatjaPkgtools *pkgtools);
gboolean katja_pkgtools_real_download(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
void katja_pkgtools_real_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name);

/* Public methods */
void katja_pkgtools_manifest(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl, gchar *filename);

G_END_DECLS

#endif /* __KATJA_PKGTOOLS_H */
