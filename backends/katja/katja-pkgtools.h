#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <string.h>
#include <curl/curl.h>
#include <sqlite3.h>
#include <glib/gstdio.h>
#include <pk-backend.h>
#include <pk-backend-job.h>

G_BEGIN_DECLS

#define KATJA_TYPE_PKGTOOLS (katja_pkgtools_get_type())
#define KATJA_PKGTOOLS(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_PKGTOOLS, KatjaPkgtools))
#define KATJA_PKGTOOLS_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_PKGTOOLS, KatjaPkgtoolsClass))
#define KATJA_IS_PKGTOOLS(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_PKGTOOLS))
#define KATJA_IS_PKGTOOLS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_PKGTOOLS))
#define KATJA_PKGTOOLS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_PKGTOOLS, KatjaPkgtoolsClass))

/*typedef struct {
	PkBackendJob *job;
	guint last_percentage;
	gdouble all;
	gdouble downloaded;
	gushort obj_counter;
} KatjaPkgtoolsJobProgress;*/

typedef struct {
	GObject parent;

	/* public */
	GString *name;
	GString *mirror;
	guint order;
	GRegex *blacklist;
} KatjaPkgtools;

typedef struct {
	GObjectClass parent_class;

	GSList *(*collect_cache_info) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	gboolean (*download) (KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name);
	void (*install) (KatjaPkgtools *pkgtools, gchar *pkg_name);
} KatjaPkgtoolsClass;

GType katja_pkgtools_get_type(void);

/* Public static members */
extern sqlite3 *katja_pkgtools_db;
/*extern KatjaPkgtoolsJobProgress katja_pkgtools_job_progress;*/

/* Virtual public methods */
GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl);
void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl);
gboolean katja_pkgtools_download(KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name);
void katja_pkgtools_install(KatjaPkgtools *pkgtools, gchar *pkg_name);

/* Public static methods */
CURLcode katja_pkgtools_get_file(CURL **curl, gchar *source_url, gchar *dest);
gchar **katja_pkgtools_cut_pkg(const gchar *pkg_filename);
gint katja_pkgtools_cmp_repo(gconstpointer a, gconstpointer b);
PkInfoEnum katja_pkgtools_is_installed(gchar *pkg_full_name);

G_END_DECLS

#endif /* __KATJA_PKGTOOLS_H */
