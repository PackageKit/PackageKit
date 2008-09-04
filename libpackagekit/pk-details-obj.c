/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-details-obj
 * @short_description: Functionality to create a details struct
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include <pk-enum.h>
#include "egg-debug.h"
#include "pk-common.h"
#include "pk-details-obj.h"

/**
 * pk_details_obj_new:
 *
 * Creates a new #PkDetailsObj object with default values
 *
 * Return value: a new #PkDetailsObj object
 **/
PkDetailsObj *
pk_details_obj_new (void)
{
	PkDetailsObj *obj;
	obj = g_new0 (PkDetailsObj, 1);
	obj->id = NULL;
	obj->license = NULL;
	obj->group = 0;
	obj->description = NULL;
	obj->url = NULL;
	obj->size = 0;

	return obj;
}

/**
 * pk_details_obj_new_from_data:
 *
 * Creates a new #PkDetailsObj object with values.
 *
 * Return value: a new #PkDetailsObj object
 **/
PkDetailsObj *
pk_details_obj_new_from_data (const PkPackageId *id, const gchar *license, PkGroupEnum group,
			      const gchar *description, const gchar *url, guint64 size)
{
	PkDetailsObj *obj = NULL;

	/* create new object */
	obj = pk_details_obj_new ();
	obj->id = pk_package_id_copy (id);
	obj->license = g_strdup (license);
	obj->group = group;
	obj->description = g_strdup (description);
	obj->url = g_strdup (url);
	obj->size = size;

	return obj;
}

/**
 * pk_details_obj_copy:
 *
 * Return value: a new #PkDetailsObj object
 **/
PkDetailsObj *
pk_details_obj_copy (const PkDetailsObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_details_obj_new_from_data (obj->id, obj->license, obj->group,
					     obj->description, obj->url, obj->size);
}

/**
 * pk_details_obj_free:
 * @obj: the #PkDetailsObj object
 *
 * Return value: %TRUE if the #PkDetailsObj object was freed.
 **/
gboolean
pk_details_obj_free (PkDetailsObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	pk_package_id_free (obj->id);
	g_free (obj->license);
	g_free (obj->description);
	g_free (obj->url);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_details_test (EggTest *test)
{
	gboolean ret;
	PkDetailsObj *obj;

	if (!egg_test_start (test, "PkDetailsObj"))
		return;

	/************************************************************/
	egg_test_title (test, "get an details object");
	obj = pk_details_obj_new ();
	if (obj != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "test details");
	ret = pk_details_obj_free (obj);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	egg_test_end (test);
}
#endif

