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

#include "config.h"
#include <glib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>

#include <egg-debug.h>

#include "pk-tools-common.h"
#include "pk-text.h"

/**
 * pk_console_resolve:
 **/
PkPackageList *
pk_console_resolve (PkBitfield filter, const gchar *package, GError **error)
{
	gboolean ret;
	guint length;
	PkPackageList *list = NULL;
	PkControl *control;
	PkClient *client;
	gchar **packages;
	GError *error_local = NULL;
	PkBitfield roles;

	/* get roles supported */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);

	/* get new client */
	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* we need to resolve it */
	packages = pk_package_ids_from_id (package);
	ret = pk_client_resolve (client, filter, packages, &error_local);
	g_strfreev (packages);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get length of items found */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);

	/* didn't resolve to anything, try to get a provide */
	if (length == 0 && pk_bitfield_contain (roles, PK_ROLE_ENUM_WHAT_PROVIDES)) {

		g_object_unref (list);
		list = NULL;

		/* reset */
		ret = pk_client_reset (client, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}
		/* anything provide it? */
		ret = pk_client_what_provides (client, filter, PK_PROVIDES_ENUM_ANY, package, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* get length of items found again (we might have had success) */
		list = pk_client_get_package_list (client);
	}
out:
	g_object_unref (control);
	g_object_unref (client);
	return list;
}

/**
 * pk_console_resolve_package_id:
 **/
gchar *
pk_console_resolve_package_id (const PkPackageList *list, GError **error)
{
	guint i;
	guint length;
	const PkPackageObj *obj;

	length = pk_package_list_get_size (list);

	if (length == 0) {
		if (error != NULL) {
			/* TRANSLATORS: The package was not found in any software sources */
			*error = g_error_new (1, 0, _("The package could not be found"));
		}
		return NULL;
	}

	/* only found one, great! */
	if (length == 1) {
		obj = pk_package_list_get_obj (list, 0);
		return pk_package_id_to_string (obj->id);
	}

	/* TRANSLATORS: more than one package could be found that matched, to follow is a list of possible packages  */
	g_print ("%s\n", _("More than one package matches:"));
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		g_print ("%i. %s-%s.%s\n", i+1, obj->id->name, obj->id->version, obj->id->arch);
	}

	/* TRANSLATORS: This finds out which package in the list to use */
	i = pk_console_get_number (_("Please choose the correct package: "), length);
	obj = pk_package_list_get_obj (list, i-1);

	return pk_package_id_to_string (obj->id);
}

