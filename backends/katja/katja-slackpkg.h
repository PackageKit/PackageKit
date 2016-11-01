#ifndef __KATJA_SLACKPKG_H
#define __KATJA_SLACKPKG_H

#include <katja-pkgtools.h>

G_BEGIN_DECLS

#define KATJA_TYPE_SLACKPKG (katja_slackpkg_get_type())
#define KATJA_SLACKPKG(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_SLACKPKG, KatjaSlackpkg))
#define KATJA_SLACKPKG_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_SLACKPKG, KatjaSlackpkgClass))
#define KATJA_IS_SLACKPKG(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_SLACKPKG))
#define KATJA_IS_SLACKPKG_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_SLACKPKG))
#define KATJA_SLACKPKG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_SLACKPKG, KatjaSlackpkgClass))

typedef struct {
	KatjaPkgtools parent;

	/* public */
	gchar **priority;
} KatjaSlackpkg;

typedef struct {
	KatjaPkgtoolsClass parent_class;
} KatjaSlackpkgClass;

GType katja_slackpkg_get_type(void);

/* Public static members */
extern GHashTable *katja_slackpkg_cat_map;

/* Constructors */
KatjaSlackpkg *katja_slackpkg_new(gchar *name, gchar *mirror, gushort order, gchar *blacklist, gchar **priority);

/* Implementations */
GSList *katja_slackpkg_real_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl);
void katja_slackpkg_real_generate_cache(KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);

G_END_DECLS

#endif /* __KATJA_SLACKPKG_H */
