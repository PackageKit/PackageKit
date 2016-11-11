#ifndef __KATJA_PKGTOOLS_H
#define __KATJA_PKGTOOLS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define KATJA_TYPE_PKGTOOLS katja_pkgtools_get_type()
G_DECLARE_INTERFACE(KatjaPkgtools, katja_pkgtools, KATJA, PKGTOOLS, GObject)

struct _KatjaPkgtoolsInterface
{
	GTypeInterface parent_iface;
};

G_END_DECLS

#endif /* __KATJA_PKGTOOLS_H */
