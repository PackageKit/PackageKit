/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-enum-list.h>

static GMainLoop *loop = NULL;

/**
 * pk_console_make_space:
 **/
static gchar *
pk_console_make_space (const gchar *data, guint length, guint *extra)
{
	gint size;
	gchar *padding;

	if (extra != NULL) {
		*extra = 0;
	}
	size = length;
	if (data != NULL) {
		size = (length - strlen(data));
		if (size < 0) {
			if (extra != NULL) {
				*extra = -size;
			}
			size = 0;
		}
	}
	padding = g_strnfill (size, ' ');
	return padding;
}

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id, const gchar *summary, gpointer data)
{
	PkPackageId *ident;
	PkPackageId *spacing;
	gchar *info_text;
	guint extra;

	info_text = pk_console_make_space (pk_info_enum_to_text (info), 10, NULL);
	spacing = pk_package_id_new ();
	ident = pk_package_id_new_from_string (package_id);

	/* these numbers are guesses */
	extra = 0;
	spacing->name = pk_console_make_space (ident->name, 20, &extra);
	spacing->version = pk_console_make_space (ident->version, 15-extra, &extra);
	spacing->arch = pk_console_make_space (ident->arch, 7-extra, &extra);
	spacing->data = pk_console_make_space (ident->data, 7-extra, &extra);

	/* pretty print */
	g_print ("%s %s%s %s%s %s%s %s%s %s\n", info_text,
		 ident->name, spacing->name,
		 ident->version, spacing->version,
		 ident->arch, spacing->arch,
		 ident->data, spacing->data,
		 summary);

	/* free all the data */
	pk_package_id_free (ident);
	pk_package_id_free (spacing);
}

/**
 * pk_console_transaction_cb:
 **/
static void
pk_console_transaction_cb (PkClient *client, const gchar *tid, const gchar *timespec,
			   gboolean succeeded, PkRoleEnum role, guint duration,
			   const gchar *data, gpointer user_data)
{
	const gchar *role_text;
	role_text = pk_role_enum_to_text (role);
	g_print ("tid          : %s\n", tid);
	g_print (" timespec    : %s\n", timespec);
	g_print (" succeeded   : %i\n", succeeded);
	g_print (" role        : %s\n", role_text);
	g_print (" duration    : %i (seconds)\n", duration);
	g_print (" data        : %s\n", data);
}

/**
 * pk_console_update_detail_cb:
 **/
static void
pk_console_update_detail_cb (PkClient *client, const gchar *package_id,
			     const gchar *updates, const gchar *obsoletes,
			     const gchar *url, const gchar *restart,
			     const gchar *update_text, gpointer data)
{
	g_print ("update-detail\n");
	g_print ("  package:    '%s'\n", package_id);
	g_print ("  updates:    '%s'\n", updates);
	g_print ("  obsoletes:  '%s'\n", obsoletes);
	g_print ("  url:        '%s'\n", url);
	g_print ("  restart:    '%s'\n", restart);
	g_print ("  update_text:'%s'\n", update_text);
}

/**
 * pk_console_repo_detail_cb:
 **/
static void
pk_console_repo_detail_cb (PkClient *client, const gchar *repo_id,
			   const gchar *description, gboolean enabled, gpointer data)
{
	g_print ("[%s]\n", repo_id);
	g_print ("  %i, %s\n", enabled, description);
}

/**
 * pk_console_percentage_changed_cb:
 **/
static void
pk_console_percentage_changed_cb (PkClient *client, guint percentage, gpointer data)
{
	g_print ("%i%%\n", percentage);
}

/**
 * pk_console_usage:
 **/
static void
pk_console_usage (const gchar *error)
{
	if (error != NULL) {
		g_print ("Error: %s\n", error);
	}
	g_print ("usage:\n");
	g_print ("  pkcon [verbose] search name|details|group|file data\n");
	g_print ("  pkcon [verbose] install <package_id>\n");
	g_print ("  pkcon [verbose] remove <package_id>\n");
	g_print ("  pkcon [verbose] update <package_id>\n");
	g_print ("  pkcon [verbose] refresh\n");
	g_print ("  pkcon [verbose] resolve\n");
	g_print ("  pkcon [verbose] force-refresh\n");
	g_print ("  pkcon [verbose] update-system\n");
	g_print ("  pkcon [verbose] get updates\n");
	g_print ("  pkcon [verbose] get depends <package_id>\n");
	g_print ("  pkcon [verbose] get requires <package_id>\n");
	g_print ("  pkcon [verbose] get description <package_id>\n");
	g_print ("  pkcon [verbose] get updatedetail <package_id>\n");
	g_print ("  pkcon [verbose] get actions\n");
	g_print ("  pkcon [verbose] get groups\n");
	g_print ("  pkcon [verbose] get filters\n");
	g_print ("  pkcon [verbose] get transactions\n");
	g_print ("  pkcon [verbose] get repos\n");
	g_print ("  pkcon [verbose] enable-repo <repo_id>\n");
	g_print ("  pkcon [verbose] disable-repo <repo_id>\n");
	g_print ("\n");
	g_print ("    package_id is typically gimp;2:2.4.0-0.rc1.1.fc8;i386;development\n");
}

/**
 * pk_client_wait:
 **/
static gboolean
pk_client_wait (void)
{
	pk_debug ("starting loop");
	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
	return TRUE;
}

/**
 * pk_console_parse_multiple_commands:
 **/
static void
pk_console_parse_multiple_commands (PkClient *client, GPtrArray *array)
{
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	guint remove;
	PkEnumList *elist;

	mode = g_ptr_array_index (array, 0);
	if (array->len > 1) {
		value = g_ptr_array_index (array, 1);
	}
	if (array->len > 2) {
		details = g_ptr_array_index (array, 2);
	}
	remove = 1;

	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a search type");
			remove = 1;
			goto out;
		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_search_name (client, "none", details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_search_details (client, "none", details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_search_group (client, "none", details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_search_file (client, "none", details);
				pk_client_wait ();
				remove = 3;
			}
		} else {
			pk_console_usage ("invalid search type");
		}
	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package to install");
			remove = 1;
			goto out;
		} else {
			pk_client_install_package (client, value);
			pk_client_wait ();
			remove = 2;
		}
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package to remove");
			remove = 1;
			goto out;
		} else {
			pk_client_remove_package (client, value, FALSE);
			pk_client_wait ();
			remove = 2;
		}
	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package to update");
			remove = 1;
			goto out;
		} else {
			pk_client_update_package (client, value);
			pk_client_wait ();
			remove = 2;
		}
	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package name to resolve");
			remove = 1;
			goto out;
		} else {
			pk_client_resolve (client, value);
			pk_client_wait ();
			remove = 2;
		}
	} else if (strcmp (mode, "enable-repo") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a repo name");
			remove = 1;
			goto out;
		} else {
			pk_client_repo_enable (client, value, TRUE);
			remove = 2;
		}
	} else if (strcmp (mode, "disable-repo") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a repo name");
			remove = 1;
			goto out;
		} else {
			pk_client_repo_enable (client, value, FALSE);
			remove = 2;
		}
	} else if (strcmp (mode, "get") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a get type");
			remove = 1;
			goto out;
		} else if (strcmp (value, "depends") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_get_depends (client, details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "updatedetail") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_get_update_detail (client, details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "requires") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a search term");
				remove = 2;
				goto out;
			} else {
				pk_client_get_requires (client, details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "description") == 0) {
			if (details == NULL) {
				pk_console_usage ("you need to specify a package to find the description for");
				remove = 2;
				goto out;
			} else {
				pk_client_get_description (client, details);
				pk_client_wait ();
				remove = 3;
			}
		} else if (strcmp (value, "updates") == 0) {
			pk_client_get_updates (client);
			pk_client_wait ();
			remove = 2;
		} else if (strcmp (value, "actions") == 0) {
			elist = pk_client_get_actions (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
			remove = 2;
		} else if (strcmp (value, "filters") == 0) {
			elist = pk_client_get_filters (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
			remove = 2;
		} else if (strcmp (value, "repos") == 0) {
			pk_client_get_repo_list (client);
			pk_client_wait ();
			remove = 2;
		} else if (strcmp (value, "groups") == 0) {
			elist = pk_client_get_groups (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
			remove = 2;
		} else if (strcmp (value, "transactions") == 0) {
			pk_client_get_old_transactions (client, 10);
			pk_client_wait ();
			remove = 2;
		} else {
			pk_console_usage ("invalid get type");
		}
	} else if (strcmp (mode, "debug") == 0) {
		pk_debug_init (TRUE);
	} else if (strcmp (mode, "verbose") == 0) {
		pk_debug_init (TRUE);
	} else if (strcmp (mode, "update-system") == 0) {
		pk_client_update_system (client);
	} else if (strcmp (mode, "refresh") == 0) {
		pk_client_refresh_cache (client, FALSE);
	} else if (strcmp (mode, "force-refresh") == 0) {
		pk_client_refresh_cache (client, TRUE);
	} else {
		pk_console_usage ("option not yet supported");
	}

out:
	/* remove the right number of items from the pointer index */
	g_ptr_array_remove_index (array, 0);
	if (remove > 1) {
		g_ptr_array_remove_index (array, 0);
	}
	if (remove > 2) {
		g_ptr_array_remove_index (array, 0);
	}
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, gpointer data)
{
	g_print ("Runtime was %i seconds\n", runtime);
	if (loop != NULL) {
		g_main_loop_quit (loop);
	}
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	g_print ("Error: %s : %s\n", pk_error_enum_to_text (error_code), details);
}

/**
 * pk_console_description_cb:
 **/
static void
pk_console_description_cb (PkClient *client, const gchar *package_id,
			   const gchar *licence, PkGroupEnum group,
			   const gchar *description, const gchar *url,
			   gulong size, const gchar *filelist, gpointer data)
{
	g_print ("description\n");
	g_print ("  package:     '%s'\n", package_id);
	g_print ("  licence:     '%s'\n", licence);
	g_print ("  group:       '%s'\n", pk_group_enum_to_text (group));
	g_print ("  description: '%s'\n", description);
	g_print ("  size:        '%ld' bytes\n", size);
	/* filelist is probably too long to just dump to the screen */
	/* g_print ("  files:       '%s'\n", filelist); */
	g_print ("  url:         '%s'\n", url);
}

/**
 * pk_console_repo_signature_required_cb:
 **/
static void
pk_console_repo_signature_required_cb (PkClient *client, const gchar *repository_name, const gchar *key_url,
				       const gchar *key_userid, const gchar *key_id, const gchar *key_fingerprint,
				       const gchar *key_timestamp, PkSigTypeEnum type, gpointer data)
{
	g_print ("Signature Required\n");
	g_print ("  repo name:       '%s'\n", repository_name);
	g_print ("  key url:         '%s'\n", key_url);
	g_print ("  key userid:      '%s'\n", key_userid);
	g_print ("  key id:          '%s'\n", key_id);
	g_print ("  key fingerprint: '%s'\n", key_fingerprint);
	g_print ("  key timestamp:   '%s'\n", key_timestamp);
	g_print ("  key type:        '%s'\n", pk_sig_type_enum_to_text (type));

}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *system_connection;
	GError *error = NULL;
	PkClient *client;
	GPtrArray *array;
	guint i;

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	if (argc < 2) {
		pk_console_usage (NULL);
		return 1;
	}

	client = pk_client_new ();
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_console_package_cb), NULL);
	g_signal_connect (client, "transaction",
			  G_CALLBACK (pk_console_transaction_cb), NULL);
	g_signal_connect (client, "description",
			  G_CALLBACK (pk_console_description_cb), NULL);
	g_signal_connect (client, "repo-signature-required",
			  G_CALLBACK (pk_console_repo_signature_required_cb), NULL);
	g_signal_connect (client, "update-detail",
			  G_CALLBACK (pk_console_update_detail_cb), NULL);
	g_signal_connect (client, "repo-detail",
			  G_CALLBACK (pk_console_repo_detail_cb), NULL);
	g_signal_connect (client, "percentage-changed",
			  G_CALLBACK (pk_console_percentage_changed_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_console_finished_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (pk_console_error_code_cb), NULL);

	/* add argv to a pointer array */
	array = g_ptr_array_new ();
	for (i=1; i<argc; i++) {
		g_ptr_array_add (array, (gpointer) argv[i]);
	}
	/* process all the commands */
	while (array->len > 0) {
		pk_console_parse_multiple_commands (client, array);
	}

	g_ptr_array_free (array, TRUE);
	g_object_unref (client);

	return 0;
}
