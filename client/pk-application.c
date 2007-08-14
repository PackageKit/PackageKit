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

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#include "pk-application.h"
#include "pk-debug.h"

static void     pk_application_class_init (PkApplicationClass *klass);
static void     pk_application_init       (PkApplication      *application);
static void     pk_application_finalize   (GObject	    *object);

#define PK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_APPLICATION, PkApplicationPrivate))

struct PkApplicationPrivate
{
	GladeXML		*glade_xml;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkApplication, pk_application, G_TYPE_OBJECT)

/**
 * pk_application_class_init:
 * @klass: This graph class instance
 **/
static void
pk_application_class_init (PkApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_application_finalize;
	g_type_class_add_private (klass, sizeof (PkApplicationPrivate));

	signals [ACTION_HELP] =
		g_signal_new ("action-help",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkApplicationClass, action_help),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkApplicationClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_application_help_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_help_cb (GtkWidget *widget,
		   PkApplication  *application)
{
	pk_debug ("emitting action-help");
	g_signal_emit (application, signals [ACTION_HELP], 0);
}

/**
 * pk_application_close_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_close_cb (GtkWidget	*widget,
		    PkApplication	*application)
{
	pk_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
}

/**
 * pk_application_find_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_find_cb (GtkWidget	*widget,
		        PkApplication	*application)
{
	g_error ("find");
}

/**
 * pk_application_delete_event_cb:
 * @widget: The GtkWidget object
 * @event: The event type, unused.
 * @graph: This graph class instance
 **/
static gboolean
pk_application_delete_event_cb (GtkWidget	*widget,
				GdkEvent	*event,
				PkApplication	*application)
{
	pk_application_close_cb (widget, application);
	return FALSE;
}

/**
 * pk_graph_widget_custom_handler:
 * @xml: The glade file we are reading.
 * @func_name: The function name to create the object
 *
 * Handler for libglade to provide interface with a pointer
 *
 * Return value: The custom widget.
 **/
static GtkWidget *
pk_graph_widget_custom_handler (GladeXML *xml,
			  gchar *func_name, gchar *name,
			  gchar *string1, gchar *string2,
			  gint int1, gint int2,
			  gpointer user_data)
{
	GtkWidget *widget = NULL;
	if (strcmp ("pk_graph_new", func_name) == 0) {
		widget = NULL;
		return widget;
	}
	return NULL;
}

#if 0
/**
 * pk_application_checkbox_events_cb:
 * @widget: The GtkWidget object
 **/
static void
pk_application_checkbox_events_cb (GtkWidget     *widget,
			           PkApplication *application)
{
	gboolean checked;
	checked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	pk_debug ("Events enable %i", checked);
}
#endif

/**
 * pk_application_init:
 * @graph: This graph class instance
 **/
static void
pk_application_init (PkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *widget;

	application->priv = PK_APPLICATION_GET_PRIVATE (application);

	glade_set_custom_handler (pk_graph_widget_custom_handler, application);

	application->priv->glade_xml = glade_xml_new (PK_DATA "/pk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
//	gtk_window_set_icon_name (GTK_WINDOW (main_window), PK_STOCK_APP_ICON);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_application_delete_event_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_close_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);

//	widget = glade_xml_get_widget (application->priv->glade_xml, "custom_graph");
//	gtk_widget_set_size_request (widget, 600, 300);

	/* FIXME: There's got to be a better way than this */
	gtk_widget_hide (GTK_WIDGET (widget));
	gtk_widget_show (GTK_WIDGET (widget));

//	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_type");
//	pk_application_populate_graph_types (application, widget);

//	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_events");
//	pk_conf_get_bool (application->priv->conf, PK_CONF_STATS_SHOW_EVENTS, &checked);
//	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), checked);
//	g_signal_connect (widget, "clicked",
//			  G_CALLBACK (pk_application_checkbox_events_cb), application);

	gtk_widget_show (main_window);
}

/**
 * pk_application_finalize:
 * @object: This graph class instance
 **/
static void
pk_application_finalize (GObject *object)
{
	PkApplication *application;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_APPLICATION (object));

	application = PK_APPLICATION (object);
	application->priv = PK_APPLICATION_GET_PRIVATE (application);
	G_OBJECT_CLASS (pk_application_parent_class)->finalize (object);
}

/**
 * pk_application_new:
 * Return value: new PkApplication instance.
 **/
PkApplication *
pk_application_new (void)
{
	PkApplication *application;
	application = g_object_new (PK_TYPE_APPLICATION, NULL);
	return PK_APPLICATION (application);
}

