/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>

#include "egg-debug.h"

#include "pk-service-pack.h"

#define PK_SERVICE_PACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SERVICE_PACK, PkServicePackPrivate))

struct PkServicePackPrivate
{
	PkPackageList		*list;
};

G_DEFINE_TYPE (PkServicePack, pk_service_pack, G_TYPE_OBJECT)

/**
 * pk_service_pack_check_valid:
 **/
gboolean
pk_service_pack_check_valid (PkServicePack *pack, const gchar *filename, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	return TRUE;
}

/**
 * pk_service_pack_finalize:
 **/
static void
pk_service_pack_finalize (GObject *object)
{
	PkServicePack *pack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SERVICE_PACK (object));
	pack = PK_SERVICE_PACK (object);

	G_OBJECT_CLASS (pk_service_pack_parent_class)->finalize (object);
}

/**
 * pk_service_pack_class_init:
 **/
static void
pk_service_pack_class_init (PkServicePackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_service_pack_finalize;
	g_type_class_add_private (klass, sizeof (PkServicePackPrivate));
}

/**
 * pk_service_pack_init:
 **/
static void
pk_service_pack_init (PkServicePack *pack)
{
	pack->priv = PK_SERVICE_PACK_GET_PRIVATE (pack);
}

/**
 * pk_service_pack_new:
 * Return value: A new service_pack class instance.
 **/
PkServicePack *
pk_service_pack_new (void)
{
	PkServicePack *pack;
	pack = g_object_new (PK_TYPE_SERVICE_PACK, NULL);
	return PK_SERVICE_PACK (pack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_service_pack (EggTest *test)
{
	PkServicePack *pack;

	if (!egg_test_start (test, "PkServicePack"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	pack = pk_service_pack_new ();
	egg_test_assert (test, pack != NULL);

	g_object_unref (pack);

	egg_test_end (test);
}
#endif

