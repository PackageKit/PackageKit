/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Bastien Nocera <bnocera@redhat.com>
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

#include <gst/gst.h>
#include <string.h>
#include <sys/utsname.h>
#include <dbus/dbus-glib.h>

typedef struct {
	GstStructure *structure;
	char *type_name;
	char *codec_name;
	char *app_name;
} codec_info;

enum {
	FIELD_VERSION = 0,
	FIELD_LAYER,
	FIELD_VARIANT,
	FIELD_SYSTEMSTREAM
};

/**
 * pk_gst_parse_codec:
 **/
static codec_info *
pk_gst_parse_codec (const char *codec)
{
	char **split;
	GstStructure *s;
	char *type_name, *caps;
	codec_info *info;

	split = g_strsplit (codec, "|", -1);
	if (split == NULL || g_strv_length (split) != 5) {
		g_message ("not a GStreamer codec line");
		g_strfreev (split);
		return NULL;
	}
	if (strcmp (split[0], "gstreamer") != 0 ||
	    strcmp (split[1], "0.10") != 0) {
		g_message ("not for GStreamer 0.10");
		g_strfreev (split);
		return NULL;
	}

	if (g_str_has_prefix (split[4], "uri") != FALSE) {
		char **ss;

		ss = g_strsplit (split[4], " ", 2);

		info = g_new0 (codec_info, 1);
		info->app_name = g_strdup (split[2]);
		info->codec_name = g_strdup (split[3]);
		info->type_name = g_strdup (ss[0]);
		g_strfreev (ss);
		g_strfreev (split);

		return info;
	}

	{
		char **ss;
		ss = g_strsplit (split[4], "-", 2);
		type_name = g_strdup (ss[0]);
		caps = g_strdup (ss[1]);
		g_strfreev (ss);
	}

	s = gst_structure_from_string (caps, NULL);
	if (s == NULL) {
		g_message ("failed to parse caps: %s", caps);
		g_strfreev (split);
		g_free (caps);
		g_free (type_name);
		return NULL;
	}

	/* remove fields that are almost always just MIN-MAX of some sort
	 * in order to make the caps look less messy */
	gst_structure_remove_field (s, "pixel-aspect-ratio");
	gst_structure_remove_field (s, "framerate");
	gst_structure_remove_field (s, "channels");
	gst_structure_remove_field (s, "width");
	gst_structure_remove_field (s, "height");
	gst_structure_remove_field (s, "rate");
	gst_structure_remove_field (s, "depth");
	gst_structure_remove_field (s, "clock-rate");
	gst_structure_remove_field (s, "bitrate");

	info = g_new0 (codec_info, 1);
	info->app_name = g_strdup (split[2]);
	info->codec_name = g_strdup (split[3]);
	info->type_name = type_name;
	info->structure = s;
	g_strfreev (split);

	return info;
}

/**
 * pk_gst_field_get_type:
 **/
static int
pk_gst_field_get_type (const char *field_name)
{
	if (strstr (field_name, "version") != NULL)
		return FIELD_VERSION;
	if (strcmp (field_name, "layer") == 0)
		return FIELD_LAYER;
	if (strcmp (field_name, "systemstream") == 0)
		return FIELD_SYSTEMSTREAM;
	if (strcmp (field_name, "variant") == 0)
		return FIELD_VARIANT;

	return -1;
}

/**
 * pk_gst_fields_type_compare:
 **/
static gint
pk_gst_fields_type_compare (const char *a, const char *b)
{
	gint a_type, b_type;

	a_type = pk_gst_field_get_type (a);
	b_type = pk_gst_field_get_type (b);
	if (a_type < b_type)
		return -1;
	if (b_type < a_type)
		return 1;
	return 0;
}

/**
 * pk_gst_structure_to_provide:
 **/
static char *
pk_gst_structure_to_provide (GstStructure *s)
{
	GString *string;
	guint i, num_fields;
	GList *fields, *l;

	num_fields = gst_structure_n_fields (s);
	fields = NULL;

	for (i = 0; i < num_fields; i++) {
		const char *field_name;

		field_name = gst_structure_nth_field_name (s, i);
		if (pk_gst_field_get_type (field_name) < 0) {
			//g_message ("ignoring field named %s", field_name);
			continue;
		}

		fields = g_list_insert_sorted (fields, g_strdup (field_name), (GCompareFunc) pk_gst_fields_type_compare);
	}

	string = g_string_new("");
	for (l = fields; l != NULL; l = l->next) {
		char *field_name;
		GType type;

		field_name = l->data;

		type = gst_structure_get_field_type (s, field_name);
		//g_message ("field is: %s, type: %s", field_name, g_type_name (type));

		if (type == G_TYPE_INT) {
			int value;

			gst_structure_get_int (s, field_name, &value);
			g_string_append_printf (string, "(%s=%d)", field_name, value);
		} else if (type == G_TYPE_BOOLEAN) {
			int value;

			gst_structure_get_boolean (s, field_name, &value);
			g_string_append_printf (string, "(%s=%s)", field_name, value ? "true" : "false");
		} else if (type == G_TYPE_STRING) {
			const char *value;

			value = gst_structure_get_string (s, field_name);
			g_string_append_printf (string, "(%s=%s)", field_name, value);
		} else {
			g_warning ("unhandled type! %s", g_type_name (type));
		}

		g_free (field_name);
	}

	g_list_free (fields);

	return g_string_free (string, FALSE);
}

/**
 * pk_gst_codec_free:
 **/
static void
pk_gst_codec_free (codec_info *codec)
{
	if (codec->structure)
		gst_structure_free (codec->structure);
	g_free (codec->type_name);
	g_free (codec->codec_name);
	g_free (codec->app_name);
}

/**
 * pk_gst_get_arch_suffix:
 *
 * Return value: something other than blank if we are running on 64 bit.
 **/
static const gchar *
pk_gst_get_arch_suffix (void)
{
	gint retval;
	const gchar *suffix = "";
	struct utsname buf;

	retval = uname (&buf);

	/* did we get valid value? */
	if (retval != 0 || buf.machine == NULL) {
		g_warning ("cannot get machine type");
		goto out;
	}

	/* 32 bit machines */
	if (strcmp (buf.machine, "i386") == 0 ||
	    strcmp (buf.machine, "i586") == 0 ||
	    strcmp (buf.machine, "i686") == 0)
		goto out;

	/* 64 bit machines */
	if (strcmp (buf.machine, "x86_64") == 0) {
		suffix = "()(64bit)";
		goto out;
	}

	g_warning ("did not recognise machine type: '%s'", buf.machine);
out:
	return suffix;
}


/**
 * main:
 **/
int
main (int argc, char **argv)
{
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;
	GPtrArray *array = NULL;
	GValueArray *varray;
	GValue *value;
	gboolean ret;
	GType array_type;
	GOptionContext *context;
	GError *error = NULL;
	guint i;
	gchar **codecs = NULL;
	gint xid = 0;
	gint retval = 1;
	const gchar *suffix;

	const GOptionEntry options[] = {
		{ "transient-for", '\0', 0, G_OPTION_ARG_INT, &xid, "The XID of the parent window", NULL },
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &codecs, "GStreamer install infos", NULL },
		{ NULL }
	};

	g_type_init ();
	gst_init (&argc, &argv);

	context = g_option_context_new ("Install missing codecs");
	g_option_context_add_main_entries (context, options, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_print ("%s\nRun '%s --help' to see a full list of available command line options.\n",
			 error->message, argv[0]);
		g_error_free (error);
		goto out;
	}
	if (codecs == NULL) {
		g_print ("Missing codecs information\n");
		g_print ("Run 'with --help' to see a full list of available command line options.\n");
		goto out;
	}

	/* get bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		g_print ("Could not connect to session DBUS: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* get proxy */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit");
	if (proxy == NULL) {
		g_print ("Cannot connect to PackageKit session service\n");
		goto out;
	}

	/* use a ()(64bit) suffix for 64 bit */
	suffix = pk_gst_get_arch_suffix ();

	/* process argv */
	array = g_ptr_array_new ();
	for (i = 0; codecs[i] != NULL; i++) {
		codec_info *info;
		char *s;
		char *type;

		info = pk_gst_parse_codec (codecs[i]);
		if (info == NULL) {
			g_print ("skipping %s\n", codecs[i]);
			continue;
		}
		g_message ("Codec nice name: %s", info->codec_name);
		if (info->structure != NULL) {
			s = pk_gst_structure_to_provide (info->structure);
			type = g_strdup_printf ("gstreamer0.10(%s-%s)%s%s", info->type_name,
						gst_structure_get_name (info->structure), s, suffix);
			g_free (s);
			g_message ("structure: %s", type);
		} else {
			type = g_strdup_printf ("gstreamer0.10(%s)", info->type_name);
			g_message ("non-structure: %s", type);
		}

		/* create (ss) structure */
		varray = g_value_array_new (2);
		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, info->codec_name);
		g_value_array_append (varray, value);
		g_value_reset (value);
		g_value_set_string (value, type);
		g_value_array_append (varray, value);
		g_value_unset (value);
		g_free (value);

		/* add to array of (ss) */
		g_ptr_array_add (array, varray);

		/* free codec structure */
		pk_gst_codec_free (info);
	}

	/* marshall a(ss) */
	array_type = dbus_g_type_get_collection ("GPtrArray",
					dbus_g_type_get_struct("GValueArray",
						G_TYPE_STRING,
						G_TYPE_STRING,
						G_TYPE_INVALID));

	/* invoke the method */
	ret = dbus_g_proxy_call (proxy, "InstallGStreamerCodecs", &error,
				 G_TYPE_UINT, xid,
				 G_TYPE_UINT, 0,
				 array_type, array,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret) {
		g_error ("Did not install codec: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* all okay */
	retval = 0;

out:
	if (array != NULL) {
		g_ptr_array_foreach (array, (GFunc) g_value_array_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
	if (proxy != NULL)
		g_object_unref (proxy);
	return 0;
}

