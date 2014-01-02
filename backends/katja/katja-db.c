#include "katja-db.h"

#define KATJA_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), KATJA_TYPE_DB, KatjaDbPrivate))

struct KatjaDbPrivate {
};

G_DEFINE_TYPE(KatjaDb, katja_db, G_TYPE_OBJECT);

/**
 * katja_db_finalize:
 **/
static void katja_db_finalize(GObject *object) {
	g_return_if_fail(KATJA_IS_DB(object));

	G_OBJECT_CLASS(katja_db_parent_class)->finalize(object);
}

/**
 * katja_db_class_init:
 **/
static void katja_db_class_init(KatjaDbClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = katja_db_finalize;
}

/**
 * katja_db_init:
 **/
static void katja_db_init(KatjaDb *db) {
}

/**
 * katja_db_new:
 **/
KatjaDb *katja_db_new() {
	KatjaDb *db;

	db = g_object_new(KATJA_TYPE_DB, NULL);

	return KATJA_DB(db);
}
