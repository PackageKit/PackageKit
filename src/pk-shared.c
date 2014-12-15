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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-common
 * @short_description: Common utility functions for PackageKit
 *
 * This file contains functions that may be useful.
 */

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "pk-cleanup.h"
#include "pk-shared.h"

#ifdef linux
  #include <sys/syscall.h>
#endif

#ifdef PK_BUILD_DAEMON
  #include "pk-resources.h"
#endif

/**
 * pk_is_thread_default_real:
 **/
gboolean
pk_is_thread_default_real (const gchar *strloc, const gchar *strfunc)
{
	static gpointer main_thread = NULL;

	/* first run */
	if (main_thread == NULL) {
		main_thread = g_thread_self ();
		return TRUE;
	}

	/* check we're on the main thread */
	if (main_thread != g_thread_self ()) {
		g_warning ("%s [%s] called from non-main thread", strfunc, strloc);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_directory_remove_contents:
 *
 * Does not remove the directory itself, only the contents.
 **/
gboolean
pk_directory_remove_contents (const gchar *directory)
{
	gboolean ret = FALSE;
	const gchar *filename;
	gint retval;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_dir_close_ GDir *dir = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		goto out;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		_cleanup_free_ gchar *src = NULL;
		src = g_build_filename (directory, filename, NULL);
		ret = g_file_test (src, G_FILE_TEST_IS_DIR);
		if (ret) {
			g_debug ("directory %s found in %s, deleting", filename, directory);
			/* recurse, but should be only 1 level deep */
			pk_directory_remove_contents (src);
			retval = g_remove (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		} else {
			g_debug ("file found in %s, deleting", directory);
			retval = g_unlink (src);
			if (retval != 0)
				g_warning ("failed to delete %s", src);
		}
	}
	ret = TRUE;
out:
	return ret;
}

/**
 * pk_load_introspection:
 **/
GDBusNodeInfo *
pk_load_introspection (const gchar *filename, GError **error)
{
#ifdef PK_BUILD_DAEMON
	_cleanup_bytes_unref_ GBytes *data = NULL;
	_cleanup_free_ gchar *path = NULL;

	/* lookup data */
	path = g_build_filename ("/org/freedesktop/PackageKit", filename, NULL);
	data = g_resource_lookup_data (pk_get_resource (),
				       path,
				       G_RESOURCE_LOOKUP_FLAGS_NONE,
				       error);
	if (data == NULL)
		return NULL;

	/* build introspection from XML */
	return g_dbus_node_info_new_for_xml (g_bytes_get_data (data, NULL), error);
#else
	return NULL;
#endif
}

/**
 * pk_strtoint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtoint (const gchar *text, gint *value)
{
	gchar *endptr = NULL;
	gint64 value_raw;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	value_raw = g_ascii_strtoll (text, &endptr, 10);

	/* parsing error */
	if (endptr == text)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXINT || value_raw < G_MININT)
		return FALSE;

	/* cast back down to value */
	*value = (gint) value_raw;
	return TRUE;
}

/**
 * pk_strtouint64:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtouint64 (const gchar *text, guint64 *value)
{
	gchar *endptr = NULL;

	/* invalid */
	if (text == NULL)
		return FALSE;

	/* parse */
	*value = g_ascii_strtoull (text, &endptr, 10);
	if (endptr == text)
		return FALSE;

	return TRUE;
}

/**
 * pk_strtouint:
 * @text: The text the convert
 * @value: The return numeric return value
 *
 * Converts a string into a unsigned integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strtouint (const gchar *text, guint *value)
{
	gboolean ret;
	guint64 value_raw;

	ret = pk_strtouint64 (text, &value_raw);
	if (!ret)
		return FALSE;

	/* out of range */
	if (value_raw > G_MAXUINT)
		return FALSE;

	/* cast back down to value */
	*value = (guint) value_raw;
	return TRUE;
}

/**
 * pk_strzero:
 * @text: The text to check
 *
 * This function is a much safer way of doing "if (strlen (text) == 0))"
 * as it does not rely on text being NULL terminated. It's also much
 * quicker as it only checks the first byte rather than scanning the whole
 * string just to verify it's not zero length.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
gboolean
pk_strzero (const gchar *text)
{
	if (text == NULL)
		return TRUE;
	if (text[0] == '\0')
		return TRUE;
	return FALSE;
}

/**
 * pk_strlen:
 * @text: The text to check
 * @len: The maximum length of the string
 *
 * This function is a much safer way of doing strlen as it checks for NULL and
 * a stupidly long string.
 *
 * Return value: the length of the string, or len if the string is too long.
 **/
guint
pk_strlen (const gchar *text, guint len)
{
	guint i;

	/* common case */
	if (text == NULL || text[0] == '\0')
		return 0;

	/* only count up to len */
	for (i = 1; i < len; i++) {
		if (text[i] == '\0')
			break;
	}
	return i;
}

/**
 * pk_util_get_config_filename:
 **/
gchar *
pk_util_get_config_filename (void)
{
	gchar *path;

#if PK_BUILD_LOCAL
	/* try a local path first */
	path = g_build_filename ("..", "etc", "PackageKit.conf", NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS))
		goto out;
	g_debug ("local config file not found '%s'", path);
	g_free (path);
#endif
	/* check the prefix path */
	path = g_build_filename (SYSCONFDIR, "PackageKit", "PackageKit.conf", NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS))
		goto out;

	/* none found! */
	g_warning ("config file not found '%s'", path);
	g_free (path);
	path = NULL;
out:
	return path;
}

/**
 * pk_util_sort_backends_cb:
 **/
static gint
pk_util_sort_backends_cb (const gchar **name1, const gchar **name2)
{
	return g_strcmp0 (*name2, *name1);
}

/**
 * pk_util_set_auto_backend:
 **/
gboolean
pk_util_set_auto_backend (GKeyFile *conf, GError **error)
{
	const gchar *tmp;
	gchar *name_tmp;
	_cleanup_dir_close_ GDir *dir = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	dir = g_dir_open (LIBDIR "/packagekit-backend", 0, error);
	if (dir == NULL)
		return FALSE;
	array = g_ptr_array_new_with_free_func (g_free);
	do {
		tmp = g_dir_read_name (dir);
		if (tmp == NULL)
			break;
		if (!g_str_has_prefix (tmp, "libpk_backend_"))
			continue;
		if (!g_str_has_suffix (tmp, G_MODULE_SUFFIX))
			continue;
		if (g_strstr_len (tmp, -1, "libpk_backend_dummy"))
			continue;
		if (g_strstr_len (tmp, -1, "libpk_backend_test"))
			continue;

		/* turn 'libpk_backend_test.so' into 'test' */
		name_tmp = g_strdup (tmp + 14);
		g_strdelimit (name_tmp, ".", '\0');
		g_ptr_array_add (array,
				 name_tmp);
	} while (1);

	/* need to sort by id predictably */
	g_ptr_array_sort (array,
			  (GCompareFunc) pk_util_sort_backends_cb);

	/* set best backend */
	if (array->len == 0) {
		g_set_error_literal (error, 1, 0, "No backends found");
		return FALSE;
	}
	tmp = g_ptr_array_index (array, 0);
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", tmp);
	return TRUE;
}

/**
 * pk_ioprio_set_idle:
 *
 * Set the IO priority to idle
 **/
gboolean
pk_ioprio_set_idle (GPid pid)
{
#if defined(PK_BUILD_DAEMON) && defined(linux)
	enum {
		IOPRIO_CLASS_NONE,
		IOPRIO_CLASS_RT,
		IOPRIO_CLASS_BE,
		IOPRIO_CLASS_IDLE
	};

	enum {
		IOPRIO_WHO_PROCESS = 1,
		IOPRIO_WHO_PGRP,
		IOPRIO_WHO_USER
	};
	#define IOPRIO_CLASS_SHIFT	13
	gint prio = 7;
	gint class = IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT;
	/* FIXME: glibc should have this function */
	return syscall (SYS_ioprio_set, IOPRIO_WHO_PROCESS, pid, prio | class) == 0;
#else
	return TRUE;
#endif
}
