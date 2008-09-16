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
 * SECTION:pk-package-list
 * @short_description: A list of Package data needed for an offline cache
 *
 * These provide a way to query and store a list of packages.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-common.h"
#include "pk-package-id.h"
#include "pk-package-obj.h"
#include "pk-package-list.h"

static void     pk_package_list_class_init	(PkPackageListClass *klass);
static void     pk_package_list_init		(PkPackageList      *plist);
static void     pk_package_list_finalize	(GObject            *object);

#define PK_PACKAGE_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE_LIST, PkPackageListPrivate))

/**
 * PkPackageListPrivate:
 *
 * Private #PkPackageList data
 **/
struct _PkPackageListPrivate
{
	GPtrArray	*array;
};

G_DEFINE_TYPE (PkPackageList, pk_package_list, G_TYPE_OBJECT)

/**
 * pk_package_list_add:
 **/
gboolean
pk_package_list_add (PkPackageList *plist, PkInfoEnum info, const PkPackageId *ident, const gchar *summary)
{
	PkPackageObj *obj;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (ident != NULL, FALSE);

	obj = pk_package_obj_new (info, ident, summary);
	g_ptr_array_add (plist->priv->array, obj);

	return TRUE;
}

/**
 * pk_package_list_add_obj:
 *
 * Makes a deep copy, and adds to the array
 **/
gboolean
pk_package_list_add_obj (PkPackageList *plist, const PkPackageObj *obj)
{
	PkPackageObj *obj_new;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);

	obj_new = pk_package_obj_copy (obj);
	g_ptr_array_add (plist->priv->array, obj_new);

	return TRUE;
}

/**
 * pk_package_list_add_list:
 *
 * Makes a deep copy of the list
 **/
gboolean
pk_package_list_add_list (PkPackageList *plist, PkPackageList *list)
{
	guint i;
	guint len;
	const PkPackageObj *obj;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (list), FALSE);

	/* add list to plist */
	len = pk_package_list_get_size (list);
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		pk_package_list_add_obj (plist, obj);
	}
	return TRUE;
}

/**
 * pk_package_list_to_string:
 **/
gchar *
pk_package_list_to_string (PkPackageList *plist)
{
	PkPackageObj *obj;
	guint i;
	guint length;
	const gchar *info_text;
	GString *package_cache;
	gchar *package_id;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), NULL);

	package_cache = g_string_new ("");
	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj = g_ptr_array_index (plist->priv->array, i);
		info_text = pk_info_enum_to_text (obj->info);
		package_id = pk_package_id_to_string (obj->id);
		g_string_append_printf (package_cache, "%s\t%s\t%s\n", info_text, package_id, obj->summary);
		g_free (package_id);
	}

	/* remove trailing newline */
	if (package_cache->len != 0) {
		g_string_set_size (package_cache, package_cache->len-1);
	}

	return g_string_free (package_cache, FALSE);
}

/**
 * pk_package_list_to_argv:
 **/
gchar **
pk_package_list_to_argv (PkPackageList *plist)
{
	PkPackageObj *obj;
	GPtrArray *array;
	gchar **package_ids;
	gchar *package_id;
	guint length;
	guint i;

	array = g_ptr_array_new ();
	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj = g_ptr_array_index (plist->priv->array, i);
		package_id = pk_package_id_to_string (obj->id);
		g_ptr_array_add (array, g_strdup (package_id));
		g_free (package_id);
	}

	/* convert to argv */
	package_ids = pk_ptr_array_to_strv (array);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	return package_ids;
}

/**
 * pk_package_list_get_size:
 **/
guint
pk_package_list_get_size (PkPackageList *plist)
{
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), 0);
	return plist->priv->array->len;
}

/**
 * pk_package_list_sort_compare_package_id_func:
 **/
static gint
pk_package_list_sort_compare_package_id_func (PkPackageObj **a, PkPackageObj **b)
{
	guint ret;
	gchar *a_text = pk_package_id_to_string ((*a)->id);
	gchar *b_text = pk_package_id_to_string ((*b)->id);
	ret = strcmp (a_text, b_text);
	g_free (a_text);
	g_free (b_text);
	return ret;
}

/**
 * pk_package_list_sort_compare_summary_func:
 **/
static gint
pk_package_list_sort_compare_summary_func (PkPackageObj **a, PkPackageObj **b)
{
	if ((*a)->summary == NULL && (*b)->summary == NULL) {
		return 0;
	} else if ((*a)->summary == NULL) {
		return -1;
	} else if ((*b)->summary == NULL) {
		return 1;
	}
	return strcmp ((*a)->summary, (*b)->summary);
}

/**
 * pk_package_list_sort_compare_info_func:
 **/
static gint
pk_package_list_sort_compare_info_func (PkPackageObj **a, PkPackageObj **b)
{
	if ((*a)->info == (*b)->info) {
		return 0;
	} else if ((*a)->info > (*b)->info) {
		return -1;
	}
	return 1;
}

/**
 * pk_package_list_sort:
 *
 * Sorts by package_id
 **/
gboolean
pk_package_list_sort (PkPackageList *plist)
{
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_ptr_array_sort (plist->priv->array, (GCompareFunc) pk_package_list_sort_compare_package_id_func);
	return TRUE;
}

/**
 * pk_package_list_sort_summary:
 *
 * Sorts by summary
 **/
gboolean
pk_package_list_sort_summary (PkPackageList *plist)
{
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_ptr_array_sort (plist->priv->array, (GCompareFunc) pk_package_list_sort_compare_summary_func);
	return TRUE;
}

/**
 * pk_package_list_sort_info:
 *
 * Sorts by PkInfoEnum
 **/
gboolean
pk_package_list_sort_info (PkPackageList *plist)
{
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_ptr_array_sort (plist->priv->array, (GCompareFunc) pk_package_list_sort_compare_info_func);
	return TRUE;
}

/**
 * pk_package_list_get_obj:
 **/
const PkPackageObj *
pk_package_list_get_obj (PkPackageList *plist, guint item)
{
	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), NULL);
	if (item >= plist->priv->array->len) {
		egg_warning ("item too large!");
		return NULL;
	}
	return g_ptr_array_index (plist->priv->array, item);
}

/**
 * pk_package_list_clear:
 **/
gboolean
pk_package_list_clear (PkPackageList *plist)
{
	PkPackageObj *obj;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);

	while (plist->priv->array->len > 0) {
		obj = g_ptr_array_index (plist->priv->array, 0);
		pk_package_obj_free (obj);
		g_ptr_array_remove_index_fast (plist->priv->array, 0);
	}
	return TRUE;
}

/**
 * pk_package_list_contains:
 **/
gboolean
pk_package_list_contains (PkPackageList *plist, const gchar *package_id)
{
	PkPackageObj *obj;
	guint i;
	guint length;
	gboolean ret = FALSE;
	gchar *package_id_temp;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj = g_ptr_array_index (plist->priv->array, i);
		package_id_temp = pk_package_id_to_string (obj->id);
		ret = pk_package_id_equal_strings (package_id_temp, package_id);
		g_free (package_id_temp);
		if (ret) {
			break;
		}
	}
	return ret;
}

/**
 * pk_package_list_remove:
 **/
gboolean
pk_package_list_remove (PkPackageList *plist, const gchar *package_id)
{
	PkPackageObj *obj;
	guint i;
	guint length;
	gboolean ret = FALSE;
	gchar *package_id_temp;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj = g_ptr_array_index (plist->priv->array, i);
		package_id_temp = pk_package_id_to_string (obj->id);
		ret = pk_package_id_equal_strings (package_id_temp, package_id);
		g_free (package_id_temp);
		if (ret) {
			pk_package_obj_free (obj);
			g_ptr_array_remove_index (plist->priv->array, i);
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * pk_package_list_remove_obj:
 **/
gboolean
pk_package_list_remove_obj (PkPackageList *plist, const PkPackageObj *obj)
{
	PkPackageObj *obj_temp;
	guint i;
	guint length;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);

	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj_temp = g_ptr_array_index (plist->priv->array, i);
		if (pk_package_obj_equal (obj_temp, obj)) {
			pk_package_obj_free (obj_temp);
			g_ptr_array_remove_index (plist->priv->array, i);
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * pk_package_list_contains_obj:
 **/
gboolean
pk_package_list_contains_obj (PkPackageList *plist, const PkPackageObj *obj)
{
	PkPackageObj *obj_temp;
	guint i;
	guint length;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (obj != NULL, FALSE);

	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj_temp = g_ptr_array_index (plist->priv->array, i);
		ret = pk_package_obj_equal (obj_temp, obj);
		if (ret) {
			break;
		}
	}
	return ret;
}

/**
 * pk_package_list_to_file:
 **/
gboolean
pk_package_list_to_file (PkPackageList *plist, const gchar *filename)
{
	PkPackageObj *obj;
	guint i;
	guint length;
	gboolean ret;
	gchar *text;
	GString *buffer;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* add each object */
	buffer = g_string_new ("");
	length = plist->priv->array->len;
	for (i=0; i<length; i++) {
		obj = g_ptr_array_index (plist->priv->array, i);
		text = pk_package_obj_to_string (obj);
		g_string_append_printf (buffer, "%s\n", text);
		g_free (text);
	}

	/* write to disk */
	text = g_string_free (buffer, FALSE);
	ret = g_file_set_contents (filename, text, -1, &error);
	if (!ret) {
		egg_warning ("Failed to write to disk: %s", error->message);
		g_error_free (error);
	}
	g_free (text);
	return ret;
}

/**
 * pk_package_list_add_file:
 **/
gboolean
pk_package_list_add_file (PkPackageList *plist, const gchar *filename)
{
	PkPackageObj *obj;
	guint i;
	guint length;
	gboolean ret;
	gchar *text = NULL;
	gchar **split;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE_LIST (plist), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	ret = g_file_get_contents (filename, &text, NULL, &error);
	if (!ret) {
		egg_warning ("Failed to read from disk: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split into lines */
	split = g_strsplit (text, "\n", 0);
	length = g_strv_length (split);
	for (i=0; i<length; i++) {
		/* we get trailing whitespace sometimes */
		if (!egg_strzero (split[i])) {
			obj = pk_package_obj_from_string (split[i]);
			if (obj != NULL) {
				pk_package_list_add_obj (plist, obj);
			}
			pk_package_obj_free (obj);
		}
	}
	g_strfreev (split);
	g_free (text);
out:
	return ret;
}

/**
 * pk_package_list_class_init:
 * @klass: The PkPackageListClass
 **/
static void
pk_package_list_class_init (PkPackageListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_package_list_finalize;
	g_type_class_add_private (klass, sizeof (PkPackageListPrivate));
}

/**
 * pk_package_list_init:
 **/
static void
pk_package_list_init (PkPackageList *plist)
{
	g_return_if_fail (plist != NULL);
	g_return_if_fail (PK_IS_PACKAGE_LIST (plist));

	plist->priv = PK_PACKAGE_LIST_GET_PRIVATE (plist);
	plist->priv->array = g_ptr_array_new ();
}

/**
 * pk_package_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_package_list_finalize (GObject *object)
{
	PkPackageList *plist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_PACKAGE_LIST (object));
	plist = PK_PACKAGE_LIST (object);
	g_return_if_fail (plist->priv != NULL);

	/* removed any cached packages */
	g_ptr_array_foreach (plist->priv->array, (GFunc) pk_package_obj_free, NULL);
	g_ptr_array_free (plist->priv->array, TRUE);

	G_OBJECT_CLASS (pk_package_list_parent_class)->finalize (object);
}

/**
 * pk_package_list_new:
 *
 * Return value: a new PkPackageList object.
 **/
PkPackageList *
pk_package_list_new (void)
{
	PkPackageList *plist;
	plist = g_object_new (PK_TYPE_PACKAGE_LIST, NULL);
	return PK_PACKAGE_LIST (plist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_list_test (EggTest *test)
{
	PkPackageList *plist;
	gchar *text;
	gboolean ret;
	const PkPackageObj *r0;
	const PkPackageObj *r1;
	const PkPackageObj *r2;
	gchar *r0_text;
	gchar *r1_text;
	gchar *r2_text;
	PkPackageId *id;
	guint size;
	gchar **argv;

	if (!egg_test_start (test, "PkPackageList"))
		return;

	/************************************************************/
	egg_test_title (test, "create");
	plist = pk_package_list_new ();
	if (plist != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "make sure size is zero");
	size = pk_package_list_get_size (plist);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size: %i", size);

	/************************************************************/
	egg_test_title (test, "add entry");
	id = pk_package_id_new_from_string ("gnome;1.23;i386;data");
	ret = pk_package_list_add (plist, PK_INFO_ENUM_INSTALLED, id, "GNOME!");
	pk_package_id_free (id);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "make sure size is one");
	size = pk_package_list_get_size (plist);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size: %i", size);

	/************************************************************/
	egg_test_title (test, "make sure argv is correct");
	argv = pk_package_list_to_argv (plist);
	if (argv != NULL &&
	    egg_strequal (argv[0], "gnome;1.23;i386;data") &&
	    argv[1] == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "list: %s", argv[0]);
	g_strfreev (argv);

	/************************************************************/
	egg_test_title (test, "check not exists");
	id = pk_package_id_new_from_string ("gnome;1.23;i386;data");
	ret = pk_package_list_contains (plist, "liferea;1.23;i386;data");
	pk_package_id_free (id);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check exists");
	ret = pk_package_list_contains (plist, "gnome;1.23;i386;data");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check exists different data");
	ret = pk_package_list_contains (plist, "gnome;1.23;i386;fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "add entry");
	text = pk_package_list_to_string (plist);
	if (egg_strequal (text, "installed\tgnome;1.23;i386;data\tGNOME!"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "get string incorrect '%s'", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "add entry with NULL summary");
	id = pk_package_id_new_from_string ("nosummary;1.23;i386;data");
	ret = pk_package_list_add (plist, PK_INFO_ENUM_INSTALLED, id, NULL);
	pk_package_id_free (id);
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "could not add NULL summary");
	}
	g_object_unref (plist);

	plist = pk_package_list_new ();

	/************************************************************/
	egg_test_title (test, "add entries");
	id = pk_package_id_new_from_string ("def;1.23;i386;data");
	ret = pk_package_list_add (plist, PK_INFO_ENUM_SECURITY, id, "zed");
	pk_package_id_free (id);
	if (!ret)
		egg_test_failed (test, NULL);
	id = pk_package_id_new_from_string ("abc;1.23;i386;data");
	ret = pk_package_list_add (plist, PK_INFO_ENUM_BUGFIX, id, "fed");
	pk_package_id_free (id);
	if (!ret)
		egg_test_failed (test, NULL);
	id = pk_package_id_new_from_string ("ghi;1.23;i386;data");
	ret = pk_package_list_add (plist, PK_INFO_ENUM_ENHANCEMENT, id, "aed");
	pk_package_id_free (id);
	if (!ret)
		egg_test_failed (test, NULL);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "sort by package_id");
	ret = pk_package_list_sort (plist);
	r0 = pk_package_list_get_obj (plist, 0);
	r1 = pk_package_list_get_obj (plist, 1);
	r2 = pk_package_list_get_obj (plist, 2);
	r0_text = pk_package_id_to_string (r0->id);
	r1_text = pk_package_id_to_string (r1->id);
	r2_text = pk_package_id_to_string (r2->id);
	if (egg_strequal (r0_text, "abc;1.23;i386;data") &&
	    egg_strequal (r1_text, "def;1.23;i386;data") &&
	    egg_strequal (r2_text, "ghi;1.23;i386;data"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "could not sort: %s,%s,%s", r0_text, r1_text, r2_text);
	g_free (r0_text);
	g_free (r1_text);
	g_free (r2_text);

	/************************************************************/
	egg_test_title (test, "sort by summary");
	ret = pk_package_list_sort_summary (plist);
	r0 = pk_package_list_get_obj (plist, 0);
	r1 = pk_package_list_get_obj (plist, 1);
	r2 = pk_package_list_get_obj (plist, 2);
	if (egg_strequal (r0->summary, "aed") &&
	    egg_strequal (r1->summary, "fed") &&
	    egg_strequal (r2->summary, "zed"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "could not sort: %s,%s,%s", r0->summary, r1->summary, r2->summary);
	}

	/************************************************************/
	egg_test_title (test, "sort by severity");
	ret = pk_package_list_sort_info (plist);
	r0 = pk_package_list_get_obj (plist, 0);
	r1 = pk_package_list_get_obj (plist, 1);
	r2 = pk_package_list_get_obj (plist, 2);
	if (r0->info == PK_INFO_ENUM_SECURITY &&
	    r1->info == PK_INFO_ENUM_BUGFIX &&
	    r2->info == PK_INFO_ENUM_ENHANCEMENT)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "could not sort: %s,%s,%s", r0->summary, r1->summary, r2->summary);
	}

	g_object_unref (plist);

	plist = pk_package_list_new ();
	id = pk_package_id_new_from_string ("def;1.23;i386;data");
	pk_package_list_add (plist, PK_INFO_ENUM_SECURITY, id, "zed");
	pk_package_id_free (id);
	id = pk_package_id_new_from_string ("abc;1.23;i386;data");
	pk_package_list_add (plist, PK_INFO_ENUM_SECURITY, id, "fed");
	pk_package_id_free (id);
	id = pk_package_id_new_from_string ("ghi;1.23;i386;data");
	pk_package_list_add (plist, PK_INFO_ENUM_BUGFIX, id, "aed");
	pk_package_id_free (id);
	id = pk_package_id_new_from_string ("jkl;1.23;i386;data");
	pk_package_list_add (plist, PK_INFO_ENUM_BUGFIX, id, "med");
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "sort by package_id then priority (should not mess up previous sort)");
	pk_package_list_sort (plist);
	pk_package_list_sort_info (plist);
	r0 = pk_package_list_get_obj (plist, 0);
	r1 = pk_package_list_get_obj (plist, 1);
	r2 = pk_package_list_get_obj (plist, 2);
	r0_text = pk_package_id_to_string (r0->id);
	r1_text = pk_package_id_to_string (r1->id);
	r2_text = pk_package_id_to_string (r2->id);
	if (egg_strequal (r0_text, "abc;1.23;i386;data") &&
	    egg_strequal (r1_text, "def;1.23;i386;data") &&
	    egg_strequal (r2_text, "ghi;1.23;i386;data"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "could not sort: %s,%s,%s", r0_text, r1_text, r2_text);
	g_free (r0_text);
	g_free (r1_text);
	g_free (r2_text);

	g_object_unref (plist);

	egg_test_end (test);
}
#endif

