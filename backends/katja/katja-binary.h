#ifndef __KATJA_BINARY_H
#define __KATJA_BINARY_H

#include <stdlib.h>
#include <bzlib.h>
#include <katja-pkgtools.h>

G_BEGIN_DECLS

#define KATJA_TYPE_BINARY (katja_binary_get_type())
#define KATJA_BINARY(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_BINARY, KatjaBinary))
#define KATJA_BINARY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_BINARY, KatjaBinaryClass))
#define KATJA_IS_BINARY(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_BINARY))
#define KATJA_IS_BINARY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_BINARY))
#define KATJA_BINARY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_BINARY, KatjaBinaryClass))

typedef struct {
	GObject parent;

	/* protected */
	gchar *name;
	gchar *mirror;
	gushort order;
	GRegex *blacklist;
} KatjaBinary;

typedef struct {
	GObjectClass parent_class;

	GSList *(*collect_cache_info) (KatjaBinary *binary, const gchar *tmpl);
	void (*generate_cache) (KatjaBinary *binary, PkBackendJob *job, const gchar *tmpl);
} KatjaBinaryClass;

GType katja_binary_get_type(void);

G_END_DECLS

/* Virtual public methods */
GSList *katja_binary_collect_cache_info(KatjaBinary *binary, const gchar *tmpl);
void katja_binary_generate_cache(KatjaBinary *binary, PkBackendJob *job, const gchar *tmpl);

/* Public methods */
void katja_binary_manifest(KatjaBinary *binary, PkBackendJob *job, const gchar *tmpl, gchar *filename);

/* Implementations */
gchar *katja_binary_real_get_name(KatjaPkgtools *pkgtools);
gchar *katja_binary_real_get_mirror(KatjaPkgtools *pkgtools);
gushort katja_binary_real_get_order(KatjaPkgtools *pkgtools);
GRegex *katja_binary_real_get_blacklist(KatjaPkgtools *pkgtools);
gboolean katja_binary_real_download(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *dest_dir_name, gchar *pkg_name);
void katja_binary_real_install(KatjaPkgtools *pkgtools, PkBackendJob *job, gchar *pkg_name);

#endif /* __KATJA_BINARY_H */
