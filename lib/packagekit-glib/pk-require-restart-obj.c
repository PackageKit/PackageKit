/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-require-restart-obj
 * @short_description: Functionality to create a require restart struct
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-require-restart-obj.h>

#include "egg-debug.h"

/**
 * pk_require_restart_obj_new:
 *
 * Creates a new #PkRequireRestartObj object with default values
 *
 * Return value: a new #PkRequireRestartObj object
 **/
PkRequireRestartObj *
pk_require_restart_obj_new (void)
{
	PkRequireRestartObj *obj;
	obj = g_new0 (PkRequireRestartObj, 1);
	obj->restart = PK_RESTART_ENUM_UNKNOWN;
	obj->id = NULL;
	return obj;
}

/**
 * pk_require_restart_obj_new_from_data:
 *
 * Creates a new #PkRequireRestartObj object with values.
 *
 * Return value: a new #PkRequireRestartObj object
 **/
PkRequireRestartObj *
pk_require_restart_obj_new_from_data (PkRestartEnum restart, const PkPackageId *id)
{
	PkRequireRestartObj *obj = NULL;

	/* create new object */
	obj = pk_require_restart_obj_new ();
	obj->restart = restart;
	obj->id = pk_package_id_copy (id);
	return obj;
}

/**
 * pk_require_restart_obj_copy:
 *
 * Return value: a new #PkRequireRestartObj object
 **/
PkRequireRestartObj *
pk_require_restart_obj_copy (const PkRequireRestartObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_require_restart_obj_new_from_data (obj->restart, obj->id);
}

/**
 * pk_require_restart_obj_free:
 * @obj: the #PkRequireRestartObj object
 *
 * Return value: %TRUE if the #PkRequireRestartObj object was freed.
 **/
gboolean
pk_require_restart_obj_free (PkRequireRestartObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	pk_package_id_free (obj->id);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_require_restart (EggTest *test)
{
	gboolean ret;
	PkRequireRestartObj *obj;

	if (!egg_test_start (test, "PkRequireRestartObj"))
		return;

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	/************************************************************/
	egg_test_title (test, "get an upgrade object");
	obj = pk_require_restart_obj_new ();
	egg_test_assert (test, obj != NULL);

	/************************************************************/
	egg_test_title (test, "test upgrade");
	ret = pk_require_restart_obj_free (obj);
	egg_test_assert (test, ret);

	egg_test_end (test);
}
#endif

