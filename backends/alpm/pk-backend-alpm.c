/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
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

#define _GNU_SOURCE

#define ALPM_CONFIG_PATH "/etc/pacman.conf"

#define ALPM_ROOT "/"
#define ALPM_DBPATH "/var/lib/pacman"
#define ALPM_CACHEDIR "/var/cache/pacman/pkg"
#define ALPM_LOGFILE "/var/log/pacman.log"

#define ALPM_PKG_EXT ".pkg.tar.gz"
#define ALPM_LOCAL_DB_ALIAS "installed"

#define ALPM_PROGRESS_UPDATE_INTERVAL 400

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <egg-debug.h>
#include <packagekit-glib/packagekit.h>

#include <alpm.h>
#include <alpm_list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

PkBackend *backend_instance = NULL;

GHashTable *group_map;

alpm_list_t *syncfirst = NULL;
alpm_list_t *downloaded_files = NULL;
gchar *current_file = NULL;

off_t trans_xfered;
off_t trans_total;

typedef enum {
	PK_ALPM_SEARCH_TYPE_NULL,
	PK_ALPM_SEARCH_TYPE_RESOLVE,
	PK_ALPM_SEARCH_TYPE_NAME,
	PK_ALPM_SEARCH_TYPE_DETAILS,
	PK_ALPM_SEARCH_TYPE_GROUP,
	PK_ALPM_SEARCH_TYPE_PROVIDES
} PkAlpmSearchType;

static gchar *
pkg_to_package_id_str (pmpkg_t *pkg, const gchar *repo)
{
	gchar *arch = (gchar *) alpm_pkg_get_arch (pkg);
	if (arch == NULL)
		arch = (gchar *) "unknown";

	return pk_package_id_build (alpm_pkg_get_name (pkg), alpm_pkg_get_version (pkg), arch, repo);
}

static pmpkg_t *
pkg_from_package_id_str (const gchar *package_id_str)
{
	pmdb_t *repo = NULL;
	pmpkg_t *pkg;
	PkPackageId *pkg_id = pk_package_id_new_from_string (package_id_str);

	/* do all this fancy stuff */
	if (egg_strequal (ALPM_LOCAL_DB_ALIAS, pk_package_id_get_data (pkg_id)))
		repo = alpm_option_get_localdb ();
	else {
		alpm_list_t *iterator;
		for (iterator = alpm_option_get_syncdbs (); iterator; iterator = alpm_list_next (iterator)) {
			repo = alpm_list_getdata (iterator);
			if (egg_strequal (alpm_db_get_name (repo), pk_package_id_get_data (pkg_id)) == 0)
				break;
		}
	}

	if (repo != NULL)
		pkg = alpm_db_get_pkg (repo, pk_package_id_get_name (pkg_id));
	else
		pkg = NULL;

	/* free package id as we no longer need it */
	pk_package_id_free (pkg_id);

	return pkg;
}

static int
pkg_cmp (pmpkg_t *pkg1, pmpkg_t *pkg2) {
	int comparison;
	/* check for no package */
	if (pkg1 == NULL)
		return -1;
	if (pkg2 == NULL)
		return 1;
	/* compare package names */
	comparison = g_strcmp0 (alpm_pkg_get_name (pkg1), alpm_pkg_get_name (pkg2));
	if (comparison != 0)
		return comparison;
	/* compare package versions */
	comparison = alpm_pkg_vercmp (alpm_pkg_get_version (pkg1), alpm_pkg_get_version (pkg2));
	if (comparison != 0)
		return comparison;
	/* packages are equal */
	return 0;
}

/* temporary commented to not produce compilation errors :) */
/*
static gboolean
pkg_equal (pmpkg_t *p1, pmpkg_t *p2)
{
	if (g_strcmp0 (alpm_pkg_get_name (p1), alpm_pkg_get_name (p2)) != 0)
		return FALSE;
	if (g_strcmp0 (alpm_pkg_get_version (p1), alpm_pkg_get_version (p2)) != 0)
		return FALSE;
	return TRUE;
}

static gboolean
pkg_equals_to (pmpkg_t *pkg, const gchar *name, const gchar *version)
{
	if (g_strcmp0 (alpm_pkg_get_name (pkg), name) != 0)
		return FALSE;
	if (version != NULL)
		if (g_strcmp0 (alpm_pkg_get_version (pkg), version) != 0)
			return FALSE;
	return TRUE;
}
*/

static void
cb_trans_evt (pmtransevt_t event, void *data1, void *data2)
{
	// TODO: Add more code here
	gchar **package_ids;
	gchar *package_id_needle;
	gchar *package_id_str;
	GString *package_id_builder;

	switch (event) {
		case PM_TRANS_EVT_REMOVE_START:
			pk_backend_set_allow_cancel (backend_instance, FALSE);

			package_id_str = pkg_to_package_id_str (data1, ALPM_LOCAL_DB_ALIAS);
			pk_backend_package (backend_instance, PK_INFO_ENUM_REMOVING, package_id_str, alpm_pkg_get_desc (data1));
			g_free (package_id_str);
			break;
		case PM_TRANS_EVT_ADD_START:
			pk_backend_set_allow_cancel (backend_instance, FALSE);

			pk_backend_set_status (backend_instance, PK_STATUS_ENUM_INSTALL);
			package_id_needle = pkg_to_package_id_str (data1, "");
			egg_debug ("needle is %s", package_id_needle);
			package_ids = pk_backend_get_strv (backend_instance, "package_ids");

			if (package_ids != NULL) {
				/* search for this package in package_ids */
				unsigned int iterator;
				for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator)
					if (strstr (package_ids[iterator], package_id_needle) != NULL) {
						pk_backend_package (backend_instance, PK_INFO_ENUM_INSTALLING, package_ids[iterator], alpm_pkg_get_desc (data1));
						break;
					}
			} else {
				/* we are installing a local file */
				package_id_builder = g_string_new (package_id_needle);
				g_string_append (package_id_builder, "local");
				package_id_str = g_string_free (package_id_builder, FALSE);
				pk_backend_package (backend_instance, PK_INFO_ENUM_INSTALLING, package_id_str, alpm_pkg_get_desc (data1));
				g_free (package_id_str);
			}

			g_free (package_id_needle);
			break;
		case PM_TRANS_EVT_UPGRADE_START:
			package_id_str = pkg_to_package_id_str (data1, "local");
			pk_backend_package (backend_instance, PK_INFO_ENUM_UPDATING, package_id_str, alpm_pkg_get_desc (data1));
			g_free (package_id_str);
			break;
		default: egg_debug ("alpm: event %i happened", event);
	}
}

static void
cb_trans_conv (pmtransconv_t conv, void *data1, void *data2, void *data3, int *response)
{
	// TODO: check if some code needs to be placed there
}

static void
cb_trans_progress (pmtransprog_t event, const char *pkgname, int percent, int howmany, int current)
{
	if (event == PM_TRANS_PROGRESS_ADD_START || event == PM_TRANS_PROGRESS_UPGRADE_START || event == PM_TRANS_PROGRESS_REMOVE_START) {
		int trans_percent;

		egg_debug ("alpm: transaction percentage for %s is %i", pkgname, percent);
		egg_debug ("alpm: current is %i", current);
		trans_percent = (int) ((float) ((current - 1) * 100 + percent)) / ((float) (howmany * 100)) * 100;
		pk_backend_set_sub_percentage ((PkBackend *) backend_instance, percent);
		pk_backend_set_percentage ((PkBackend *) backend_instance, trans_percent);
	}
}

static void
cb_dl_progress (const char *filename, off_t file_xfered, off_t file_total)
{
	int file_percent;
	int trans_percent;

	if (g_str_has_suffix (filename, ALPM_PKG_EXT)) {
		if (!egg_strequal (filename, current_file)) {
			unsigned int iterator;
			gchar *package_id = NULL;
			gchar **package_ids = pk_backend_get_strv (backend_instance, "package_ids");

			g_free (current_file);
			current_file = g_strdup (filename);

			/* search for this package in package_ids */
			for (iterator = 0; package_id == NULL && iterator < g_strv_length (package_ids); ++iterator) {
				PkPackageId *id = pk_package_id_new_from_string (package_ids[iterator]);
				gchar *pkginfo = g_strjoin ("-", pk_package_id_get_name (id), pk_package_id_get_version (id), NULL);
				if (g_str_has_prefix (filename, pkginfo))
					package_id = package_ids[iterator];
				g_free (pkginfo);
				pk_package_id_free (id);
			}

			/* emit package */
			if (package_id != NULL) {
				pmpkg_t *pkg = pkg_from_package_id_str (package_id);
				pk_backend_package (backend_instance, PK_INFO_ENUM_DOWNLOADING, package_id, alpm_pkg_get_desc (pkg));
			}
		}
	}

	file_percent = (int) ((float) file_xfered) / ((float) file_total) * 100;
	trans_percent = (int) ((float) (trans_xfered + file_xfered)) / ((float) trans_total) * 100;
	pk_backend_set_sub_percentage ((PkBackend *) backend_instance, file_percent);
	pk_backend_set_percentage((PkBackend *) backend_instance, trans_percent);

	if (file_xfered == file_total) {
		downloaded_files = alpm_list_add (downloaded_files, g_strdup (filename));
		trans_xfered = trans_xfered + file_total;
	}
}

static void
cb_dl_total (off_t total)
{
	trans_total = total;
	/* zero total size means that download is finished, so clear trans_xfered */
	if (total == 0)
		trans_xfered = 0;
}

static void
emit_package (PkBackend *backend, pmpkg_t *pkg, const gchar *repo, PkInfoEnum info)
{
	gchar *package_id_str;

	egg_debug ("alpm: emitting package with name %s", alpm_pkg_get_name (pkg));

	package_id_str = pkg_to_package_id_str (pkg, repo);
	pk_backend_package (backend, info, package_id_str, alpm_pkg_get_desc (pkg));
	g_free (package_id_str);
}

/**
 * strtrim:
 * Trim whitespaces and newlines from a string
 */
static char *
strtrim (char *str)
{
	char *pch = str;

	if (str == NULL || *str == '\0')
		/* string is empty, so we're done. */
		return (str);

	while (isspace (*pch))
		pch++;

	if (pch != str)
		memmove (str, pch, (strlen (pch) + 1));

	/* check if there wasn't anything but whitespace in the string. */
	if (*str == '\0')
		return (str);

	pch = (str + (strlen (str) - 1));

	while (isspace (*pch))
		pch--;

	*++pch = '\0';

	return (str);
}

/**
 * _strnadd:
 * Helper function for strreplace
 */
static void
_strnadd(char **str, const char *append, unsigned int count)
{
	if (*str)
		*str = realloc (*str, strlen (*str) + count + 1);
	else
		*str = calloc (sizeof (char), count + 1);

	strncat(*str, append, count);
}

/**
 * strreplace:
 * Replace all occurences of 'needle' with 'replace' in 'str', returning
 * a new string (must be free'd)
 */
static char *
strreplace(const char *str, const char *needle, const char *replace)
{
	const char *p, *q;
	char *newstr = NULL;
	unsigned int needlesz = strlen (needle), replacesz = strlen (replace);

	p = q = str;

	while (1) {
		q = strstr(p, needle);
		if (!q) {
			/* not found */
			if (*p) /* add the rest of 'p' */
				_strnadd(&newstr, p, strlen(p));

			break;
		} else { /* found match */
			if (q > p) /* add chars between this occurance and last occurance, if any */
				_strnadd(&newstr, p, q - p);

			_strnadd(&newstr, replace, replacesz);
			p = q + needlesz;
		}
	}

	return newstr;
}

/**
 * set_repeating_option:
 * Add repeating options such as NoExtract, NoUpgrade, etc to alpm settings.
 * @param ptr a pointer to the start of the multiple options
 * @param option the string (friendly) name of the option, used for messages
 * @param optionfunc a function pointer to an alpm_option_add_* function
 */
static void
set_repeating_option(const char *ptr, const char *option, void (*optionfunc) (const char*))
{
	char *p = (char*) ptr;
	char *q;

	while ((q = strchr (p, ' '))) {
		*q = '\0';
		(*optionfunc) (p);
		egg_debug ("config: %s: %s", option, p);
		p = q;
		p++;
	}
	(*optionfunc) (p);
	egg_debug ("config: %s: %s", option, p);
}

/**
 * option_add_syncfirst:
 * Add package name to SyncFirst list
 * @param name name of the package to be added
 */
static void
option_add_syncfirst(const char *name) {
	syncfirst = alpm_list_add (syncfirst, strdup (name));
}

/**
 * parse_config:
 * Parse config file and set all the needed options
 * Based heavily on the pacman source code
 * @param file full name of config file
 * @param givensection section to start from
 * @param givendb db to start from
 */
static int
parse_config (const char *file, const char *givensection, pmdb_t * const givendb)
{
	FILE *fp = NULL;
	char line[PATH_MAX + 1];
	int linenum = 0;
	char *ptr, *section = NULL;
	pmdb_t *db = NULL;

	/* set default options */
	alpm_option_set_root (ALPM_ROOT);
	alpm_option_set_dbpath (ALPM_DBPATH);
	alpm_option_add_cachedir (ALPM_CACHEDIR);
	alpm_option_set_logfile (ALPM_LOGFILE);

	fp = fopen(file, "r");
	if (fp == NULL) {
		egg_error ("config file %s could not be read", file);
		return (1);
	}

	/* if we are passed a section, use it as our starting point */
	if (givensection != NULL)
		section = strdup (givensection);

	/* if we are passed a db, use it as our starting point */
	if (givendb != NULL)
		db = givendb;

	while (fgets (line, PATH_MAX, fp)) {
		linenum++;
		strtrim (line);

		if (strlen (line) == 0 || line[0] == '#')
			continue;

		if ((ptr = strchr (line, '#')))
			*ptr = '\0';

		if (line[0] == '[' && line[strlen (line) - 1] == ']') {
			/* new config section, skip the '[' */
			ptr = line;
			ptr++;
			if (section)
				free (section);

			section = strdup (ptr);
			section[strlen (section) - 1] = '\0';
			egg_debug ("config: new section '%s'", section);
			if (!strlen (section)) {
				egg_debug ("config file %s, line %d: bad section name", file, linenum);
				return 1;
			}

			/* if we are not looking at the options section, register a db */
			if (!egg_strequal (section, "options"))
				db = alpm_db_register_sync (section);
		} else {
			/* directive */
			char *key;
			key = line;
			ptr = line;
			/* strsep modifies the 'line' string: 'key \0 ptr' */
			strsep (&ptr, "=");
			strtrim (key);
			strtrim (ptr);

			if (key == NULL) {
				egg_error ("config file %s, line %d: syntax error in config file - missing key.", file, linenum);
				return 1;
			}
			if (section == NULL) {
				egg_error ("config file %s, line %d: all directives must belong to a section.", file, linenum);
				return 1;
			}

			if (ptr == NULL && egg_strequal (section, "options")) {
				/* directives without settings, all in [options] */
				if (egg_strequal (key, "NoPassiveFTP")) {
					alpm_option_set_nopassiveftp (1);
					egg_debug ("config: nopassiveftp");
				} else if (egg_strequal (key, "UseSyslog")) {
					alpm_option_set_usesyslog (1);
					egg_debug ("config: usesyslog");
				} else if (egg_strequal (key, "UseDelta")) {
					alpm_option_set_usedelta (1);
					egg_debug ("config: usedelta");
				} else {
					egg_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
					return 1;
				}
			} else {
				/* directives with settings */
				if (egg_strequal (key, "Include")) {
					egg_debug ("config: including %s", ptr);
					parse_config(ptr, section, db);
					/* Ignore include failures... assume non-critical */
				} else if (egg_strequal (section, "options")) {
					if (egg_strequal (key, "NoUpgrade")) {
						set_repeating_option (ptr, "NoUpgrade", alpm_option_add_noupgrade);
					} else if (egg_strequal (key, "NoExtract")) {
						set_repeating_option (ptr, "NoExtract", alpm_option_add_noextract);
					} else if (egg_strequal (key, "IgnorePkg")) {
						set_repeating_option (ptr, "IgnorePkg", alpm_option_add_ignorepkg);
					} else if (egg_strequal (key, "IgnoreGroup")) {
						set_repeating_option (ptr, "IgnoreGroup", alpm_option_add_ignoregrp);
					} else if (egg_strequal (key, "HoldPkg")) {
						set_repeating_option (ptr, "HoldPkg", alpm_option_add_holdpkg);
					} else if (egg_strequal (key, "SyncFirst")) {
						set_repeating_option (ptr, "SyncFirst", option_add_syncfirst);
					} else if (egg_strequal (key, "DBPath")) {
						alpm_option_set_dbpath (ptr);
					} else if (egg_strequal (key, "CacheDir")) {
						if (alpm_option_add_cachedir(ptr) != 0) {
							egg_error ("problem adding cachedir '%s' (%s)", ptr, alpm_strerrorlast ());
							return 1;
						}
						egg_debug ("config: cachedir: %s", ptr);
					} else if (egg_strequal (key, "RootDir")) {
						alpm_option_set_root (ptr);
						egg_debug ("config: rootdir: %s", ptr);
					} else if (egg_strequal (key, "LogFile")) {
						alpm_option_set_logfile (ptr);
						egg_debug ("config: logfile: %s", ptr);
					} else if (egg_strequal (key, "XferCommand")) {
						alpm_option_set_xfercommand (ptr);
						egg_debug ("config: xfercommand: %s", ptr);
					} else {
						egg_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
						return 1;
					}
				} else if (egg_strequal (key, "Server")) {
					/* let's attempt a replacement for the current repo */
					char *server = strreplace (ptr, "$repo", section);

					if (alpm_db_setserver (db, server) != 0) {
						/* pm_errno is set by alpm_db_setserver */
						return 1;
					}
					free (server);
				} else {
					egg_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
					return 1;
				}
			}
		}
	}

	fclose (fp);
	if (section)
		free (section);

	egg_debug ("config: finished parsing %s", file);
	return 0;
}

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	// initialize backend_instance for use in callback functions
	backend_instance = backend;

	egg_debug ("alpm: initializing backend");

	if (alpm_initialize () == -1) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_INITIALIZATION, "Failed to initialize package manager");
		egg_debug ("alpm: %s", alpm_strerror (pm_errno));
		return;
	}

	/* read options from config file */
	if (parse_config (ALPM_CONFIG_PATH, NULL, NULL) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING, "Failed to parse config file");
		alpm_release ();
		return;
	}

	if (alpm_db_register_local () == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, "Failed to load local database");
		egg_debug ("alpm: %s", alpm_strerror (pm_errno));
		alpm_release ();
		return;
	}

	alpm_option_set_dlcb (cb_dl_progress);
	alpm_option_set_totaldlcb (cb_dl_total);

	/* fill in group mapping */
	group_map = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (group_map, (gchar *) "gnome", (gchar *) "desktop-gnome");
	g_hash_table_insert (group_map, (gchar *) "gnome-extra", (gchar *) "desktop-gnome");
	g_hash_table_insert (group_map, (gchar *) "compiz-gnome", (gchar *) "desktop-gnome");
	g_hash_table_insert (group_map, (gchar *) "kde", (gchar *) "desktop-kde");
	g_hash_table_insert (group_map, (gchar *) "compiz-kde", (gchar *) "desktop-kde");
	g_hash_table_insert (group_map, (gchar *) "compiz-fusion-kde", (gchar *) "desktop-kde");
	g_hash_table_insert (group_map, (gchar *) "lxde", (gchar *) "desktop-other");
	g_hash_table_insert (group_map, (gchar *) "rox-desktop", (gchar *) "desktop-other");
	g_hash_table_insert (group_map, (gchar *) "e17-cvs", (gchar *) "desktop-other");
	g_hash_table_insert (group_map, (gchar *) "e17-extra-cvs", (gchar *) "desktop-other");
	g_hash_table_insert (group_map, (gchar *) "e17-libs-cvs", (gchar *) "desktop-other");
	g_hash_table_insert (group_map, (gchar *) "xfce4", (gchar *) "desktop-xfce");
	g_hash_table_insert (group_map, (gchar *) "xfce4-goodies", (gchar *) "desktop-xfce");
	g_hash_table_insert (group_map, (gchar *) "bmp-io-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "bmp-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "bmp-visualization-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "gstreamer0.10-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "ladspa-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "pvr", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "mythtv-extras", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "xmms-effect-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "xmms-io-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "xmms-plugins", (gchar *) "multimedia");
	g_hash_table_insert (group_map, (gchar *) "base-devel", (gchar *) "programming");
	g_hash_table_insert (group_map, (gchar *) "texlive-lang", (gchar *) "publishing");
	g_hash_table_insert (group_map, (gchar *) "texlive-lang-doc", (gchar *) "publishing");
	g_hash_table_insert (group_map, (gchar *) "texlive-most", (gchar *) "publishing");
	g_hash_table_insert (group_map, (gchar *) "texlive-most-doc", (gchar *) "publishing");
	g_hash_table_insert (group_map, (gchar *) "texlive-most-svn", (gchar *) "publishing");
	g_hash_table_insert (group_map, (gchar *) "base", (gchar *) "system");

	egg_debug ("alpm: ready to go");
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_hash_table_destroy (group_map);

	if (alpm_release () == -1) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_FINALISE, "Failed to release package manager");
		egg_debug ("alpm: %s", alpm_strerror (pm_errno));
	}
}

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_DESKTOP_XFCE,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-compressed-tar");
}

/**
 * backend_cancel:
 **/
static void
backend_cancel (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_CANCEL);
}

/**
 * backend_download_packages:
 */
static gboolean
backend_download_packages_thread (PkBackend *backend)
{
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	const gchar *directory = pk_backend_get_string (backend, "directory");
	unsigned int iterator;
	alpm_list_t *list_iterator;
	alpm_list_t *cachedirs = NULL;
	alpm_list_t *data = NULL;

	egg_debug ("alpm: downloading packages to %s", directory);

	/* old cachedirs list automatically gets freed in alpm, so make a copy */
	for (list_iterator = alpm_option_get_cachedirs (); list_iterator; list_iterator = alpm_list_next (list_iterator))
		cachedirs = alpm_list_add (cachedirs, g_strdup (alpm_list_getdata (list_iterator)));
	/* set new download destination */
	alpm_option_set_cachedirs (NULL);
	alpm_option_add_cachedir (directory);

	/* create a new transaction */
	if (alpm_trans_init (PM_TRANS_TYPE_SYNC, PM_TRANS_FLAG_NODEPS | PM_TRANS_FLAG_DOWNLOADONLY, cb_trans_evt, cb_trans_conv, cb_trans_progress) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		pk_backend_finished (backend);
		return FALSE;
	}

	/* add targets to the transaction */
	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		PkPackageId *id = pk_package_id_new_from_string (package_ids[iterator]);
		if (alpm_trans_addtarget ((char *) pk_package_id_get_name (id)) != 0) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
			alpm_trans_release ();
			pk_backend_finished (backend);
			return FALSE;
		}
		pk_package_id_free (id);
	}

	/* prepare and commit transaction */
	if (alpm_trans_prepare (&data) != 0 || alpm_trans_commit (&data) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		alpm_trans_release ();
		pk_backend_finished (backend);
		return FALSE;
	}

	alpm_trans_release ();

	/* emit/cleanup downloaded files list */
	for (list_iterator = downloaded_files; list_iterator; list_iterator = alpm_list_next (list_iterator)) {
		gchar *fullname = g_strjoin ("/", directory, alpm_list_getdata (list_iterator), NULL);
		pk_backend_files (backend, NULL, fullname);
		g_free (fullname);
		g_free (alpm_list_getdata (list_iterator));
	}
	alpm_list_free (downloaded_files);

	/* return cachedirs back */
	alpm_option_set_cachedirs (cachedirs);

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_download_packages:
 */
static void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_download_packages_thread);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	unsigned int iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, FALSE);

	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		alpm_list_t *list_iterator;

		pmpkg_t *pkg = pkg_from_package_id_str (package_ids[iterator]);
		if (pkg == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, alpm_strerrorlast ());
			pk_backend_finished (backend);
			return;
		}

		for (list_iterator = alpm_pkg_get_depends (pkg); list_iterator; list_iterator = alpm_list_next (list_iterator)) {
			pmdepend_t *dep = alpm_list_getdata (list_iterator);
			pmpkg_t *dep_pkg;
			gboolean found = FALSE;

			if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
				/* search in sync dbs */
				alpm_list_t *db_iterator;
				for (db_iterator = alpm_option_get_syncdbs (); found == FALSE && db_iterator; db_iterator = alpm_list_next (db_iterator)) {
					pmdb_t *syncdb = alpm_list_getdata (db_iterator);

					egg_debug ("alpm: searching for %s in %s", alpm_dep_get_name (dep), alpm_db_get_name (syncdb));

					dep_pkg = alpm_db_get_pkg (syncdb, alpm_dep_get_name (dep));
					if (dep_pkg && alpm_depcmp (dep_pkg, dep) && pkg_cmp (dep_pkg, alpm_db_get_pkg (alpm_option_get_localdb (), alpm_dep_get_name (dep))) != 0) {
						found = TRUE;
						emit_package (backend, dep_pkg, alpm_db_get_name (syncdb), PK_INFO_ENUM_AVAILABLE);
					}
				}
			}

			if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
				egg_debug ("alpm: searching for %s in local db", alpm_dep_get_name (dep));

				/* search in local db */
				dep_pkg = alpm_db_get_pkg (alpm_option_get_localdb (), alpm_dep_get_name (dep));
				if (dep_pkg && alpm_depcmp (dep_pkg, dep)) {
					found = TRUE;
					emit_package (backend, dep_pkg, ALPM_LOCAL_DB_ALIAS, PK_INFO_ENUM_INSTALLED);
				}
			}

			if (found == FALSE) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, alpm_strerrorlast ());
				pk_backend_finished (backend);
				return;
			}
		}
	}

	pk_backend_finished (backend);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	unsigned int iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, FALSE);

	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		alpm_list_t *licenses_list;
		GString *licenses_str;
		gchar *licenses;

		pmpkg_t *pkg = pkg_from_package_id_str (package_ids[iterator]);
		if (pkg == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, alpm_strerrorlast ());
			pk_backend_finished (backend);
			return;
		}

		licenses_list = alpm_pkg_get_licenses (pkg);
		if (licenses_list == NULL)
			licenses_str = g_string_new ("unknown");
		else {
			alpm_list_t *list_iterator;

			licenses_str = g_string_new ("");
			for (list_iterator = licenses_list; list_iterator; list_iterator = alpm_list_next (list_iterator)) {
				if (list_iterator != licenses_list)
					g_string_append (licenses_str, ", ");
				g_string_append (licenses_str, (char *) alpm_list_getdata (list_iterator));
			}
		}

		// get licenses_str content to licenses array
		licenses = g_string_free (licenses_str, FALSE);

		// return details
		pk_backend_details (backend, package_ids[iterator], licenses, PK_GROUP_ENUM_OTHER, alpm_pkg_get_desc (pkg), alpm_pkg_get_url(pkg), alpm_pkg_get_size (pkg));

		// free licenses array as we no longer need it
		g_free (licenses);
	}

	pk_backend_finished (backend);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	unsigned int iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, FALSE);

	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		pmpkg_t *pkg;
		alpm_list_t *files_list;
		GString *files_str;
		gchar *files;

		pkg = pkg_from_package_id_str (package_ids[iterator]);
		if (pkg == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, alpm_strerrorlast ());
			pk_backend_finished (backend);
			return;
		}

		files_list = alpm_pkg_get_files (pkg);
		files_str = g_string_new ("");
		if (files_list != NULL) {
			alpm_list_t *list_iterator;

			for (list_iterator = files_list; list_iterator; list_iterator = alpm_list_next (list_iterator)) {
				if (list_iterator != files_list)
					g_string_append (files_str, ";");
				g_string_append (files_str, alpm_option_get_root ());
				g_string_append (files_str, (char *) alpm_list_getdata (list_iterator));
			}
		}
		files = g_string_free (files_str, FALSE);

		pk_backend_files (backend, package_ids[iterator], files);
	}

	pk_backend_finished (backend);
}

static void
backend_search (PkBackend *backend, pmdb_t *repo, const gchar *needle, PkAlpmSearchType search_type) {
	/* package cache */
	alpm_list_t *pkg_cache;

	/* utility variables */
	alpm_list_t *iterator;
	const gchar *repo_name;
	PkInfoEnum info;
	gboolean match;
	gboolean repo_is_local;

	if (repo == alpm_option_get_localdb ()) {
		repo_name = ALPM_LOCAL_DB_ALIAS;
		info = PK_INFO_ENUM_INSTALLED;
		repo_is_local = TRUE;
	} else {
		repo_name = alpm_db_get_name (repo);
		info = PK_INFO_ENUM_AVAILABLE;
		repo_is_local = FALSE;
	}

	/* get package cache for specified repo */
	pkg_cache = alpm_db_getpkgcache (repo);

	/* iterate package cache */
	for (iterator = pkg_cache; iterator; iterator = alpm_list_next (iterator)) {
		pmpkg_t *pkg = alpm_list_getdata (iterator);

		switch (search_type) {
			alpm_list_t *provides;
			alpm_list_t *groups;

			case PK_ALPM_SEARCH_TYPE_NULL:
				match = TRUE;
				break;
			case PK_ALPM_SEARCH_TYPE_RESOLVE:
				match = egg_strequal (alpm_pkg_get_name (pkg), needle);
				break;
			case PK_ALPM_SEARCH_TYPE_NAME:
				match = strstr (alpm_pkg_get_name (pkg), needle) != NULL;
				break;
			case PK_ALPM_SEARCH_TYPE_DETAILS:
				/* workaround for buggy packages with no description */
				if (alpm_pkg_get_desc (pkg) == NULL)
					match = FALSE;
				else
					/* TODO: strcasestr is a non-standard extension, replace it? */
					match = strcasestr (alpm_pkg_get_desc (pkg), needle) != NULL;
				break;
			case PK_ALPM_SEARCH_TYPE_GROUP:
				match = FALSE;
				/* iterate groups */
				for (groups = alpm_pkg_get_groups (pkg); groups && !match; groups = alpm_list_next (groups)) {
					gchar *group = (gchar *) g_hash_table_lookup (group_map, (char *) alpm_list_getdata (groups));
					if (group == NULL)
						group = (gchar *) "other";
					match = egg_strequal (group, needle);
				}
				break;
			case PK_ALPM_SEARCH_TYPE_PROVIDES:
				match = FALSE;
				/* iterate provides */
				for (provides = alpm_pkg_get_provides (pkg); provides && !match; provides = alpm_list_next (provides))
					match = egg_strequal (needle, alpm_list_getdata (provides));
				break;
			default:
				match = FALSE;
		}

		if (match && (repo_is_local || pkg_cmp (pkg, alpm_db_get_pkg (alpm_option_get_localdb (), alpm_pkg_get_name (pkg))) != 0)) {
			/* we found what we wanted */
			emit_package (backend, pkg, repo_name, info);
		}
	}
}

/**
 * backend_get_packages_thread:
 */
static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	PkBitfield filters = pk_backend_get_uint (backend, "filters");

	gboolean search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	gboolean search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	if (!search_not_installed) {
		/* search in local db */
		backend_search (backend, alpm_option_get_localdb (), NULL, PK_ALPM_SEARCH_TYPE_NULL);
	}

	if (!search_installed) {
		/* search in sync repos */
		alpm_list_t *repos;
		/* iterate repos */
		for (repos = alpm_option_get_syncdbs (); repos; repos = alpm_list_next (repos))
			backend_search (backend, alpm_list_getdata (repos), NULL, PK_ALPM_SEARCH_TYPE_NULL);
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_get_packages_thread);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	alpm_list_t *list_iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* iterate on repo list */
	for (list_iterator = alpm_option_get_syncdbs (); list_iterator; list_iterator = alpm_list_next (list_iterator)) {
		pmdb_t *db = alpm_list_getdata (list_iterator);

		pk_backend_repo_detail (backend, alpm_db_get_name (db), alpm_db_get_name (db), TRUE);
	}

	pk_backend_finished (backend);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	unsigned int iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, FALSE);

	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		// TODO: add changelog code here
		PkPackageId *id = pk_package_id_new_from_string (package_ids[iterator]);

		pmpkg_t *installed_pkg = alpm_db_get_pkg (alpm_option_get_localdb (), pk_package_id_get_name (id));

		gchar *installed_package_id = installed_pkg ? pkg_to_package_id_str (installed_pkg, ALPM_LOCAL_DB_ALIAS) : NULL;
		pk_backend_update_detail (backend, package_ids[iterator], installed_package_id, "", "", "", "", PK_RESTART_ENUM_NONE,
			installed_pkg ? "Update to latest available version" : "Install as a dependency for another update",
			NULL, PK_UPDATE_STATE_ENUM_UNKNOWN, NULL, NULL);
		g_free (installed_package_id);

		pk_package_id_free (id);
	}

	pk_backend_finished (backend);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	alpm_list_t *list_iterator;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, FALSE);

	/* iterate through list of installed packages to find update for each */
	for (list_iterator = alpm_db_getpkgcache (alpm_option_get_localdb ()); list_iterator; list_iterator = alpm_list_next (list_iterator)) {
		alpm_list_t *db_iterator;

		pmpkg_t *pkg = alpm_list_getdata (list_iterator);

		for (db_iterator = alpm_option_get_syncdbs (); db_iterator; db_iterator = alpm_list_next (db_iterator)) {
			pmdb_t *db = alpm_list_getdata (db_iterator);
			pmpkg_t *repo_pkg = alpm_db_get_pkg (db, alpm_pkg_get_name (pkg));

			if (repo_pkg != NULL && alpm_pkg_vercmp (alpm_pkg_get_version (pkg), alpm_pkg_get_version (repo_pkg)) < 0) {
				gchar *package_id_str = pkg_to_package_id_str (repo_pkg, alpm_db_get_name (db));
				pk_backend_package (backend, PK_INFO_ENUM_NORMAL, package_id_str, alpm_pkg_get_desc (repo_pkg));
				g_free (package_id_str);

				break;
			}
		}
	}

	pk_backend_finished (backend);
}

/**
 * backend_install_files_thread:
 */
static gboolean
backend_install_files_thread (PkBackend *backend)
{
	unsigned int iterator;
	alpm_list_t *data = NULL;

	gchar **full_paths = pk_backend_get_strv (backend, "full_paths");

	/* create a new transaction */
	if (alpm_trans_init (PM_TRANS_TYPE_UPGRADE, 0, cb_trans_evt, cb_trans_conv, cb_trans_progress) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		pk_backend_finished (backend);
		return FALSE;
	}

	/* add targets to the transaction */
	for (iterator = 0; iterator < g_strv_length (full_paths); ++iterator) {
		if (alpm_trans_addtarget (full_paths[iterator]) != 0) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
			alpm_trans_release ();
			pk_backend_finished (backend);
			return FALSE;
		} else
			egg_debug ("alpm: %s added to transaction queue", full_paths[iterator]);
	}

	/* prepare and commit transaction */
	if (alpm_trans_prepare (&data) != 0 || alpm_trans_commit (&data) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		alpm_trans_release ();
		pk_backend_finished (backend);
		return FALSE;
	}

	alpm_trans_release ();

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_install_files:
 */
static void
backend_install_files (PkBackend *backend, gboolean trusted, gchar **full_paths)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_install_files_thread);
}

/**
 * backend_install_packages_thread:
 */
static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	unsigned int iterator;
	alpm_list_t *data = NULL;

	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");

	/* create a new transaction */
	if (alpm_trans_init (PM_TRANS_TYPE_SYNC, PM_TRANS_FLAG_NODEPS, cb_trans_evt, cb_trans_conv, cb_trans_progress) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		pk_backend_finished (backend);
		return FALSE;
	}

	/* add targets to the transaction */
	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		PkPackageId *id = pk_package_id_new_from_string (package_ids[iterator]);
		if (alpm_trans_addtarget ((char *) pk_package_id_get_name (id)) != 0) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
			alpm_trans_release ();
			pk_backend_finished (backend);
			return FALSE;
		}
		pk_package_id_free (id);
	}

	/* prepare and commit transaction */
	if (alpm_trans_prepare (&data) != 0 || alpm_trans_commit (&data) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		alpm_trans_release ();
		pk_backend_finished (backend);
		return FALSE;
	}

	alpm_trans_release ();

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * backend_refresh_cache_thread:
 */
static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	alpm_list_t *list_iterator;

	if (alpm_trans_init (PM_TRANS_TYPE_SYNC, PM_TRANS_FLAG_NOSCRIPTLET, cb_trans_evt, cb_trans_conv, cb_trans_progress) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		pk_backend_finished (backend);
		return FALSE;
	}

	for (list_iterator = alpm_option_get_syncdbs (); list_iterator; list_iterator = alpm_list_next (list_iterator)) {
		pmdb_t *db = (pmdb_t *) alpm_list_getdata (list_iterator);
		if (alpm_db_update (FALSE, db) == -1) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
			pk_backend_finished (backend);
			return FALSE;
		}
	}

	alpm_trans_release ();

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Can not refresh cache in offline mode");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_refresh_cache_thread);
}

/**
 * backend_remove_packages_thread:
 */
static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	unsigned int iterator;
	alpm_list_t *data = NULL;

	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	gboolean allow_deps = pk_backend_get_bool (backend, "allow_deps");

	pmtransflag_t flags = 0;
	if (allow_deps)
		flags |= PM_TRANS_FLAG_CASCADE;

	/* create a new transaction */
	if (alpm_trans_init (PM_TRANS_TYPE_REMOVE, flags, cb_trans_evt, cb_trans_conv, cb_trans_progress) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		pk_backend_finished (backend);
		return FALSE;
	}

	/* add targets to the transaction */
	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		PkPackageId *id = pk_package_id_new_from_string (package_ids[iterator]);
		if (alpm_trans_addtarget ((char *) pk_package_id_get_name (id)) != 0) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
			alpm_trans_release ();
			pk_backend_finished (backend);
			return FALSE;
		}
		pk_package_id_free (id);
	}

	/* prepare and commit transaction */
	if (alpm_trans_prepare (&data) != 0 || alpm_trans_commit (&data) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerrorlast ());
		alpm_trans_release ();
		pk_backend_finished (backend);
		return FALSE;
	}

	alpm_trans_release ();

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

/**
 * backend_resolve_thread:
 */
static gboolean
backend_resolve_thread (PkBackend *backend)
{
	unsigned int iterator;

	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	PkBitfield filters = pk_backend_get_uint (backend, "filters");

	for (iterator = 0; iterator < g_strv_length (package_ids); ++iterator) {
		gboolean search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
		gboolean search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

		if (!search_not_installed) {
			/* search in local db */
			backend_search (backend, alpm_option_get_localdb (), package_ids[iterator], PK_ALPM_SEARCH_TYPE_RESOLVE);
		}

		if (!search_installed) {
			/* search in sync repos */
			alpm_list_t *repos;
			/* iterate repos */
			for (repos = alpm_option_get_syncdbs (); repos; repos = alpm_list_next (repos))
				backend_search (backend, alpm_list_getdata (repos), package_ids[iterator], PK_ALPM_SEARCH_TYPE_RESOLVE);
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_resolve_thread);
}

/**
 * backend_search_thread:
 */
static gboolean
backend_search_thread (PkBackend *backend)
{
	const gchar *search = pk_backend_get_string (backend, "search");
	PkBitfield filters = pk_backend_get_uint (backend, "filters");
	PkAlpmSearchType search_type = pk_backend_get_uint (backend, "search-type");

	gboolean search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	gboolean search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	if (!search_not_installed) {
		/* search in local db */
		backend_search (backend, alpm_option_get_localdb (), search, search_type);
	}

	if (!search_installed) {
		/* search in sync repos */
		alpm_list_t *repos;
		/* iterate repos */
		for (repos = alpm_option_get_syncdbs (); repos; repos = alpm_list_next (repos))
			backend_search (backend, alpm_list_getdata (repos), search, search_type);
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_ALPM_SEARCH_TYPE_DETAILS);

	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_ALPM_SEARCH_TYPE_GROUP);

	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_ALPM_SEARCH_TYPE_NAME);

	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * backend_what_provides:
 */
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_ALPM_SEARCH_TYPE_PROVIDES);

	pk_backend_thread_create (backend, backend_search_thread);
}

PK_BACKEND_OPTIONS (
	"alpm",						/* description */
	"Valeriy Lyasotskiy <onestep@ukr.net>",		/* author */
	backend_initialize,				/* initialize */
	backend_destroy,				/* destroy */
	backend_get_groups,				/* get_groups */
	backend_get_filters,				/* get_filters */
	backend_get_mime_types,				/* get_mime_types */
	backend_cancel,					/* cancel */
	backend_download_packages,			/* download_packages */
	NULL,						/* get_categories */
	backend_get_depends,				/* get_depends */
	backend_get_details,				/* get_details */
	NULL,						/* get_distro_upgrades */
	backend_get_files,				/* get_files */
	backend_get_packages,				/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	NULL,						/* get_requires */
	backend_get_update_detail,			/* get_update_detail */
	backend_get_updates,				/* get_updates */
	backend_install_files,				/* install_files */
	backend_install_packages,			/* install_packages */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_packages,			/* remove_packages */
	NULL,						/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	NULL,						/* search_file */
	backend_search_group,				/* search_group */
	backend_search_name,				/* search_name */
	backend_update_packages,			/* update_packages */
	NULL,						/* update_system */
	backend_what_provides				/* what_provides */
);
