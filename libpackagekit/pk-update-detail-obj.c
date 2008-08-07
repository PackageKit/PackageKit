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
#include "pk-debug.h"
#include "pk-common.h"
#include "pk-update-detail-obj.h"

/**
 * pk_update_detail_obj_new:
 *
 * Creates a new #PkUpdateDetailObj object with default values
 *
 * Return value: a new #PkUpdateDetailObj object
 **/
PkUpdateDetailObj *
pk_update_detail_obj_new (void)
{
	PkUpdateDetailObj *obj;
	obj = g_new0 (PkUpdateDetailObj, 1);
	obj->updates = NULL;
	obj->obsoletes = NULL;
	obj->vendor_url = NULL;
	obj->bugzilla_url = NULL;
	obj->cve_url = NULL;
	obj->restart = 0;
	obj->update_text = NULL;
	obj->changelog = NULL;
	obj->state = PK_UPDATE_STATE_ENUM_UNKNOWN;
	obj->issued = NULL;
	obj->updated = NULL;

	return obj;
}

/**
 * pk_update_detail_obj_new_from_data:
 *
 * Creates a new #PkUpdateDetailObj object with values.
 *
 * Return value: a new #PkUpdateDetailObj object
 **/
PkUpdateDetailObj *
pk_update_detail_obj_new_from_data (const PkPackageId *id, const gchar *updates, const gchar *obsoletes,
				    const gchar *vendor_url, const gchar *bugzilla_url, const gchar *cve_url,
				    PkRestartEnum restart, const gchar *update_text,
				    const gchar *changelog, PkUpdateStateEnum state, GDate *issued, GDate *updated)
{
	PkUpdateDetailObj *obj = NULL;

	/* create new object */
	obj = pk_update_detail_obj_new ();
	obj->id = pk_package_id_copy (id);
	obj->updates = g_strdup (updates);
	obj->obsoletes = g_strdup (obsoletes);
	obj->vendor_url = g_strdup (vendor_url);
	obj->bugzilla_url = g_strdup (bugzilla_url);
	obj->cve_url = g_strdup (cve_url);
	obj->restart = restart;
	obj->update_text = g_strdup (update_text);
	obj->changelog = g_strdup (changelog);
	obj->state = state;
	if (issued != NULL)
		obj->issued = g_date_new_dmy (issued->day, issued->month, issued->year);
	if (updated != NULL)
		obj->updated = g_date_new_dmy (updated->day, updated->month, updated->year);

	return obj;
}

/**
 * pk_update_detail_obj_copy:
 *
 * Return value: a new #PkUpdateDetailObj object
 **/
PkUpdateDetailObj *
pk_update_detail_obj_copy (const PkUpdateDetailObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_update_detail_obj_new_from_data (obj->id, obj->updates, obj->obsoletes,
						   obj->vendor_url, obj->bugzilla_url, obj->cve_url,
						   obj->restart, obj->update_text,
						   obj->changelog, obj->state, obj->issued, obj->updated);
}

/**
 * pk_update_detail_obj_free:
 * @obj: the #PkUpdateDetailObj object
 *
 * Return value: %TRUE if the #PkUpdateDetailObj object was freed.
 **/
gboolean
pk_update_detail_obj_free (PkUpdateDetailObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	pk_package_id_free (obj->id);
	g_free (obj->updates);
	g_free (obj->obsoletes);
	g_free (obj->vendor_url);
	g_free (obj->bugzilla_url);
	g_free (obj->cve_url);
	g_free (obj->update_text);
	g_free (obj->changelog);
	if (obj->issued)
		g_date_free (obj->issued);
	if (obj->updated)
		g_date_free (obj->updated);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_update_detail (LibSelfTest *test)
{
	gboolean ret;
	PkUpdateDetailObj *obj;

	if (libst_start (test, "PkUpdateDetailObj", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	/************************************************************/
	libst_title (test, "get an detail object");
	obj = pk_update_detail_obj_new ();
	if (obj != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test detail");
	ret = pk_update_detail_obj_free (obj);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_end (test);
}
#endif

