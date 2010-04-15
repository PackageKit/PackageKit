/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-changeset
 * @short_description: Generic object to represent some information about a changeset.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "egg-debug.h"

#include "zif-changeset.h"

#define ZIF_CHANGESET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_CHANGESET, ZifChangesetPrivate))

struct _ZifChangesetPrivate
{
	guint64			 date;
	gchar			*author;
	gchar			*description;
	gchar			*version;
};

enum {
	PROP_0,
	PROP_DATE,
	PROP_AUTHOR,
	PROP_DESCRIPTION,
	PROP_VERSION,
	PROP_LAST
};

G_DEFINE_TYPE (ZifChangeset, zif_changeset, G_TYPE_OBJECT)

/**
 * zif_changeset_get_date:
 * @changeset: the #ZifChangeset object
 *
 * Gets the date and date of the update.
 *
 * Return value: the date of the update, or 0 for unset.
 *
 * Since: 0.0.1
 **/
guint64
zif_changeset_get_date (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), 0);
	return changeset->priv->date;
}

/**
 * zif_changeset_get_author:
 * @changeset: the #ZifChangeset object
 *
 * Gets the author for this changeset.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.0.1
 **/
const gchar *
zif_changeset_get_author (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->author;
}

/**
 * zif_changeset_get_description:
 * @changeset: the #ZifChangeset object
 *
 * Gets the description for this changeset.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.0.1
 **/
const gchar *
zif_changeset_get_description (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->description;
}

/**
 * zif_changeset_get_version:
 * @changeset: the #ZifChangeset object
 *
 * Gets the date this changeset was version.
 *
 * Return value: A string value, or %NULL.
 *
 * Since: 0.0.1
 **/
const gchar *
zif_changeset_get_version (ZifChangeset *changeset)
{
	g_return_val_if_fail (ZIF_IS_CHANGESET (changeset), NULL);
	return changeset->priv->version;
}

/**
 * zif_changeset_set_date:
 * @changeset: the #ZifChangeset object
 * @date: The date of the changeset
 *
 * Sets the changeset date status.
 *
 * Since: 0.0.1
 **/
void
zif_changeset_set_date (ZifChangeset *changeset, guint64 date)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	changeset->priv->date = date;
}

/**
 * zif_changeset_set_author:
 * @changeset: the #ZifChangeset object
 * @author: the changeset author
 *
 * Sets the changeset author.
 *
 * Since: 0.0.1
 **/
void
zif_changeset_set_author (ZifChangeset *changeset, const gchar *author)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (author != NULL);
	g_return_if_fail (changeset->priv->author == NULL);

	changeset->priv->author = g_strdup (author);
}

/**
 * zif_changeset_set_description:
 * @changeset: the #ZifChangeset object
 * @description: the changeset description
 *
 * Sets the changeset description.
 *
 * Since: 0.0.1
 **/
void
zif_changeset_set_description (ZifChangeset *changeset, const gchar *description)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (description != NULL);
	g_return_if_fail (changeset->priv->description == NULL);

	changeset->priv->description = g_strdup (description);
}

/**
 * zif_changeset_set_version:
 * @changeset: the #ZifChangeset object
 * @version: the changeset version date
 *
 * Sets the date the changeset was version.
 *
 * Since: 0.0.1
 **/
void
zif_changeset_set_version (ZifChangeset *changeset, const gchar *version)
{
	g_return_if_fail (ZIF_IS_CHANGESET (changeset));
	g_return_if_fail (version != NULL);
	g_return_if_fail (changeset->priv->version == NULL);

	changeset->priv->version = g_strdup (version);
}

/**
 * zif_changeset_parse_header:
 * @changeset: the #ZifChangeset object
 * @header: the package header, e.g "Ania Hughes <ahughes@redhat.com> - 2.29.91-1.fc13"
 *
 * Sets the author and version from the package header.
 *
 * Return value: %TRUE if the data was parsed correctly
 *
 * Since: 0.0.1
 **/
gboolean
zif_changeset_parse_header (ZifChangeset *changeset, const gchar *header, GError **error)
{
	gboolean ret = FALSE;
	gchar *temp = NULL;
	gchar *found;
	guint len;

	/* check if there is a version field */
	len = strlen (header);
	if (header[len-1] == '>') {
		zif_changeset_set_author (changeset, header);
		ret = TRUE;
		goto out;
	}

	/* operate on copy */
	temp = g_strdup (header);

	/* get last space */
	found = g_strrstr (temp, " ");
	if (found == NULL) {
		g_set_error (error, 1, 0, "format invalid: %s", header);
		goto out;
	}

	/* set version */
	zif_changeset_set_version (changeset, found + 1);

	/* trim to first non-space or '-' char */
	for (;found != temp;found--) {
		if (*found != ' ' && *found != '-')
			break;
	}

	/* terminate here */
	found[1] = '\0';

	/* set author */
	zif_changeset_set_author (changeset, temp);

	/* success */
	ret = TRUE;
out:
	g_free (temp);
	return ret;
}

/**
 * zif_changeset_get_property:
 **/
static void
zif_changeset_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ZifChangeset *changeset = ZIF_CHANGESET (object);
	ZifChangesetPrivate *priv = changeset->priv;

	switch (prop_id) {
	case PROP_DATE:
		g_value_set_uint64 (value, priv->date);
		break;
	case PROP_AUTHOR:
		g_value_set_string (value, priv->author);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_VERSION:
		g_value_set_string (value, priv->version);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * zif_changeset_set_property:
 **/
static void
zif_changeset_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

/**
 * zif_changeset_finalize:
 **/
static void
zif_changeset_finalize (GObject *object)
{
	ZifChangeset *changeset;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_CHANGESET (object));
	changeset = ZIF_CHANGESET (object);

	g_free (changeset->priv->author);
	g_free (changeset->priv->description);
	g_free (changeset->priv->version);

	G_OBJECT_CLASS (zif_changeset_parent_class)->finalize (object);
}

/**
 * zif_changeset_class_init:
 **/
static void
zif_changeset_class_init (ZifChangesetClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_changeset_finalize;
	object_class->get_property = zif_changeset_get_property;
	object_class->set_property = zif_changeset_set_property;

	/**
	 * ZifChangeset:date:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_uint64 ("date", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DATE, pspec);

	/**
	 * ZifChangeset:author:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_string ("author", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_AUTHOR, pspec);

	/**
	 * ZifChangeset:description:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * ZifChangeset:version:
	 *
	 * Since: 0.0.1
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	g_type_class_add_private (klass, sizeof (ZifChangesetPrivate));
}

/**
 * zif_changeset_init:
 **/
static void
zif_changeset_init (ZifChangeset *changeset)
{
	changeset->priv = ZIF_CHANGESET_GET_PRIVATE (changeset);
	changeset->priv->date = 0;
	changeset->priv->author = NULL;
	changeset->priv->description = NULL;
	changeset->priv->version = NULL;
}

/**
 * zif_changeset_new:
 *
 * Return value: A new #ZifChangeset class instance.
 *
 * Since: 0.0.1
 **/
ZifChangeset *
zif_changeset_new (void)
{
	ZifChangeset *changeset;
	changeset = g_object_new (ZIF_TYPE_CHANGESET, NULL);
	return ZIF_CHANGESET (changeset);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_changeset_test (EggTest *test)
{
	gboolean ret;
	const gchar *temp;
	ZifChangeset *changeset;

	if (!egg_test_start (test, "ZifChangeset"))
		return;

	/************************************************************/
	egg_test_title (test, "get changeset");
	changeset = zif_changeset_new ();
	zif_changeset_set_description (changeset, "Update to latest stable version");
	egg_test_assert (test, changeset != NULL);

	/************************************************************/
	egg_test_title (test, "parse header 0 (fail)");
	ret = zif_changeset_parse_header (changeset, "this-is-an-invalid-header", NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "parse header 1 (success)");
	ret = zif_changeset_parse_header (changeset, "Milan Crha <mcrha@redhat.com> - 2.29.91-1.fc13", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check description");
	temp = zif_changeset_get_description (changeset);
	if (g_strcmp0 (temp, "Update to latest stable version") != 0)
		egg_test_failed (test, "incorrect value, got: %s", temp);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check author");
	temp = zif_changeset_get_author (changeset);
	if (g_strcmp0 (temp, "Milan Crha <mcrha@redhat.com>") != 0)
		egg_test_failed (test, "incorrect value, got: %s", temp);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check version");
	temp = zif_changeset_get_version (changeset);
	if (g_strcmp0 (temp, "2.29.91-1.fc13") != 0)
		egg_test_failed (test, "incorrect value, got: %s", temp);
	egg_test_success (test, NULL);

	/* reset */
	g_object_unref (changeset);
	changeset = zif_changeset_new ();

	/************************************************************/
	egg_test_title (test, "parse header 2 (success)");
	ret = zif_changeset_parse_header (changeset, "Milan Crha <mcrha at redhat.com> 2.29.91-1.fc13", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check author");
	temp = zif_changeset_get_author (changeset);
	if (g_strcmp0 (temp, "Milan Crha <mcrha at redhat.com>") != 0)
		egg_test_failed (test, "incorrect value, got: %s", temp);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check version");
	temp = zif_changeset_get_version (changeset);
	if (g_strcmp0 (temp, "2.29.91-1.fc13") != 0)
		egg_test_failed (test, "incorrect value, got: %s", temp);
	egg_test_success (test, NULL);

	g_object_unref (changeset);

	egg_test_end (test);
}
#endif

