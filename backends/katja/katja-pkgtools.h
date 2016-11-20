#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <glib-object.h>
#include <pk-backend.h>

G_BEGIN_DECLS

#define KATJA_TYPE_PKGTOOLS katja_pkgtools_get_type()
G_DECLARE_INTERFACE(KatjaPkgtools, katja_pkgtools, KATJA, PKGTOOLS, GObject)

struct _KatjaPkgtoolsInterface
{
	GTypeInterface parent_iface;

	GSList *(*collect_cache_info) (KatjaPkgtools *pkgtools, const gchar *tmpl);
	void (*generate_cache) (KatjaPkgtools *pkgtools, PkBackendJob *job, const gchar *tmpl);
};

GSList *katja_pkgtools_collect_cache_info(KatjaPkgtools *pkgtools,
                                          const gchar   *tmpl);

void katja_pkgtools_generate_cache(KatjaPkgtools *pkgtools,
                                   PkBackendJob  *job,
                                   const gchar   *tmpl);

G_END_DECLS

#endif /* __KATJA_PKGTOOLS_H */
