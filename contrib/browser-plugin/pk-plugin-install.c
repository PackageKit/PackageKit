/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Red Hat, Inc.
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib/gi18n-lib.h>
#include <gio/gdesktopappinfo.h>
#include <pango/pangocairo.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <math.h>
#include <packagekit-glib2/packagekit.h>

#include "pk-plugin-install.h"

#define PK_PLUGIN_INSTALL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PLUGIN_INSTALL, PkPluginInstallPrivate))

typedef enum {
	IN_PROGRESS, /* looking up package information */
	INSTALLED,   /* package installed */
	UPGRADABLE,  /* package installed, newer version available */
	AVAILABLE,   /* package not installed, version available */
	UNAVAILABLE, /* package not installed or available */
	INSTALLING   /* currently installing a new version */
} PkPluginInstallPackageStatus;

struct PkPluginInstallPrivate
{
	PkPluginInstallPackageStatus	 status;
	gchar			*available_version;
	gchar			*available_package_name;
	gchar			*installed_version;
	gchar			*installed_package_name;
	GAppInfo		*app_info;
	gchar			*display_name;
	gchar			**package_names;
	PangoLayout		*pango_layout;
	PkClient		*client;
	GDBusProxy		*session_pk_proxy;
	GCancellable		*cancellable;
	gint			timeout;
	gint			current;
	gint			update_spinner;
};

G_DEFINE_TYPE (PkPluginInstall, pk_plugin_install, PK_TYPE_PLUGIN)

/**
 * pk_plugin_install_clear_layout:
 **/
static void
pk_plugin_install_clear_layout (PkPluginInstall *self)
{
	g_debug ("clearing layout");

	if (self->priv->pango_layout) {
		g_object_unref (self->priv->pango_layout);
		self->priv->pango_layout = NULL;
	}
}

/**
 * pk_plugin_install_refresh:
 **/
static void
pk_plugin_install_refresh (PkPluginInstall *self)
{
	pk_plugin_request_refresh (PK_PLUGIN (self));
}

#define SPINNER_LINES 12
#define SPINNER_SIZE 24

static gboolean
spinner_timeout (gpointer data)
{
	PkPluginInstall *self = data;

	self->priv->current++;
	if (self->priv->current >= SPINNER_LINES)
		self->priv->current = 0;
	self->priv->update_spinner = TRUE;

	pk_plugin_install_refresh (self);

	return TRUE;
}

/**
 * pk_plugin_install_set_status:
 **/
static void
pk_plugin_install_set_status (PkPluginInstall *self, PkPluginInstallPackageStatus status)
{
	if (self->priv->status != status) {
		g_debug ("setting status %u", status);
		self->priv->status = status;

		if (status == INSTALLING) {
			self->priv->timeout = g_timeout_add (80, spinner_timeout, self);
			g_source_set_name_by_id (self->priv->timeout, "[PkPluginInstall] spinner");
		}
		else if (self->priv->timeout) {
			g_source_remove (self->priv->timeout);
			self->priv->timeout = 0;
		}
	}

}

/**
 * pk_plugin_install_set_available_version:
 **/
static void
pk_plugin_install_set_available_version (PkPluginInstall *self, const gchar *version)
{
	g_debug ("setting available version: %s", version);

	g_free (self->priv->available_version);
	self->priv->available_version = g_strdup (version);
}

/**
 * pk_plugin_install_set_available_package_name:
 **/
static void
pk_plugin_install_set_available_package_name (PkPluginInstall *self, const gchar *name)
{
	g_debug ("setting available package name: %s", name);

	g_free (self->priv->available_package_name);
	self->priv->available_package_name = g_strdup (name);
}

/**
 * pk_plugin_install_set_installed_package_name:
 **/
static void
pk_plugin_install_set_installed_package_name (PkPluginInstall *self, const gchar *name)
{
	g_debug ("setting installed package name: %s", name);

	g_free (self->priv->installed_package_name);
	self->priv->installed_package_name = g_strdup (name);
}

/**
 * pk_plugin_install_set_installed_version:
 **/
static void
pk_plugin_install_set_installed_version (PkPluginInstall *self, const gchar *version)
{
	g_debug ("setting installed version: %s", version);

	g_free (self->priv->installed_version);
	self->priv->installed_version = g_strdup (version);
}

/**
 * pk_plugin_install_finished_cb:
 **/
static void
pk_plugin_install_finished_cb (GObject *object, GAsyncResult *res, PkPluginInstall *self)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	GPtrArray *packages = NULL;
	PkPackage *item;
	gchar *filename = NULL;
	gchar **split = NULL;
	PkError *error_code = NULL;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to install: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get packages */
	packages = pk_results_get_package_array (results);
	if (packages->len == 0)
		goto out;

	/* no results */
	if (packages->len > 1)
		g_warning ("more than one result (%i), just choosing first", packages->len);

	/* choose first package */
	item = g_ptr_array_index (packages, 0);
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* if we didn't use displayname, use the summary */
	if (self->priv->display_name == NULL)
		self->priv->display_name = g_strdup (summary);

	/* parse the data */
	if (info == PK_INFO_ENUM_AVAILABLE) {
		if (self->priv->status == IN_PROGRESS)
			pk_plugin_install_set_status (self, AVAILABLE);
		else if (self->priv->status == INSTALLED)
			pk_plugin_install_set_status (self, UPGRADABLE);
		split = pk_package_id_split (package_id);
		pk_plugin_install_set_available_package_name (self, split[0]);
		pk_plugin_install_set_available_version (self, split[1]);
		g_strfreev (split);

		pk_plugin_install_clear_layout (self);
		pk_plugin_install_refresh (self);

	} else if (info == PK_INFO_ENUM_INSTALLED) {
		if (self->priv->status == IN_PROGRESS)
			pk_plugin_install_set_status (self, INSTALLED);
		else if (self->priv->status == AVAILABLE)
			pk_plugin_install_set_status (self, UPGRADABLE);
		split = pk_package_id_split (package_id);
		pk_plugin_install_set_installed_package_name (self, split[0]);
		pk_plugin_install_set_installed_version (self, split[1]);
		g_strfreev (split);

		pk_plugin_install_set_status (self, INSTALLED);
		pk_plugin_install_clear_layout (self);
		pk_plugin_install_refresh (self);
	}
out:
	g_free (filename);
	g_free (package_id);
	g_free (summary);

	/* we didn't get any results, or we failed */
	if (self->priv->status == IN_PROGRESS) {
		pk_plugin_install_set_status (self, UNAVAILABLE);
		pk_plugin_install_clear_layout (self);
		pk_plugin_install_refresh (self);
	}
	if (error_code != NULL)
		g_object_unref (error_code);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * pk_plugin_install_recheck:
 **/
static void
pk_plugin_install_recheck (PkPluginInstall *self)
{
//	guint i;
	const gchar *data;
//	gchar **package_ids;

	self->priv->status = IN_PROGRESS;
	pk_plugin_install_set_available_version (self, NULL);
	pk_plugin_install_set_available_package_name (self, NULL);
	pk_plugin_install_set_installed_version (self, NULL);
	pk_plugin_install_set_installed_package_name (self, NULL);

	/* get data, if if does not exist */
	if (self->priv->package_names == NULL) {
		data = pk_plugin_get_data (PK_PLUGIN (self), "displayname");
		self->priv->display_name = g_strdup (data);
		data = pk_plugin_get_data (PK_PLUGIN (self), "packagenames");
		self->priv->package_names = g_strsplit (data, " ", -1);
	}

	/* do async resolve */
	pk_client_resolve_async (self->priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1),
				 self->priv->package_names, NULL, NULL, NULL,
				 (GAsyncReadyCallback) pk_plugin_install_finished_cb, self);
}

/* just to please -Wmissing-format-attribute */
static void pk_plugin_install_append_markup (GString *str, const gchar *format, ...) G_GNUC_PRINTF(2,3);

/**
 * pk_plugin_install_append_markup:
 **/
static void
pk_plugin_install_append_markup (GString *str, const gchar *format, ...)
{
	va_list vap;
	gchar *tmp;

	va_start (vap, format);
	tmp = g_markup_vprintf_escaped (format, vap);
	va_end (vap);

	g_string_append (str, tmp);
	g_free (tmp);
}

/**
 * pk_plugin_install_rgba_from_gdk_color:
 **/
static guint32
pk_plugin_install_rgba_from_gdk_color (GdkColor *color)
{
	return (((color->red >> 8) << 24) |
		((color->green >> 8) << 16) |
		 ((color->blue >> 8) << 8) |
		  0xff);
}

/**
 * pk_plugin_install_set_source_from_rgba:
 **/
static void
pk_plugin_install_set_source_from_rgba (cairo_t *cr, guint32 rgba)
{
	cairo_set_source_rgba (cr,
			      ((rgba & 0xff000000) >> 24) / 255.,
			      ((rgba & 0x00ff0000) >> 16) / 255.,
			      ((rgba & 0x0000ff00) >> 8) / 255.,
			      (rgba & 0x000000ff) / 255.);
}

/**
 * pk_plugin_install_get_style:
 *
 * Retrieve the system colors and fonts.
 * This looks incredibly expensive .... to create a GtkWindow for
 * every expose ... but actually it's only moderately expensive;
 * Creating a GtkWindow is just normal GObject creation overhead --
 * the extra expense beyond that will come when we actually create
 * the window.
 **/
static void
pk_plugin_install_get_style (PangoFontDescription **font_desc,
			     guint32 *foreground,
			     guint32 *background,
			     guint32 *linked)
{
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkStyle *style;
	GdkColor link_color = { 0, 0, 0, 0xeeee };
	GdkColor *tmp = NULL;

	gtk_widget_ensure_style (window);

	style = gtk_widget_get_style (window);
	*foreground = pk_plugin_install_rgba_from_gdk_color (&style->text[GTK_STATE_NORMAL]);
	*background = pk_plugin_install_rgba_from_gdk_color (&style->base[GTK_STATE_NORMAL]);

	gtk_widget_style_get (GTK_WIDGET (window), "link-color", &tmp, NULL);
	if (tmp != NULL) {
		link_color = *tmp;
		gdk_color_free (tmp);
	}

	*linked = pk_plugin_install_rgba_from_gdk_color (&link_color);
	*font_desc = pango_font_description_copy (style->font_desc);

	gtk_widget_destroy (window);
}

/**
 * pk_plugin_install_ensure_layout:
 **/
static void
pk_plugin_install_ensure_layout (PkPluginInstall *self,
				 cairo_t *cr,
				 PangoFontDescription *font_desc,
				 guint32 link_color)
{
	GString *markup = g_string_new (NULL);

	if (self->priv->pango_layout != NULL)
		return;

	self->priv->pango_layout = pango_cairo_create_layout (cr);
	pango_layout_set_font_description (self->priv->pango_layout, font_desc);

	/* WARNING: Any changes to what links are created here will require corresponding
	 * changes to the pk_plugin_install_button_release () method
	 */
	switch (self->priv->status) {
	case IN_PROGRESS:
		/* TRANSLATORS: when we are getting data from the daemon */
		pk_plugin_install_append_markup (markup, _("Getting package information..."));
		break;
	case INSTALLED:
		if (self->priv->app_info != 0) {
			pk_plugin_install_append_markup (markup, "<span color='#%06x' underline='single'>", link_color >> 8);
			/* TRANSLATORS: run an applicaiton */
			pk_plugin_install_append_markup (markup, _("Run %s"), self->priv->display_name);
			pk_plugin_install_append_markup (markup, "</span>");
		} else
			pk_plugin_install_append_markup (markup, "<big>%s</big>", self->priv->display_name);
		if (self->priv->installed_version != NULL)
			/* TRANSLATORS: show the installed version of a package */
			pk_plugin_install_append_markup (markup, "\n<small>%s: %s</small>", _("Installed version"), self->priv->installed_version);
		break;
	case UPGRADABLE:
		pk_plugin_install_append_markup (markup, "<big>%s</big>", self->priv->display_name);
		if (self->priv->app_info != 0) {
			if (self->priv->installed_version != NULL) {
				pk_plugin_install_append_markup (markup, "\n<span color='#%06x' underline='single'>", link_color >> 8);
				/* TRANSLATORS: run the application now */
				pk_plugin_install_append_markup (markup, _("Run version %s now"), self->priv->installed_version);
				pk_plugin_install_append_markup (markup, "</span>");
			} else {
				pk_plugin_install_append_markup (markup,
				              "\n<span color='#%06x' underline='single'>%s</span>",
					      /* TRANSLATORS: run the application now */
					      link_color >> 8, _("Run now"));
		        }
		}

		pk_plugin_install_append_markup (markup, "\n<span color='#%06x' underline='single'>", link_color >> 8);
		/* TRANSLATORS: update to a new version of the package */
		pk_plugin_install_append_markup (markup, _("Update to version %s"), self->priv->available_version);
		pk_plugin_install_append_markup (markup, "</span>");
		break;
	case AVAILABLE:
		pk_plugin_install_append_markup (markup, "<span color='#%06x' underline='single'>", link_color >> 8);
		/* TRANSLATORS: To install a package */
		pk_plugin_install_append_markup (markup, _("Install %s now"), self->priv->display_name);
		pk_plugin_install_append_markup (markup, "</span>");
		/* TRANSLATORS: the version of the package */
		pk_plugin_install_append_markup (markup, "\n<small>%s: %s</small>", _("Version"), self->priv->available_version);
		break;
	case UNAVAILABLE:
		pk_plugin_install_append_markup (markup, "<big>%s</big>", self->priv->display_name);
		/* TRANSLATORS: noting found, so can't install */
		pk_plugin_install_append_markup (markup, "\n<small>%s</small>", _("No packages found for your system"));
		break;
	case INSTALLING:
		pk_plugin_install_append_markup (markup, "<big>%s</big>", self->priv->display_name);
		/* TRANSLATORS: package is being installed */
		pk_plugin_install_append_markup (markup, "\n<small>%s</small>", _("Installing..."));
		break;
	}

	pango_layout_set_markup (self->priv->pango_layout, markup->str, -1);
	g_string_free (markup, TRUE);
}

/**
 * pk_plugin_install_start:
 **/
static gboolean
pk_plugin_install_start (PkPlugin *plugin)
{
	PkPluginInstall *self = PK_PLUGIN_INSTALL (plugin);
	pk_plugin_install_recheck (self);
	return TRUE;
}

/**
 * pk_plugin_install_draw_spinner:
 **/
static void
pk_plugin_install_draw_spinner (PkPlugin *plugin, cairo_t *cr, int cx, int cy)
{
	gint width, height;
	double x, y;
	double radius;
	double half;
	gint i;

	PkPluginInstall *self = PK_PLUGIN_INSTALL (plugin);

        cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	width = height = SPINNER_SIZE;
	radius = MIN (width / 2.0, height / 2.0);
	half = SPINNER_LINES / 2;

	x = cx + width / 2;
	y = cy + height / 2;

	for (i = 0; i < SPINNER_LINES; i++) {
		gint inset = 0.7 * radius;
		/* transparency is a function of time and intial value */
		gdouble t = (gdouble) ((i + SPINNER_LINES - self->priv->current) % SPINNER_LINES) / SPINNER_LINES;
		cairo_save (cr);

		cairo_set_source_rgba (cr, 0, 0, 0, t);
		cairo_set_line_width (cr, 2.0);
		cairo_move_to (cr,
			       x + (radius - inset) * cos (i * G_PI / half),
			       y + (radius - inset) * sin (i * G_PI / half));
		cairo_line_to (cr,
			       x + radius * cos (i * G_PI / half),
			       y + radius * sin (i * G_PI / half));
		cairo_stroke (cr);
		cairo_restore (cr);
	}
}

/**
 * pk_plugin_install_rounded_rectangle:
 **/
static void
pk_plugin_install_rounded_rectangle (cairo_t *cr, gdouble x, gdouble y,
				     gdouble w, gdouble h, gdouble radius)
{
	const gdouble ARC_TO_BEZIER = 0.55228475;
	gdouble c;

	if (radius == 0) {
		cairo_rectangle (cr, x, y, w, h);
		return;
	}

	if (radius > w - radius)
		radius = w / 2;
	if (radius > h - radius)
		radius = h / 2;

	c = ARC_TO_BEZIER * radius;

	cairo_new_path (cr);
	cairo_move_to (cr, x + radius, y);
	cairo_rel_line_to (cr, w - 2 * radius, 0);
	cairo_rel_curve_to (cr, c, 0, radius, c, radius, radius);
	cairo_rel_line_to (cr, 0, h - 2 * radius);
	cairo_rel_curve_to (cr, 0, c, c - radius, radius, -radius, radius);
	cairo_rel_line_to (cr, -w + 2 * radius, 0);
	cairo_rel_curve_to (cr, -c, 0, -radius, -c, -radius, -radius);
	cairo_rel_line_to (cr, 0, -h + 2 * radius);
	cairo_rel_curve_to (cr, 0, -c, radius - c, -radius, radius, -radius);
	cairo_close_path (cr);
}

/**
 * pk_plugin_install_draw:
 **/
static gboolean
pk_plugin_install_draw (PkPlugin *plugin, cairo_t *cr)
{
	guint32 foreground, background, linked;
	PangoFontDescription *font_desc;
	guint x;
	guint y;
	guint width;
	guint height;
	guint radius;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf = NULL;
	PangoRectangle rect;
	PkPluginInstall *self = PK_PLUGIN_INSTALL (plugin);
	guint sep;
	const gchar *data;
	PangoColor color;
	gboolean has_color;

	/* get parameters */
	g_object_get (self,
		      "x", &x,
		      "y", &y,
		      "width", &width,
		      "height", &height,
		      NULL);

	data = pk_plugin_get_data (plugin, "radius");
	if (data)
		radius = atoi (data);
	else
		radius = 0;

	data = pk_plugin_get_data (plugin, "color");
	if (data)
		has_color = pango_color_parse (&color, data);
	else
		has_color = FALSE;

	sep = MAX ((height - 48) / 2, radius);

	g_debug ("drawing on %ux%u (%ux%u)", x, y, width, height);

	/* get properties */
	pk_plugin_install_get_style (&font_desc, &foreground, &background, &linked);
	if (self->priv->update_spinner) {
		self->priv->update_spinner = FALSE;
		goto update_spinner;
	}

        /* fill background */
	pk_plugin_install_set_source_from_rgba (cr, background);
	cairo_rectangle (cr, x, y, width, height);
	cairo_fill (cr);
	if (has_color)
		cairo_set_source_rgb (cr, color.red / 65536.0, color.green / 65536.0, color.blue / 65536.0);
	else
		pk_plugin_install_set_source_from_rgba (cr, background);
	pk_plugin_install_rounded_rectangle (cr, x + 0.5, y + 0.5, width - 1, height - 1, radius);
	cairo_fill (cr);

        /* grey outline */
	cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
	pk_plugin_install_rounded_rectangle (cr, x + 0.5, y + 0.5, width - 1, height - 1, radius);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);

	/* get themed icon */
	theme = gtk_icon_theme_get_default ();
	pixbuf = gtk_icon_theme_load_icon (theme, "package-x-generic", 48,
					   GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	if (pixbuf == NULL)
		goto skip;

	gdk_cairo_set_source_pixbuf (cr, pixbuf, x + sep, y + (height - 48) / 2);
	cairo_rectangle (cr, x + sep, y + (height - 48) / 2, 48, 48);
	cairo_fill (cr);

skip:
	/* write text */
	pk_plugin_install_ensure_layout (self, cr, font_desc, linked);
	pango_layout_get_pixel_extents (self->priv->pango_layout, &rect, NULL);
	cairo_move_to (cr, x + sep + 48 + sep, y + (height - (rect.height + 48) / 2) / 2);
	pk_plugin_install_set_source_from_rgba (cr, foreground);
	pango_cairo_show_layout (cr, self->priv->pango_layout);

update_spinner:
	if (self->priv->status == INSTALLING) {
		pango_layout_get_pixel_extents (self->priv->pango_layout, &rect, NULL);
		if (has_color)
			cairo_set_source_rgb (cr, color.red / 65536.0, color.green / 65536.0, color.blue / 65536.0);
		else
			pk_plugin_install_set_source_from_rgba (cr, background);
		cairo_rectangle (cr,
				 x + sep + 48 + sep + rect.width + 2 * sep,
				 y + (height - SPINNER_SIZE) / 2,
				 SPINNER_SIZE, SPINNER_SIZE);
		cairo_fill (cr);
		pk_plugin_install_set_source_from_rgba (cr, foreground);

		pk_plugin_install_draw_spinner (plugin, cr,
						x + sep + 48 + sep + rect.width + 2 * sep,
						y + (height - SPINNER_SIZE) / 2);
	}
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	return TRUE;
}

/**
 * pk_plugin_install_line_is_terminated:
 *
 * Cut and paste from pango-layout.c; determines if a layout iter is on
 * a line terminated by a real line break (rather than a line break from
 * wrapping). We use this to determine whether the empty run at the end
 * of a display line should be counted as a break between links or not.
 *
 * (Code in pango-layout.c is by me, Copyright Red Hat, and hereby relicensed
 * to the license of this file)
 **/
static gboolean
pk_plugin_install_line_is_terminated (PangoLayoutIter *iter)
{
	/* There is a real terminator at the end of each paragraph other
	 * than the last.
	 */
	PangoLayoutLine *line = pango_layout_iter_get_line (iter);
	GSList *lines = pango_layout_get_lines (pango_layout_iter_get_layout (iter));
	GSList *found = g_slist_find (lines, line);
	if (!found) {
		g_warning ("Can't find line in layout line list");
		return FALSE;
	}

	if (found->next) {
		PangoLayoutLine *next_line = (PangoLayoutLine *)found->next->data;
		if (next_line->is_paragraph_start)
			return TRUE;
	}

	return FALSE;
}

/**
 * pk_plugin_install_get_link_index:
 *
 * This function takes an X,Y position and determines whether it is over one
 * of the underlined portions of the layout (a link). It works by iterating
 * through the runs of the layout (a run is a segment with a consistent
 * font and display attributes, more or less), and counting the underlined
 * segments that we see. A segment that is underlined could be broken up
 * into multiple runs if it is drawn with multiple fonts due to fonts
 * substitution, so we actually count non-underlined => underlined
 * transitions.
 **/
static gint
pk_plugin_install_get_link_index (PkPluginInstall *self, gint x, gint y)
{
	gint idx;
	gint trailing;
	PangoLayoutIter *iter;
	gint seen_links = 0;
	gboolean in_link = FALSE;
	gint result = -1;
	guint height;
	guint radius;
	guint sep;
	PangoRectangle rect;
	const char *data;

	/* Coordinates are relative to origin of plugin (different from drawing) */

	if (!self->priv->pango_layout)
		return -1;

	g_object_get (self, "height", &height, NULL);
	data = pk_plugin_get_data (PK_PLUGIN (self), "radius");
	if (data)
		radius = atoi (data);
	else
		radius = 0;
	sep = MAX ((height - 48) / 2, radius);
	pango_layout_get_pixel_extents (self->priv->pango_layout, &rect, NULL);
	x -= sep + 48 + sep;
	y -= (height - (rect.height + 48) / 2) / 2;

	if (!pango_layout_xy_to_index (self->priv->pango_layout, x * PANGO_SCALE, y * PANGO_SCALE, &idx, &trailing))
		return - 1;

	iter = pango_layout_get_iter (self->priv->pango_layout);
	while (TRUE) {
		PangoLayoutRun *run = pango_layout_iter_get_run (iter);
		if (run) {
			PangoItem *item = run->item;
			PangoUnderline uline = PANGO_UNDERLINE_NONE;
			GSList *l;

			for (l = item->analysis.extra_attrs; l; l = l->next) {
				PangoAttribute *attr = (PangoAttribute *)l->data;
				if (attr->klass->type == PANGO_ATTR_UNDERLINE) {
					uline = (PangoUnderline) ( (PangoAttrInt *)attr)->value;
				}
			}

			if (uline == PANGO_UNDERLINE_NONE)
				in_link = FALSE;
			else if (!in_link) {
				in_link = TRUE;
				seen_links++;
			}

			if (item->offset <= idx && idx < item->offset + item->length) {
				if (in_link)
					result = seen_links - 1;

				break;
			}
		} else {
			/* We have an empty run at the end of each line. A line break doesn't
			 * terminate the link, but a real newline does.
			 */
			if (pk_plugin_install_line_is_terminated (iter))
				in_link = FALSE;
		}

		if (!pango_layout_iter_next_run (iter))
			break;
	}

	pango_layout_iter_free (iter);

	return result;
}

/**
 * pk_plugin_install_method_finished_cb:
 **/
static void
pk_plugin_install_method_finished_cb (GObject *source_object,
				      GAsyncResult *res,
				      gpointer user_data)
{
	PkPluginInstall *self = PK_PLUGIN_INSTALL (user_data);
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	GError *error = NULL;
	GVariant *value;

	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		g_warning ("Error occurred during install: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (value != NULL)
		g_variant_unref (value);
	pk_plugin_install_recheck (self);
}

/**
 * pk_plugin_install_install_package:
 **/
static void
pk_plugin_install_install_package (PkPluginInstall *self, Time event_time)
{
	GdkEvent *event;
	GdkWindow *window;
	guint xid = 0;
	gchar **packages;

	if (self->priv->available_package_name == NULL) {
		g_warning ("No available package to install");
		return;
	}

	/* will be NULL when activated not using a keyboard or a mouse */
	event = gtk_get_current_event ();
	if (event != NULL && event->any.window != NULL) {
		window = gdk_window_get_toplevel (event->any.window);
		xid = GDK_DRAWABLE_XID (window);
	}

	packages = g_strsplit (self->priv->available_package_name, ";", -1);
	g_dbus_proxy_call (self->priv->session_pk_proxy,
			   "InstallPackageNames",
			   g_variant_new ("(u^a&ss)",
					  xid,
					  packages,
					  "hide-confirm-search,"
					  "hide-progress,"
					  "hide-confirm-deps,"
					  "hide-finished"),
			   G_DBUS_CALL_FLAGS_NONE,
			   60 * 60 * 1000, /* 1 hour */
			   self->priv->cancellable,
			   pk_plugin_install_method_finished_cb,
			   self);
	g_strfreev (packages);

	pk_plugin_install_set_status (self, INSTALLING);
	pk_plugin_install_clear_layout (self);
	pk_plugin_install_refresh (self);
}

/**
 * pk_plugin_install_get_server_timestamp:
 **/
static guint32
pk_plugin_install_get_server_timestamp ()
{
	GtkWidget *invisible = gtk_invisible_new ();
	GdkWindow *window;
	guint32 server_time;

	gtk_widget_realize (invisible);
	window = gtk_widget_get_window (invisible);
	server_time = gdk_x11_get_server_time (window);
	gtk_widget_destroy (invisible);
	return server_time;
}

/**
 * pk_plugin_install_run_application:
 **/
static void
pk_plugin_install_run_application (PkPluginInstall *self, Time event_time)
{
	GError *error = NULL;
	GdkAppLaunchContext *context;

	if (self->priv->app_info == 0) {
		g_warning ("Didn't find application to launch");
		return;
	}

	if (event_time == 0)
		event_time = pk_plugin_install_get_server_timestamp ();

	context = gdk_app_launch_context_new ();
	gdk_app_launch_context_set_timestamp (context, event_time);
	if (!g_app_info_launch (self->priv->app_info, NULL, G_APP_LAUNCH_CONTEXT (context), &error)) {
		g_warning ("%s\n", error->message);
		g_clear_error (&error);
		return;
	}

	if (context != NULL)
		g_object_unref (context);
}

/**
 * pk_plugin_install_button_release:
 **/
static gboolean
pk_plugin_install_button_release (PkPlugin *plugin, gint x, gint y, Time event_time)
{
	PkPluginInstall *self = PK_PLUGIN_INSTALL (plugin);
	gint idx = pk_plugin_install_get_link_index (self, x, y);
	if (idx < 0)
		return FALSE;

	switch (self->priv->status) {
	case IN_PROGRESS:
	case INSTALLING:
	case UNAVAILABLE:
		break;
	case INSTALLED:
		if (self->priv->app_info != NULL)
			pk_plugin_install_run_application (self, event_time);
		break;
	case UPGRADABLE:
		if (self->priv->app_info != NULL && idx == 0)
			pk_plugin_install_run_application (self, event_time);
		else
			pk_plugin_install_install_package (self, event_time);
		break;
	case AVAILABLE:
		if (self->priv->available_package_name != NULL)
			pk_plugin_install_install_package (self, event_time);
		break;
	}
	return TRUE;
}

static void
pk_plugin_set_cursor (GdkWindow     *window,
                      GdkCursorType  cursor)
{
	Display *display;
	Cursor xcursor;

	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default());
        if (cursor >= 0)
		xcursor = XCreateFontCursor (display, cursor);
	else
		xcursor = None;
	XDefineCursor (display, GDK_WINDOW_XID (window), xcursor);
}

static gboolean
pk_plugin_install_motion (PkPlugin *plugin,
                          gint      x,
                          gint      y)
{
	PkPluginInstall *self = PK_PLUGIN_INSTALL (plugin);
	GdkWindow *window;
	gint idx;

	idx = pk_plugin_install_get_link_index (self, x, y);
        g_object_get (plugin, "gdk-window", &window, NULL);

	if (idx < 0) {
		pk_plugin_set_cursor (window, -1);
		return FALSE;
	}
	switch (self->priv->status) {
	case IN_PROGRESS:
	case INSTALLING:
	case UNAVAILABLE:
		pk_plugin_set_cursor (window, -1);
		break;
	case INSTALLED:
	case UPGRADABLE:
	case AVAILABLE:
		pk_plugin_set_cursor (window, GDK_HAND2);
		break;
	}
	return FALSE;
}

/**
 * pk_plugin_install_finalize:
 **/
static void
pk_plugin_install_finalize (GObject *object)
{
	PkPluginInstall *self;
	g_return_if_fail (PK_IS_PLUGIN_INSTALL (object));
	self = PK_PLUGIN_INSTALL (object);

	pk_plugin_install_clear_layout (self);

	if (self->priv->app_info != NULL)
		g_object_unref (self->priv->app_info);

	g_cancellable_cancel (self->priv->cancellable);
	g_object_unref (self->priv->session_pk_proxy);

	/* remove clients */
	g_object_unref (self->priv->client);

	G_OBJECT_CLASS (pk_plugin_install_parent_class)->finalize (object);
}

/**
 * pk_plugin_install_class_init:
 **/
static void
pk_plugin_install_class_init (PkPluginInstallClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkPluginClass *plugin_class = PK_PLUGIN_CLASS (klass);

	object_class->finalize = pk_plugin_install_finalize;
	plugin_class->start = pk_plugin_install_start;
	plugin_class->draw = pk_plugin_install_draw;
	plugin_class->button_release = pk_plugin_install_button_release;
	plugin_class->motion = pk_plugin_install_motion;

	g_type_class_add_private (klass, sizeof (PkPluginInstallPrivate));
}

/**
 * pk_plugin_install_init:
 **/
static void
pk_plugin_install_init (PkPluginInstall *self)
{
	GError *error = NULL;

	self->priv = PK_PLUGIN_INSTALL_GET_PRIVATE (self);
	self->priv->status = IN_PROGRESS;
	self->priv->client = pk_client_new ();

	/* connect early to allow the service to start */
	self->priv->session_pk_proxy =
		g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify",
					       self->priv->cancellable,
					       &error);
	if (self->priv->session_pk_proxy == NULL) {
		g_warning ("Error connecting to PK session instance: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * pk_plugin_install_new:
 * Return value: A new plugin_install class instance.
 **/
PkPluginInstall *
pk_plugin_install_new (void)
{
	PkPluginInstall *self;
	self = g_object_new (PK_TYPE_PLUGIN_INSTALL, NULL);
	return PK_PLUGIN_INSTALL (self);
}
