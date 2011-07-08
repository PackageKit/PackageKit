/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#include <config.h>
#include <gio/gio.h>
#include <pk-transaction.h>

/**
 * pk_transaction_plugin_get_description:
 */
const gchar *
pk_transaction_plugin_get_description (void)
{
	return "Runs external scrips";
}

/**
 * pk_transaction_process_script:
 **/
static void
pk_transaction_process_script (PkTransaction *transaction, const gchar *filename)
{
	GFile *file = NULL;
	GFileInfo *info = NULL;
	guint file_uid;
	gchar *command = NULL;
	gint exit_status = 0;
	gboolean ret;
	GError *error = NULL;
	PkRoleEnum role;

	/* get content type for file */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_UNIX_UID ","
				  G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
	if (info == NULL) {
		g_warning ("failed to get info: %s", error->message);
		goto out;
	}

	/* check is executable */
	ret = g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
	if (!ret) {
		g_warning ("%s is not executable", filename);
		goto out;
	}

	/* check is owned by the correct user */
	file_uid = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID);
	if (file_uid != 0) {
		g_warning ("%s is not owned by the root user", filename);
		goto out;
	}

	/* format the argument list */
	role = pk_transaction_priv_get_role (transaction);
	command = g_strdup_printf ("%s %s NOTAPISTABLE",
				   filename,
				   pk_role_enum_to_string (role));

	/* run the command, but don't exit if fails */
	ret = g_spawn_command_line_sync (command, NULL, NULL, &exit_status, &error);
	if (!ret) {
		g_warning ("failed to spawn %s [%i]: %s", command, exit_status, error->message);
		g_error_free (error);
	} else {
		g_debug ("ran %s", command);
	}

out:
	g_free (command);
	if (info != NULL)
		g_object_unref (info);
	if (file != NULL)
		g_object_unref (file);
}

/**
 * pk_transaction_process_scripts:
 *
 * Run all scripts in a given directory
 **/
static void
pk_transaction_process_scripts (PkTransaction *transaction, const gchar *location)
{
	GError *error = NULL;
	gchar *filename;
	gchar *dirname;
	const gchar *file;
	GDir *dir;

	/* get location to search */
	dirname = g_build_filename (SYSCONFDIR, "PackageKit", "events", location, NULL);
	dir = g_dir_open (dirname, 0, &error);
	if (dir == NULL) {
		g_warning ("Failed to open %s: %s", dirname, error->message);
		g_error_free (error);
		goto out;
	}

	/* run scripts */
	file = g_dir_read_name (dir);
	while (file != NULL) {
		filename = g_build_filename (dirname, file, NULL);

		/* we put this here */
		if (g_strcmp0 (file, "README") != 0) {
			pk_transaction_process_script (transaction, filename);
		}

		g_free (filename);
		file = g_dir_read_name (dir);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (dirname);
}

/**
 * pk_transaction_plugin_transaction_pre:
 */
void
pk_transaction_plugin_transaction_pre (PkTransaction *transaction)
{
	pk_transaction_process_scripts (transaction,
					"pre-transaction.d");
}

/**
 * pk_transaction_plugin_transaction_post:
 */
void
pk_transaction_plugin_transaction_post (PkTransaction *transaction)
{
	pk_transaction_process_scripts (transaction,
					"post-transaction.d");
}
