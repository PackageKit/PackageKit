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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gtk/gtkstatusicon.h>

#include "pk-debug.h"
#include "pk-notify.h"
#include "pk-job-list.h"
#include "pk-task-client.h"
#include "pk-task-common.h"
#include "pk-task-list.h"

static void     pk_notify_class_init	(PkNotifyClass *klass);
static void     pk_notify_init		(PkNotify      *notify);
static void     pk_notify_finalize	(GObject       *object);

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))
#define PK_NOTIFY_NOTHING	"system-installer" /*NULL*/

struct PkNotifyPrivate
{
	GtkStatusIcon		*status_icon;
	PkTaskList		*tlist;
};

G_DEFINE_TYPE (PkNotify, pk_notify, G_TYPE_OBJECT)

/**
 * pk_notify_get_updates:
 **/
gboolean
pk_notify_get_updates (PkNotify *notify, guint *job, GError **error)
{
	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);
	return TRUE;
}

/**
 * pk_notify_class_init:
 * @klass: The PkNotifyClass
 **/
static void
pk_notify_class_init (PkNotifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_notify_finalize;

	g_type_class_add_private (klass, sizeof (PkNotifyPrivate));
}

/**
 * pk_notify_set_icon:
 **/
static gboolean
pk_notify_set_icon (PkNotify *notify, const gchar *icon)
{
	if (icon == NULL) {
		gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), FALSE);
		return FALSE;
	}
	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (notify->priv->status_icon), icon);
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), TRUE);
	return TRUE;
}

/**
 * pk_notify_refresh_tooltip:
 **/
static gboolean
pk_notify_refresh_tooltip (PkNotify *notify)
{
	guint i;
	PkTaskListItem *item;
	guint length;
	GPtrArray *array;
	GString *status;

	array = pk_task_list_get_latest	(notify->priv->tlist);

	length = array->len;
	pk_debug ("refresh tooltip %i", length);
	if (length == 0) {
		gtk_status_icon_set_tooltip (GTK_STATUS_ICON (notify->priv->status_icon), "Doing nothing...");
		return TRUE;
	}
	status = g_string_new ("");
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (array, i);
		if (item->package == NULL) {
			g_string_append_printf (status, "%s\n", pk_task_status_to_text (item->status));
		} else {
			g_string_append_printf (status, "%s: %s\n", pk_task_status_to_localised_text (item->status), item->package);
		}
	}
	if (status->len == 0) {
		g_string_append (status, "Doing something...");
	} else {
		g_string_set_size (status, status->len-1);
	}
	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (notify->priv->status_icon), status->str);
	g_string_free (status, TRUE);
	return TRUE;
}

/**
 * pk_notify_refresh_icon:
 **/
static gboolean
pk_notify_refresh_icon (PkNotify *notify)
{
	pk_debug ("rescan");
	guint i;
	PkTaskListItem *item;
	PkTaskStatus state;
	guint length;
	GPtrArray *array;
	gboolean state_install = FALSE;
	gboolean state_remove = FALSE;
	gboolean state_setup = FALSE;
	gboolean state_update = FALSE;
	gboolean state_download = FALSE;
	gboolean state_query = FALSE;
	gboolean state_refresh_cache = FALSE;
	const gchar *icon = PK_NOTIFY_NOTHING;

	array = pk_task_list_get_latest	(notify->priv->tlist);

	length = array->len;
	if (length == 0) {
		pk_debug ("no activity");
		pk_notify_set_icon (notify, PK_NOTIFY_NOTHING);
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (array, i);
		state = item->status;
		pk_debug ("%i %s", item->job, pk_task_status_to_text (state));
		if (state == PK_TASK_STATUS_SETUP) {
			state_setup = TRUE;
		} else if (state == PK_TASK_STATUS_REFRESH_CACHE) {
			state_refresh_cache = TRUE;
		} else if (state == PK_TASK_STATUS_QUERY) {
			state_query = TRUE;
		} else if (state == PK_TASK_STATUS_REMOVE) {
			state_remove = TRUE;
		} else if (state == PK_TASK_STATUS_DOWNLOAD) {
			state_download = TRUE;
		} else if (state == PK_TASK_STATUS_INSTALL) {
			state_install = TRUE;
		} else if (state == PK_TASK_STATUS_UPDATE) {
			state_update = TRUE;
		}
	}
	/* in order of priority */
	if (state_refresh_cache == TRUE) {
		icon = "view-refresh";
	} else if (state_install == TRUE) {
		icon = "emblem-system";
	} else if (state_remove == TRUE) {
		icon = "edit-clear";
	} else if (state_setup == TRUE) {
		icon = "emblem-system";
	} else if (state_update == TRUE) {
		icon = "system-software-update";
	} else if (state_download == TRUE) {
		icon = "mail-send-receive";
	} else if (state_query == TRUE) {
		icon = "system-search";
	}
	pk_notify_set_icon (notify, icon);

	return TRUE;
}

/**
 * pk_notify_task_list_changed_cb:
 **/
static void
pk_notify_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	PkNotify *notify = (PkNotify *) data;
	pk_notify_refresh_icon (notify);
	pk_notify_refresh_tooltip (notify);
}


/**
 * pk_notify_show_help_cb:
 **/
static void
pk_notify_show_help_cb (GtkMenuItem *item, gpointer data)
{
	pk_debug ("show help");
}

/**
 * pk_notify_show_preferences_cb:
 **/
static void
pk_notify_show_preferences_cb (GtkMenuItem *item, gpointer data)
{
	pk_debug ("show preferences");
}

/**
 * pk_notify_show_about_cb:
 **/
static void
pk_notify_show_about_cb (GtkMenuItem *item, gpointer data)
{
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *artists[] = {
		NULL};
	const char *license[] = {
		N_("Licensed under the GNU General Public License Version 2"),
		N_("PackageKit is free software; you can redistribute it and/or\n"
		   "modify it under the terms of the GNU General Public License\n"
		   "as published by the Free Software Foundation; either version 2\n"
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License\n"
		   "along with this program; if not, write to the Free Software\n"
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA\n"
		   "02110-1301, USA.")
	};
  	const char  *translators = _("translator-credits");
	char	    *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
  	if (!strcmp (translators, "translator-credits")) {
		translators = NULL;
	}

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	gtk_window_set_default_icon_name ("system-installer");
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007 Richard Hughes",
			       "license", license_trans,
			       "website-label", _("PackageKit Website"),
			       "website", "www.hughsie.com",
			       "comments", "PackageKit",
			       "authors", authors,
			       "documenters", documenters,
			       "artists", artists,
			       "translator-credits", translators,
			       "logo-icon-name", "system-installer",
			       NULL);
	g_free (license_trans);
}

/**
 * pk_notify_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
pk_notify_popup_menu_cb (GtkStatusIcon *status_icon,
			     guint          button,
			     guint32        timestamp,
			     PkNotify   *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	pk_debug ("icon right clicked");

	/* Preferences */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Preferences"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_show_preferences_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* Separator for HIG? */
	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* Help */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_show_help_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* About */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_show_about_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			button, timestamp);
	if (button == 0) {
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
	}
}

/**
 * pk_notify_finished_cb:
 **/
static void
pk_notify_finished_cb (PkTaskClient *tclient, PkTaskExit exit_code, gpointer data)
{
	pk_debug ("unref'ing %p", tclient);
	g_object_unref (tclient);
}

/**
 * pk_notify_refresh_cache_cb:
 **/
static void
pk_notify_refresh_cache_cb (GtkMenuItem *item, gpointer data)
{
	gboolean ret;
	PkTaskClient *tclient;
	PkNotify *notify = PK_NOTIFY (data);
	pk_debug ("refresh cache");

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "finished",
			  G_CALLBACK (pk_notify_finished_cb), notify);
	ret = pk_task_client_refresh_cache (tclient);
	if (ret == FALSE) {
		g_object_unref (tclient);
		pk_warning ("failed to refresh cache");
	}
}

/**
 * pk_notify_update_system_cb:
 **/
static void
pk_notify_update_system_cb (GtkMenuItem *item, gpointer data)
{
	gboolean ret;
	PkTaskClient *tclient;
	PkNotify *notify = PK_NOTIFY (data);
	pk_debug ("install updates");

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "finished",
			  G_CALLBACK (pk_notify_finished_cb), notify);
	ret = pk_task_client_update_system (tclient);
	if (ret == FALSE) {
		g_object_unref (tclient);
		pk_warning ("failed to update system");
	}
}

/**
 * pk_notify_manage_packages_cb:
 **/
static void
pk_notify_manage_packages_cb (GtkMenuItem *item, gpointer data)
{
	const gchar *command = "pk-application";
	if (g_spawn_command_line_async (command, NULL) == FALSE) {
		pk_warning ("Couldn't execute command: %s", command);
	}
}

/**
 * pk_notify_activate_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
pk_notify_activate_cb (GtkStatusIcon *status_icon,
			   PkNotify   *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	pk_debug ("icon left clicked");

	/* force a refresh */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Refresh cache"));
	image = gtk_image_new_from_icon_name ("view-refresh", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_refresh_cache_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* manage packages */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Manage packages"));
	image = gtk_image_new_from_icon_name ("system-installer", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_manage_packages_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* update system */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Update system"));
	image = gtk_image_new_from_icon_name ("software-update-available", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_update_system_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * pk_notify_init:
 * @notify: This class instance
 **/
static void
pk_notify_init (PkNotify *notify)
{
	notify->priv = PK_NOTIFY_GET_PRIVATE (notify);

	notify->priv->status_icon = gtk_status_icon_new ();
	g_signal_connect_object (G_OBJECT (notify->priv->status_icon),
				 "popup_menu",
				 G_CALLBACK (pk_notify_popup_menu_cb),
				 notify, 0);
	g_signal_connect_object (G_OBJECT (notify->priv->status_icon),
				 "activate",
				 G_CALLBACK (pk_notify_activate_cb),
				 notify, 0);

	notify->priv->tlist = pk_task_list_new ();
	g_signal_connect (notify->priv->tlist, "task-list-changed",
			  G_CALLBACK (pk_notify_task_list_changed_cb), notify);
	pk_notify_refresh_icon (notify);
	pk_notify_refresh_tooltip (notify);
}

/**
 * pk_notify_finalize:
 * @object: The object to finalize
 **/
static void
pk_notify_finalize (GObject *object)
{
	PkNotify *notify;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NOTIFY (object));

	notify = PK_NOTIFY (object);

	g_return_if_fail (notify->priv != NULL);
	g_object_unref (notify->priv->status_icon);
	g_object_unref (notify->priv->tlist);

	G_OBJECT_CLASS (pk_notify_parent_class)->finalize (object);
}

/**
 * pk_notify_new:
 *
 * Return value: a new PkNotify object.
 **/
PkNotify *
pk_notify_new (void)
{
	PkNotify *notify;
	notify = g_object_new (PK_TYPE_NOTIFY, NULL);
	return PK_NOTIFY (notify);
}

