/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
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

#define ALPM_CONFIG_PATH "/etc/pacman.conf"

#define ALPM_ROOT "/"
#define ALPM_DBPATH "/var/lib/pacman"
#define ALPM_CACHEDIR "/var/cache/pacman/pkg"
#define ALPM_LOGFILE "/var/log/pacman.log"
#define ALPM_XFERCOMMAND "/usr/bin/wget --passive-ftp -c -O \%o \%u"

#define ALPM_PROGRESS_UPDATE_INTERVAL 400

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-debug.h>
#include <pk-package-ids.h>
#include <pk-network.h>

#include <alpm.h>
#include <alpm_list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int progress_percentage;
static int subprogress_percentage;
PkBackend *install_backend = NULL;

static PkNetwork *network;

typedef struct _PackageSource
{
    pmpkg_t *pkg;
    gchar *repo;
    guint installed;
} PackageSource;

void
package_source_free (PackageSource *source)
{
    alpm_pkg_free (source->pkg);
}

void
trans_event_cb (pmtransevt_t event, void *data1, void *data2)
{
    // TODO: Add code here
}

void
trans_conv_cb (pmtransconv_t conv, void *data1, void *data2, void *data3, int *response)
{
    // TODO: Add code here
}

void
trans_prog_cb (pmtransprog_t prog, const char *pkgname, int percent, int n, int remain)
{
    pk_debug ("Percentage is %i", percent);
    subprogress_percentage = percent;
    pk_backend_set_percentage ((PkBackend *) install_backend, subprogress_percentage);
}

gboolean
update_subprogress (void *data)
{
    if (subprogress_percentage == -1)
	return FALSE;

    pk_debug ("alpm: subprogress is %i", subprogress_percentage);

    pk_backend_set_percentage ((PkBackend *) data, subprogress_percentage);
    return TRUE;
}

gboolean
update_progress (void *data)
{
    if (progress_percentage == -1)
	return FALSE;

    pk_backend_set_percentage ((PkBackend *) data, progress_percentage);
    return TRUE;
}

gpointer
state_notify (void *backend)
{
    g_timeout_add (300, update_subprogress, backend);
    return backend;
}

alpm_list_t *
my_list_remove_node (alpm_list_t *node)
{
    if (!node)
	return (NULL);

    alpm_list_t *ret = NULL;

    if (node->prev) {
	node->prev->next = node->next;
	ret = node->prev;
	//node->prev = NULL;
    }

    if (node->next) {
	node->next->prev = node->prev;
	ret = node->next;
	node->next = NULL;
    }

    return (ret);
}

gboolean
pkg_equal (pmpkg_t *p1, pmpkg_t *p2)
{
/*
    pk_debug (alpm_pkg_get_name (p1));
    pk_debug (alpm_pkg_get_name (p2));
*/
    if (strcmp (alpm_pkg_get_name (p1), alpm_pkg_get_name (p2)) != 0)
	return FALSE;
    if (strcmp (alpm_pkg_get_version (p1), alpm_pkg_get_version (p2)) != 0)
	return FALSE;
    return TRUE;
}

gboolean
pkg_equals_to (pmpkg_t *pkg, const gchar *name, const gchar *version)
{
    if (strcmp (alpm_pkg_get_name (pkg), name) != 0)
	return FALSE;
    if (version != NULL)
	if (strcmp (alpm_pkg_get_version (pkg), version) != 0)
	    return FALSE;
    return TRUE;
}

static void
add_package (PkBackend *backend, PackageSource *package)
{
    pk_debug ("add_package: hi, package_name=%s", alpm_pkg_get_name(package->pkg));

    PkInfoEnum info;
    gchar *pkg_string;
    gchar *arch = (gchar *) alpm_pkg_get_arch (package->pkg);

    if (arch == NULL)
	arch = "unknown";

    pkg_string = pk_package_id_build (alpm_pkg_get_name (package->pkg), alpm_pkg_get_version (package->pkg), arch, package->repo);
    if (package->installed)
	info = PK_INFO_ENUM_INSTALLED;
    else
	info = PK_INFO_ENUM_AVAILABLE;
    pk_backend_package (backend, info, pkg_string, alpm_pkg_get_desc (package->pkg));

    g_free(pkg_string);
}

static void
add_packages_from_list (PkBackend *backend, alpm_list_t *list)
{
    PackageSource *package = NULL;
    alpm_list_t *li = NULL;

    if (list == NULL)
	pk_warning ("add_packages_from_list: list is empty!");

    for (li = list; li != NULL; li = alpm_list_next (li)) {
	package = (PackageSource *) li->data;
	add_package (backend, package);
    }
}

alpm_list_t *
find_packages_by_desc (const gchar *name, pmdb_t *db)
{
    if (db == NULL || name == NULL)
	return NULL;

    alpm_list_t *needle = NULL;
    alpm_list_t *result = NULL;
    alpm_list_t *query_result = NULL;

    // determine if repository is local
    gboolean repo_is_local = (db == alpm_option_get_localdb ());
    // determine repository name
    const gchar *repo = alpm_db_get_name (db);
    // set search term
    needle = alpm_list_add (needle, (gchar *) name);
    // execute query
    query_result = alpm_db_search (db, needle);

    alpm_list_t *iterator;
    for (iterator = query_result; iterator; iterator = alpm_list_next (iterator)) {
	PackageSource *source = g_malloc (sizeof (PackageSource));

	source->pkg = (pmpkg_t *) alpm_list_getdata(iterator);
	source->repo = (gchar *) repo;
	source->installed = repo_is_local;

	result = alpm_list_add (result, (PackageSource *) source);
    }

    alpm_list_free (query_result);
    alpm_list_free (needle);
    return result;
}

gboolean
pkg_is_installed (const gchar *name, const gchar *version)
{
    pmdb_t *localdb = NULL;
    alpm_list_t *result = NULL;

    if (name == NULL)
	return FALSE;
    localdb = alpm_option_get_localdb ();
    if (localdb == NULL)
	return FALSE;

    result = find_packages_by_desc (name, localdb);
    if (result == NULL)
	return FALSE;
    if (!alpm_list_count (result))
	return FALSE;

    if (version == NULL)
	return TRUE;

    alpm_list_t *icmp = NULL;
    for (icmp = result; icmp; icmp = alpm_list_next (icmp))
	if (strcmp (alpm_pkg_get_version ((pmpkg_t *) icmp->data), version) == 0)
	    return TRUE;

    return FALSE;
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
    g_return_if_fail (backend != NULL);

    if (alpm_release () == -1)
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_FAILED_FINALISE,
		"Failed to release control");
}

/**
 * strtrim:
 * Trim whitespaces and newlines from a string
 */
char *
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
char *
strreplace(const char *str, const char *needle, const char *replace)
{
    const char *p, *q;
    p = q = str;

    char *newstr = NULL;
    unsigned int needlesz = strlen (needle), replacesz = strlen (replace);

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

/** Add repeating options such as NoExtract, NoUpgrade, etc to alpm settings.
 * @param ptr a pointer to the start of the multiple options
 * @param option the string (friendly) name of the option, used for messages
 * @param optionfunc a function pointer to an alpm_option_add_* function
 */
static void
set_repeating_option(const char *ptr, const char *option, void (*optionfunc) (const char*))
{
    char *p = (char*) ptr;
    char *q;

    while ((q = strchr(p, ' '))) {
	*q = '\0';
	(*optionfunc) (p);
	pk_debug("config: %s: %s", option, p);
	p = q;
	p++;
    }
    (*optionfunc) (p);
    pk_debug("config: %s: %s", option, p);
}

/**
 * parse_config:
 * Parse config file and set all the needed options
 * Based heavily on the pacman source code
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
    alpm_option_set_xfercommand (ALPM_XFERCOMMAND);

    fp = fopen(file, "r");
    if (fp == NULL) {
	pk_error ("config file %s could not be read", file);
	return (1);
    }

    /* if we are passed a section, use it as our starting point */
    if (givensection != NULL) {
	section = strdup (givensection);
    }
    /* if we are passed a db, use it as our starting point */
    if (givendb != NULL) {
	db = givendb;
    }

    while (fgets (line, PATH_MAX, fp))
    {
	linenum++;
	strtrim(line);

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
	    pk_debug ("config: new section '%s'", section);
	    if (!strlen (section)) {
		pk_debug ("config file %s, line %d: bad section name", file, linenum);
		return (1);
	    }

	    /* if we are not looking at the options section, register a db */
	    if (strcmp (section, "options") != 0)
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
		pk_error ("config file %s, line %d: syntax error in config file - missing key.", file, linenum);
		return (1);
	    }
	    if (section == NULL) {
		pk_error ("config file %s, line %d: all directives must belong to a section.", file, linenum);
		return (1);
	    }

	    if (ptr == NULL && strcmp (section, "options") == 0) {
		/* directives without settings, all in [options] */
		if (strcmp (key, "NoPassiveFTP") == 0) {
		    alpm_option_set_nopassiveftp (1);
		    pk_debug ("config: nopassiveftp");
		} else if (strcmp (key, "UseSyslog") == 0) {
		    alpm_option_set_usesyslog (1);
		    pk_debug ("config: usesyslog");
		} else if (strcmp (key, "UseDelta") == 0) {
		    alpm_option_set_usedelta (1);
		    pk_debug ("config: usedelta");
		} else {
		    pk_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
		    return(1);
		}
	    } else {
		/* directives with settings */
		if (strcmp (key, "Include") == 0) {
		    pk_debug ("config: including %s", ptr);
		    parse_config(ptr, section, db);
		    /* Ignore include failures... assume non-critical */
		} else if (strcmp (section, "options") == 0) {
		    if (strcmp (key, "NoUpgrade") == 0) {
			set_repeating_option (ptr, "NoUpgrade", alpm_option_add_noupgrade);
		    } else if (strcmp (key, "NoExtract") == 0) {
			set_repeating_option (ptr, "NoExtract", alpm_option_add_noextract);
		    } else if (strcmp (key, "IgnorePkg") == 0) {
			set_repeating_option (ptr, "IgnorePkg", alpm_option_add_ignorepkg);
		    } else if (strcmp (key, "IgnoreGroup") == 0) {
			set_repeating_option (ptr, "IgnoreGroup", alpm_option_add_ignoregrp);
		    } else if (strcmp (key, "HoldPkg") == 0) {
			set_repeating_option (ptr, "HoldPkg", alpm_option_add_holdpkg);
		    } else if (strcmp (key, "DBPath") == 0) {
			alpm_option_set_dbpath (ptr);
		    } else if (strcmp (key, "CacheDir") == 0) {
			if (alpm_option_add_cachedir(ptr) != 0) {
			    pk_error ("problem adding cachedir '%s' (%s)", ptr, alpm_strerrorlast ());
			    return (1);
			}
			pk_debug ("config: cachedir: %s", ptr);
		    } else if (strcmp (key, "RootDir") == 0) {
			alpm_option_set_root (ptr);
			pk_debug ("config: rootdir: %s", ptr);
		    } else if (strcmp (key, "LogFile") == 0) {
			alpm_option_set_logfile (ptr);
			pk_debug ("config: logfile: %s", ptr);
		    } else if (strcmp (key, "XferCommand") == 0) {
			alpm_option_set_xfercommand (ptr);
			pk_debug ("config: xfercommand: %s", ptr);
		    } else {
			pk_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
			return (1);
		    }
		} else if (strcmp (key, "Server") == 0) {
		    /* let's attempt a replacement for the current repo */
		    char *server = strreplace (ptr, "$repo", section);
		    if (alpm_db_setserver (db, server) != 0) {
			/* pm_errno is set by alpm_db_setserver */
			return (1);
		    }
		    free (server);
		} else {
		    pk_error ("config file %s, line %d: directive '%s' not recognized.", file, linenum, key);
		    return (1);
		}
	    }
	}
    }

    fclose (fp);
    if (section)
	free (section);

    pk_debug ("config: finished parsing %s", file);
    return 0;
}

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
    g_return_if_fail (backend != NULL);
    progress_percentage = -1;
    subprogress_percentage = -1;
    pk_debug ("alpm: hi!");

    if (alpm_initialize () == -1)
    {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_FAILED_INITIALIZATION,
		"Failed to initialize package manager");
	pk_debug ("alpm: %s", alpm_strerror (pm_errno));
	//return;
    }

    // Read options from config file
    if (parse_config (ALPM_CONFIG_PATH, NULL, NULL) != 0)
    {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
		"Failed to parse config file");
	backend_destroy (backend);
	return;
    }

    if (alpm_db_register_local () == NULL)
    {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
		"Failed to load local database");
	backend_destroy (backend);
	return;
    }
    pk_debug ("alpm: ready to go");

    network = pk_network_new();
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
    pk_debug ("hello %i", GPOINTER_TO_INT (backend));
    g_return_if_fail (backend != NULL);
/*
    alpm_list_t *syncdbs = alpm_option_get_syncdbs ();
*/
    alpm_list_t *result = NULL;
    alpm_list_t *problems = NULL;
    PkPackageId *id = pk_package_id_new_from_string (package_id);
    pmtransflag_t flags = 0;
    GThread *progress = NULL;

    flags |= PM_TRANS_FLAG_NODEPS;

    // Next generation code?
/*
    for (; syncdbs; syncdbs = alpm_list_next (syncdbs))
	result = alpm_list_join (result, find_packages_by_desc (id->name, (pmdb_t *) syncdbs->data));

    if (result == NULL) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_PACKAGE_ID_INVALID,
		"Package not found");
	pk_backend_finished (backend);
	alpm_list_free (result);
	alpm_list_free (syncdbs);
	pk_package_id_free (id);
	return;
    }

    for (; result; result = alpm_list_next (result))
	if (pkg_equals_to ((pmpkg_t *) result->data, id->name, id->version))
	    break;

    if (!result) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_PACKAGE_ID_INVALID,
		"Package not found");
	pk_backend_finished (backend);
	alpm_list_free (result);
	alpm_list_free (syncdbs);
	pk_package_id_free (id);
	return;
    }
*/

    if (alpm_trans_init (PM_TRANS_TYPE_SYNC, flags, trans_event_cb, trans_conv_cb, trans_prog_cb) == -1) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_list_free (result);
	pk_package_id_free (id);
	return;
    }

    pk_debug ("init");

    alpm_trans_addtarget (id->name);

    if (alpm_trans_prepare (&problems) != 0) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	alpm_list_free (result);
	pk_package_id_free (id);
	return;
    }

    pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING, package_id, "An HTML widget for GTK+ 2.0");

    progress = g_thread_create (state_notify, (void *) backend, TRUE, NULL);
    install_backend = backend;

    if (alpm_trans_commit (&problems) != 0) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	alpm_list_free (result);
	pk_package_id_free (id);
	return;
    }

    alpm_trans_release ();
    alpm_list_free (result);
    pk_package_id_free (id);
    pk_backend_finished (backend);
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
    /* TODO: process the entire list */
    backend_install_package (backend, package_ids[0]);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
    g_return_if_fail (backend != NULL);

    if (pk_network_is_online (network) == FALSE) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
	pk_backend_finished (backend);
	return;
    }

    alpm_list_t *dbs = alpm_option_get_syncdbs ();
/*
    alpm_list_t *problems = NULL;
*/

    if (alpm_trans_init (PM_TRANS_TYPE_SYNC, 0, trans_event_cb, trans_conv_cb, trans_prog_cb) != 0) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	return;
    }

    pk_debug ("alpm: %s", "transaction initialized");

/*
    if (alpm_trans_prepare (&problems) != 0) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	return;
    }
*/

    alpm_list_t *i = NULL;
    pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
    g_timeout_add (ALPM_PROGRESS_UPDATE_INTERVAL, update_subprogress, backend);

    for (i = dbs; i; i = alpm_list_next (i)) {
	if (alpm_db_update (force, (pmdb_t *) i->data)) {
	    pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	    alpm_list_free (dbs);
	    pk_backend_finished (backend);
	    subprogress_percentage = -1;
	    return;
	}
	subprogress_percentage = -1;
    }

    pk_backend_finished (backend);
}

/**
 * backend_resolve:
 * Currently works only for local packages
 */
static void
backend_resolve (PkBackend *backend, PkFilterEnum filters, const gchar *package)
{
    g_return_if_fail (backend != NULL);

    pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

    pmdb_t *localdb = alpm_option_get_localdb ();
    // result will be the list of PackageSource
    alpm_list_t *result = find_packages_by_desc (package, localdb);

    if (result != NULL)
	pk_debug ("alpm: package %s found, trying to resolve...", package);

    if (result == NULL) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "Package is not installed");
	pk_backend_finished (backend);
	return;
    } else if (alpm_list_count (result) != 1 || strcmp (alpm_pkg_get_name (((PackageSource *) result->data)->pkg), package) != 0) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "Package is not installed");
	pk_backend_finished (backend);
	alpm_list_free_inner (result, (alpm_list_fn_free) package_source_free);
	alpm_list_free (result);
	return;
    }

    pmpkg_t *pkg = ((PackageSource *) result->data)->pkg;
    pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
	    pk_package_id_build (alpm_pkg_get_name (pkg), alpm_pkg_get_version (pkg), alpm_pkg_get_arch (pkg), "local"),
	    alpm_pkg_get_desc (pkg));

    alpm_list_free_inner (result, (alpm_list_fn_free) package_source_free);
    alpm_list_free (result);
    pk_backend_finished (backend);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
    g_return_if_fail (backend != NULL);

    PkPackageId *id = pk_package_id_new_from_string (package_id);
    pmtransflag_t flags = 0;
    alpm_list_t *problems = NULL;

    if (allow_deps)
	flags |= PM_TRANS_FLAG_CASCADE;

    if (alpm_trans_init (PM_TRANS_TYPE_REMOVE, flags, trans_event_cb, trans_conv_cb, trans_prog_cb) == -1) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	pk_package_id_free (id);
	return;
    }

    alpm_trans_addtarget (id->name);

    if (alpm_trans_prepare (&problems) != 0) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	pk_package_id_free (id);
	return;
    }

    if (alpm_trans_commit (&problems) != 0) {
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	pk_package_id_free (id);
	return;
    }

    pk_package_id_free (id);
    alpm_trans_release ();
    pk_backend_finished (backend);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
    g_return_if_fail (backend != NULL);

    alpm_list_t *result = NULL;
    alpm_list_t *repos = NULL;

    pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

    gboolean installed = pk_enums_contain (filters, PK_FILTER_ENUM_INSTALLED);
    gboolean ninstalled = pk_enums_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

    if (!ninstalled)
	repos = alpm_list_add (repos, alpm_option_get_localdb ());
    if (!installed)
	repos = alpm_list_join (repos, alpm_list_copy(alpm_option_get_syncdbs ()));

    alpm_list_t *iterator;
    for (iterator = repos; iterator; iterator = alpm_list_next (iterator))
	result = alpm_list_join (result, find_packages_by_desc (search, (pmdb_t *) alpm_list_getdata(iterator)));

    add_packages_from_list (backend, alpm_list_first (result));

    alpm_list_free_inner (result, (alpm_list_fn_free) package_source_free);
    alpm_list_free (result);

    alpm_list_free (repos);
    pk_backend_finished (backend);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkFilterEnum filters, const gchar *search) {
    g_return_if_fail (backend != NULL);

    alpm_list_t *repos = NULL;
    alpm_list_t *result = NULL;

    pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

    gboolean installed = pk_enums_contain (filters, PK_FILTER_ENUM_INSTALLED);
    gboolean ninstalled = pk_enums_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

    if (!ninstalled)
	repos = alpm_list_add (repos, alpm_option_get_localdb ());
    if (!installed)
	repos = alpm_list_join (repos, alpm_list_copy(alpm_option_get_syncdbs ()));

    alpm_list_t *iterator;
    for (iterator = repos; iterator; iterator = alpm_list_next (iterator)) {
	pmdb_t *db = alpm_list_getdata (iterator);
	alpm_list_t *cache = alpm_db_getpkgcache (db);

	// determine if repository is local
	gboolean db_is_local = (db == alpm_option_get_localdb ());
	// determine repository name
	const gchar *repo = alpm_db_get_name (db);

	alpm_list_t *pkg_iterator;
	for (pkg_iterator = cache; pkg_iterator; pkg_iterator = alpm_list_next (pkg_iterator)) {
	    pmpkg_t *pkg = alpm_list_getdata(pkg_iterator);

	    if (strstr (alpm_pkg_get_name (pkg), search)) {
		PackageSource *source = g_malloc (sizeof (PackageSource));

		source->pkg = (pmpkg_t *) pkg;
		source->repo = (gchar *) repo;
		source->installed = db_is_local;

		result = alpm_list_add (result, (PackageSource *) source);
	    }
	}
    }

    add_packages_from_list (backend, alpm_list_first (result));

    alpm_list_free_inner (result, (alpm_list_fn_free) package_source_free);
    alpm_list_free (result);

    alpm_list_free (repos);
    pk_backend_finished (backend);
}

/**
 * backend_get_groups:
 */
static PkGroupEnum
backend_get_groups (PkBackend *backend)
{
    g_return_val_if_fail (backend != NULL, PK_GROUP_ENUM_UNKNOWN);

    // TODO: Provide support for groups in alpm
    return PK_GROUP_ENUM_OTHER | PK_GROUP_ENUM_UNKNOWN;
}

/**
 * backend_get_filters:
 */
static PkFilterEnum
backend_get_filters (PkBackend *backend)
{
    g_return_val_if_fail (backend != NULL, PK_FILTER_ENUM_UNKNOWN);

    return PK_FILTER_ENUM_INSTALLED;
}

static void
backend_install_file (PkBackend *backend, gboolean trusted, const gchar *path)
{
    g_return_if_fail (backend != NULL);

    alpm_list_t *problems = NULL;
    if (alpm_trans_init (PM_TRANS_TYPE_ADD, 0, trans_event_cb, trans_conv_cb, trans_prog_cb) == -1) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	return;
    }

    alpm_trans_addtarget ((char *) path);

    if (alpm_trans_prepare (&problems) != 0) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	return;
    }

    if (alpm_trans_commit (&problems) != 0) {
	pk_backend_error_code (backend,
		PK_ERROR_ENUM_TRANSACTION_ERROR,
		alpm_strerror (pm_errno));
	pk_backend_finished (backend);
	alpm_trans_release ();
	return;
    }

    alpm_trans_release ();
    pk_backend_finished (backend);
}

/**
 * backend_get_repo_list:
 */
void
backend_get_repo_list (PkBackend *backend, PkFilterEnum filters)
{
    g_return_if_fail (backend != NULL);

    pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

    alpm_list_t *repos = alpm_option_get_syncdbs ();

    if (repos == NULL)
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, alpm_strerror (pm_errno));

    // Iterate on repository list
    repos = alpm_list_first (repos);
    while (repos != NULL) {
	pmdb_t *db = alpm_list_getdata (repos);
	pk_backend_repo_detail (backend, alpm_db_get_name (db), alpm_db_get_url (db), TRUE);
	repos = alpm_list_next (repos);
    }

    alpm_list_free (repos);
    pk_backend_finished (backend);
}
  
PK_BACKEND_OPTIONS (
	"alpm",						/* description */
	"Andreas Obergrusberger <tradiaz@yahoo.de>",	/* author */
	backend_initialize,				/* initialize */
	backend_destroy,				/* destroy */
	backend_get_groups,				/* get_groups */
	backend_get_filters,				/* get_filters */
	NULL,						/* cancel */
	NULL,						/* get_depends */
	NULL,						/* get_description */
	NULL,						/* get_files */
	NULL,						/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	NULL,						/* get_requires */
	NULL,						/* get_update_detail */
	NULL,						/* get_updates */
	backend_install_file,				/* install_file */
	backend_install_package,			/* install_package */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_package,				/* remove_package */
	NULL,						/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	NULL,						/* search_file */
	NULL,						/* search_group */
	backend_search_name,				/* search_name */
	NULL,						/* service_pack */
	backend_update_packages,			/* update_packages */
	NULL,						/* update_system */
	NULL						/* what_provides */
);
