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
 * pk_console_pad_string:
 **/
static gchar *
pk_console_pad_string (const gchar *data, guint length, guint *extra)
{
	gint size;
	gchar *text;
	gchar *padding;

	if (extra != NULL) {
		*extra = 0;
	}
	size = length;
	if (data != NULL) {
		/* ITS4: ignore, only used for formatting */
		size = (length - strlen(data));
		if (size < 0) {
			if (extra != NULL) {
				*extra = -size;
			}
			size = 0;
		}
	}
	padding = g_strnfill (size, ' ');
	if (data == NULL) {
		return padding;
	}
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
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
	guint extra = 0;

	/* pass this out */
	info_text = pk_console_pad_string (pk_info_enum_to_text (info), 12, NULL);

	spacing = pk_package_id_new ();
	ident = pk_package_id_new_from_string (package_id);

	/* these numbers are guesses */
	extra = 0;
	spacing->name = pk_console_pad_string (ident->name, 20, &extra);
	spacing->arch = pk_console_pad_string (ident->arch, 7-extra, &extra);
	spacing->version = pk_console_pad_string (ident->version, 15-extra, &extra);
	spacing->data = pk_console_pad_string (ident->data, 12-extra, &extra);

	/* pretty print */
	g_print ("%s %s %s %s %s %s\n", info_text, spacing->name,
		 spacing->arch, spacing->version, spacing->data, summary);

	/* free all the data */
	g_free (info_text);
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

const gchar *summary =
	"PackageKit Console Interface\n"
	"\n"
	"Subcommands:\n"
	"  search name|details|group|file data\n"
	"  install <package_id>\n"
	"  remove <package_id>\n"
	"  update <package_id>\n"
	"  refresh\n"
	"  resolve\n"
	"  force-refresh\n"
	"  update-system\n"
	"  get updates\n"
	"  get depends <package_id>\n"
	"  get requires <package_id>\n"
	"  get description <package_id>\n"
	"  get updatedetail <package_id>\n"
	"  get actions\n"
	"  get groups\n"
	"  get filters\n"
	"  get transactions\n"
	"  get repos\n"
	"  enable-repo <repo_id>\n"
	"  disable-repo <repo_id>\n"
	"\n"
	"  package_id is typically gimp;2:2.4.0-0.rc1.1.fc8;i386;development";

/**
 * pk_client_wait:
 **/
static gboolean
pk_client_wait (void)
{
	pk_debug ("starting loop");
	g_main_loop_run (loop);
	return TRUE;
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
 * pk_console_install_package:
 **/
static gboolean
pk_console_install_package (PkClient *client, const gchar *package_id)
{
//xxx
	gboolean ret;
	gboolean valid;
	PkClient *client_resolve;
	valid = pk_package_id_check (package_id);

	/* have we passed a complete package_id? */
	if (valid == TRUE) {
		return pk_client_install_package (client, package_id);
	}

	/* we need to resolve it */
	client_resolve = pk_client_new ();
	g_signal_connect (client_resolve, "finished",
			  G_CALLBACK (pk_console_finished_cb), NULL);
	ret = pk_client_resolve (client_resolve, package_id);
	if (ret == FALSE) {
		pk_warning ("Resolve not supported");
	} else {
		g_main_loop_run (loop);
	}
	pk_error ("resolve functionality not finished yet");
	return TRUE;
}

/**
 * pk_console_process_commands:
 **/
static gboolean
pk_console_process_commands (PkClient *client, int argc, char *argv[], GError **error)
{
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	gboolean wait = FALSE;
	PkEnumList *elist;

	mode = argv[1];
	if (argc > 2) {
		value = argv[2];
	}
	if (argc > 3) {
		details = argv[3];
	}

	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a search type");
			return FALSE;
		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_name (client, "none", details);
			}
		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_details (client, "none", details);
			}
		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_group (client, "none", details);
			}
		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_file (client, "none", details);
			}
		} else {
			g_set_error (error, 0, 0, "invalid search type");
		}
	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to install");
			return FALSE;
		} else {
			wait = pk_console_install_package (client, value);
		}
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to remove");
			return FALSE;
		} else {
			wait = pk_client_remove_package (client, value, FALSE);
		}
	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to update");
			return FALSE;
		} else {
			wait = pk_client_update_package (client, value);
		}
	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package name to resolve");
			return FALSE;
		} else {
			wait = pk_client_resolve (client, value);
		}
	} else if (strcmp (mode, "enable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a repo name");
			return FALSE;
		} else {
			pk_client_repo_enable (client, value, TRUE);
		}
	} else if (strcmp (mode, "disable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a repo name");
			return FALSE;
		} else {
			wait = pk_client_repo_enable (client, value, FALSE);
		}
	} else if (strcmp (mode, "get") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a get type");
			return FALSE;
		} else if (strcmp (value, "depends") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_get_depends (client, details);
			}
		} else if (strcmp (value, "updatedetail") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_get_update_detail (client, details);
			}
		} else if (strcmp (value, "requires") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_get_requires (client, details);
			}
		} else if (strcmp (value, "description") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a package to find the description for");
				return FALSE;
			} else {
				wait = pk_client_get_description (client, details);
			}
		} else if (strcmp (value, "updates") == 0) {
			wait = pk_client_get_updates (client);
		} else if (strcmp (value, "actions") == 0) {
			elist = pk_client_get_actions (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "filters") == 0) {
			elist = pk_client_get_filters (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "repos") == 0) {
			wait = pk_client_get_repo_list (client);
		} else if (strcmp (value, "groups") == 0) {
			elist = pk_client_get_groups (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "transactions") == 0) {
			wait = pk_client_get_old_transactions (client, 10);
		} else {
			g_set_error (error, 0, 0, "invalid get type");
		}
	} else if (strcmp (mode, "update-system") == 0) {
		wait = pk_client_update_system (client);
	} else if (strcmp (mode, "refresh") == 0) {
		wait = pk_client_refresh_cache (client, FALSE);
	} else if (strcmp (mode, "force-refresh") == 0) {
		wait = pk_client_refresh_cache (client, TRUE);
	} else {
		g_set_error (error, 0, 0, "option not yet supported");
	}

	/* only wait if success */
	if (wait == TRUE) {
		pk_client_wait ();
	}
	return TRUE;
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
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	gchar *options_help;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			"Show the program version and exit", NULL},
		{ NULL}
	};

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

	context = g_option_context_new (_("SUBCOMMAND"));
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	if (program_version == TRUE) {
		g_print (VERSION "\n");
		return 0;
	}

	if (argc < 2) {
		g_print (options_help);
		return 1;
	}

	pk_debug_init (verbose);
	loop = g_main_loop_new (NULL, FALSE);

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

	/* run the commands */
	pk_console_process_commands (client, argc, argv, &error);
	if (error != NULL) {
		g_print ("Error:\n  %s\n\n", error->message);
		g_error_free (error);
		g_print (options_help);
	}

	g_free (options_help);
	g_object_unref (client);

	return 0;
}
