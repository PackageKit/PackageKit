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

#include "config.h"

#include <glib.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>

/* Rationale for choosing ',' as separator is to make it match with what
 * pk-offline machinery does when storing offline update results.
 *
 * If this has to be changed, also change:
 * - lib/python/packagekit/backend.py
 */
#define PK_PACKAGE_IDS_DELIM_STR	","
#define PK_PACKAGE_IDS_DELIM_CHR	','

/* this is a stripped down version of g_key_file_parse_string_as_value from glib */
static gchar *
escape_package_id (const gchar *package_id)
{
	gchar *ret, *q;
	const gchar *p;
	gsize length;

	length = strlen (package_id) + 1;
	p = package_id;

	/* Worst case would be that every character needs to be escaped.
	* In other words every character turns to two characters
	*/
	ret = g_new (gchar, 2 * length);
	q = ret;

	while (p < (package_id + length - 1)) {
		if (*p == PK_PACKAGE_IDS_DELIM_CHR) {
			*q = '\\';
			q++;
			*q = PK_PACKAGE_IDS_DELIM_CHR;
			q++;
		} else  {
			*q = *p;
			q++;
		}
		p++;
	}

	*q = '\0';
	return ret;
}


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
	gchar **ret;

	g_return_val_if_fail (package_id != NULL, NULL);

	ret = g_new0 (char *, 2);
	ret[0] = escape_package_id (package_id);

	return ret;
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
	GPtrArray *package_ids_array;
	const gchar *p = package_id;

	g_return_val_if_fail (package_id != NULL, NULL);

	package_ids_array = g_ptr_array_new ();

	while (*p != '\0') {
		if (*p == '\\' && *(p+1) == PK_PACKAGE_IDS_DELIM_CHR) {
			p += 2;
			continue;
		}
		if (*p == PK_PACKAGE_IDS_DELIM_CHR) {
			if (p - package_id > 1)
				g_ptr_array_add (package_ids_array, g_strndup (package_id, p - package_id));
			package_id = p + 1;
		}
		p++;
	}

	if (package_id != p)
		g_ptr_array_add (package_ids_array, g_strndup (package_id, p - package_id));

	g_ptr_array_add (package_ids_array, NULL);

	return (gchar **) g_ptr_array_free (package_ids_array, FALSE);
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
	guint size;
	gchar **escaped_package_ids;
	gchar *ret;

	/* special case as this is allowed */
	if (package_ids == NULL)
		return NULL;

	size = g_strv_length (package_ids);
	escaped_package_ids = g_new (gchar *, size + 1);

	for (guint i = 0; i < size; i++)
		escaped_package_ids[i] = escape_package_id (package_ids[i]);
	escaped_package_ids[size] = NULL;

	ret = g_strjoinv (PK_PACKAGE_IDS_DELIM_STR, escaped_package_ids);
	g_strfreev (escaped_package_ids);

	return ret;
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
	g_autofree gchar *escaped_package_id;

	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	escaped_package_id = escape_package_id (package_id);

	/* iterate */
	for (guint i = 0; package_ids[i] != NULL; i++) {
		if (g_strcmp0 (escaped_package_id, package_ids[i]) == 0)
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
	result[i] = escape_package_id (package_id);
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
	g_autofree gchar *escaped_package_id;
	guint len;
	gchar **result;

	g_return_val_if_fail (package_ids != NULL, NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	escaped_package_id = escape_package_id (package_id);
	len = g_strv_length (package_ids);
	result = g_new0 (gchar *, len+1);

	/* iterate */
	for (guint i = 0, j = 0; package_ids[i] != NULL; i++) {
		if (g_strcmp0 (escaped_package_id, package_ids[i]) != 0)
			result[j++] = g_strdup (package_ids[i]);
	}
	return result;
}
