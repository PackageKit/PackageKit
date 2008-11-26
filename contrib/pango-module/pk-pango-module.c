/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Behdad Esfahbod <behdad@behdad.org>
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

#define G_LOG_DOMAIN "MissingFontModule"

#define PANGO_ENABLE_BACKEND
#include <fontconfig/fontconfig.h>
#include <pango/pango.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangocairo.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>

static GPtrArray *array = NULL;

void gtk_module_init (gint *argc, gchar ***argv);
static PangoFontset *(*pk_font_load_fontset_default) (PangoFontMap *font_map,
						      PangoContext *context,
						      const PangoFontDescription *desc,
						      PangoLanguage *language);

typedef struct {
	PangoLanguage *language;
	gboolean found;
} FonsetForeachClosure;

#if 0
/**
 * pk_font_find_window:
 **/
static void
pk_font_find_window (GtkWindow *window, GtkWindow **active)
{
	g_message ("%p=%i", window, gtk_window_has_toplevel_focus (window));
}
#endif

/**
 * pk_font_ptr_array_to_strv:
 **/
gchar **
pk_font_ptr_array_to_strv (GPtrArray *array)
{
	gchar **strv_array;
	const gchar *value_temp;
	guint i;

	strv_array = g_new0 (gchar *, array->len + 2);
	for (i=0; i<array->len; i++) {
		value_temp = (const gchar *) g_ptr_array_index (array, i);
		strv_array[i] = g_strdup (value_temp);
	}
	strv_array[i] = NULL;

	return strv_array;
}

/**
 * pk_font_not_found:
 **/
static void
pk_font_not_found (PangoLanguage *language)
{
	FcPattern *pat = NULL;
	gchar *tag = NULL;
	const gchar *lang;

	/* convert to language */
	lang = pango_language_to_string (language);
	g_message ("lang required '%s'", lang);
	if (lang == NULL || strcmp (lang, "C") == 0)
		goto out;

	/* create the font tag used in as a package buildrequire */
	pat = FcPatternCreate ();
	FcPatternAddString (pat, FC_LANG, (FcChar8 *) lang);
	tag = (gchar *) FcNameUnparse (pat);
	if (tag == NULL)
		goto out;

	g_message ("tag required '%s'", tag);

	/* add to array for processing in idle callback */
	g_ptr_array_add (array, (gpointer) g_strdup (tag));

out:
	if (pat != NULL)
		FcPatternDestroy (pat);
	g_free (tag);
}

/**
 * pk_font_foreach_callback:
 **/
static gboolean
pk_font_foreach_callback (PangoFontset *fontset G_GNUC_UNUSED, PangoFont *font, gpointer data)
{
	FonsetForeachClosure *closure = data;
	PangoFcFont *fcfont = PANGO_FC_FONT (font);
	const FcPattern *pattern = NULL;
	FcLangSet *langset = NULL;

	g_object_get (fcfont, "pattern", &pattern, NULL);

	/* old Pango version with non-readable pattern */
	if (pattern == NULL) {
		g_warning ("Old Pango version with non-readable pattern. Skipping auto missing font installation.");
		return closure->found = TRUE;
	}

	if (FcPatternGetLangSet (pattern, FC_LANG, 0, &langset) == FcResultMatch &&
				 FcLangSetHasLang (langset, (FcChar8 *) closure->language) != FcLangDifferentLang)
		closure->found = TRUE;

	return closure->found;
}

/**
 * pk_font_load_fontset:
 **/
static PangoFontset *
pk_font_load_fontset (PangoFontMap *font_map, PangoContext *context, const PangoFontDescription *desc, PangoLanguage *language)
{
	static PangoLanguage *last_language = NULL;
	static GHashTable *seen_languages = NULL;
	PangoFontset *fontset;
	
	fontset = pk_font_load_fontset_default (font_map, context, desc, language);

	/* "xx" is Pango's "unknown language" language code.
	 * we can fall back to scripts maybe, but the facilities for that
	 * is not in place yet.	Maybe Pango can use a four-letter script
	 * code instead of "xx"... */
	if (G_LIKELY (language == last_language) || language == NULL || language == pango_language_from_string ("xx"))
		return fontset;

	if (G_UNLIKELY (!seen_languages))
		seen_languages = g_hash_table_new (NULL, NULL);

	if (G_UNLIKELY (!g_hash_table_lookup (seen_languages, language))) {
		FonsetForeachClosure closure;

		g_hash_table_insert (seen_languages, language, language);

		closure.language = language;
		closure.found = FALSE;
		pango_fontset_foreach (fontset, pk_font_foreach_callback, &closure);
		if (!closure.found)
			pk_font_not_found (language);
	}

	last_language = language;
	return fontset;
}

/**
 * pk_font_map_class_init:
 **/
static void
pk_font_map_class_init (PangoFontMapClass *klass)
{
	g_assert (pk_font_load_fontset_default == NULL);
	pk_font_load_fontset_default = klass->load_fontset;
	klass->load_fontset = pk_font_load_fontset;
}

/**
 * pk_font_overload_type:
 **/
static GType
pk_font_overload_type (GType font_map_type)
{
	GTypeQuery query;
	g_type_query (font_map_type, &query);

	return g_type_register_static_simple (font_map_type,
					      g_intern_static_string ("MissingFontFontMap"),
					      query.class_size,
					      (GClassInitFunc) pk_font_map_class_init,
					      query.instance_size,
					      NULL, 0);
}

/**
 * pk_font_dbus_notify_cb:
 **/
static void
pk_font_dbus_notify_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{

	gboolean ret;
	GError *error = NULL;
	ret = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID);
	if (!ret)
		g_message ("PackageKit: Did not install font: %s", error->message);
}

/**
 * pk_font_idle_cb:
 **/
static gboolean
pk_font_idle_cb (GPtrArray *array)
{
	guint i;
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;
	guint xid;
	gchar **fonts = NULL;
	GError *error = NULL;

	/* nothing to do */
	if (array->len == 0)
		goto out;

	/* just print */
	for (i=0; i< array->len; i++)
		g_message ("array[%i]: %s", i, (const gchar *) g_ptr_array_index (array, i));

#if 0
	GtkWindow *active;
	GList *list;

	/* FIXME: try to get the window XID */
	list = gtk_window_list_toplevels ();
	g_warning ("number of windows = %i", g_list_length (list));
	g_list_foreach (list, (GFunc) pk_font_find_window, &active);
#endif

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

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	/* FIXME: get the xid from the calling application */
	xid = 0;

	/* invoke the method */
	fonts = pk_font_ptr_array_to_strv (array);
	DBusGProxyCall *call;
	call = dbus_g_proxy_begin_call (proxy, "InstallFonts", pk_font_dbus_notify_cb, NULL, NULL, 
				        G_TYPE_UINT, xid,
				        G_TYPE_UINT, 0,
				        G_TYPE_STRV, fonts,
				        G_TYPE_INVALID);
	if (call == NULL) {
		g_message ("PackageKit: could not send method");
		goto out;
	}
out:
	g_strfreev (fonts);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	if (proxy != NULL)
		g_object_unref (proxy);

	return FALSE;
}

/**
 * gtk_module_init:
 **/
void
gtk_module_init (gint *argc G_GNUC_UNUSED,
		 gchar ***argv G_GNUC_UNUSED)
{
	PangoFontMap *font_map;
	GType font_map_type;

	array = g_ptr_array_new ();
	g_idle_add ((GSourceFunc) pk_font_idle_cb, array);

	font_map = pango_cairo_font_map_get_default ();
	if (!PANGO_IS_FC_FONT_MAP (font_map))
		return;

	font_map_type = pk_font_overload_type (G_TYPE_FROM_INSTANCE (font_map));
	font_map = g_object_new (font_map_type, NULL);
	pango_cairo_font_map_set_default (PANGO_CAIRO_FONT_MAP (font_map));
	g_object_unref (font_map);
}

