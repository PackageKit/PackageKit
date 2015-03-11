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
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "src/pk-cleanup.h"

static gchar *
pk_guess_application_id (void)
{
	GApplication *app;
	const gchar *app_id;

	app = g_application_get_default ();
	if (app == NULL)
		return NULL;

	app_id = g_application_get_application_id (app);
	if (app_id == NULL)
		return NULL;

	return g_strconcat (app_id, ".desktop", NULL);
}

/**
 * Invoke the PackageKit InstallFonts method over D-BUS
 **/

static void
pk_install_fonts_method_finished_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_variant_unref_ GVariant *value = NULL;

	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		g_warning ("Error occurred during install: %s", error->message);
		return;
	}
	/* XXX Make gtk/pango reload fonts? */
}

static GVariant *
pk_make_platform_data (void)
{
	GVariantBuilder builder;
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	return g_variant_builder_end (&builder);
}

static GPtrArray *tags;

static gboolean
pk_install_fonts_idle_cb (gpointer data G_GNUC_UNUSED)
{
	_cleanup_free_ gchar *application_id = NULL;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusProxy *proxy = NULL;
	_cleanup_strv_free_ gchar **font_tags = NULL;

	g_return_val_if_fail (tags->len > 0, FALSE);

	/* get a strv out of the array that we will then own */
	g_ptr_array_add (tags, NULL);
	font_tags = (gchar **) g_ptr_array_free (tags, FALSE);
	tags = NULL;

	application_id = pk_guess_application_id ();

	/* get proxy */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify2",
					       NULL,
					       &error);
	if (proxy == NULL) {
		g_warning ("Error connecting to PK session instance: %s", error->message);
		return FALSE;
	}

	/* invoke the method */
	g_dbus_proxy_call (proxy,
			   "InstallFontconfigResources",
			   g_variant_new ("(^a&sss@a{sv})",
					  font_tags,
					  "hide-finished",
					   application_id ? application_id : "",
					   pk_make_platform_data ()),
			   G_DBUS_CALL_FLAGS_NONE,
			   60 * 60 * 1000, /* 1 hour */
			   NULL,
			   pk_install_fonts_method_finished_cb,
			   NULL);

	g_debug ("InstallFontconfigResources method invoked");
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

	g_debug ("Queue install of: %s", tag);
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
	    pango_language_matches (language, "c;xx"))
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
