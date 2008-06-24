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
 * SECTION:pk-update-detail
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
#include "pk-update-detail.h"

/**
 * pk_update_detail_new:
 *
 * Creates a new #PkUpdateDetail object with default values
 *
 * Return value: a new #PkUpdateDetail object
 **/
PkUpdateDetail *
pk_update_detail_new (void)
{
	PkUpdateDetail *detail;
	detail = g_new0 (PkUpdateDetail, 1);
	detail->updates = NULL;
	detail->obsoletes = NULL;
	detail->vendor_url = NULL;
	detail->bugzilla_url = NULL;
	detail->cve_url = NULL;
	detail->restart = 0;
	detail->update_text = NULL;

	return detail;
}

/**
 * pk_update_detail_new_from_data:
 *
 * Creates a new #PkUpdateDetail object with values.
 *
 * Return value: a new #PkUpdateDetail object
 **/
PkUpdateDetail *
pk_update_detail_new_from_data (const gchar *package_id, const gchar *updates, const gchar *obsoletes,
				const gchar *vendor_url, const gchar *bugzilla_url, const gchar *cve_url,
				PkRestartEnum restart, const gchar *update_text)
{
	PkUpdateDetail *detail = NULL;

	/* create new object */
	detail = pk_update_detail_new ();
	detail->package_id = g_strdup (package_id);
	detail->updates = g_strdup (updates);
	detail->obsoletes = g_strdup (obsoletes);
	detail->vendor_url = g_strdup (vendor_url);
	detail->bugzilla_url = g_strdup (bugzilla_url);
	detail->cve_url = g_strdup (cve_url);
	detail->restart = restart;
	detail->update_text = g_strdup (update_text);

	return detail;
}

/**
 * pk_update_detail_free:
 * @detail: the #PkUpdateDetail object
 *
 * Return value: %TRUE if the #PkUpdateDetail object was freed.
 **/
gboolean
pk_update_detail_free (PkUpdateDetail *detail)
{
	if (detail == NULL) {
		return FALSE;
	}
	g_free (detail->updates);
	g_free (detail->obsoletes);
	g_free (detail->vendor_url);
	g_free (detail->bugzilla_url);
	g_free (detail->cve_url);
	g_free (detail->update_text);
	g_free (detail);
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
	PkUpdateDetail *detail;

	if (libst_start (test, "PkUpdateDetail", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	/************************************************************/
	libst_title (test, "get an detail object");
	detail = pk_update_detail_new ();
	if (detail != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test detail");
	ret = pk_update_detail_free (detail);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_update_detail_free (detail);

	libst_end (test);
}
#endif

