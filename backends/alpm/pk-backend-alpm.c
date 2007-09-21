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
#define PROGRESS_UPDATE_INTERVAL 400

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-debug.h>

#include <alpm.h>
#include <alpm_list.h>

static int progress_percentage;
static int subprogress_percentage;

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
}

void
trans_conv_cb (pmtransconv_t conv,
	       void *data1, void *data2, void *data3,
	       int *response)
{
}

void
trans_prog_cb (pmtransprog_t prog, const char *pkgname, int percent,
                       int n, int remain)
{
  subprogress_percentage = percent;
}

gboolean
update_subprogress (void *data)
{
  if (subprogress_percentage == -1)
    return FALSE;

  pk_debug ("alpm: subprogress is %i", subprogress_percentage);

  pk_backend_change_percentage ((PkBackend *)data, subprogress_percentage);
  return TRUE;
}

gboolean
update_progress (void *data)
{
  if (progress_percentage == -1)
    return FALSE;

  pk_backend_change_percentage ((PkBackend *)data, progress_percentage);
  return TRUE;
}

alpm_list_t *
my_list_mmerge (alpm_list_t *left, alpm_list_t *right, alpm_list_fn_cmp fn)
{
  alpm_list_t *newlist, *lp;

  if (left == NULL && right == NULL)
    return NULL;

  if (left == NULL)
    return right;
  if (right == NULL)
    return left;

  if (fn(left->data, right->data) <= 0) {
    newlist = left;
    left = left->next;
  }
  else {
    newlist = right;
    right = right->next;
  }
  newlist->prev = NULL;
  newlist->next = NULL;
  lp = newlist;

  while ((left != NULL) && (right != NULL)) {
    if (fn(left->data, right->data) <= 0) {
      lp->next = left;
      left->prev = lp;
      left = left->next;
    }
    else {
      lp->next = right;
      right->prev = lp;
      right = right->next;
    }
    lp = lp->next;
    lp->next = NULL;
  }
  if (left != NULL) {
    lp->next = left;
    left->prev = lp;
  }
  else if (right != NULL) {
    lp->next = right;
    right->prev = lp;
  }
  return(newlist);
}

gboolean
pkg_equal (pmpkg_t *p1, pmpkg_t *p2)
{
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


alpm_list_t *
my_list_remove_node (alpm_list_t *node)
{
  if(!node) return(NULL);

  alpm_list_t *ret = NULL;

  if(node->prev) {
    node->prev->next = node->next;
    ret = node->prev;
    node->prev = NULL;
  }
  if(node->next) {
    node->next->prev = node->prev;
    ret = node->next;
    node->next = NULL;
  }

  return(ret);
}

static int
list_cmp_fn (const void *n1, const void *n2)
{
  return 0;
}

static void
add_package (PkBackend *backend, PackageSource *package)
{
  gchar *pkg_string;
  gchar *arch = (gchar *)alpm_pkg_get_arch (package->pkg);

  if (arch == NULL) arch = "lala";

  pkg_string = pk_package_id_build(alpm_pkg_get_name (package->pkg),
				     alpm_pkg_get_version (package->pkg),
				     arch,
				     package->repo);

  pk_backend_package (backend, package->installed, pkg_string, alpm_pkg_get_desc (package->pkg));

  g_free(pkg_string);
}

static void
add_packages_from_list (PkBackend *backend, alpm_list_t *list)
{
  PackageSource *package = NULL;
  alpm_list_t *li = NULL;

  for (li = list; li != NULL; li = alpm_list_next (li)) {
    package = (PackageSource *)li->data;
    add_package (backend, package);
  }
}

alpm_list_t *
find_packages ( const gchar *name, pmdb_t *db)
{
  if (db == NULL || name == NULL) return NULL;

  alpm_list_t *needle = NULL;
  alpm_list_t *result = NULL;
  alpm_list_t *localresult = NULL;
  pmdb_t *localdb = NULL;
  const gchar *dbname = NULL;

  needle = alpm_list_add (needle, (gchar *)name);
  dbname = alpm_db_get_name (db);
  result = alpm_db_search (db, needle);
  localdb = alpm_option_get_localdb ();

  alpm_list_t *i = NULL;

  if (db != localdb)
    {
      if (localdb != NULL)
	localresult = alpm_db_search (localdb, needle);
    }

  for (i = result; i; i = alpm_list_next (i))
    {
      PackageSource *source = g_malloc (sizeof (PackageSource));

      source->pkg = (pmpkg_t *)i->data;
      source->repo = (gchar *)dbname;

      if (localresult != NULL)
	{
	  alpm_list_t *icmp = NULL;
	  for (icmp = localresult; icmp; icmp = alpm_list_next (icmp))
	    if (pkg_equal ((pmpkg_t *)icmp->data, (pmpkg_t *)i->data))
	      source->installed = TRUE;
	    else source->installed = FALSE;
	}
      else if (localdb == db) source->installed = TRUE;
      else  source->installed = FALSE;

      i->data = source;
    }

  alpm_list_free (needle);
  if (localresult != NULL)
    alpm_list_free_inner (localresult, (alpm_list_fn_free)alpm_pkg_free);
  return result;
}

gboolean
pkg_is_installed (const gchar *name, const gchar *version)
{
  pmdb_t *localdb = NULL;
  alpm_list_t *result = NULL;

  if (name == NULL) return FALSE;
  localdb = alpm_option_get_localdb ();
  if (localdb == NULL) return FALSE;

  result = find_packages (name, localdb);
  if (result == NULL) return FALSE;
  if (!alpm_list_count (result)) return FALSE;

  if (version == NULL)
    return TRUE;

  alpm_list_t *icmp = NULL;
  for (icmp = result; icmp; icmp = alpm_list_next (icmp))
    if (strcmp (alpm_pkg_get_version ((pmpkg_t *)icmp->data), version) == 0)
      return TRUE;

  return FALSE;
}

static void
filter_packages_installed (alpm_list_t *packages, gboolean filter)
{
  alpm_list_t *i;
  for (i = packages; i; )
    {
      if (((PackageSource *)i->data)->installed == filter)
	{
	  alpm_list_t *temp = i;
	  i = alpm_list_next (i);
	  package_source_free ((PackageSource *)temp->data);
	  my_list_remove_node (temp);
	  continue;
	}
      i = alpm_list_next (i);
    }
}

/*static void
filter_packages_multiavail (alpm_list_t *packages, gboolean)
{*/


/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	if (alpm_release () == -1)
	  pk_backend_error_code (backend,
				 PK_ERROR_ENUM_INTERNAL_ERROR,
				 "Failed to release control");
}

/**
 * backend_initalize:
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
				 PK_ERROR_ENUM_INTERNAL_ERROR,
				 "Failed to initialize package manager");
	    pk_debug ("alpm: %s", alpm_strerror (pm_errno));
	    //return;
	  }

	if (alpm_parse_config ("/etc/pacman.conf", NULL, "") != 0)
	  {
	    pk_backend_error_code (backend,
				 PK_ERROR_ENUM_INTERNAL_ERROR,
				 "Failed to parse config file");
	    pk_debug ("alpm: %s", alpm_strerror (pm_errno));
	    backend_destroy (backend);
	    return;
	  }


	if (alpm_db_register ("local") == NULL)
	  {
	    pk_backend_error_code (backend,
				 PK_ERROR_ENUM_INTERNAL_ERROR,
				 "Failed to load local database");
	    backend_destroy (backend);
	    return;
	  }
	pk_debug ("alpm: ready to go");
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, "glib2;2.14.0;i386;fedora",
			 "The GLib library");
	pk_backend_package (backend, 1, "gtk2;gtk2-2.11.6-6.fc8;i386;fedora",
			 "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	PkPackageId *id = pk_package_id_new_from_string (package_id);
	//pk_backend_description (backend, package_id, "unknown", PK_GROUP_ENUM_PROGRAMMING, "sdgd");
	pk_backend_finished (backend);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, "glib2;2.14.0;i386;fedora",
			 "The GLib library");
	pk_backend_package (backend, 1, "gtk2;gtk2-2.11.6-6.fc8;i386;fedora",
			 "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "powertop;1.8-1.fc8;i386;fedora",
			 "Power consumption monitor");
	pk_backend_package (backend, 1, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
			 "The Linux kernel (the core of the Linux operating system)");
	pk_backend_package (backend, 1, "gtkhtml2;2.19.1-4.fc8;i386;fedora", "An HTML widget for GTK+ 2.0");
	pk_backend_finished (backend);
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
  g_return_if_fail (backend != NULL);
  //alpm_list_t *syncdbs = alpm_option_get_syncdbs ();
  alpm_list_t *result = NULL;
  alpm_list_t *problems = NULL;
  PkPackageId *id = pk_package_id_new_from_string (package_id);
  pmtransflag_t flags = 0;

  flags |= PM_TRANS_FLAG_NODEPS;

  // Next generation code?
  /*for (; syncdbs; syncdbs = alpm_list_next (syncdbs))
    result = my_list_mmerge (result, find_packages (id->name, (pmdb_t *)syncdbs->data), list_cmp_fn);

  if (result == NULL)
    {
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
    if (pkg_equals_to ((pmpkg_t *)result->data, id->name, id->version))
      break;

  if (!result)
    {
      pk_backend_error_code (backend,
			     PK_ERROR_ENUM_PACKAGE_ID_INVALID,
			     "Package not found");
      pk_backend_finished (backend);
      alpm_list_free (result);
      alpm_list_free (syncdbs);
      pk_package_id_free (id);
      return;
    }*/

  if (alpm_trans_init (PM_TRANS_TYPE_SYNC, flags,
		       trans_event_cb, trans_conv_cb,
		       trans_prog_cb) == -1)
    {
      pk_backend_error_code (backend,
			     PK_ERROR_ENUM_TRANSACTION_ERROR,
			     alpm_strerror (pm_errno));
      pk_backend_finished (backend);
      alpm_list_free (result);
      pk_package_id_free (id);
      return;
    }

  alpm_trans_addtarget (id->name);

  if (alpm_trans_prepare (&problems) != 0)
    {
      pk_backend_error_code (backend,
			     PK_ERROR_ENUM_TRANSACTION_ERROR,
			     alpm_strerror (pm_errno));
      pk_backend_finished (backend);
      alpm_trans_release ();
      alpm_list_free (result);
      pk_package_id_free (id);
      return;
    }

  if (alpm_trans_commit (&problems) != 0)
    {
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
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	alpm_list_t *dbs = alpm_option_get_syncdbs ();
	//alpm_list_t *problems = NULL;

	if (alpm_trans_init (PM_TRANS_TYPE_SYNC, 0,
		        trans_event_cb, trans_conv_cb,
			trans_prog_cb) != 0)
	   {
	    pk_backend_error_code (backend,
				   PK_ERROR_ENUM_TRANSACTION_ERROR,
				   alpm_strerror (pm_errno));
	    pk_backend_finished (backend);
	    return;
	  }

	pk_debug ("alpm: %s", "transaction initialized");

/*	if (alpm_trans_prepare (&problems) != 0)
	  {
	    pk_backend_error_code (backend,
				   PK_ERROR_ENUM_TRANSACTION_ERROR,
				   alpm_strerror (pm_errno));
	    pk_backend_finished (backend);
	    return;
	  }*/

	alpm_list_t *i = NULL;
	pk_backend_change_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	g_timeout_add (PROGRESS_UPDATE_INTERVAL, update_subprogress, backend);
	for (i = dbs; i; i = alpm_list_next (i))
	  {
	    if (alpm_db_update (force, (pmdb_t *)i->data))
	      {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_ERROR,
				       alpm_strerror (pm_errno));
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
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	PkPackageId *id = pk_package_id_new_from_string (package_id);
	pmdb_t *localdb = alpm_option_get_localdb ();
	alpm_list_t *result = find_packages (id->name, localdb);
	pmtransflag_t flags = 0;
	alpm_list_t *problems = NULL;

	if (result == NULL)
	  {
	    pk_backend_error_code (backend,
				  PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
				  "Package is not installed");
	    pk_backend_finished (backend);
	    pk_package_id_free (id);
	    return;
	  }
	else if (alpm_list_count (result) != 1 || 
		 strcmp (alpm_pkg_get_name(((PackageSource *)result->data)->pkg), id->name) != 0)
	  {	    
	    pk_backend_error_code (backend,
				  PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
				  "Package is not installed");
	    alpm_list_free_inner (result, (alpm_list_fn_free)package_source_free);
	    pk_backend_finished (backend);
	    pk_package_id_free (id);
	    alpm_list_free (result);
	    return;
	  }

	if (allow_deps) flags |= PM_TRANS_FLAG_CASCADE;

	if (alpm_trans_init (PM_TRANS_TYPE_REMOVE, flags,
			 trans_event_cb, trans_conv_cb,
			 trans_prog_cb) == -1)
	  {
	    pk_backend_error_code (backend,
				  PK_ERROR_ENUM_TRANSACTION_ERROR,
				  alpm_strerror (pm_errno));
	    pk_backend_finished (backend);
	    alpm_list_free (result);
	    pk_package_id_free (id);
	    return;
	  }

	alpm_trans_addtarget (id->name);

	if (alpm_trans_prepare (&problems) != 0)
	  {
	    pk_backend_error_code (backend,
				  PK_ERROR_ENUM_TRANSACTION_ERROR,
				  alpm_strerror (pm_errno));
	    pk_backend_finished (backend);
	    alpm_trans_release ();
	    alpm_list_free (result);
	    pk_package_id_free (id);
	    return;
	  }

	if (alpm_trans_commit (&problems) != 0)
	  {
	    pk_backend_error_code (backend,
				  PK_ERROR_ENUM_TRANSACTION_ERROR,
				  alpm_strerror (pm_errno));
	    pk_backend_finished (backend);
	    alpm_trans_release ();
	    alpm_list_free (result);
	    pk_package_id_free (id);
	    return;
	  }

	alpm_list_free (result);
	pk_package_id_free (id);
	alpm_trans_release ();
	pk_backend_finished (backend);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	alpm_list_t *result = NULL;
	alpm_list_t *localresult = NULL;
	alpm_list_t *dbs = NULL;
	gchar **sections = NULL;
	gboolean installed = TRUE, ninstalled = TRUE;


	sections = g_strsplit (filter, ";", 0);
	int i = 0;
	while (sections[i]) {
	  if (strcmp(sections[i], "installed") == 0)
	    {
	      installed = FALSE;
	    }
	  if (strcmp(sections[i], "~installed") == 0)
	    {
	      ninstalled = FALSE;
	    }

	  i++;
	}
	g_strfreev (sections);

	pk_debug ("alpm: searching for \"%s\" - searching in installed: %i, ~installed: %i",
		  search, installed, ninstalled);

	if (installed && !ninstalled) dbs = alpm_list_add (dbs, alpm_option_get_localdb ());
	if (ninstalled) dbs = my_list_mmerge (dbs, alpm_option_get_syncdbs (), list_cmp_fn);

	for (; dbs; dbs = alpm_list_next (dbs))
	  result  = my_list_mmerge (result, find_packages (search, (pmdb_t *)dbs->data), list_cmp_fn);

	if (ninstalled && installed)
	  {
	   pmdb_t *localdb = alpm_option_get_localdb ();
	   if (localdb != NULL)
	     {
	       localresult = find_packages (search, localdb);
	       alpm_list_t *i = NULL;
	       for (i = alpm_list_first (result); i; i = alpm_list_next (i))
		 {
		   alpm_list_t *icmp = NULL;
		   for (icmp = localresult; icmp; )
		     if (pkg_equal ((pmpkg_t *)icmp->data, (pmpkg_t *)i->data))
		       {
			 alpm_list_t *tmp = icmp;
			 icmp = alpm_list_next (icmp);
			 my_list_remove_node (tmp);
		       }
		     else icmp = alpm_list_next (icmp);
		 }
	     }
	   else  pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
					"Could not find local db");
	   result = my_list_mmerge (result, localresult, list_cmp_fn);
	  }

	if (!installed) filter_packages_installed (result, TRUE);
	if (!ninstalled) filter_packages_installed (result, FALSE);

	add_packages_from_list (backend, alpm_list_first (result));
	pk_backend_finished (backend);
}

/**
 * backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, package_id, "The same thing");
	pk_backend_finished (backend);
}

static gboolean
backend_update_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_change_status (backend, PK_STATUS_ENUM_UPDATE);
	progress_percentage += 10;
	pk_backend_change_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_change_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	progress_percentage = 0;
	pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, NULL);
	g_timeout_add (1000, backend_update_system_timeout, backend);
}

PK_BACKEND_OPTIONS (
	"alpm backend",					/* description */
	"0.0.1",					/* version */
	"Andreas Obergrusberger <tradiaz@yahoo.de>",	/* author */
	backend_initialize,				/* initalize */
	backend_destroy,				/* destroy */
	NULL,						/* get_groups */
	NULL,						/* get_filters */
	NULL,						/* cancel */
 	backend_get_depends,				/* get_depends */
	backend_get_description,			/* get_description */
	backend_get_requires,				/* get_requires */
	NULL,						/* get_update_detail */
	backend_get_updates,				/* get_updates */
	backend_install_package,			/* install_package */
	NULL,						/* install_file */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_package,				/* remove_package */
	backend_search_details,				/* search_details */
	backend_search_file,				/* search_file */
	backend_search_group,				/* search_group */
	backend_search_name,				/* search_name */
	backend_update_package,				/* update_package */
	backend_update_system				/* update_system */
);

