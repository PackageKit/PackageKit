#ifndef __KATJA_DL_H
#define __KATJA_DL_H

#include "katja-binary.h"

G_BEGIN_DECLS

#define KATJA_TYPE_DL (katja_dl_get_type())
#define KATJA_DL(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_DL, KatjaDl))
#define KATJA_DL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_DL, KatjaDlClass))
#define KATJA_IS_DL(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_DL))
#define KATJA_IS_DL_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_DL))
#define KATJA_DL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_DL, KatjaDlClass))

typedef struct {
	KatjaBinary parent;

	/* public */
	GString *index_file;
} KatjaDl;

typedef struct {
	KatjaBinaryClass parent_class;
} KatjaDlClass;

GType katja_dl_get_type(void);

/* Constructors */
KatjaDl *katja_dl_new(gchar *name, gchar *mirror, guint i, gchar *index_file);

/* Implementations */
GSList *katja_dl_real_collect_cache_info(KatjaPkgtools *pkgtools, const gchar *tmpl);
void katja_dl_real_generate_cache(KatjaPkgtools *pkgtools, const gchar *tmpl);

G_END_DECLS

#endif /* __KATJA_DL_H */
