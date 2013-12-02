/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include <gtk/gtk.h>

#include "pk-plugin.h"

#define PK_PLUGIN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PLUGIN, PkPluginPrivate))

struct PkPluginPrivate
{
	gboolean		 started;
	guint			 x;
	guint			 y;
	guint			 width;
	guint			 height;
	Display			*display;
	Visual			*visual;
	Window			 window;
	GdkWindow		*gdk_window;
	GHashTable		*data;
};

enum {
	SIGNAL_REFRESH,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_X,
	PROP_Y,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_DISPLAY,
	PROP_VISUAL,
	PROP_STARTED,
	PROP_WINDOW,
	PROP_GDKWINDOW,
	PROP_LAST,
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkPlugin, pk_plugin, G_TYPE_OBJECT)

/**
 * pk_plugin_set_data:
 **/
gboolean
pk_plugin_set_data (PkPlugin *plugin, const gchar *name, const gchar *value)
{
	g_return_val_if_fail (PK_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	g_hash_table_insert (plugin->priv->data, g_strdup (name), g_strdup (value));
	g_debug ("SET: name=%s, value=%s <%p>", name, value, plugin);

	return TRUE;
}

/**
 * pk_plugin_get_data:
 **/
const gchar *
pk_plugin_get_data (PkPlugin *plugin, const gchar *name)
{
	const gchar *value;

	g_return_val_if_fail (PK_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	value = g_hash_table_lookup (plugin->priv->data, name);
	g_debug ("GET: name=%s, value=%s <%p>", name, value, plugin);

	return value;
}

/**
 * pk_plugin_start:
 **/
gboolean
pk_plugin_start (PkPlugin *plugin)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	g_debug ("start <%p>", plugin);

	/* already started, don't restart */
	if (plugin->priv->started) {
		g_warning ("already started <%p>", plugin);
		return FALSE;
	}

	/* no support */
	if (klass->start == NULL)
		return FALSE;
	plugin->priv->started = klass->start (plugin);
	return plugin->priv->started;
}

/**
 * pk_plugin_draw:
 **/
gboolean
pk_plugin_draw (PkPlugin *plugin, cairo_t *cr)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->draw == NULL)
		return FALSE;

	g_debug ("draw on %p <%p>", cr, plugin);

	return klass->draw (plugin, cr);
}

/**
 * pk_plugin_button_press:
 **/
gboolean
pk_plugin_button_press (PkPlugin *plugin, gint x, gint y, Time event_time)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->button_press == NULL)
		return FALSE;

	g_debug ("button_press %i,%i <%p>", x, y, plugin);

	return klass->button_press (plugin, x, y, event_time);
}

/**
 * pk_plugin_button_release:
 **/
gboolean
pk_plugin_button_release (PkPlugin *plugin, gint x, gint y, Time event_time)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->button_release == NULL)
		return FALSE;

	g_debug ("button_release %i,%i <%p>", x, y, plugin);

	return klass->button_release (plugin, x, y, event_time);
}

/**
 * pk_plugin_motion:
 **/
gboolean
pk_plugin_motion (PkPlugin *plugin, gint x, gint y)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->motion == NULL)
		return FALSE;

	g_debug ("motion %i,%i <%p>", x, y, plugin);

	return klass->motion (plugin, x, y);
}

/**
 * pk_plugin_enter:
 **/
gboolean
pk_plugin_enter (PkPlugin *plugin, gint x, gint y)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->enter == NULL)
		return FALSE;

	g_debug ("enter %i,%i <%p>", x, y, plugin);

	return klass->enter (plugin, x, y);
}

/**
 * pk_plugin_leave:
 **/
gboolean
pk_plugin_leave (PkPlugin *plugin, gint x, gint y)
{
	PkPluginClass *klass = PK_PLUGIN_GET_CLASS (plugin);

	/* no support */
	if (klass->leave == NULL)
		return FALSE;

	g_debug ("leave %i,%i <%p>", x, y, plugin);

	return klass->leave (plugin, x, y);
}

/**
 * pk_plugin_request_refresh:
 **/
gboolean
pk_plugin_request_refresh (PkPlugin *plugin)
{
	g_return_val_if_fail (PK_IS_PLUGIN (plugin), FALSE);

	g_debug ("emit refresh <%p>", plugin);

	g_signal_emit (plugin, signals [SIGNAL_REFRESH], 0);
	return TRUE;
}

/**
 * pk_plugin_get_property:
 **/
static void
pk_plugin_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkPlugin *plugin = PK_PLUGIN (object);
	switch (prop_id) {
	case PROP_X:
		g_value_set_uint (value, plugin->priv->x);
		break;
	case PROP_Y:
		g_value_set_uint (value, plugin->priv->y);
		break;
	case PROP_WIDTH:
		g_value_set_uint (value, plugin->priv->width);
		break;
	case PROP_HEIGHT:
		g_value_set_uint (value, plugin->priv->height);
		break;
	case PROP_DISPLAY:
		g_value_set_pointer (value, plugin->priv->display);
		break;
	case PROP_VISUAL:
		g_value_set_pointer (value, plugin->priv->visual);
		break;
	case PROP_STARTED:
		g_value_set_boolean (value, plugin->priv->started);
		break;
	case PROP_WINDOW:
		g_value_set_ulong (value, plugin->priv->window);
		break;
	case PROP_GDKWINDOW:
		g_value_set_pointer (value, plugin->priv->gdk_window);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_plugin_set_property:
 **/
static void
pk_plugin_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkPlugin *plugin = PK_PLUGIN (object);
	switch (prop_id) {
	case PROP_X:
		plugin->priv->x = g_value_get_uint (value);
		break;
	case PROP_Y:
		plugin->priv->y = g_value_get_uint (value);
		break;
	case PROP_WIDTH:
		plugin->priv->width = g_value_get_uint (value);
		break;
	case PROP_HEIGHT:
		plugin->priv->height = g_value_get_uint (value);
		break;
	case PROP_DISPLAY:
		plugin->priv->display = g_value_get_pointer (value);
		break;
	case PROP_VISUAL:
		plugin->priv->visual = g_value_get_pointer (value);
		break;
	case PROP_STARTED:
		plugin->priv->started = g_value_get_boolean (value);
		break;
	case PROP_WINDOW:
		plugin->priv->window = g_value_get_ulong (value);
		break;
	case PROP_GDKWINDOW:
		plugin->priv->gdk_window = g_value_get_pointer (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
/**
 * pk_plugin_finalize:
 **/
static void
pk_plugin_finalize (GObject *object)
{
	PkPlugin *plugin;
	g_return_if_fail (PK_IS_PLUGIN (object));
	plugin = PK_PLUGIN (object);

	g_hash_table_unref (plugin->priv->data);

	G_OBJECT_CLASS (pk_plugin_parent_class)->finalize (object);
}

/**
 * pk_plugin_class_init:
 **/
static void
pk_plugin_class_init (PkPluginClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_plugin_finalize;
	object_class->get_property = pk_plugin_get_property;
	object_class->set_property = pk_plugin_set_property;

	signals [SIGNAL_REFRESH] =
		g_signal_new ("refresh",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkPluginClass, refresh),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	pspec = g_param_spec_uint ("x", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_X, pspec);

	pspec = g_param_spec_uint ("y", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_Y, pspec);

	pspec = g_param_spec_uint ("width", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_WIDTH, pspec);

	pspec = g_param_spec_uint ("height", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_HEIGHT, pspec);

	pspec = g_param_spec_pointer ("display", NULL, NULL,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DISPLAY, pspec);

	pspec = g_param_spec_pointer ("visual", NULL, NULL,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VISUAL, pspec);

	pspec = g_param_spec_boolean ("started", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STARTED, pspec);

	pspec = g_param_spec_ulong ("window", NULL, NULL,
				    0, G_MAXULONG, 0,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_WINDOW, pspec);

	pspec = g_param_spec_pointer ("gdk-window", NULL, NULL,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GDKWINDOW, pspec);

	g_type_class_add_private (klass, sizeof (PkPluginPrivate));
}

/**
 * pk_plugin_init:
 **/
static void
pk_plugin_init (PkPlugin *plugin)
{
	plugin->priv = PK_PLUGIN_GET_PRIVATE (plugin);
	plugin->priv->started = FALSE;
	plugin->priv->x = 0;
	plugin->priv->y = 0;
	plugin->priv->width = 0;
	plugin->priv->height = 0;
	plugin->priv->display = NULL;
	plugin->priv->visual = NULL;
	plugin->priv->window = 0;
	plugin->priv->gdk_window = NULL;
	plugin->priv->data = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/**
 * pk_plugin_new:
 * Return value: A new plugin_install class instance.
 **/
PkPlugin *
pk_plugin_new (void)
{
	PkPlugin *plugin;
	plugin = g_object_new (PK_TYPE_PLUGIN, NULL);
	return PK_PLUGIN (plugin);
}
