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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define G_LOG_DOMAIN "PkGtkModule"

#define PANGO_ENABLE_BACKEND
#include <fontconfig/fontconfig.h>
#include <pango/pango.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangocairo.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

/**
 * Try guessing the XID of the toplevel window that triggered us
 **/

static void
toplevels_foreach_cb (GtkWindow *window,
		      GtkWindow **active)
{
	if (gtk_window_has_toplevel_focus (window))
		*active = window;
}

static guint
guess_xid (void)
{
	guint xid = 0;
	GtkWindow *active = NULL;

	g_list_foreach (gtk_window_list_toplevels (),
			(GFunc) toplevels_foreach_cb, &active);

	if (active != NULL)
		xid = GDK_WINDOW_XID (gtk_widget_get_window (GTK_WIDGET(active)));

	return xid;
}


/**
 * Invoke the PackageKit InstallFonts method over D-BUS
 **/

static void
pk_install_fonts_dbus_notify_cb (DBusGProxy *proxy,
				 DBusGProxyCall *call,
				 gpointer user_data)
{
	gboolean ret;
	GError *error = NULL;

	ret = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INVALID);

	if (!ret) {
		g_debug ("Did not install fonts: %s", error->message);
		return;
	} else {
		/* XXX Actually get the return value of the method? */

		g_debug ("Fonts installed");
	}

	/* XXX Make gtk/pango reload fonts? */
}

static GPtrArray *tags;

static gboolean
pk_install_fonts_idle_cb (gpointer data G_GNUC_UNUSED)
{
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;
	guint xid;
	gchar **font_tags;
	GError *error = NULL;
	DBusGProxyCall *call;

	g_return_val_if_fail (tags->len > 0, FALSE);

	/* get a strv out of the array that we will then own */
	g_ptr_array_add (tags, NULL);
	font_tags = (gchar **) g_ptr_array_free (tags, FALSE);
	tags = NULL;

	/* try to get the window XID */
	xid = guess_xid ();

	/* get bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		g_warning ("Could not connect to session bus: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* get proxy */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Modify");
	if (proxy == NULL) {
		g_warning ("Could not connect to PackageKit session service\n");
		goto out;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	/* invoke the method */
	call = dbus_g_proxy_begin_call (proxy, "InstallFontconfigResources",
					pk_install_fonts_dbus_notify_cb, NULL, NULL,
				        G_TYPE_UINT, xid,
				        G_TYPE_STRV, font_tags,
					G_TYPE_STRING, "hide-finished",
				        G_TYPE_INVALID);
	if (call == NULL) {
		g_warning ("Could not send method");
		goto out;
	}

	g_debug ("InstallFontconfigResources method invoked");

out:
	g_strfreev (font_tags);
	if (proxy != NULL)
		g_object_unref (proxy);

	return FALSE;
}

static void
queue_install_fonts_tag (const char *tag)
{
	guint idle_id;
	if (tags == NULL) {
		tags = g_ptr_array_new ();
		idle_id = g_idle_add (pk_install_fonts_idle_cb, NULL);
		g_source_set_name_by_id (idle_id, "[PkGtkModule] install fonts");
	}

	g_ptr_array_add (tags, (gpointer) g_strdup (tag));
}

static void
pk_font_not_found (PangoLanguage *language)
{
	FcPattern *pat = NULL;
	gchar *tag = NULL;
	const gchar *lang;

	g_return_if_fail (language != NULL);

	/* convert to language */
	lang = pango_language_to_string (language);
	if (lang == NULL || lang[0] == '\0') {
		g_warning ("failed to convert language to string");
		goto out;
	}

	/* create the font tag used in as a package provides */
	pat = FcPatternCreate ();
	FcPatternAddString (pat, FC_LANG, (FcChar8 *) lang);
	tag = (gchar *) FcNameUnparse (pat);
	if (tag == NULL || tag[0] == '\0') {
		g_warning ("failed to create font tag: %s", lang);
		goto out;
	}

	/* add to array for processing in idle callback */
	queue_install_fonts_tag (tag);

out:
	if (pat != NULL)
		FcPatternDestroy (pat);
	if (tag != NULL)
		free (tag);
}


/**
 * A PangoFcFontMap implementation that detects font-not-found events
 **/


typedef struct {
	PangoLanguage *language;
	gboolean found;
} FontsetForeachClosure;

static gboolean
fontset_foreach_cb (PangoFontset *fontset G_GNUC_UNUSED,
		    PangoFont *font,
		    gpointer data)
{
	FontsetForeachClosure *closure = data;
	PangoFcFont *fcfont = PANGO_FC_FONT (font);
	const FcPattern *pattern = NULL;
	FcLangSet *langset = NULL;

	g_object_get (fcfont, "pattern", &pattern, NULL);

	/* old Pango version with non-readable pattern */
	if (pattern == NULL) {
		g_warning ("Old Pango version with non-readable pattern. "
			   "Skipping automatic missing-font installation.");
		return closure->found = TRUE;
	}

	if (FcPatternGetLangSet (pattern, FC_LANG, 0, &langset) == FcResultMatch &&
	    FcLangGetCharSet ((FcChar8 *) closure->language) != NULL &&
	    FcLangSetHasLang (langset, (FcChar8 *) closure->language) != FcLangDifferentLang)
		closure->found = TRUE;

	return closure->found;
}


static PangoFontset *(*pk_pango_fc_font_map_load_fontset_default) (PangoFontMap *font_map,
								   PangoContext *context,
								   const PangoFontDescription *desc,
								   PangoLanguage *language);

static PangoFontset *
pk_pango_fc_font_map_load_fontset (PangoFontMap *font_map,
				   PangoContext *context,
				   const PangoFontDescription *desc,
				   PangoLanguage *language)
{
	static PangoLanguage *last_language = NULL;
	static GHashTable *seen_languages = NULL;
	PangoFontset *fontset;
	
	fontset = pk_pango_fc_font_map_load_fontset_default (font_map, context, desc, language);

	/* "xx" is Pango's "unknown language" language code.
	 * we can fall back to scripts maybe, but the facilities for that
	 * is not in place yet.	Maybe Pango can use a four-letter script
	 * code instead of "xx"... */
	if (G_LIKELY (language == last_language) ||
	    language == NULL ||
	    language == pango_language_from_string ("C") ||
	    language == pango_language_from_string ("xx"))
		return fontset;

	if (G_UNLIKELY (!seen_languages))
		seen_languages = g_hash_table_new (NULL, NULL);

	if (G_UNLIKELY (!g_hash_table_lookup (seen_languages, language))) {
		FontsetForeachClosure closure;

		g_hash_table_insert (seen_languages, language, language);

		closure.language = language;
		closure.found = FALSE;
		pango_fontset_foreach (fontset, fontset_foreach_cb, &closure);
		if (!closure.found)
			pk_font_not_found (language);
	}

	last_language = language;
	return fontset;
}

static void
pk_pango_fc_font_map_class_init (PangoFontMapClass *klass)
{
	g_return_if_fail (pk_pango_fc_font_map_load_fontset_default == NULL);

	pk_pango_fc_font_map_load_fontset_default = klass->load_fontset;
	klass->load_fontset = pk_pango_fc_font_map_load_fontset;
}

static GType
pk_pango_fc_font_map_overload_type (GType default_pango_fc_font_map_type)
{
	GTypeQuery query;
	g_type_query (default_pango_fc_font_map_type, &query);

	return g_type_register_static_simple (default_pango_fc_font_map_type,
					      g_intern_static_string ("PkPangoFcFontMap"),
					      query.class_size,
					      (GClassInitFunc) pk_pango_fc_font_map_class_init,
					      query.instance_size,
					      NULL, 0);
}

static void
install_pango_font_map (void)
{
	static GType font_map_type = 0;

	if (!font_map_type) {
		PangoFontMap *font_map;

		font_map = pango_cairo_font_map_get_default ();
		if (!PANGO_IS_FC_FONT_MAP (font_map)) {
			g_warning ("Default pangocairo font map is not a pangofc fontmap. "
				   "Skipping automatic missing-font installation.");
			return;
		}

		font_map_type = pk_pango_fc_font_map_overload_type (G_TYPE_FROM_INSTANCE (font_map));
		font_map = g_object_new (font_map_type, NULL);
		pango_cairo_font_map_set_default (PANGO_CAIRO_FONT_MAP (font_map));
		g_object_unref (font_map);
	}
}


/**
 * GTK module declaraction
 **/

void gtk_module_init (gint *argc, gchar ***argv);

void
gtk_module_init (gint *argc G_GNUC_UNUSED,
		 gchar ***argv G_GNUC_UNUSED)
{
	install_pango_font_map ();
}

const char *g_module_check_init (GModule *module);

const char *
g_module_check_init (GModule *module)
{
	/* make the GTK+ module resident
	 * without doing this, killing gnome-settings-daemon brings down every
	 * single application in the session, since the module doesn't clean up
	 * when being unloaded */
	g_module_make_resident (module);
	return NULL;
}
