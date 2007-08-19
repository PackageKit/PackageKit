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
#include "pk-task-client.h"

static void     pk_application_class_init (PkApplicationClass *klass);
static void     pk_application_init       (PkApplication      *application);
static void     pk_application_finalize   (GObject	    *object);

#define PK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_APPLICATION, PkApplicationPrivate))

struct PkApplicationPrivate
{
	GladeXML		*glade_xml;
	GtkListStore		*store;
	PkTaskClient		*tclient;
	gchar			*package;
	gboolean		 task_ended;
	gboolean		 find_installed;
	gboolean		 find_available;
	guint			 search_depth;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

enum
{
	COLUMN_INSTALLED,
	COLUMN_PACKAGE,
	COLUMN_DESCRIPTION,
	NUM_COLUMNS
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
 * pk_application_install_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_install_cb (GtkWidget      *widget,
		           PkApplication  *application)
{
	pk_debug ("install %s", application->priv->package);
	pk_task_client_install_package (application->priv->tclient,
					application->priv->package);
}

/**
 * pk_application_remove_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_remove_cb (GtkWidget      *widget,
		          PkApplication  *application)
{
	pk_debug ("remove %s", application->priv->package);
	pk_task_client_remove_package (application->priv->tclient,
				       application->priv->package);
}

/**
 * pk_application_deps_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_deps_cb (GtkWidget *widget,
		   PkApplication  *application)
{
	pk_debug ("deps %s", application->priv->package);
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
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkTaskClient *tclient, guint value, const gchar *package, const gchar *summary, PkApplication *application)
{
	GtkTreeIter iter;
	pk_debug ("package = %i:%s:%s", value, package, summary);
	gtk_list_store_append (application->priv->store, &iter);
	gtk_list_store_set (application->priv->store, &iter,
			    COLUMN_INSTALLED, value,
			    COLUMN_PACKAGE, package,
			    COLUMN_DESCRIPTION, summary,
			    -1);
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkTaskClient *tclient, PkTaskErrorCode code, const gchar *details, PkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *dialog;

	pk_warning ("error %i:%s", code, details);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					 "%s", pk_task_error_code_to_localised_text (code));
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), details);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkTaskClient *tclient, PkTaskStatus status, PkApplication *application)
{
	GtkWidget *widget;

	application->priv->task_ended = TRUE;

	/* hide widget */
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_status");
	gtk_widget_hide (widget);

	/* make find button sensitive again */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, TRUE);

	/* reset tclient */
	pk_task_client_reset (application->priv->tclient);
}

/**
 * pk_console_percentage_changed_cb:
 **/
static void
pk_console_percentage_changed_cb (PkTaskClient *tclient, guint percentage, PkApplication *application)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_status");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
}

/**
 * pk_console_no_percentage_updates_timeout:
 **/
gboolean
pk_console_no_percentage_updates_timeout (gpointer data)
{
	gfloat fraction;
	GtkWidget *widget;
	PkApplication *application = (PkApplication *) data;

	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_status");
	fraction = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (widget));
	fraction += 0.05;
	if (fraction > 1.00) {
		fraction = 0.0;
	}
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), fraction);
	if (application->priv->task_ended == TRUE) {
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_console_no_percentage_updates_cb:
 **/
static void
pk_console_no_percentage_updates_cb (PkTaskClient *tclient, PkApplication *application)
{
	g_timeout_add (100, pk_console_no_percentage_updates_timeout, application);
}

/**
 * pk_application_find_options_available_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_find_options_available_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_available = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("available %i", application->priv->find_available);
}

/**
 * pk_application_find_options_available_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_find_options_installed_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_installed = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("installed %i", application->priv->find_installed);
}

/**
 * pk_application_find_cb:
 * @widget: The GtkWidget object
 * @graph: This graph class instance
 **/
static void
pk_application_find_cb (GtkWidget	*button_widget,
		        PkApplication	*application)
{
	GtkWidget *widget;
	const gchar *package;

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear existing list */
	gtk_list_store_clear (application->priv->store);

	pk_debug ("find %s", package);
	application->priv->task_ended = FALSE;

	/* show widget */
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_status");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0);
	gtk_widget_show (widget);

	pk_task_client_find_packages (application->priv->tclient, package,
				      application->priv->search_depth,
				      application->priv->find_installed,
				      application->priv->find_available);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, FALSE);
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

static gboolean
pk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, PkApplication *application)
{
	GtkWidget *widget;
	const gchar *package;

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	if (package == NULL || strlen (package) == 0) {
		gtk_widget_set_sensitive (widget, FALSE);
	} else {
		gtk_widget_set_sensitive (widget, TRUE);
	}
	return FALSE;
}

static void
pk_misc_installed_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean installed;

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, COLUMN_INSTALLED, &installed, -1);

	/* do something with the value */
//	installed ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_INSTALLED, installed, -1);

	/* clean up */
	gtk_tree_path_free (path);
}

static void
pk_misc_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (pk_misc_installed_toggled), model);

	column = gtk_tree_view_column_new_with_attributes ("Installed", renderer,
							   "active", COLUMN_INSTALLED, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for severities */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
							   "text", COLUMN_PACKAGE, NULL);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_PACKAGE);
	gtk_tree_view_append_column (treeview, column);

	/* column for description */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Description", renderer,
							   "text", COLUMN_DESCRIPTION, NULL);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_DESCRIPTION);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_application_combobox_changed_cb:
 **/
static void
pk_application_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	application->priv->search_depth = gtk_combo_box_get_active (combobox);
	pk_debug ("search depth: %i", application->priv->search_depth);
}

/**
 * pk_application_treeview_clicked_cb:
 **/
static void
pk_application_treeview_clicked_cb (GtkTreeSelection *selection,
				    PkApplication *application)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean installed;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->package);
		gtk_tree_model_get (model, &iter, COLUMN_PACKAGE, &application->priv->package, -1);
		gtk_tree_model_get (model, &iter, COLUMN_INSTALLED, &installed, -1);
		g_print ("selected row is: %i %s\n", installed, application->priv->package);

		/* make the button sensitivities correct */
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, TRUE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
		gtk_widget_set_sensitive (widget, !installed);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
		gtk_widget_set_sensitive (widget, installed);
	} else {
		g_print ("no row selected.\n");
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
		gtk_widget_set_sensitive (widget, FALSE);
	}
}

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
	application->priv->package = NULL;
	application->priv->task_ended = FALSE;
	application->priv->find_installed = TRUE;
	application->priv->find_available = TRUE;
	application->priv->search_depth = 0;

	application->priv->tclient = pk_task_client_new ();
	g_signal_connect (application->priv->tclient, "package",
			  G_CALLBACK (pk_console_package_cb), application);
	g_signal_connect (application->priv->tclient, "error-code",
			  G_CALLBACK (pk_console_error_code_cb), application);
	g_signal_connect (application->priv->tclient, "finished",
			  G_CALLBACK (pk_console_finished_cb), application);
	g_signal_connect (application->priv->tclient, "no-percentage-updates",
			  G_CALLBACK (pk_console_no_percentage_updates_cb), application);
	g_signal_connect (application->priv->tclient, "percentage-changed",
			  G_CALLBACK (pk_console_percentage_changed_cb), application);

	application->priv->glade_xml = glade_xml_new (PK_DATA "/pk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "software-update-available");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_application_delete_event_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_close_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_install_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_remove_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_deps_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_status");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_depth");
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_installed");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_installed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_available");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_available_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (pk_application_text_changed_cb), application);
	g_signal_connect (widget, "key-release-event",
			  G_CALLBACK (pk_application_text_changed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, FALSE);

//	widget = glade_xml_get_widget (application->priv->glade_xml, "custom_graph");
//	gtk_widget_set_size_request (widget, 600, 300);

	/* FIXME: There's got to be a better way than this */
	gtk_widget_hide (GTK_WIDGET (widget));
	gtk_widget_show (GTK_WIDGET (widget));

	gtk_widget_set_size_request (main_window, 700, 300);
	gtk_widget_show (main_window);

	/* create list store */
	application->priv->store = gtk_list_store_new (NUM_COLUMNS,
						       G_TYPE_BOOLEAN,
						       G_TYPE_STRING,
						       G_TYPE_STRING);

	/* create tree view */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
//	g_signal_connect (widget, "select-cursor-row",
//			  G_CALLBACK (pk_application_treeview_clicked_cb), application);
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->store));

	GtkTreeSelection *selection;
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_application_treeview_clicked_cb), application);


	/* add columns to the tree view */
	pk_misc_add_columns (GTK_TREE_VIEW (widget));
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

	g_object_unref (application->priv->store);
	g_object_unref (application->priv->tclient);
	g_free (application->priv->package);

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

