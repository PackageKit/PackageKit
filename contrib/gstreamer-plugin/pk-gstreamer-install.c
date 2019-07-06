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
	gchar		*gstreamer_version;
} PkGstCodecInfo;

enum {
	FIELD_VERSION = 0,
	FIELD_LAYER,
	FIELD_VARIANT,
	FIELD_SYSTEMSTREAM
};

static PkGstCodecInfo *
pk_gst_parse_codec (const gchar *codec)
{
	GstStructure *s;
	PkGstCodecInfo *info = NULL;
	g_autofree gchar *caps = NULL;
	g_autofree gchar *type_name = NULL;
	g_auto(GStrv) split = NULL;
	g_auto(GStrv) ss = NULL;

	split = g_strsplit (codec, "|", -1);
	if (split == NULL || g_strv_length (split) != 5) {
		g_message ("PackageKit: not a GStreamer codec line");
		return NULL;
	}
	if (g_strcmp0 (split[0], "gstreamer") != 0) {
		g_message ("PackageKit: not a GStreamer codec request");
		return NULL;
	}
	if (g_strcmp0 (split[1], "0.10") != 0 &&
	    g_strcmp0 (split[1], "1.0") != 0) {
		g_message ("PackageKit: not recognised GStreamer version");
		return NULL;
	}

	if (g_str_has_prefix (split[4], "uri") != FALSE) {
		/* split uri */
		ss = g_strsplit (split[4], " ", 2);
		info = g_new0 (PkGstCodecInfo, 1);
		info->app_name = g_strdup (split[2]);
		info->codec_name = g_strdup (split[3]);
		info->type_name = g_strdup (ss[0]);
		return info;
	}

	/* split */
	ss = g_strsplit (split[4], "-", 2);
	type_name = g_strdup (ss[0]);
	caps = g_strdup (ss[1]);

	s = gst_structure_from_string (caps, NULL);
	if (s == NULL) {
		g_message ("PackageKit: failed to parse caps: %s", caps);
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

	info = g_new0 (PkGstCodecInfo, 1);
	info->gstreamer_version = g_strdup (split[1]);
	info->app_name = g_strdup (split[2]);
	info->codec_name = g_strdup (split[3]);
	info->type_name = g_strdup (type_name);
	info->structure = s;
	return info;
}

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

static gchar *
pk_gst_structure_to_provide (GstStructure *s)
{
	GString *string;
	guint i, num_fields;
	GList *l;
	g_autoptr(GList) fields = NULL;

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
	return g_string_free (string, FALSE);
}

static void
pk_gst_codec_free (PkGstCodecInfo *codec)
{
	if (codec->structure)
		gst_structure_free (codec->structure);
	g_free (codec->type_name);
	g_free (codec->codec_name);
	g_free (codec->app_name);
	g_free (codec->gstreamer_version);
	g_free (codec);
}

static const gchar *
pk_gst_get_arch_suffix (void)
{
	gint retval;
	const gchar *suffix = "";
	struct utsname buf;

	retval = uname (&buf);

	/* did we get valid value? */
	if (retval != 0 || buf.machine[0] == '\0') {
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

	/* fall back */
	if (sizeof (void *) == 8) {
		suffix = "()(64bit)";
		g_debug ("PackageKit: assume 64-bit suffix for machine type: '%s'", buf.machine);
	} else {
		g_debug ("PackageKit: assume no suffix for machine type: '%s'", buf.machine);
	}

out:
	return suffix;
}

static GVariant *
make_platform_data (const gchar *startup_id)
{
	GVariantBuilder builder;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

	if (startup_id && g_utf8_validate (startup_id, -1, NULL)) {
		g_variant_builder_add (&builder, "{sv}",
		                       "desktop-startup-id", g_variant_new_string (startup_id));
	}

	return g_variant_builder_end (&builder);
}

static gboolean
pk_gst_dbus_install_resources (gchar **resources, const gchar *desktop_id, const gchar *startup_id, const gchar *interaction, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) value = NULL;

	/* get proxy */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify2",
					       NULL,
					       error);
	if (proxy != NULL) {
		/* invoke the method */
		value = g_dbus_proxy_call_sync (proxy,
						"InstallGStreamerResources",
						g_variant_new ("(^a&sss@a{sv})",
							       resources,
							       interaction ? interaction : "",
							       desktop_id ? desktop_id : "",
							       make_platform_data (startup_id)),
						G_DBUS_CALL_FLAGS_NONE,
						60 * 60 * 1000, /* 1 hour */
						NULL,
						error);
		if (value != NULL)
			return TRUE;
	}

	return FALSE;
}

static gboolean
pk_gst_dbus_install_resources_compat (gchar **resources, gint xid, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) value = NULL;

	/* get proxy */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify",
					       NULL,
					       error);
	if (proxy != NULL) {
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
						error);
		if (value != NULL)
			return TRUE;
	}

	return FALSE;
}

int
main (int argc, gchar **argv)
{
	GOptionContext *context;
	guint i;
	guint len;
	gboolean ret;
	gchar **codecs = NULL;
	gint xid = 0;
	const gchar *suffix;
	gchar *resource;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *desktop_id = NULL;
	g_autofree gchar *interaction = NULL;
	g_autofree gchar *startup_id = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_auto(GStrv) resources = NULL;

	const GOptionEntry options[] = {
		{ "transient-for", '\0', 0, G_OPTION_ARG_INT, &xid, "The XID of the parent window", NULL },
		{ "desktop-id", '\0', 0, G_OPTION_ARG_STRING, &desktop_id, "The desktop ID of the calling application", NULL },
		{ "interaction", '\0', 0, G_OPTION_ARG_STRING, &interaction, "Interaction mode specifying which UI elements should be shown", NULL },
		{ "startup-notification-id", '\0', 0, G_OPTION_ARG_STRING, &startup_id, "The startup notification ID for focus stealing prevention", NULL },
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &codecs, "GStreamer install infos", NULL },
		{ NULL }
	};

	gst_init (&argc, &argv);

	context = g_option_context_new ("Install missing codecs");
	g_option_context_add_main_entries (context, options, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_print ("%s\nRun '%s --help' to see a full list of available command line options.\n",
			 error->message, argv[0]);
		return GST_INSTALL_PLUGINS_ERROR;
	}
	if (codecs == NULL) {
		g_print ("Missing codecs information\n");
		g_print ("Run 'with --help' to see a full list of available command line options.\n");
		return GST_INSTALL_PLUGINS_ERROR;
	}

	/* this is our parent window */
	g_message ("PackageKit: xid = %i", xid);
	g_message ("PackageKit: desktop_id = %s", desktop_id);

	/* use a ()(64bit) suffix for 64 bit */
	suffix = pk_gst_get_arch_suffix ();

	array = g_ptr_array_new_with_free_func (g_free);
	len = g_strv_length (codecs);

	/* process argv */
	for (i = 0; i < len; i++) {
		PkGstCodecInfo *info;
		gchar *type;
		const gchar *gstreamer_version;

		info = pk_gst_parse_codec (codecs[i]);
		if (info == NULL) {
			g_message ("skipping %s", codecs[i]);
			continue;
		}

		/* gstreamer1 is the provide name used for the
		 * first version of the new release */
		if (g_strcmp0 (info->gstreamer_version, "1.0") == 0)
			gstreamer_version = "1";
		else
			gstreamer_version = info->gstreamer_version;

		g_message ("PackageKit: Codec nice name: %s", info->codec_name);
		if (info->structure != NULL) {
			g_autofree gchar *s = NULL;
			s = pk_gst_structure_to_provide (info->structure);
			type = g_strdup_printf ("gstreamer%s(%s-%s)%s%s",
						gstreamer_version,
						info->type_name,
						gst_structure_get_name (info->structure),
						s, suffix);
			g_message ("PackageKit: structure: %s", type);
		} else {
			type = g_strdup_printf ("gstreamer%s(%s)%s",
						gstreamer_version,
						info->type_name,
						suffix);
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
		return GST_INSTALL_PLUGINS_ERROR;
	}

	/* convert to a GStrv */
	resources = pk_ptr_array_to_strv (array);

	/* first try the new interface */
	ret = pk_gst_dbus_install_resources (resources, desktop_id, startup_id, interaction, &error);
	if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
		/* ... and if that fails, fall back to the compat interface */
		g_clear_error (&error);
		g_message ("PackageKit: falling back to compat dbus interface");
		ret = pk_gst_dbus_install_resources_compat (resources, xid, &error);
	}
	if (!ret) {
		/* use the error string to return a good GStreamer exit code */
		g_message ("PackageKit: Did not install codec: %s", error->message);
		if (g_strrstr (error->message, "did not agree to search") != NULL)
			return GST_INSTALL_PLUGINS_USER_ABORT;
		if (g_strrstr (error->message, "not all codecs were installed") != NULL)
			return GST_INSTALL_PLUGINS_PARTIAL_SUCCESS;
		return GST_INSTALL_PLUGINS_NOT_FOUND;
	}
	return GST_INSTALL_PLUGINS_SUCCESS;
}

