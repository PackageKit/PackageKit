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
 * GNU General Public License for more category.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:pk-category-obj
 * @short_description: Functionality to create a category struct
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "egg-debug.h"
#include "pk-common.h"
#include "pk-category-obj.h"

/**
 * pk_category_obj_new:
 *
 * Creates a new #PkCategoryObj object with default values
 *
 * Return value: a new #PkCategoryObj object
 **/
PkCategoryObj *
pk_category_obj_new (void)
{
	PkCategoryObj *obj;
	obj = g_new0 (PkCategoryObj, 1);
	obj->parent_id = NULL;
	obj->cat_id = NULL;
	obj->name = NULL;
	obj->summary = NULL;
	obj->icon = NULL;

	return obj;
}

/**
 * pk_category_obj_new_from_data:
 *
 * Creates a new #PkCategoryObj object with values.
 *
 * Return value: a new #PkCategoryObj object
 **/
PkCategoryObj *
pk_category_obj_new_from_data (const gchar *parent_id, const gchar *cat_id, const gchar *name,
			       const gchar *summary, const gchar *icon)
{
	PkCategoryObj *obj = NULL;

	/* create new object */
	obj = pk_category_obj_new ();
	obj->parent_id = g_strdup (parent_id);
	obj->cat_id = g_strdup (cat_id);
	obj->name = g_strdup (name);
	obj->summary = g_strdup (summary);
	obj->icon = g_strdup (icon);

	return obj;
}

/**
 * pk_category_obj_copy:
 *
 * Return value: a new #PkCategoryObj object
 **/
PkCategoryObj *
pk_category_obj_copy (const PkCategoryObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_category_obj_new_from_data (obj->parent_id, obj->cat_id, obj->name, obj->summary, obj->icon);
}

/**
 * pk_category_obj_free:
 * @obj: the #PkCategoryObj object
 *
 * Return value: %TRUE if the #PkCategoryObj object was freed.
 **/
gboolean
pk_category_obj_free (PkCategoryObj *obj)
{
	if (obj == NULL)
		return FALSE;
	g_free (obj->parent_id);
	g_free (obj->cat_id);
	g_free (obj->name);
	g_free (obj->summary);
	g_free (obj->icon);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_category_test (EggTest *test)
{
	gboolean ret;
	PkCategoryObj *obj;

	if (!egg_test_start (test, "PkCategoryObj"))
		return;

	/************************************************************/
	egg_test_title (test, "get an category object");
	obj = pk_category_obj_new ();
	egg_test_assert (test, obj != NULL);

	/************************************************************/
	egg_test_title (test, "test category");
	ret = pk_category_obj_free (obj);
	egg_test_assert (test, ret);

	egg_test_end (test);
}
#endif

