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

#include "config.h"

#include <string.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-tools-common.h"

#define PK_MAX_PATH_LEN 1023

typedef enum {
	PK_CNF_POLICY_RUN,
	PK_CNF_POLICY_INSTALL,
	PK_CNF_POLICY_ASK,
	PK_CNF_POLICY_WARN,
	PK_CNF_POLICY_UNKNOWN
} PkCnfPolicy;

typedef struct {
	PkCnfPolicy	 single_match;
	PkCnfPolicy	 multiple_match;
	PkCnfPolicy	 single_install;
	PkCnfPolicy	 multiple_install;
	gboolean	 software_source_search;
	gchar		**locations;
} PkCnfPolicyConfig;

/**
 * pk_cnf_find_alternatives_swizzle:
 *
 * Swizzle ordering, e.g. amke -> make
 **/
static void
pk_cnf_find_alternatives_swizzle (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar swap;

	/*  */
	for (i=0; i<len-1; i++) {
		possible = g_strdup (cmd);
		swap = possible[i];
		possible[i] = possible[i+1];
		possible[i+1] = swap;
		g_ptr_array_add (array, possible);
	}
}

/**
 * pk_cnf_find_alternatives_replace:
 *
 * Replace some easily confused chars, e.g. gnome-power-managir to gnome-power-manager
 **/
static void
pk_cnf_find_alternatives_replace (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar temp;

	/* replace some easily confused chars */
	for (i=0; i<len; i++) {
		temp = cmd[i];
		if (temp == 'i') {
			possible = g_strdup (cmd);
			possible[i] = 'e';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'e') {
			possible = g_strdup (cmd);
			possible[i] = 'i';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'i') {
			possible = g_strdup (cmd);
			possible[i] = 'o';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'c') {
			possible = g_strdup (cmd);
			possible[i] = 's';
			g_ptr_array_add (array, possible);
		}
		if (temp == 's') {
			possible = g_strdup (cmd);
			possible[i] = 'c';
			g_ptr_array_add (array, possible);
		}
		if (temp == 's') {
			possible = g_strdup (cmd);
			possible[i] = 'z';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'z') {
			possible = g_strdup (cmd);
			possible[i] = 's';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'k') {
			possible = g_strdup (cmd);
			possible[i] = 'c';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'c') {
			possible = g_strdup (cmd);
			possible[i] = 'k';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_truncate:
 *
 * Truncate first and last char, so lshall -> lshal
 **/
static void
pk_cnf_find_alternatives_truncate (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;

	/* truncate last char */
	possible = g_strdup (cmd);
	possible[len-1] = '\0';
	g_ptr_array_add (array, possible);

	/* truncate first char */
	possible = g_strdup (cmd);
	for (i=0; i<len-1; i++)
		possible[i] = possible[i+1];
	possible[len-1] = '\0';
	g_ptr_array_add (array, possible);
}

/**
 * pk_cnf_find_alternatives_remove_double:
 *
 * Remove double chars, e.g. gnome-power-manaager -> gnome-power-manager
 **/
static void
pk_cnf_find_alternatives_remove_double (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i, j;
	gchar *possible;

	for (i=1; i<len; i++) {
		if (cmd[i-1] == cmd[i]) {
			possible = g_strdup (cmd);
			for (j=i; j<len; j++)
				possible[j] = possible[j+1];
			possible[len-1] = '\0';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_locale:
 *
 * Fix British spellings, e.g. colourdiff -> colordiff
 **/
static void
pk_cnf_find_alternatives_locale (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i, j;
	gchar *possible;

	for (i=1; i<len; i++) {
		if (cmd[i-1] == 'o' && cmd[i] == 'u') {
			possible = g_strdup (cmd);
			for (j=i; j<len; j++)
				possible[j] = possible[j+1];
			possible[len-1] = '\0';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_case:
 *
 * Remove double chars, e.g. Lshal -> lshal
 **/
static void
pk_cnf_find_alternatives_case (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar temp;

	for (i=0; i<len; i++) {
		temp = g_ascii_tolower (cmd[i]);
		if (temp != cmd[i]) {
			possible = g_strdup (cmd);
			possible[i] = temp;
			g_ptr_array_add (array, possible);
		}
		temp = g_ascii_toupper (cmd[i]);
		if (temp != cmd[i]) {
			possible = g_strdup (cmd);
			possible[i] = temp;
			g_ptr_array_add (array, possible);
		}
	}

	/* all lower */
	possible = g_strdup (cmd);
	for (i=0; i<len; i++)
		possible[i] = g_ascii_tolower (cmd[i]);
	if (strcmp (possible, cmd) != 0)
		g_ptr_array_add (array, possible);
	else
		g_free (possible);

	/* all upper */
	possible = g_strdup (cmd);
	for (i=0; i<len; i++)
		possible[i] = g_ascii_toupper (cmd[i]);
	if (strcmp (possible, cmd) != 0)
		g_ptr_array_add (array, possible);
	else
		g_free (possible);
}

/**
 * pk_cnf_find_alternatives:
 *
 * Generate a list of commands it might be
 **/
static GPtrArray *
pk_cnf_find_alternatives (const gchar *cmd, guint len)
{
	GPtrArray *array;
	GPtrArray *possible;
	GPtrArray *unique;
	const gchar *cmdt;
	const gchar *cmdt2;
	guint i, j;
	gchar buffer_bin[PK_MAX_PATH_LEN+1];
	gchar buffer_sbin[PK_MAX_PATH_LEN+1];
	gboolean ret;

	array = g_ptr_array_new ();
	possible = g_ptr_array_new ();
	unique = g_ptr_array_new ();
	pk_cnf_find_alternatives_swizzle (cmd, len, possible);
	pk_cnf_find_alternatives_replace (cmd, len, possible);
	if (len > 3)
		pk_cnf_find_alternatives_truncate (cmd, len, possible);
	pk_cnf_find_alternatives_remove_double (cmd, len, possible);
	pk_cnf_find_alternatives_case (cmd, len, possible);
	pk_cnf_find_alternatives_locale (cmd, len, possible);

	/* remove duplicates using a helper array */
	for (i=0; i<possible->len; i++) {
		cmdt = g_ptr_array_index (possible, i);
		ret = TRUE;
		for (j=0; j<unique->len; j++) {
			cmdt2 = g_ptr_array_index (unique, j);
			if (strcmp (cmdt, cmdt2) == 0) {
				ret = FALSE;
				break;
			}
		}
		/* only add if not duplicate */
		if (ret)
			g_ptr_array_add (unique, (gpointer) cmdt);
	}

	/* ITS4: ignore, source is constant size */
	strncpy (buffer_bin, "/usr/bin/", PK_MAX_PATH_LEN);

	/* ITS4: ignore, source is constant size */
	strncpy (buffer_sbin, "/usr/sbin/", PK_MAX_PATH_LEN);

	/* remove any that exist (fast path) */
	for (i=0; i<unique->len; i++) {
		cmdt = g_ptr_array_index (unique, i);

		/* ITS4: ignore, size is checked */
		strncpy (&buffer_bin[9], cmdt, PK_MAX_PATH_LEN-9);

		/* ITS4: ignore, size is checked */
		strncpy (&buffer_sbin[10], cmdt, PK_MAX_PATH_LEN-10);

		/* does file exist in bindir (common case) */
		ret = g_file_test (buffer_bin, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE);
		if (ret) {
			g_ptr_array_add (array, g_strdup (cmdt));
			continue;
		}

		/* does file exist in sbindir */
		ret = g_file_test (buffer_sbin, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE);
		if (ret)
			g_ptr_array_add (array, g_strdup (cmdt));
	}

	g_ptr_array_foreach (possible, (GFunc) g_free, NULL);
	g_ptr_array_free (possible, TRUE);
	g_ptr_array_free (unique, TRUE);
	return array;
}

/**
 * pk_cnf_find_available:
 *
 * Find software we could install
 **/
static gboolean
pk_cnf_find_available (GPtrArray *array, const gchar *prefix, const gchar *cmd)
{
	PkClient *client;
	PkControl *control;
	GError *error = NULL;
	PkBitfield roles;
	PkBitfield filters;
	gboolean ret;
	guint i, len;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;
	gchar *path = NULL;

	control = pk_control_new ();
	client = pk_client_new ();
	pk_client_set_synchronous (client, TRUE, NULL);
	pk_client_set_use_buffer (client, TRUE, NULL);
	roles = pk_control_get_actions (control, NULL);

	/* can we search the repos */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		egg_warning ("cannot search file");
		goto out;
	}

	/* reset instance */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to reset the client, this shouldn't happen */
		egg_warning ("%s: %s", _("Failed to reset client"), error->message);
		g_error_free (error);
		goto out;
	}

	/* do search */
	path = g_build_filename (prefix, cmd, NULL);
	egg_debug ("searching for %s", path);
	filters = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1);
	ret = pk_client_search_file (client, filters, path, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		egg_warning ("%s: %s", _("Failed to search for file"), error->message);
		g_error_free (error);
		goto out;
	}

	/* get package list */
	list = pk_client_get_package_list (client);
	if (list == NULL) {
		egg_warning ("failed to get list");
		ret = FALSE;
		goto out;
	}

	/* nothing found */
	len = PK_OBJ_LIST(list)->len;
	if (len == 0)
		goto out;

	/* add all package names */
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		g_ptr_array_add (array, g_strdup (obj->id->name));
		egg_warning ("obj->id->name=%s", obj->id->name);
	}
out:
	if (list != NULL)
		g_object_unref (list);
	g_object_unref (control);
	g_object_unref (client);
	g_free (path);

	return ret;
}

/**
 * pk_cnf_get_policy_from_string:
 **/
static PkCnfPolicy
pk_cnf_get_policy_from_string (const gchar *policy_text)
{
	if (policy_text == NULL)
		return PK_CNF_POLICY_UNKNOWN;
	if (strcmp (policy_text, "run") == 0)
		return PK_CNF_POLICY_RUN;
	if (strcmp (policy_text, "ask") == 0)
		return PK_CNF_POLICY_ASK;
	if (strcmp (policy_text, "warn") == 0)
		return PK_CNF_POLICY_WARN;
	return PK_CNF_POLICY_UNKNOWN;
}

/**
 * pk_cnf_get_policy_from_file:
 **/
static PkCnfPolicy
pk_cnf_get_policy_from_file (GKeyFile *file, const gchar *key)
{
	PkCnfPolicy policy;
	gchar *policy_text;
	GError *error = NULL;

	/* get from file */
	policy_text = g_key_file_get_string (file, "CommandNotFound", key, &error);
	if (policy_text == NULL) {
		egg_warning ("failed to get key %s: %s", key, error->message);
		g_error_free (error);
	}

	/* convert to enum */
	policy = pk_cnf_get_policy_from_string (policy_text);
	g_free (policy_text);
	return policy;
}

/**
 * pk_cnf_get_config:
 **/
static PkCnfPolicyConfig *
pk_cnf_get_config (void)
{
	GKeyFile *file;
	gchar *path;
	gboolean ret;
	GError *error = NULL;
	PkCnfPolicyConfig *config;

	/* create */
	config = g_new0 (PkCnfPolicyConfig, 1);

	/* set defaults if the conf file is not found */
	config->single_match = PK_CNF_POLICY_UNKNOWN;
	config->multiple_match = PK_CNF_POLICY_UNKNOWN;
	config->single_install = PK_CNF_POLICY_UNKNOWN;
	config->multiple_install = PK_CNF_POLICY_UNKNOWN;
	config->software_source_search = FALSE;
	config->locations = NULL;

	/* load file */
	file = g_key_file_new ();
	path = g_build_filename (SYSCONFDIR, "PackageKit", "CommandNotFound.conf", NULL);
	ret = g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &error);
	if (!ret) {
		egg_warning ("failed to open policy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get data */
	config->single_match = pk_cnf_get_policy_from_file (file, "SingleMatch");
	config->multiple_match = pk_cnf_get_policy_from_file (file, "MultipleMatch");
	config->single_install = pk_cnf_get_policy_from_file (file, "SingleInstall");
	config->multiple_install = pk_cnf_get_policy_from_file (file, "MultipleInstall");
	config->software_source_search = g_key_file_get_boolean (file, "CommandNotFound", "SoftwareSourceSearch", NULL);
	config->locations = g_key_file_get_string_list (file, "CommandNotFound", "SearchLocations", NULL, NULL);

	/* fallback */
	if (config->locations == NULL) {
		egg_warning ("not found SearchLocations, using fallback");
		config->locations = g_strsplit ("/usr/bin;/usr/sbin", ";", -1);
	}
out:
	g_free (path);
	g_key_file_free (file);
	return config;
}

/**
 * pk_cnf_spawn_command:
 **/
static gboolean
pk_cnf_spawn_command (const gchar *exec)
{
	gboolean ret;
	GError *error = NULL;
	ret = g_spawn_command_line_sync (exec, NULL, NULL, NULL, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to launch the executable, the error follows */
		g_print ("%s '%s': %s", _("Failed to launch:"), exec, error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean verbose = FALSE;
	GOptionContext *context;
	GPtrArray *array = NULL;
	GPtrArray *available = NULL;
	PkCnfPolicyConfig *config = NULL;
	guint i;
	guint len;
	gchar *text;
	const gchar *possible;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the command is not found */
	g_option_context_set_summary (context, _("PackageKit Command Not Found"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	/* no input */
	if (argv[1] == NULL)
		goto out;

	/* get policy config */
	config = pk_cnf_get_config ();

	/* get length */
	len = egg_strlen (argv[1], 1024);
	if (len < 1)
		goto out;

	/* generate swizzles */
	array = pk_cnf_find_alternatives (argv[1], len);

	/* TRANSLATORS: the prefix of all the output telling the user why it's not executing */
	g_print ("%s ", _("Command not found."));

	/* one exact possibility */
	if (array->len == 1) {
		possible = g_ptr_array_index (array, 0);
		if (config->single_match == PK_CNF_POLICY_WARN) {
			/* TRANSLATORS: tell the user what we think the command is */
			g_print ("%s '%s'\n", _("Similar command is:"), possible);

		/* run */
		} else if (config->single_match == PK_CNF_POLICY_RUN) {
			pk_cnf_spawn_command (possible);

		/* ask */
		} else if (config->single_match == PK_CNF_POLICY_ASK) {
			/* TRANSLATORS: Ask the user if we should run the similar command */
			text = g_strdup_printf ("%s %s", _("Run similar command:"), possible);
			ret = pk_console_get_prompt (text, TRUE);
			if (ret)
				pk_cnf_spawn_command (possible);
			g_free (text);
		}
		goto out;

	/* multiple choice */
	} else if (array->len > 1) {
		if (config->multiple_match == PK_CNF_POLICY_WARN) {
			/* TRANSLATORS: show the user a list of commands that they could have meant */
			g_print ("%s:\n", _("Similar commands are:"));
			for (i=0; i<array->len; i++) {
				possible = g_ptr_array_index (array, i);
				g_print ("'%s'\n", possible);
			}

		/* ask */
		} else if (config->multiple_match == PK_CNF_POLICY_ASK) {
			/* TRANSLATORS: show the user a list of commands we could run */
			g_print ("%s:\n", _("Similar commands are:"));
			for (i=0; i<array->len; i++) {
				possible = g_ptr_array_index (array, i);
				g_print ("%i\t'%s'\n", i+1, possible);
			}

			/* TRANSLATORS: ask the user to choose a file to run */
			i = pk_console_get_number (_("Please choose a command to run"), array->len);

			/* run command */
			possible = g_ptr_array_index (array, i);
			pk_cnf_spawn_command (possible);
		}
		goto out;

	/* only search using PackageKit if configured to do so */
	} else if (config->software_source_search) {
		available = g_ptr_array_new ();
		pk_cnf_find_available (available, "/usr/bin", argv[1]);
		pk_cnf_find_available (available, "/usr/sbin", argv[1]);
		pk_cnf_find_available (available, "/bin", argv[1]);
		pk_cnf_find_available (available, "/sbin", argv[1]);
		if (available->len == 1) {
			possible = g_ptr_array_index (available, 0);
			if (config->single_install == PK_CNF_POLICY_WARN) {
				/* TRANSLATORS: tell the user what package provides the command */
				g_print ("%s '%s'\n", _("The package providing this file is:"), possible);

			/* ask */
			} else if (config->single_install == PK_CNF_POLICY_ASK) {
				/* TRANSLATORS: as the user if we want to install a package to provide the command */
				text = g_strdup_printf (_("Install package '%s' to provide command '%s'?"), possible, argv[1]);
				ret = pk_console_get_prompt (text, FALSE);
				g_free (text);
				if (ret) {
					text = g_strdup_printf ("pkcon install %s", possible);
					ret = pk_cnf_spawn_command (text);
					if (ret)
						pk_cnf_spawn_command (argv[1]);
					g_free (text);
				}

			/* install */
			} else if (config->single_install == PK_CNF_POLICY_INSTALL) {
				text = g_strdup_printf ("pkcon install %s", possible);
				pk_cnf_spawn_command (text);
				g_free (text);
			}
			goto out;
		} else if (available->len > 1) {
			if (config->multiple_install == PK_CNF_POLICY_WARN) {
				/* TRANSLATORS: Show the user a list of packages that provide this command */
				g_print ("%s:\n", _("Packages providing this file are:"));
				for (i=0; i<available->len; i++) {
					possible = g_ptr_array_index (available, i);
					g_print ("'%s'\n", possible);
				}

			/* ask */
			} else if (config->multiple_install == PK_CNF_POLICY_ASK) {
				/* TRANSLATORS: Show the user a list of packages that they can install to provide this command */
				g_print ("%s:\n", _("Suitable packages are:"));
				for (i=0; i<available->len; i++) {
					possible = g_ptr_array_index (available, i);
					g_print ("%i\t'%s'\n", i+1, possible);
				}

				/* get selection */
				/* TRANSLATORS: ask the user to choose a file to install */
				i = pk_console_get_number (_("Please choose a package to install"), available->len);

				/* run command */
				possible = g_ptr_array_index (available, i);
				text = g_strdup_printf ("pkcon install %s", possible);
				ret = pk_cnf_spawn_command (text);
				if (ret)
					pk_cnf_spawn_command (argv[1]);
				g_free (text);
			}
			goto out;
		}
	}

	g_print ("\n");

out:
	if (config != NULL) {
		g_strfreev (config->locations);
		g_free (config);
	}
	if (array != NULL) {
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
	if (available != NULL) {
		g_ptr_array_foreach (available, (GFunc) g_free, NULL);
		g_ptr_array_free (available, TRUE);
	}

	return 0;
}

