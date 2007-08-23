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
#include <libnotify/notify.h>
#include <gtk/gtkstatusicon.h>

#include <pk-debug.h>
#include <pk-job-list.h>
#include <pk-task-client.h>
#include <pk-task-common.h>
#include <pk-task-list.h>
#include <pk-connection.h>

#include "pk-notify.h"

static void     pk_notify_class_init	(PkNotifyClass *klass);
static void     pk_notify_init		(PkNotify      *notify);
static void     pk_notify_finalize	(GObject       *object);

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))

#define PK_NOTIFY_ICON_STOCK	"system-installer"

#define PK_NOTIFY_DELAY_REFRESH_CACHE_STARTUP	5	/* time till the first refresh */
#define PK_NOTIFY_DELAY_REFRESH_CACHE_CHECK	60	/* if we failed the first refresh, check after this much time */
#define PK_NOTIFY_DELAY_REFRESH_CACHE_PERIODIC	2*60*60	/* check for updates every this much time */

struct PkNotifyPrivate
{
	GtkStatusIcon		*status_icon;
	GtkStatusIcon		*update_icon;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
	gboolean		 cache_okay;
	gboolean		 cache_update_in_progress;
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
	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

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
	const gchar *localised_status;

	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

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
		localised_status = pk_task_status_to_localised_text (item->status);
		if (item->package == NULL || strlen (item->package) == 0) {
			g_string_append_printf (status, "%s\n", localised_status);
		} else {
			g_string_append_printf (status, "%s: %s\n", localised_status, item->package);
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
	const gchar *icon = PK_NOTIFY_ICON_STOCK;

	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

	array = pk_task_list_get_latest	(notify->priv->tlist);

	length = array->len;
	if (length == 0) {
		pk_debug ("no activity");
		pk_notify_set_icon (notify, PK_NOTIFY_ICON_STOCK);
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
pk_notify_task_list_changed_cb (PkTaskList *tlist, PkNotify *notify)
{
	pk_notify_refresh_icon (notify);
	pk_notify_refresh_tooltip (notify);
}

/**
 * pk_notify_task_list_finished_cb:
 **/
static void
pk_notify_task_list_finished_cb (PkTaskList *tlist, PkTaskStatus status, const gchar *package, PkNotify *notify)
{
	NotifyNotification *dialog;
	const gchar *title;
	gchar *message = NULL;

	pk_debug ("status=%i, package=%s", status, package);

	if (status == PK_TASK_STATUS_REMOVE) {
		message = g_strdup_printf ("Package '%s' has been removed", package);
	} else if (status == PK_TASK_STATUS_INSTALL) {
		message = g_strdup_printf ("Package '%s' has been installed", package);
	} else if (status == PK_TASK_STATUS_UPDATE) {
		message = g_strdup ("System has been updated");
	}

	/* nothing of interest */
	if (message == NULL) {
		return;
	}
	title = "Task completed";
	dialog = notify_notification_new_with_status_icon (title, message, "help-browser",
							   notify->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
	g_free (message);
}

/**
 * pk_notify_show_help_cb:
 **/
static void
pk_notify_show_help_cb (GtkMenuItem *item, PkNotify *notify)
{
	NotifyNotification *dialog;
	const gchar *title;
	const gchar *message;

	pk_debug ("show help");
	title = "Functionality incomplete";
	message = "No help yet, sorry...";
	dialog = notify_notification_new_with_status_icon (title, message, "help-browser",
							   notify->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
}

/**
 * pk_notify_show_preferences_cb:
 **/
static void
pk_notify_show_preferences_cb (GtkMenuItem *item, PkNotify *notify)
{
	NotifyNotification *dialog;
	const gchar *title;
	const gchar *message;

	pk_debug ("show preferences");
	title = "Functionality incomplete";
	message = "No preferences yet, sorry...";
	dialog = notify_notification_new_with_status_icon (title, message, "help-browser",
							   notify->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
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

static gboolean pk_notify_check_for_updates_cb (PkNotify *notify);
static void pk_notify_refresh_cache_finished_cb (PkTaskClient *tclient, PkTaskExit exit_code, PkNotify *notify);

/**
 * pk_notify_libnotify_reboot_now_cb:
 **/
static void
pk_notify_libnotify_reboot_now_cb (NotifyNotification *dialog, gchar *action, PkNotify *notify)
{
	pk_warning ("reboot now");
}

/**
 * pk_notify_update_system_finished_cb:
 **/
static void
pk_notify_update_system_finished_cb (PkTaskClient *tclient, PkTaskExit exit_code, PkNotify *notify)
{
	PkTaskRestart restart;
	restart = pk_task_client_get_require_restart (tclient);
	if (restart != PK_TASK_RESTART_NONE) {
		NotifyNotification *dialog;
		const gchar *message;

		pk_debug ("Doing requires-restart notification");
		message = pk_task_restart_to_localised_text (restart);
		dialog = notify_notification_new_with_status_icon ("The update has completed", message,
								   "software-update-available",
								   notify->priv->status_icon);
		notify_notification_set_timeout (dialog, 50000);
		notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
		notify_notification_add_action (dialog, "reboot-now", "Restart computer now",
						(NotifyActionCallback) pk_notify_libnotify_reboot_now_cb,
						notify, NULL);
		notify_notification_show (dialog, NULL);
	}
	pk_debug ("unref'ing %p", tclient);
	g_object_unref (tclient);
}

/**
 * pk_notify_not_supported:
 **/
static void
pk_notify_not_supported (PkNotify *notify, const gchar *title)
{
	NotifyNotification *dialog;
	const gchar *message;

	pk_debug ("not_supported");
	message = "The action could not be completed due to the backend refusing the command.\n"
	          "Possible causes are an incomplete backend or other critical error.";
	dialog = notify_notification_new_with_status_icon (title, message, "process-stop",
							   notify->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
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
			  G_CALLBACK (pk_notify_refresh_cache_finished_cb), notify);
	ret = pk_task_client_refresh_cache (tclient, TRUE);
	if (ret == FALSE) {
		g_object_unref (tclient);
		pk_warning ("failed to refresh cache");
		pk_notify_not_supported (notify, "Failed to refresh cache");
	}
}

/**
 * pk_notify_update_system:
 **/
static void
pk_notify_update_system (PkNotify *notify)
{
	gboolean ret;
	PkTaskClient *tclient;
	pk_debug ("install updates");

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "finished",
			  G_CALLBACK (pk_notify_update_system_finished_cb), notify);
	ret = pk_task_client_update_system (tclient);
	if (ret == FALSE) {
		g_object_unref (tclient);
		pk_warning ("failed to update system");
		pk_notify_not_supported (notify, "Failed to update system");
	}
}

/**
 * pk_notify_menuitem_update_system_cb:
 **/
static void
pk_notify_menuitem_update_system_cb (GtkMenuItem *item, gpointer data)
{
	PkNotify *notify = PK_NOTIFY (data);
	pk_notify_update_system (notify);
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
 * pk_notify_activate_status_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
pk_notify_activate_status_cb (GtkStatusIcon *status_icon,
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

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * pk_notify_activate_update_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
pk_notify_activate_update_cb (GtkStatusIcon *status_icon,
			      PkNotify   *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	pk_debug ("icon left clicked");

	/* update system */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Update system"));
	image = gtk_image_new_from_icon_name ("software-update-available", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_notify_menuitem_update_system_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkNotify *notify)
{
	pk_debug ("connected=%i", connected);
	if (connected == TRUE) {
		pk_notify_refresh_icon (notify);
		pk_notify_refresh_tooltip (notify);
	} else {
		pk_notify_set_icon (notify, NULL);
	}
}

/**
 * pk_notify_libnotify_update_system_cb:
 **/
static void
pk_notify_libnotify_update_system_cb (NotifyNotification *dialog, gchar *action, PkNotify *notify)
{
	pk_debug ("update something");
	pk_notify_update_system (notify);
}

/**
 * pk_notify_query_updates_finished_cb:
 **/
static void
pk_notify_critical_updates_warning (PkNotify *notify, const gchar *details, gboolean plural)
{
	NotifyNotification *dialog;
	const gchar *title;
	gchar *message;

	g_return_if_fail (notify != NULL);
	g_return_if_fail (PK_IS_NOTIFY (notify));

	if (plural == TRUE) {
		title = "Security Updates Available";
		message = g_strdup_printf ("The following important updates are available for your computer:\n\n%s", details);
	} else {
		title = "Security Update Available";
		message = g_strdup_printf ("The following important update is available for your computer:\n\n%s", details);
	}
	dialog = notify_notification_new_with_status_icon (title, message, "software-update-urgent",
							   notify->priv->status_icon);
	notify_notification_set_timeout (dialog, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (dialog, "update-system", "Update system now",
					(NotifyActionCallback) pk_notify_libnotify_update_system_cb,
					notify, NULL);
	notify_notification_add_action (dialog, "update-system", "Don't warn me again",
					(NotifyActionCallback) pk_notify_libnotify_update_system_cb,
					notify, NULL);
	notify_notification_show (dialog, NULL);
	g_free (message);
}

/**
 * pk_notify_query_updates_finished_cb:
 **/
static void
pk_notify_query_updates_finished_cb (PkTaskClient *tclient, PkTaskExit exit, PkNotify *notify)
{
	PkTaskClientPackageItem *item;
	GPtrArray *packages;
	guint length;
	guint i;
	gboolean is_security;
	const gchar *icon;
	GString *status_security;
	GString *status_tooltip;

	g_return_if_fail (notify != NULL);
	g_return_if_fail (PK_IS_NOTIFY (notify));

	status_security = g_string_new ("");
	status_tooltip = g_string_new ("");
	g_print ("exit: %i\n", exit);

	/* find packages */
	packages = pk_task_client_get_package_buffer (tclient);
	length = packages->len;
	pk_debug ("length=%i", length);
	if (length == 0) {
		pk_debug ("no updates");
		gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->update_icon), FALSE);
		return;
	}

	is_security = FALSE;
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (packages, i);
		pk_debug ("%i, %s, %s", item->value, item->package, item->summary);
		if (item->value == 1) {
			is_security = TRUE;
			g_string_append_printf (status_security, "<b>%s</b> - %s\n", item->package, item->summary);
			g_string_append_printf (status_tooltip, "%s - %s (Security)\n", item->package, item->summary);
		} else {
			g_string_append_printf (status_tooltip, "%s - %s\n", item->package, item->summary);
		}
	}
	g_object_unref (tclient);

	/* work out icon */
	if (is_security == TRUE) {
		icon = "software-update-urgent";
	} else {
		icon = "software-update-available";
	}

	/* trim off extra newlines */
	if (status_security->len != 0) {
		g_string_set_size (status_security, status_security->len-1);
	}
	if (status_tooltip->len != 0) {
		g_string_set_size (status_tooltip, status_tooltip->len-1);
	}

	/* make tooltip */
	if (status_tooltip->len != 0) {
		g_string_prepend (status_tooltip, "Updates:\n");
	}

	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (notify->priv->update_icon), icon);
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->update_icon), TRUE);
	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (notify->priv->update_icon), status_tooltip->str);

	/* do we warn the user? */
	if (is_security == TRUE) {
		pk_notify_critical_updates_warning (notify, status_security->str, (length > 1));
	}

	g_string_free (status_security, TRUE);
	g_string_free (status_tooltip, TRUE);
}

/**
 * pk_notify_query_updates:
 **/
static gboolean
pk_notify_query_updates (PkNotify *notify)
{
	PkTaskClient *tclient;

	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "finished",
			  G_CALLBACK (pk_notify_query_updates_finished_cb), notify);
	pk_task_client_set_use_buffer (tclient, TRUE);
	pk_task_client_get_updates (tclient);
	return TRUE;
}

/**
 * pk_notify_check_for_updates_cb:
 **/
static gboolean
pk_notify_invalidate_cache_cb (PkNotify *notify)
{
	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

	notify->priv->cache_okay = FALSE;
	g_timeout_add_seconds (5, (GSourceFunc) pk_notify_check_for_updates_cb, notify);
	return FALSE;
}

/**
 * pk_notify_refresh_cache_finished_cb:
 **/
static void
pk_notify_refresh_cache_finished_cb (PkTaskClient *tclient, PkTaskExit exit_code, PkNotify *notify)
{
	g_return_if_fail (notify != NULL);
	g_return_if_fail (PK_IS_NOTIFY (notify));

	pk_debug ("finished refreshing cache :%s", pk_task_exit_to_text (exit_code));
	if (exit_code != PK_TASK_EXIT_SUCCESS) {
		/* we failed to get the cache */
		notify->priv->cache_okay = FALSE;
	} else {
		/* stop the polling */
		notify->priv->cache_okay = TRUE;

		/* schedule the next cache reload in a few hours */
		g_timeout_add_seconds (PK_NOTIFY_DELAY_REFRESH_CACHE_PERIODIC,
				       (GSourceFunc) pk_notify_invalidate_cache_cb, notify);

		/* now try to get updates */
		pk_debug ("get updates");
		pk_notify_query_updates (notify);
	}
	notify->priv->cache_update_in_progress = FALSE;
}

/**
 * pk_notify_check_for_updates_cb:
 **/
static gboolean
pk_notify_check_for_updates_cb (PkNotify *notify)
{
	gboolean ret;
	PkTaskClient *tclient;
	pk_debug ("refresh cache");

	/* got a cache, no need to poll */
	if (notify->priv->cache_okay == TRUE) {
		return FALSE;
	}

	/* already in progress, but not yet certified okay */
	if (notify->priv->cache_update_in_progress == TRUE) {
		return TRUE;
	}

	notify->priv->cache_update_in_progress = TRUE;
	notify->priv->cache_okay = TRUE;
	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "finished",
			  G_CALLBACK (pk_notify_refresh_cache_finished_cb), notify);
	ret = pk_task_client_refresh_cache (tclient, TRUE);
	if (ret == FALSE) {
		g_object_unref (tclient);
		pk_warning ("failed to refresh cache");
		/* try again in a few minutes */
	}
	return TRUE;
}

/**
 * pk_notify_check_for_updates_early_cb:
 **/
static gboolean
pk_notify_check_for_updates_early_cb (PkNotify *notify)
{
	pk_notify_check_for_updates_cb (notify);
	/* we don't want to do this quick timer again */
	return FALSE;
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
	notify->priv->update_icon = gtk_status_icon_new ();
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), FALSE);
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->update_icon), FALSE);

	/* right click actions are common */
	g_signal_connect_object (G_OBJECT (notify->priv->status_icon),
				 "popup_menu",
				 G_CALLBACK (pk_notify_popup_menu_cb),
				 notify, 0);
	g_signal_connect_object (G_OBJECT (notify->priv->update_icon),
				 "popup_menu",
				 G_CALLBACK (pk_notify_popup_menu_cb),
				 notify, 0);
	g_signal_connect_object (G_OBJECT (notify->priv->status_icon),
				 "activate",
				 G_CALLBACK (pk_notify_activate_status_cb),
				 notify, 0);
	g_signal_connect_object (G_OBJECT (notify->priv->update_icon),
				 "activate",
				 G_CALLBACK (pk_notify_activate_update_cb),
				 notify, 0);

	notify_init ("packagekit-update-applet");
	notify->priv->tlist = pk_task_list_new ();
	g_signal_connect (notify->priv->tlist, "task-list-changed",
			  G_CALLBACK (pk_notify_task_list_changed_cb), notify);
	g_signal_connect (notify->priv->tlist, "task-list-finished",
			  G_CALLBACK (pk_notify_task_list_finished_cb), notify);

	notify->priv->pconnection = pk_connection_new ();
	g_signal_connect (notify->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), notify);
	if (pk_connection_valid (notify->priv->pconnection)) {
		pk_connection_changed_cb (notify->priv->pconnection, TRUE, notify);
	}

	/* refresh the cache, and poll until we get a good refresh */
	notify->priv->cache_okay = FALSE;
	notify->priv->cache_update_in_progress = FALSE;
	/* set up one quick (start of session) timer and one long (wait for changes timer) */
	g_timeout_add_seconds (PK_NOTIFY_DELAY_REFRESH_CACHE_STARTUP,
			       (GSourceFunc) pk_notify_check_for_updates_early_cb, notify);
	g_timeout_add_seconds (PK_NOTIFY_DELAY_REFRESH_CACHE_CHECK,
			       (GSourceFunc) pk_notify_check_for_updates_cb, notify);
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
	g_object_unref (notify->priv->update_icon);
	g_object_unref (notify->priv->tlist);
	g_object_unref (notify->priv->pconnection);

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

