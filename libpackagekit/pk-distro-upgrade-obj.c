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

/**
 * SECTION:pk-update-detail-obj
 * @short_description: Functionality to create an update detail struct
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
#include "pk-distro-upgrade-obj.h"

/**
 * pk_distro_upgrade_obj_new:
 *
 * Creates a new #PkDistroUpgradeObj object with default values
 *
 * Return value: a new #PkDistroUpgradeObj object
 **/
PkDistroUpgradeObj *
pk_distro_upgrade_obj_new (void)
{
	PkDistroUpgradeObj *obj;
	obj = g_new0 (PkDistroUpgradeObj, 1);
	obj->name = NULL;
	obj->summary = NULL;
	obj->state = PK_DISTRO_UPGRADE_ENUM_UNKNOWN;
	return obj;
}

/**
 * pk_distro_upgrade_obj_new_from_data:
 *
 * Creates a new #PkDistroUpgradeObj object with values.
 *
 * Return value: a new #PkDistroUpgradeObj object
 **/
PkDistroUpgradeObj *
pk_distro_upgrade_obj_new_from_data (PkUpdateStateEnum state, const gchar *name, const gchar *summary)
{
	PkDistroUpgradeObj *obj = NULL;

	/* create new object */
	obj = pk_distro_upgrade_obj_new ();
	obj->state = state;
	obj->name = g_strdup (name);
	obj->summary = g_strdup (summary);
	return obj;
}

/**
 * pk_distro_upgrade_obj_copy:
 *
 * Return value: a new #PkDistroUpgradeObj object
 **/
PkDistroUpgradeObj *
pk_distro_upgrade_obj_copy (const PkDistroUpgradeObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_distro_upgrade_obj_new_from_data (obj->state, obj->name, obj->summary);
}

/**
 * pk_distro_upgrade_obj_free:
 * @obj: the #PkDistroUpgradeObj object
 *
 * Return value: %TRUE if the #PkDistroUpgradeObj object was freed.
 **/
gboolean
pk_distro_upgrade_obj_free (PkDistroUpgradeObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	g_free (obj->name);
	g_free (obj->summary);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_distro_upgrade (EggTest *test)
{
	gboolean ret;
	PkDistroUpgradeObj *obj;

	if (!egg_test_start (test, "PkDistroUpgradeObj"))
		return;

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	/************************************************************/
	egg_test_title (test, "get an upgrade object");
	obj = pk_distro_upgrade_obj_new ();
	egg_test_assert (test, obj != NULL);

	/************************************************************/
	egg_test_title (test, "test upgrade");
	ret = pk_distro_upgrade_obj_free (obj);
	egg_test_assert (test, ret);

	egg_test_end (test);
}
#endif

