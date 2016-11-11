#ifndef __KATJA_SLACKPKG_H
#define __KATJA_SLACKPKG_H

#include "katja-binary.h"

G_BEGIN_DECLS

#define KATJA_TYPE_SLACKPKG katja_slackpkg_get_type()
G_DECLARE_FINAL_TYPE(KatjaSlackpkg, katja_slackpkg, KATJA, SLACKPKG, KatjaBinary)

/* Public static members */
extern GHashTable *katja_slackpkg_cat_map;

/* Constructors */
KatjaSlackpkg *katja_slackpkg_new(gchar *name,
                                  gchar *mirror,
                                  gushort order,
                                  gchar *blacklist,
                                  gchar **priority);

/* Implementations */
GSList *katja_slackpkg_real_collect_cache_info(KatjaBinary *pkgtools, const gchar *tmpl);
void katja_slackpkg_real_generate_cache(KatjaBinary *pkgtools, PkBackendJob *job, const gchar *tmpl);

G_END_DECLS

#endif /* __KATJA_SLACKPKG_H */
