#ifndef __KATJA_BINARY_H
#define __KATJA_BINARY_H

#include <stdlib.h>
#include <katja-pkgtools.h>

G_BEGIN_DECLS

#define KATJA_TYPE_BINARY (katja_binary_get_type())
#define KATJA_BINARY(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_BINARY, KatjaBinary))
#define KATJA_BINARY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_BINARY, KatjaBinaryClass))
#define KATJA_IS_BINARY(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_BINARY))
#define KATJA_IS_BINARY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_BINARY))
#define KATJA_BINARY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_BINARY, KatjaBinaryClass))

typedef struct {
	KatjaPkgtools parent;
} KatjaBinary;

typedef struct {
	KatjaPkgtoolsClass parent_class;
} KatjaBinaryClass;

GType katja_binary_get_type(void);

G_END_DECLS

/* Implementations */
gboolean katja_binary_real_download(KatjaPkgtools *pkgtools, gchar *dest_dir_name, gchar *pkg_name);
void katja_binary_real_install(KatjaPkgtools *pkgtools, gchar *pkg_name);

#endif /* __KATJA_BINARY_H */
