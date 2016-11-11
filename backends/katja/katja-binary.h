#ifndef __KATJA_BINARY_H
#define __KATJA_BINARY_H

#include <errno.h>
#include <sqlite3.h>
#include "katja-utils.h"

G_BEGIN_DECLS

#define KATJA_TYPE_BINARY katja_binary_get_type()
G_DECLARE_DERIVABLE_TYPE(KatjaBinary, katja_binary, KATJA, BINARY, GObject)

#define KATJA_BINARY_MAX_BUF_SIZE 8192

typedef struct
{
	GObjectClass parent_class;

	sqlite3 *db;
	CURL *curl;
	GSList *(*collect_cache_info) (KatjaBinary *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaBinary *pkgtools, PkBackendJob *job, const gchar *tmpl);
} PkBackendKatjaJobData;

struct _KatjaBinaryClass
{
	GObjectClass parent_class;

	gchar *(*get_name) (KatjaBinary *pkgtools);
	gchar *(*get_mirror) (KatjaBinary *pkgtools);
	gushort (*get_order) (KatjaBinary *pkgtools);
	GRegex *(*get_blacklist) (KatjaBinary *pkgtools);
	GSList *(*collect_cache_info) (KatjaBinary *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaBinary *pkgtools, PkBackendJob *job, const gchar *tmpl);
	gboolean (*download) (KatjaBinary *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
	void (*install) (KatjaBinary *pkgtools, PkBackendJob *job, gchar *pkg_name);
};

/* Virtual public methods */
const gchar *katja_binary_get_name(KatjaBinary *binary);
const gchar *katja_binary_get_mirror(KatjaBinary *binary);
gushort katja_binary_get_order(KatjaBinary *binary);
const GRegex *katja_binary_get_blacklist(KatjaBinary *binary);
GSList *katja_binary_collect_cache_info(KatjaBinary *pkgtools, const gchar *tmpl);
void katja_binary_generate_cache(KatjaBinary *pkgtools, PkBackendJob *job, const gchar *tmpl);
gboolean katja_binary_download(KatjaBinary *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
void katja_binary_install(KatjaBinary *pkgtools, PkBackendJob *job, gchar *pkg_name);

/* Implementations */
gboolean katja_binary_real_download(KatjaBinary *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
void katja_binary_real_install(KatjaBinary *pkgtools, PkBackendJob *job, gchar *pkg_name);

/* Public methods */
void katja_binary_manifest(KatjaBinary *pkgtools, PkBackendJob *job, const gchar *tmpl, gchar *filename);

G_END_DECLS

#endif /* __KATJA_BINARY_H */
