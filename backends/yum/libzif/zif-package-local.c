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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-package-local
 * @short_description: Local package object
 *
 * This object is a subclass of #ZifPackage
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <packagekit-glib2/packagekit.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>

#include "egg-debug.h"

#include "zif-utils.h"
#include "zif-package.h"
#include "zif-package-local.h"
#include "zif-groups.h"
#include "zif-string.h"
#include "zif-depend.h"

#define ZIF_PACKAGE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_PACKAGE_LOCAL, ZifPackageLocalPrivate))

/**
 * ZifPackageLocalPrivate:
 *
 * Private #ZifPackageLocal data
 **/
struct _ZifPackageLocalPrivate
{
	ZifGroups		*groups;
	Header			 header;
};

G_DEFINE_TYPE (ZifPackageLocal, zif_package_local, ZIF_TYPE_PACKAGE)

/**
 * zif_get_header_string:
 **/
static ZifString *
zif_get_header_string (Header header, rpmTag tag)
{
	gint retval;
	ZifString *data = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);

	if (retval != 1)
		goto out;
	data = zif_string_new (rpmtdGetString (td));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * zif_get_header_u32:
 **/
static guint
zif_get_header_u32 (Header header, rpmTag tag)
{
	gint retval;
	uint32_t *data_p;
	guint data = 0;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, RPMTAG_SIZE, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	data_p = rpmtdGetUint32 (td);
	if (data_p != NULL)
		data = *data_p;
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return data;
}

/**
 * zif_get_header_uint32_index:
 **/
static GPtrArray *
zif_get_header_uint32_index (Header header, rpmTag tag, guint length)
{
	gint retval;
	guint32 *data;
	GPtrArray *array = NULL;
	rpmtd td;
	guint i;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	array = g_ptr_array_new ();
	data = rpmtdGetUint32 (td);
	for (i=0;i<length; i++)
		g_ptr_array_add (array, GUINT_TO_POINTER (*(data+i)));
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * zif_get_header_string_array:
 **/
static GPtrArray *
zif_get_header_string_array (Header header, rpmTag tag)
{
	gint retval;
	const gchar *data;
	GPtrArray *array = NULL;
	rpmtd td;

	td = rpmtdNew ();
	retval = headerGet (header, tag, td, HEADERGET_MINMEM);
	if (retval != 1)
		goto out;
	array = g_ptr_array_new_with_free_func (g_free);
	data = rpmtdGetString (td);
	while (data != NULL) {
		g_ptr_array_add (array, g_strdup (data));
		data = rpmtdNextString (td);
	}
out:
	rpmtdFreeData (td);
	rpmtdFree (td);
	return array;
}

/**
 * zif_package_local_id_from_header:
 **/
static gchar *
zif_package_local_id_from_header (Header header)
{
	gchar *package_id;
	const gchar *name = NULL;
	const gchar *epoch = NULL;
	const gchar *version = NULL;
	const gchar *release = NULL;
	const gchar *arch = NULL;
	struct rpmtd_s value;

	/* get NEVRA */
	if (headerGet(header, RPMTAG_NAME, &value, HEADERGET_DEFAULT))
		name = rpmtdGetString (&value);
	if (headerGet(header, RPMTAG_EPOCH, &value, HEADERGET_DEFAULT))
		epoch = rpmtdGetString (&value);
	if (headerGet(header, RPMTAG_VERSION, &value, HEADERGET_DEFAULT))
		version = rpmtdGetString (&value);
	if (headerGet(header, RPMTAG_RELEASE, &value, HEADERGET_DEFAULT))
		release = rpmtdGetString (&value);
	if (headerGet(header, RPMTAG_ARCH, &value, HEADERGET_DEFAULT))
		arch = rpmtdGetString (&value);

	/* trivial */
	if (epoch == NULL) {
		package_id = zif_package_id_from_nevra (name, NULL, version, release, arch, "installed");
		goto out;
	}

	/* with epoch */
	package_id = zif_package_id_from_nevra (name, epoch, version, release, arch, "installed");
out:
	return package_id;
}

/**
 * zif_package_local_get_depends_from_name_flags_version:
 **/
static GPtrArray *
zif_package_local_get_depends_from_name_flags_version (GPtrArray *names, GPtrArray *flags, GPtrArray *versions)
{
	guint i;
	rpmsenseFlags rpmflags;
	ZifDepend *depend;
	ZifDependFlag flag;
	const gchar *name;
	const gchar *version;
	GPtrArray *array;

	/* create requires */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_depend_unref);
	for (i=0; i<names->len; i++) {
		name = g_ptr_array_index (names, i);
		version = g_ptr_array_index (versions, i);

		/* no version string */
		if (version == NULL || version[0] == '\0') {
			depend = zif_depend_new (name, ZIF_DEPEND_FLAG_ANY, NULL);
			g_ptr_array_add (array, depend);
			continue;
		}

		/* ignore rpmlib flags */
		rpmflags = GPOINTER_TO_UINT (g_ptr_array_index (flags, i));
		if ((rpmflags & RPMSENSE_RPMLIB) > 0)
			continue;

		/* convert to enums */
		flag = ZIF_DEPEND_FLAG_UNKNOWN;
		if ((rpmflags & RPMSENSE_LESS) > 0) {
			flag = ZIF_DEPEND_FLAG_LESS;
		} else if ((rpmflags & RPMSENSE_GREATER) > 0) {
			flag = ZIF_DEPEND_FLAG_GREATER;
		} else if ((rpmflags & RPMSENSE_EQUAL) > 0) {
			flag = ZIF_DEPEND_FLAG_EQUAL;
		}

		/* unknown */
		if (flag == ZIF_DEPEND_FLAG_UNKNOWN) {
//			egg_debug ("ignoring %s %s %s", name, zif_depend_flag_to_string (flag), version);
			continue;
		}

		depend = zif_depend_new (name, flag, version);
		g_ptr_array_add (array, depend);
	}
	return array;
}

/*
 * zif_package_local_ensure_data:
 */
static gboolean
zif_package_local_ensure_data (ZifPackage *pkg, ZifPackageEnsureType type, GError **error)
{
	GPtrArray *files;
	GPtrArray *dirnames;
	GPtrArray *basenames;
	GPtrArray *fileindex;
	guint i;
	gchar *filename;
	guint size;
	ZifString *tmp;
	PkGroupEnum group;
//	GPtrArray *tmparray;
	GPtrArray *depends;
//	GPtrArray *provides;
	GPtrArray *flags;
	GPtrArray *names;
	GPtrArray *versions;
	gboolean ret = TRUE;
	Header header = ZIF_PACKAGE_LOCAL(pkg)->priv->header;

	/* eigh? */
	if (header == NULL) {
		g_set_error (error, 1, 0, "no header for %s", zif_package_get_id (pkg));
		ret = FALSE;
		goto out;
	}

	if (type == ZIF_PACKAGE_ENSURE_TYPE_FILES) {
		/* files */
		basenames = zif_get_header_string_array (header, RPMTAG_BASENAMES);

		/* create the files */
		if (basenames != NULL) {

			/* get the mapping */
			dirnames = zif_get_header_string_array (header, RPMTAG_DIRNAMES);
			fileindex = zif_get_header_uint32_index (header, RPMTAG_DIRINDEXES, basenames->len);
			if (basenames->len != fileindex->len)
				egg_error ("internal error, basenames length is not the same as index length, possibly corrupt db?");
			if (fileindex->len > fileindex->len)
				egg_error ("internal error, fileindex length is bigger than index length, possibly corrupt db?");

			files = g_ptr_array_new_with_free_func (g_free);
			for (i=0; i<basenames->len-2 /* why -1? I'm not sure */; i++) {
				guint idx;
				idx = GPOINTER_TO_UINT (g_ptr_array_index (fileindex, i));
				if (idx > dirnames->len) {
					egg_warning ("index bigger than dirnames (%i > %i) for package %s [%s], i=%i, dn=%i, bn=%i, fi=%i",
						     idx, dirnames->len, zif_package_get_package_id (pkg),
						     (const gchar *) g_ptr_array_index (basenames, i),
						     i, dirnames->len, basenames->len, fileindex->len);
					continue;
				}
				filename = g_strconcat (g_ptr_array_index (dirnames, idx), g_ptr_array_index (basenames, i), NULL);
				g_ptr_array_add (files, filename);
			}
			zif_package_set_files (pkg, files);
			g_ptr_array_unref (files);

			/* free, as we have files */
			g_ptr_array_unref (dirnames);
			g_ptr_array_unref (basenames);
			g_ptr_array_unref (fileindex);
		} else {
			files = g_ptr_array_new_with_free_func (g_free);
			zif_package_set_files (pkg, files);
			g_ptr_array_unref (files);
		}
	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_SUMMARY) {
		/* summary */
		tmp = zif_get_header_string (header, RPMTAG_SUMMARY);
		zif_package_set_summary (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_LICENCE) {
		/* license */
		tmp = zif_get_header_string (header, RPMTAG_LICENSE);
		zif_package_set_license (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_DESCRIPTION) {
		/* description */
		tmp = zif_get_header_string (header, RPMTAG_DESCRIPTION);
		if (tmp == NULL) {
			egg_warning ("no description for %s", zif_package_get_id (pkg));
			tmp = zif_string_new ("");
		}
		zif_package_set_description (pkg, tmp);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_URL) {
		/* url */
		tmp = zif_get_header_string (header, RPMTAG_URL);
		if (tmp != NULL) {
			zif_package_set_url (pkg, tmp);
			zif_string_unref (tmp);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_SIZE) {
		/* size */
		size = zif_get_header_u32 (header, RPMTAG_SIZE);
		if (size != 0)
			zif_package_set_size (pkg, size);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_GROUP) {
		/* category && group */
		tmp = zif_get_header_string (header, RPMTAG_GROUP);
		zif_package_set_category (pkg, tmp);
		group = zif_groups_get_group_for_cat (ZIF_PACKAGE_LOCAL (pkg)->priv->groups, zif_string_get_value (tmp), NULL);
		if (group != PK_GROUP_ENUM_UNKNOWN)
			zif_package_set_group (pkg, group);
		zif_string_unref (tmp);

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_REQUIRES) {
		/* requires */
		names = zif_get_header_string_array (header, RPMTAG_REQUIRENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_depend_unref);
			zif_package_set_requires (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_REQUIREVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_REQUIREFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_requires (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_PROVIDES) {
		/* provides */
		names = zif_get_header_string_array (header, RPMTAG_PROVIDENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_depend_unref);
			zif_package_set_provides (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_PROVIDEVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_PROVIDEFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			zif_package_set_provides (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_CONFLICTS) {
		/* conflicts */
		names = zif_get_header_string_array (header, RPMTAG_CONFLICTNAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_depend_unref);
			//zif_package_set_conflicts (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_CONFLICTVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_CONFLICTFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			//zif_package_set_conflicts (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}

	} else if (type == ZIF_PACKAGE_ENSURE_TYPE_OBSOLETES) {
		/* obsoletes */
		names = zif_get_header_string_array (header, RPMTAG_OBSOLETENAME);
		if (names == NULL) {
			depends = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_depend_unref);
			//zif_package_set_obsoletes (pkg, depends);
			g_ptr_array_unref (depends);
		} else {
			versions = zif_get_header_string_array (header, RPMTAG_OBSOLETEVERSION);
			flags = zif_get_header_uint32_index (header, RPMTAG_OBSOLETEFLAGS, names->len);
			depends = zif_package_local_get_depends_from_name_flags_version (names, flags, versions);
			//zif_package_set_obsoletes (pkg, depends);
			g_ptr_array_unref (depends);
			g_ptr_array_unref (names);
			g_ptr_array_unref (versions);
			g_ptr_array_unref (flags);
		}
	}
out:
	return ret;
}

/**
 * zif_package_local_set_from_header:
 * @pkg: the #ZifPackageLocal object
 * @header: a rpm #Header structure
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets the local package from an RPM header object.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_package_local_set_from_header (ZifPackageLocal *pkg, Header header, GError **error)
{
	gchar *package_id;

	g_return_val_if_fail (ZIF_IS_PACKAGE_LOCAL (pkg), FALSE);
	g_return_val_if_fail (header != NULL, FALSE);

	zif_package_set_installed (ZIF_PACKAGE (pkg), TRUE);

	/* save header so we can read when required */
	pkg->priv->header = headerLink (header);

	/* id */
	package_id = zif_package_local_id_from_header (header);
	zif_package_set_id (ZIF_PACKAGE (pkg), package_id);
	g_free (package_id);

	return TRUE;
}

/**
 * zif_package_local_rpmrc_to_string:
 **/
static const gchar *
zif_package_local_rpmrc_to_string (rpmRC rc)
{
	if (rc == RPMRC_OK)
		return "Generic success";
	if (rc == RPMRC_NOTFOUND)
		return "Generic not found";
	if (rc == RPMRC_FAIL)
		return "Generic failure";
	if (rc == RPMRC_NOTTRUSTED)
		return "Signature is OK, but key is not trusted";
	if (rc == RPMRC_NOKEY)
		return "Public key is unavailable";
	return "Unknown error";
}

/**
 * zif_package_local_set_from_filename:
 * @pkg: the #ZifPackageLocal object
 * @filename: the local filename
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets a local package object from a local file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_package_local_set_from_filename (ZifPackageLocal *pkg, const gchar *filename, GError **error)
{
	rpmRC rc;
	FD_t fd = NULL;
	Header hdr = NULL;
	rpmts ts;
	gboolean ret = FALSE;
	GError *error_local = NULL;

	/* open the file for reading */
	fd = Fopen(filename, "r.fdio"); 
	if (fd == NULL) {
		g_set_error (error, 1, 0, "failed to open %s", filename);
		goto out;
	}
	if (Ferror(fd)) {
		g_set_error (error, 1, 0, "failed to open %s: %s", filename, Fstrerror(fd));
		goto out;
	}

	/* create an empty transaction set */
	ts = rpmtsCreate ();

	/* we don't want to abort on missing keys */
	rpmtsSetVSFlags (ts, _RPMVSF_NOSIGNATURES);

	/* read in the file */
	rc = rpmReadPackageFile (ts, fd, filename, &hdr);
	if (rc != RPMRC_OK) {
		g_set_error (error, 1, 0, "failed to read %s: %s", filename, zif_package_local_rpmrc_to_string (rc));
		goto out;
	}

	/* convert and upscale */
	headerConvert (hdr, HEADERCONV_RETROFIT_V3);

	/* set from header */
	ret = zif_package_local_set_from_header (pkg, hdr, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set from header: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* close the database used by the transaction */
	rc = rpmtsCloseDB (ts);
	if (rc != RPMRC_OK) {
		g_set_error (error, 1, 0, "failed to close: %s", zif_package_local_rpmrc_to_string (rc));
		ret = FALSE;
		goto out;
	}
out:
	/* close header and file */
	if (hdr != NULL)
		headerFree (hdr);
	if (fd != NULL)
		Fclose (fd);
	return ret;
}

/**
 * zif_package_local_finalize:
 **/
static void
zif_package_local_finalize (GObject *object)
{
	ZifPackageLocal *pkg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_PACKAGE_LOCAL (object));
	pkg = ZIF_PACKAGE_LOCAL (object);

	g_object_unref (pkg->priv->groups);
	if (pkg->priv->header != NULL)
		headerUnlink (pkg->priv->header);

	G_OBJECT_CLASS (zif_package_local_parent_class)->finalize (object);
}

/**
 * zif_package_local_class_init:
 **/
static void
zif_package_local_class_init (ZifPackageLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifPackageClass *package_class = ZIF_PACKAGE_CLASS (klass);
	object_class->finalize = zif_package_local_finalize;

	package_class->ensure_data = zif_package_local_ensure_data;

	g_type_class_add_private (klass, sizeof (ZifPackageLocalPrivate));
}

/**
 * zif_package_local_init:
 **/
static void
zif_package_local_init (ZifPackageLocal *pkg)
{
	pkg->priv = ZIF_PACKAGE_LOCAL_GET_PRIVATE (pkg);
	pkg->priv->groups = zif_groups_new ();
	pkg->priv->header = NULL;
}

/**
 * zif_package_local_new:
 *
 * Return value: A new #ZifPackageLocal class instance.
 **/
ZifPackageLocal *
zif_package_local_new (void)
{
	ZifPackageLocal *pkg;
	pkg = g_object_new (ZIF_TYPE_PACKAGE_LOCAL, NULL);
	return ZIF_PACKAGE_LOCAL (pkg);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_package_local_test (EggTest *test)
{
	ZifPackageLocal *pkg;
	gboolean ret;
	GError *error = NULL;
	gchar *filename;

	if (!egg_test_start (test, "ZifPackageLocal"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_local");
	pkg = zif_package_local_new ();
	egg_test_assert (test, pkg != NULL);

	/************************************************************/
	egg_test_title (test, "load file");
	filename = egg_test_get_data_file ("accountsdialog-0.4.1-1.fc13.i686.rpm");
	ret = zif_package_local_set_from_filename (pkg, filename, &error);
	if (!ret) {
		egg_test_failed (test, "failed to set from filename: %s", error->message);
		g_error_free (error);
	}
	g_free (filename);
	egg_test_success (test, NULL);

	g_object_unref (pkg);

	egg_test_end (test);
}
#endif

