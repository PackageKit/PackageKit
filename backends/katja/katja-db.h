#ifndef __KATJA_DB_H
#define __KATJA_DB_H

#include <db.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define KATJA_TYPE_DB (katja_db_get_type())
#define KATJA_DB(o) (G_TYPE_CHECK_INSTANCE_CAST((o), KATJA_TYPE_DB, KatjaDb))
#define KATJA_DB_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), KATJA_TYPE_DB, KatjaDbClass))
#define KATJA_IS_DB(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), KATJA_TYPE_DB))
#define KATJA_IS_DB_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), KATJA_TYPE_DB))
#define KATJA_DB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), KATJA_TYPE_DB, KatjaDbClass))

typedef struct KatjaDbPrivate KatjaDbPrivate;

typedef struct {
	GObject parent;
	KatjaDbPrivate *priv;
} KatjaDb;

typedef struct {
	GObjectClass parent_class;
} KatjaDbClass;

GType katja_db_get_type(void);

/* Constructors */
KatjaDb *katja_db_new();

G_END_DECLS

#endif /* __KATJA_DB_H */
