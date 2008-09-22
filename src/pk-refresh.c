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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "egg-debug.h"

#include "pk-refresh.h"
#include "pk-shared.h"
#include "pk-extra.h"
#include "pk-marshal.h"
#include "pk-backend-internal.h"

#define PK_REFRESH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_REFRESH, PkRefreshPrivate))

struct PkRefreshPrivate
{
	PkBackend		*backend;
	PkExtra			*extra;
	GMainLoop		*loop;
	PkPackageList		*list;
	guint			 finished_id;
	guint			 package_id;
};

enum {
	PK_REFRESH_STATUS_CHANGED,
	PK_REFRESH_PROGRESS_CHANGED,
	PK_REFRESH_LAST_SIGNAL
};

static guint signals [PK_REFRESH_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (PkRefresh, pk_refresh, G_TYPE_OBJECT)

/**
 * pk_refresh_finished_cb:
 **/
static void
pk_refresh_finished_cb (PkBackend *backend, PkExitEnum exit, PkRefresh *refresh)
{
	if (g_main_loop_is_running (refresh->priv->loop))
		g_main_loop_quit (refresh->priv->loop);
}

/**
 * pk_refresh_package_cb:
 **/
static void
pk_refresh_package_cb (PkBackend *backend, const PkPackageObj *obj, PkRefresh *refresh)
{
	pk_package_list_add_obj (refresh->priv->list, obj);
}

/**
 * pk_refresh_emit_status_changed:
 **/
static void
pk_refresh_emit_status_changed (PkRefresh *refresh, PkStatusEnum status)
{
	egg_debug ("emiting status-changed %s", pk_status_enum_to_text (status));
	g_signal_emit (refresh, signals [PK_REFRESH_STATUS_CHANGED], 0, status);
}

/**
 * pk_refresh_emit_progress_changed:
 **/
static void
pk_refresh_emit_progress_changed (PkRefresh *refresh, guint percentage)
{
	egg_debug ("emiting progress-changed %i", percentage);
	g_signal_emit (refresh, signals [PK_REFRESH_PROGRESS_CHANGED], 0, percentage, 0, 0, 0);
}

/**
 * pk_import_get_locale:
 **/
static gchar *
pk_import_get_locale (const gchar *buffer)
{
	guint len;
	gchar *locale;
	gchar *result;
	result = g_strrstr (buffer, "[");
	if (result == NULL)
		return NULL;
	locale = g_strdup (result+1);
	len = egg_strlen (locale, 20);
	locale[len-1] = '\0';
	return locale;
}

/**
 * pk_refresh_import_desktop_files_process_desktop:
 **/
static void
pk_refresh_import_desktop_files_process_desktop (PkRefresh *refresh, const gchar *package_name, const gchar *filename)
{
	GKeyFile *key;
	gboolean ret;
	guint i;
	gchar *name = NULL;
	gchar *name_unlocalised = NULL;
	gchar *exec = NULL;
	gchar *icon = NULL;
	gchar *comment = NULL;
	gchar *genericname = NULL;
	const gchar *locale = NULL;
	gchar **key_array;
	gsize len;
	gchar *locale_temp;
	static GPtrArray *locale_array = NULL;

	key = g_key_file_new ();
	ret = g_key_file_load_from_file (key, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (!ret) {
		egg_warning ("cannot open desktop file %s", filename);
		return;
	}

	/* get this specific locale list */
	key_array = g_key_file_get_keys (key, G_KEY_FILE_DESKTOP_GROUP, &len, NULL);
	locale_array = g_ptr_array_new ();
	for (i=0; i<len; i++) {
		if (g_str_has_prefix (key_array[i], "Name")) {
			/* set the locale */
			locale_temp = pk_import_get_locale (key_array[i]);
			if (locale_temp != NULL)
				g_ptr_array_add (locale_array, g_strdup (locale_temp));
		}
	}
	g_strfreev (key_array);

	/* get the default entry */
	name_unlocalised = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL);
	if (!egg_strzero (name_unlocalised)) {
		pk_extra_set_locale (refresh->priv->extra, "C");
		pk_extra_set_data_locale (refresh->priv->extra, package_name, name_unlocalised);
	}

	/* for each locale */
	for (i=0; i<locale_array->len; i++) {
		locale = g_ptr_array_index (locale_array, i);
		/* compare the translated against the default */
		name = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", locale, NULL);

		/* if different, then save */
		if (egg_strequal (name_unlocalised, name) == FALSE) {
			comment = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP,
								"Comment", locale, NULL);
			genericname = g_key_file_get_locale_string (key, G_KEY_FILE_DESKTOP_GROUP,
								    "GenericName", locale, NULL);
			pk_extra_set_locale (refresh->priv->extra, locale);

			/* save in order of priority */
			if (comment != NULL)
				pk_extra_set_data_locale (refresh->priv->extra, package_name, comment);
			else if (genericname != NULL)
				pk_extra_set_data_locale (refresh->priv->extra, package_name, genericname);
			else
				pk_extra_set_data_locale (refresh->priv->extra, package_name, name);
			g_free (comment);
			g_free (genericname);
		}
		g_free (name);
	}
	g_ptr_array_foreach (locale_array, (GFunc) g_free, NULL);
	g_ptr_array_free (locale_array, TRUE);
	g_free (name_unlocalised);

	exec = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Exec", NULL);
	icon = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Icon", NULL);
	pk_extra_set_data_package (refresh->priv->extra, package_name, icon, exec);
	g_free (icon);
	g_free (exec);

	g_key_file_free (key);
}

/**
 * pk_refresh_import_desktop_files_get_package:
 **/
static gchar *
pk_refresh_import_desktop_files_get_package (PkRefresh *refresh, const gchar *filename)
{
	guint size;
	gchar *name = NULL;
	const PkPackageObj *obj;

	/* use PK to find the correct package */
	pk_package_list_clear (refresh->priv->list);
	pk_backend_reset (refresh->priv->backend);
	refresh->priv->backend->desc->search_file (refresh->priv->backend, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), filename);

	/* wait for finished */
	g_main_loop_run (refresh->priv->loop);

	/* check that we only matched one package */
	size = pk_package_list_get_size (refresh->priv->list);
	if (size != 1) {
		egg_warning ("not correct size, %i", size);
		goto out;
	}

	/* get the obj */
	obj = pk_package_list_get_obj (refresh->priv->list, 0);
	if (obj == NULL) {
		egg_warning ("cannot get obj");
		goto out;
	}

	/* strip the name */
	name = g_strdup (obj->id->name);

out:
	return name;
}

/**
 * pk_refresh_import_desktop_files:
 **/
gboolean
pk_refresh_import_desktop_files (PkRefresh *refresh)
{
	GDir *dir;
	guint i;
	const gchar *name;
	GPatternSpec *pattern;
	gboolean match;
	gchar *filename;
	gchar *package_name;
	GPtrArray *array;
	gfloat step;

	const gchar *directory = "/usr/share/applications";

	g_return_val_if_fail (PK_IS_REFRESH (refresh), FALSE);

	if (refresh->priv->backend->desc->search_file == NULL) {
		egg_debug ("cannot search files");
		return FALSE;
	}

	/* open directory */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		egg_warning ("not a valid desktop dir!");
		return FALSE;
	}

	/* use a local backend instance */
	pk_backend_reset (refresh->priv->backend);
	pk_refresh_emit_status_changed (refresh, PK_STATUS_ENUM_SCAN_APPLICATIONS);

	/* find files */
	pattern = g_pattern_spec_new ("*.desktop");
	name = g_dir_read_name (dir);
	array = g_ptr_array_new ();
	while (name != NULL) {
		/* ITS4: ignore, not used for allocation and has to be NULL terminated */
		match = g_pattern_match (pattern, strlen (name), name, NULL);
		if (match) {
			filename = g_build_filename (directory, name, NULL);
			g_ptr_array_add (array, filename);
		}
		name = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* update UI */
	pk_refresh_emit_progress_changed (refresh, 0);
	step = 100.0f / array->len;

	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);

		/* get the name */
		package_name = pk_refresh_import_desktop_files_get_package (refresh, filename);

		/* process the file */
		if (package_name != NULL)
			pk_refresh_import_desktop_files_process_desktop (refresh, package_name, filename);
		else
			egg_warning ("%s ignored, failed to get package name\n", filename);
		g_free (package_name);

		/* update UI */
		pk_refresh_emit_progress_changed (refresh, i * step);
	}

	/* update UI */
	pk_refresh_emit_progress_changed (refresh, 100);
	pk_refresh_emit_status_changed (refresh, PK_STATUS_ENUM_FINISHED);

	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	return TRUE;
}

/**
 * pk_refresh_update_package_list:
 **/
gboolean
pk_refresh_update_package_list (PkRefresh *refresh)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_REFRESH (refresh), FALSE);

	if (refresh->priv->backend->desc->get_packages == NULL) {
		egg_debug ("cannot get packages");
		return FALSE;
	}

	egg_debug ("updating package lists");

	/* clear old list */
	pk_package_list_clear (refresh->priv->list);

	/* update UI */
	pk_refresh_emit_status_changed (refresh, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_refresh_emit_progress_changed (refresh, 101);

	/* get the new package list */
	pk_backend_reset (refresh->priv->backend);
	refresh->priv->backend->desc->get_packages (refresh->priv->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (refresh->priv->loop);

	/* update UI */
	pk_refresh_emit_progress_changed (refresh, 90);

	/* convert to a file */
	ret = pk_package_list_to_file (refresh->priv->list, "/var/lib/PackageKit/package-list.txt");
	if (!ret)
		egg_warning ("failed to save to file");

	/* update UI */
	pk_refresh_emit_progress_changed (refresh, 100);
	pk_refresh_emit_status_changed (refresh, PK_STATUS_ENUM_FINISHED);

	return ret;
}

/**
 * pk_refresh_clear_firmware_requests:
 **/
gboolean
pk_refresh_clear_firmware_requests (PkRefresh *refresh)
{
	gboolean ret;
	gchar *filename;

	g_return_val_if_fail (PK_IS_REFRESH (refresh), FALSE);

	/* clear the firmware requests directory */
	filename = g_build_filename (LOCALSTATEDIR, "run", "PackageKit", "udev", NULL);
	egg_debug ("clearing udev firmware requests at %s", filename);
	ret = pk_directory_remove_contents (filename);
	if (!ret)
		egg_warning ("failed to clear %s", filename);
	g_free (filename);
	return ret;
}

/**
 * pk_refresh_finalize:
 **/
static void
pk_refresh_finalize (GObject *object)
{
	PkRefresh *refresh;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_REFRESH (object));
	refresh = PK_REFRESH (object);

	g_signal_handler_disconnect (refresh->priv->backend, refresh->priv->finished_id);
	g_signal_handler_disconnect (refresh->priv->backend, refresh->priv->package_id);

	if (g_main_loop_is_running (refresh->priv->loop))
		g_main_loop_quit (refresh->priv->loop);
	g_main_loop_unref (refresh->priv->loop);

	g_object_unref (refresh->priv->backend);
	g_object_unref (refresh->priv->extra);
	g_object_unref (refresh->priv->list);

	G_OBJECT_CLASS (pk_refresh_parent_class)->finalize (object);
}

/**
 * pk_refresh_class_init:
 **/
static void
pk_refresh_class_init (PkRefreshClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_refresh_finalize;
	signals [PK_REFRESH_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_REFRESH_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkRefreshPrivate));
}

/**
 * pk_refresh_init:
 *
 * initializes the refresh class. NOTE: We expect refresh objects
 * to *NOT* be removed or added during the session.
 * We only control the first refresh object if there are more than one.
 **/
static void
pk_refresh_init (PkRefresh *refresh)
{
	gboolean ret;

	refresh->priv = PK_REFRESH_GET_PRIVATE (refresh);
	refresh->priv->loop = g_main_loop_new (NULL, FALSE);
	refresh->priv->list = pk_package_list_new ();
	refresh->priv->backend = pk_backend_new ();

	refresh->priv->finished_id =
		g_signal_connect (refresh->priv->backend, "finished",
				  G_CALLBACK (pk_refresh_finished_cb), refresh);
	refresh->priv->package_id =
		g_signal_connect (refresh->priv->backend, "package",
				  G_CALLBACK (pk_refresh_package_cb), refresh);

	refresh->priv->extra = pk_extra_new ();

	/* use the default location */
	ret = pk_extra_set_database (refresh->priv->extra, NULL);
	if (!ret)
		egg_warning ("Could not open extra database");
}

/**
 * pk_refresh_new:
 * Return value: A new refresh class instance.
 **/
PkRefresh *
pk_refresh_new (void)
{
	PkRefresh *refresh;
	refresh = g_object_new (PK_TYPE_REFRESH, NULL);
	return PK_REFRESH (refresh);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_refresh (EggTest *test)
{
	PkRefresh *refresh;

	if (!egg_test_start (test, "PkRefresh"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	refresh = pk_refresh_new ();
	egg_test_assert (test, refresh != NULL);

	g_object_unref (refresh);

	egg_test_end (test);
}
#endif

