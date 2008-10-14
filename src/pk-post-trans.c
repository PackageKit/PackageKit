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

#ifdef PK_BUILD_GIO
  #include <gio/gio.h>
#endif

#include <pk-common.h>

#include "egg-debug.h"
#include "egg-string-list.h"

#include "pk-post-trans.h"
#include "pk-shared.h"
#include "pk-extra.h"
#include "pk-marshal.h"
#include "pk-backend-internal.h"

#define PK_POST_TRANS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_POST_TRANS, PkPostTransPrivate))

struct PkPostTransPrivate
{
	PkBackend		*backend;
	PkExtra			*extra;
	GMainLoop		*loop;
	PkPackageList		*list;
	guint			 finished_id;
	guint			 package_id;
};

enum {
	PK_POST_TRANS_STATUS_CHANGED,
	PK_POST_TRANS_PROGRESS_CHANGED,
	PK_POST_TRANS_LAST_SIGNAL
};

static guint signals [PK_POST_TRANS_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (PkPostTrans, pk_post_trans, G_TYPE_OBJECT)

/**
 * pk_post_trans_finished_cb:
 **/
static void
pk_post_trans_finished_cb (PkBackend *backend, PkExitEnum exit, PkPostTrans *post)
{
	if (g_main_loop_is_running (post->priv->loop))
		g_main_loop_quit (post->priv->loop);
}

/**
 * pk_post_trans_package_cb:
 **/
static void
pk_post_trans_package_cb (PkBackend *backend, const PkPackageObj *obj, PkPostTrans *post)
{
	pk_package_list_add_obj (post->priv->list, obj);
}

/**
 * pk_post_trans_emit_status_changed:
 **/
static void
pk_post_trans_emit_status_changed (PkPostTrans *post, PkStatusEnum status)
{
	egg_debug ("emiting status-changed %s", pk_status_enum_to_text (status));
	g_signal_emit (post, signals [PK_POST_TRANS_STATUS_CHANGED], 0, status);
}

/**
 * pk_post_trans_emit_progress_changed:
 **/
static void
pk_post_trans_emit_progress_changed (PkPostTrans *post, guint percentage)
{
	egg_debug ("emiting progress-changed %i", percentage);
	g_signal_emit (post, signals [PK_POST_TRANS_PROGRESS_CHANGED], 0, percentage, 0, 0, 0);
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
 * pk_post_trans_import_desktop_files_process_desktop:
 **/
static void
pk_post_trans_import_desktop_files_process_desktop (PkPostTrans *post, const gchar *package_name, const gchar *filename)
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

	/* make sure this is still set, as we are sharing PkExtra */
	pk_extra_set_access (post->priv->extra, PK_EXTRA_ACCESS_WRITE_ONLY);

	/* get the default entry */
	name_unlocalised = g_key_file_get_string (key, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL);
	if (!egg_strzero (name_unlocalised)) {
		pk_extra_set_locale (post->priv->extra, "C");
		pk_extra_set_data_locale (post->priv->extra, package_name, name_unlocalised);
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
			pk_extra_set_locale (post->priv->extra, locale);

			/* save in order of priority */
			if (comment != NULL)
				pk_extra_set_data_locale (post->priv->extra, package_name, comment);
			else if (genericname != NULL)
				pk_extra_set_data_locale (post->priv->extra, package_name, genericname);
			else
				pk_extra_set_data_locale (post->priv->extra, package_name, name);
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
	pk_extra_set_data_package (post->priv->extra, package_name, icon, exec);
	g_free (icon);
	g_free (exec);

	g_key_file_free (key);
}

/**
 * pk_post_trans_import_desktop_files_get_package:
 **/
static gchar *
pk_post_trans_import_desktop_files_get_package (PkPostTrans *post, const gchar *filename)
{
	guint size;
	gchar *name = NULL;
	const PkPackageObj *obj;
	PkStore *store;

	/* use PK to find the correct package */
	pk_package_list_clear (post->priv->list);
	pk_backend_reset (post->priv->backend);
	store = pk_backend_get_store (post->priv->backend);
	pk_store_set_uint (store, "filters", pk_bitfield_value (PK_FILTER_ENUM_INSTALLED));
	pk_store_set_string (store, "search", filename);
	post->priv->backend->desc->search_file (post->priv->backend, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), filename);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	/* check that we only matched one package */
	size = pk_package_list_get_size (post->priv->list);
	if (size != 1) {
		egg_warning ("not correct size, %i", size);
		goto out;
	}

	/* get the obj */
	obj = pk_package_list_get_obj (post->priv->list, 0);
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
 * pk_post_trans_import_desktop_files_get_files:
 *
 * Returns a list of all the files in the applicaitons directory
 **/
#ifdef PK_BUILD_GIO
static guint
pk_post_trans_get_filename_mtime (const gchar *filename)
{
	GFileInfo *info;
	GFile *file;
	GError *error = NULL;
	GTimeVal time;

	file = g_file_new_for_path (filename);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, &error);
	if (info == NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		return 0;
	}

	/* get the mtime */
	g_file_info_get_modification_time (info, &time);
	g_object_unref (file);
	g_object_unref (info);

	return time.tv_sec;
}
#else
static guint
pk_post_trans_get_filename_mtime (const gchar *filename)
{
	return 0;
}
#endif

/**
 * pk_post_trans_import_desktop_files_get_files:
 *
 * Returns a list of all the files in the applicaitons directory
 **/
static EggStringList *
pk_post_trans_import_desktop_files_get_files (PkPostTrans *post)
{
	GDir *dir;
	EggStringList *list;
	GPatternSpec *pattern;
	gchar *filename;
	gboolean match;
	const gchar *name;
	const gchar *directory = "/usr/share/applications";

	/* open directory */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		egg_warning ("not a valid desktop dir!");
		return NULL;
	}

	/* find files */
	pattern = g_pattern_spec_new ("*.desktop");
	name = g_dir_read_name (dir);
	list = egg_string_list_new ();
	while (name != NULL) {
		/* ITS4: ignore, not used for allocation and has to be NULL terminated */
		match = g_pattern_match (pattern, strlen (name), name, NULL);
		if (match) {
			filename = g_build_filename (directory, name, NULL);
			egg_obj_list_add (EGG_OBJ_LIST (list), filename);
		}
		name = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	return list;
}

/**
 * pk_post_trans_import_desktop_files_get_mtimes:
 **/
static EggStringList *
pk_post_trans_import_desktop_files_get_mtimes (const EggStringList *files)
{
	guint i;
	guint mtime;
	gchar *encode;
	const gchar *filename;
	EggStringList *list;

	list = egg_string_list_new ();
	for (i=0; i<EGG_OBJ_LIST(files)->len; i++) {
		filename = egg_string_list_index (files, i);
		mtime = pk_post_trans_get_filename_mtime (filename);
		encode = g_strdup_printf ("%s|%i|v1", filename, mtime);
		egg_obj_list_add (EGG_OBJ_LIST (list), encode);
		g_free (encode);
	}
	return list;
}

/**
 * pk_post_trans_import_desktop_files:
 **/
gboolean
pk_post_trans_import_desktop_files (PkPostTrans *post)
{
	guint i;
	gboolean ret;
	gchar *package_name;
	gfloat step;
	EggStringList *files;
	EggStringList *mtimes;
	EggStringList *mtimes_old;
	gchar *filename;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->search_file == NULL) {
		egg_debug ("cannot search files");
		return FALSE;
	}

	/* use a local backend instance */
	pk_backend_reset (post->priv->backend);
	pk_post_trans_emit_status_changed (post, PK_STATUS_ENUM_SCAN_APPLICATIONS);

	egg_debug ("getting old desktop mtimes");
	mtimes_old = egg_string_list_new ();
	ret = egg_obj_list_from_file (EGG_OBJ_LIST (mtimes_old), "/var/lib/PackageKit/desktop-mtimes.txt");
	if (!ret)
		egg_warning ("failed to get old mtimes of desktop files");

	/* get the file list */
	files = pk_post_trans_import_desktop_files_get_files (post);

	/* get the mtimes */
	mtimes = pk_post_trans_import_desktop_files_get_mtimes (files);

	/* remove old desktop files we've already processed */
	egg_obj_list_remove_list (EGG_OBJ_LIST(mtimes), EGG_OBJ_LIST (mtimes_old));

	/* shortcut, there are no files to scan */
	if (EGG_OBJ_LIST(mtimes)->len == 0) {
		egg_debug ("no desktop files needed to scan");
		goto no_changes;
	}

	/* update UI */
	pk_post_trans_emit_progress_changed (post, 0);
	step = 100.0f / EGG_OBJ_LIST(mtimes)->len;

	/* for each new package, process the desktop file */
	for (i=0; i<EGG_OBJ_LIST(mtimes)->len; i++) {

		/* get the filename from the mtime encoded string */
		filename = g_strdup (egg_string_list_index (mtimes, i));
		g_strdelimit (filename, "|", '\0');

		/* get the name */
		package_name = pk_post_trans_import_desktop_files_get_package (post, filename);

		/* process the file */
		if (package_name != NULL)
			pk_post_trans_import_desktop_files_process_desktop (post, package_name, filename);
		else
			egg_warning ("%s ignored, failed to get package name\n", filename);
		g_free (package_name);
		g_free (filename);

		/* update UI */
		pk_post_trans_emit_progress_changed (post, i * step);
	}

	/* save new mtimes data */
	ret = egg_obj_list_to_file (EGG_OBJ_LIST (mtimes), "/var/lib/PackageKit/desktop-mtimes.txt");
	if (!ret)
		egg_warning ("failed to set old mtimes of desktop files");

no_changes:
	/* update UI */
	pk_post_trans_emit_progress_changed (post, 100);
	pk_post_trans_emit_status_changed (post, PK_STATUS_ENUM_FINISHED);

	g_object_unref (files);
	g_object_unref (mtimes);
	g_object_unref (mtimes_old);
	return TRUE;
}

/**
 * pk_post_trans_update_package_list:
 **/
gboolean
pk_post_trans_update_package_list (PkPostTrans *post)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->get_packages == NULL) {
		egg_debug ("cannot get packages");
		return FALSE;
	}

	egg_debug ("updating package lists");

	/* clear old list */
	pk_package_list_clear (post->priv->list);

	/* update UI */
	pk_post_trans_emit_status_changed (post, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_post_trans_emit_progress_changed (post, 101);

	/* get the new package list */
	pk_backend_reset (post->priv->backend);
	pk_store_set_uint (pk_backend_get_store (post->priv->backend), "filters", pk_bitfield_value (PK_FILTER_ENUM_NONE));
	post->priv->backend->desc->get_packages (post->priv->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	/* update UI */
	pk_post_trans_emit_progress_changed (post, 90);

	/* convert to a file */
	ret = pk_package_list_to_file (post->priv->list, PK_SYSTEM_PACKAGE_LIST_FILENAME);
	if (!ret)
		egg_warning ("failed to save to file");

	/* update UI */
	pk_post_trans_emit_progress_changed (post, 100);
	pk_post_trans_emit_status_changed (post, PK_STATUS_ENUM_FINISHED);

	return ret;
}

/**
 * pk_post_trans_clear_firmware_requests:
 **/
gboolean
pk_post_trans_clear_firmware_requests (PkPostTrans *post)
{
	gboolean ret;
	gchar *filename;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

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
 * pk_post_trans_finalize:
 **/
static void
pk_post_trans_finalize (GObject *object)
{
	PkPostTrans *post;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_POST_TRANS (object));
	post = PK_POST_TRANS (object);

	g_signal_handler_disconnect (post->priv->backend, post->priv->finished_id);
	g_signal_handler_disconnect (post->priv->backend, post->priv->package_id);

	if (g_main_loop_is_running (post->priv->loop))
		g_main_loop_quit (post->priv->loop);
	g_main_loop_unref (post->priv->loop);

	g_object_unref (post->priv->backend);
	g_object_unref (post->priv->extra);
	g_object_unref (post->priv->list);

	G_OBJECT_CLASS (pk_post_trans_parent_class)->finalize (object);
}

/**
 * pk_post_trans_class_init:
 **/
static void
pk_post_trans_class_init (PkPostTransClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_post_trans_finalize;
	signals [PK_POST_TRANS_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_POST_TRANS_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkPostTransPrivate));
}

/**
 * pk_post_trans_init:
 *
 * initializes the post_trans class. NOTE: We expect post_trans objects
 * to *NOT* be removed or added during the session.
 * We only control the first post_trans object if there are more than one.
 **/
static void
pk_post_trans_init (PkPostTrans *post)
{
	gboolean ret;

	post->priv = PK_POST_TRANS_GET_PRIVATE (post);
	post->priv->loop = g_main_loop_new (NULL, FALSE);
	post->priv->list = pk_package_list_new ();
	post->priv->backend = pk_backend_new ();

	post->priv->finished_id =
		g_signal_connect (post->priv->backend, "finished",
				  G_CALLBACK (pk_post_trans_finished_cb), post);
	post->priv->package_id =
		g_signal_connect (post->priv->backend, "package",
				  G_CALLBACK (pk_post_trans_package_cb), post);

	post->priv->extra = pk_extra_new ();
	pk_extra_set_access (post->priv->extra, PK_EXTRA_ACCESS_WRITE_ONLY);

	/* use the default location */
	ret = pk_extra_set_database (post->priv->extra, NULL);
	if (!ret)
		egg_warning ("Could not open extra database");
}

/**
 * pk_post_trans_new:
 * Return value: A new post_trans class instance.
 **/
PkPostTrans *
pk_post_trans_new (void)
{
	PkPostTrans *post;
	post = g_object_new (PK_TYPE_POST_TRANS, NULL);
	return PK_POST_TRANS (post);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_post_trans (EggTest *test)
{
	PkPostTrans *post;

	if (!egg_test_start (test, "PkPostTrans"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	post = pk_post_trans_new ();
	egg_test_assert (test, post != NULL);

	g_object_unref (post);

	egg_test_end (test);
}
#endif

