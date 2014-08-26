/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-package-ids
 * @short_description: Functionality to modify multiple PackageIDs
 *
 * Composite PackageId's are difficult to read and create.
 */

#include "config.h"

#include <glib.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>

/**
 * pk_package_ids_from_id:
 * @package_id: A single package_id
 *
 * Form a composite string array of package_id's from
 * a single package_id
 *
 * Return value: (transfer full): the string array, or %NULL if invalid, free with g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_package_ids_from_id (const gchar *package_id)
{
	g_return_val_if_fail (package_id != NULL, NULL);
	return g_strsplit (package_id, PK_PACKAGE_IDS_DELIM, 1);
}

/**
 * pk_package_ids_from_string:
 * @package_id: A single package_id
 *
 * Form a composite string array of package_id's from
 * a delimited string
 *
 * Return value: (transfer full): the string array, or %NULL if invalid, free with g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_package_ids_from_string (const gchar *package_id)
{
	g_return_val_if_fail (package_id != NULL, NULL);
	return g_strsplit (package_id, PK_PACKAGE_IDS_DELIM, 0);
}

/**
 * pk_package_ids_check:
 * @package_ids: a string array of package_id's
 *
 * Check the string array of package_id's for validity
 *
 * Return value: %TRUE if the package_ids are all valid.
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_ids_check (gchar **package_ids)
{
	guint i;
	guint size;
	gboolean ret = FALSE;
	const gchar *package_id;

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* check all */
	size = g_strv_length (package_ids);
	for (i = 0; i < size; i++) {
		package_id = package_ids[i];
		ret = pk_package_id_check (package_id);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * pk_package_ids_to_string:
 * @package_ids: a string array of package_id's
 *
 * Cats the string array of package_id's into one delimited string
 *
 * Return value: a string representation of all the package_id's.
 *
 * Since: 0.5.2
 **/
gchar *
pk_package_ids_to_string (gchar **package_ids)
{
	/* special case as this is allowed */
	if (package_ids == NULL)
		return NULL;
	return g_strjoinv (PK_PACKAGE_IDS_DELIM, package_ids);
}

/**
 * pk_package_ids_present_id:
 * @package_ids: a string array of package_id's
 * @package_id: a single package_id
 *
 * Finds out if a package ID is present in the list.
 *
 * Return value: %TRUE if the package ID is present
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_ids_present_id (gchar **package_ids, const gchar *package_id)
{
	guint i;

	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* iterate */
	for (i = 0; package_ids[i] != NULL; i++) {
		if (g_strcmp0 (package_id, package_ids[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_package_ids_add_id:
 * @package_ids: a string array of package_id's
 * @package_id: a single package_id
 *
 * Adds a package_id to an existing list.
 *
 * Return value: (transfer full): the new list, free g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_package_ids_add_id (gchar **package_ids, const gchar *package_id)
{
	guint i;
	guint len;
	gchar **result;

	g_return_val_if_fail (package_ids != NULL, NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	len = g_strv_length (package_ids);
	result = g_new0 (gchar *, len+2);

	/* iterate */
	for (i = 0; package_ids[i] != NULL; i++)
		result[i] = g_strdup (package_ids[i]);
	result[i] = g_strdup (package_id);
	return result;
}

/**
 * pk_package_ids_add_ids:
 * @package_ids: a string array of package_id's
 * @package_ids_new: a string array of package_id's
 *
 * Adds a package_id to an existing list.
 *
 * Return value: (transfer full): the new list, free g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_package_ids_add_ids (gchar **package_ids, gchar **package_ids_new)
{
	guint i;
	guint j = 0;
	guint len;
	gchar **result;

	g_return_val_if_fail (package_ids != NULL, NULL);
	g_return_val_if_fail (package_ids_new != NULL, NULL);

	/* get length of both arrays */
	len = g_strv_length (package_ids) + g_strv_length (package_ids_new);
	result = g_new0 (gchar *, len+1);

	/* iterate */
	for (i = 0; package_ids[i] != NULL; i++)
		result[j++] = g_strdup (package_ids[i]);
	for (i = 0; package_ids_new[i] != NULL; i++)
		result[j++] = g_strdup (package_ids_new[i]);
	return result;
}

/**
 * pk_package_ids_remove_id:
 * @package_ids: a string array of package_id's
 * @package_id: a single package_id
 *
 * Removes a package ID from the the list.
 *
 * Return value: (transfer full): the new list, free g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_package_ids_remove_id (gchar **package_ids, const gchar *package_id)
{
	guint i;
	guint j = 0;
	guint len;
	gchar **result;

	g_return_val_if_fail (package_ids != NULL, NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	len = g_strv_length (package_ids);
	result = g_new0 (gchar *, len+1);

	/* iterate */
	for (i = 0; package_ids[i] != NULL; i++) {
		if (g_strcmp0 (package_id, package_ids[i]) != 0)
			result[j++] = g_strdup (package_ids[i]);
	}
	return result;
}
