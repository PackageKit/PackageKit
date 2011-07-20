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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>
#include <string.h>
#include <sys/utsname.h>
#include <packagekit-glib2/packagekit.h>

typedef struct {
	GstStructure	*structure;
	gchar		*type_name;
	gchar		*codec_name;
	gchar		*app_name;
} PkGstCodecInfo;

enum {
	FIELD_VERSION = 0,
	FIELD_LAYER,
	FIELD_VARIANT,
	FIELD_SYSTEMSTREAM
};

/**
 * pk_gst_parse_codec:
 **/
static PkGstCodecInfo *
pk_gst_parse_codec (const gchar *codec)
{
	gchar **split = NULL;
	gchar **ss = NULL;
	GstStructure *s;
	gchar *type_name = NULL;
	gchar *caps = NULL;
	PkGstCodecInfo *info = NULL;

	split = g_strsplit (codec, "|", -1);
	if (split == NULL || g_strv_length (split) != 5) {
		g_message ("PackageKit: not a GStreamer codec line");
		goto out;
	}
	if (g_strcmp0 (split[0], "gstreamer") != 0 ||
	    g_strcmp0 (split[1], "0.10") != 0) {
		g_message ("PackageKit: not for GStreamer 0.10");
		goto out;
	}

	if (g_str_has_prefix (split[4], "uri") != FALSE) {
		/* split uri */
		ss = g_strsplit (split[4], " ", 2);

		info = g_new0 (PkGstCodecInfo, 1);
		info->app_name = g_strdup (split[2]);
		info->codec_name = g_strdup (split[3]);
		info->type_name = g_strdup (ss[0]);
		goto out;
	}

	/* split */
	ss = g_strsplit (split[4], "-", 2);
	type_name = g_strdup (ss[0]);
	caps = g_strdup (ss[1]);

	s = gst_structure_from_string (caps, NULL);
	if (s == NULL) {
		g_message ("PackageKit: failed to parse caps: %s", caps);
		goto out;
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

	info = g_new0 (PkGstCodecInfo, 1);
	info->app_name = g_strdup (split[2]);
	info->codec_name = g_strdup (split[3]);
	info->type_name = g_strdup (type_name);
	info->structure = s;

out:
	g_free (caps);
	g_free (type_name);
	g_strfreev (ss);
	g_strfreev (split);
	return info;
}

/**
 * pk_gst_field_get_type:
 **/
static int
pk_gst_field_get_type (const gchar *field_name)
{
	if (g_strrstr (field_name, "version") != NULL)
		return FIELD_VERSION;
	if (g_strcmp0 (field_name, "layer") == 0)
		return FIELD_LAYER;
	if (g_strcmp0 (field_name, "systemstream") == 0)
		return FIELD_SYSTEMSTREAM;
	if (g_strcmp0 (field_name, "variant") == 0)
		return FIELD_VARIANT;
	return -1;
}

/**
 * pk_gst_fields_type_compare:
 **/
static gint
pk_gst_fields_type_compare (const gchar *a, const gchar *b)
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
static gchar *
pk_gst_structure_to_provide (GstStructure *s)
{
	GString *string;
	guint i, num_fields;
	GList *fields, *l;

	num_fields = gst_structure_n_fields (s);
	fields = NULL;

	for (i = 0; i < num_fields; i++) {
		const gchar *field_name;

		field_name = gst_structure_nth_field_name (s, i);
		if (pk_gst_field_get_type (field_name) < 0) {
			g_message ("PackageKit: ignoring field named %s", field_name);
			continue;
		}

		fields = g_list_insert_sorted (fields, g_strdup (field_name), (GCompareFunc) pk_gst_fields_type_compare);
	}

	string = g_string_new("");
	for (l = fields; l != NULL; l = l->next) {
		gchar *field_name;
		GType type;

		field_name = l->data;

		type = gst_structure_get_field_type (s, field_name);
		g_message ("PackageKit: field is: %s, type: %s", field_name, g_type_name (type));

		if (type == G_TYPE_INT) {
			int value;

			gst_structure_get_int (s, field_name, &value);
			g_string_append_printf (string, "(%s=%d)", field_name, value);
		} else if (type == G_TYPE_BOOLEAN) {
			int value;

			gst_structure_get_boolean (s, field_name, &value);
			g_string_append_printf (string, "(%s=%s)", field_name, value ? "true" : "false");
		} else if (type == G_TYPE_STRING) {
			const gchar *value;

			value = gst_structure_get_string (s, field_name);
			g_string_append_printf (string, "(%s=%s)", field_name, value);
		} else {
			g_warning ("PackageKit: unhandled type! %s", g_type_name (type));
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
pk_gst_codec_free (PkGstCodecInfo *codec)
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
		g_warning ("PackageKit: cannot get machine type");
		goto out;
	}

	/* 32 bit machines */
	if (g_strcmp0 (buf.machine, "i386") == 0 ||
	    g_strcmp0 (buf.machine, "i586") == 0 ||
	    g_strcmp0 (buf.machine, "i686") == 0)
		goto out;

	/* 64 bit machines */
	if (g_strcmp0 (buf.machine, "x86_64") == 0) {
		suffix = "()(64bit)";
		goto out;
	}

	g_warning ("PackageKit: did not recognise machine type: '%s'", buf.machine);
out:
	return suffix;
}


/**
 * main:
 **/
int
main (int argc, gchar **argv)
{
	GDBusProxy *proxy = NULL;
	GOptionContext *context;
	GError *error = NULL;
	guint i;
	guint len;
	gchar **codecs = NULL;
	gint xid = 0;
	gint retval = GST_INSTALL_PLUGINS_ERROR;
	const gchar *suffix;
	gchar **resources = NULL;
	GPtrArray *array = NULL;
	gchar *resource;
	GVariant *value = NULL;

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

	/* this is our parent window */
	g_message ("PackageKit: xid = %i", xid);

	/* get proxy */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify",
					       NULL,
					       &error);
	if (proxy == NULL) {
		g_warning ("Cannot connect to PackageKit session service: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* use a ()(64bit) suffix for 64 bit */
	suffix = pk_gst_get_arch_suffix ();

	array = g_ptr_array_new_with_free_func (g_free);
	len = g_strv_length (codecs);
	resources = g_new0 (gchar*, len+1);

	/* process argv */
	for (i=0; i<len; i++) {
		PkGstCodecInfo *info;
		gchar *s;
		gchar *type;

		info = pk_gst_parse_codec (codecs[i]);
		if (info == NULL) {
			g_message ("skipping %s", codecs[i]);
			continue;
		}
		g_message ("PackageKit: Codec nice name: %s", info->codec_name);
		if (info->structure != NULL) {
			s = pk_gst_structure_to_provide (info->structure);
			type = g_strdup_printf ("gstreamer0.10(%s-%s)%s%s", info->type_name,
						gst_structure_get_name (info->structure), s, suffix);
			g_free (s);
			g_message ("PackageKit: structure: %s", type);
		} else {
			type = g_strdup_printf ("gstreamer0.10(%s)%s", info->type_name, suffix);
			g_message ("PackageKit: non-structure: %s", type);
		}

		/* "encode" */
		resource = g_strdup_printf ("%s|%s", info->codec_name, type);
		g_ptr_array_add (array, resource);

		/* free codec structure */
		pk_gst_codec_free (info);
	}

	/* nothing parsed */
	if (array->len == 0) {
		g_message ("no codec lines could be parsed");
		goto out;
	}

	/* convert to a GStrv */
	resources = pk_ptr_array_to_strv (array);

	/* invoke the method */
	value = g_dbus_proxy_call_sync (proxy,
					"InstallGStreamerResources",
					g_variant_new ("(u^a&ss)",
						  xid,
						  resources,
						  "hide-finished"),
					G_DBUS_CALL_FLAGS_NONE,
					60 * 60 * 1000, /* 1 hour */
					NULL,
					&error);
	if (value == NULL) {
		/* use the error string to return a good GStreamer exit code */
		retval = GST_INSTALL_PLUGINS_NOT_FOUND;
		if (g_strrstr (error->message, "did not agree to search") != NULL)
			retval = GST_INSTALL_PLUGINS_USER_ABORT;
		else if (g_strrstr (error->message, "not all codecs were installed") != NULL)
			retval = GST_INSTALL_PLUGINS_PARTIAL_SUCCESS;
		g_message ("PackageKit: Did not install codec: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* all okay */
	retval = GST_INSTALL_PLUGINS_SUCCESS;

out:
	if (value != NULL)
		g_variant_unref (value);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_strfreev (resources);
	if (proxy != NULL)
		g_object_unref (proxy);
	return retval;
}

