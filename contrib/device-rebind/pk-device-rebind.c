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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gudev/gudev.h>
#include <locale.h>

#define PK_DEVICE_REBIND_EXIT_CODE_SUCCESS			0
#define PK_DEVICE_REBIND_EXIT_CODE_FAILED			1
#define PK_DEVICE_REBIND_EXIT_CODE_ARGUMENTS_INVALID		3
#define PK_DEVICE_REBIND_EXIT_CODE_FAILED_TO_WRITE		4

/* globals */
static gboolean simulate = FALSE;
static gboolean verbose = FALSE;

/**
 * pk_device_rebind_set_contents:
 *
 * This is a UNIXy version of g_file_set_contents as this is a device file
 * and we can't play games with temporary files.
 **/
static gboolean
pk_device_rebind_set_contents (const gchar *filename, const gchar *contents, GError **error)
{
	gboolean ret = FALSE;
	guint len;
	guint wrote;
	gint fd;

	/* just write to file */
	fd = open (filename, O_WRONLY | O_SYNC);
	if (fd < 0) {
		/* TRANSLATORS: couldn't open device to write */
		*error = g_error_new (1, 0, "%s: %s [%s]", _("Failed to open file"), filename, strerror (errno));
		goto out;
	}

	/* just write to file */
	len = strlen (contents);
	wrote = write (fd, contents, len);
	if (wrote != len) {
		/* TRANSLATORS: could not write to the device */
		*error = g_error_new (1, 0, "%s: %s [%s]", _("Failed to write to the file"), filename, strerror (errno));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	if (fd >= 0)
		close (fd);
	return ret;
}

/**
 * pk_device_unbind:
 *
 * echo -n "5-2" > /sys/devices/pci0000\:00/0000\:00\:1d.0/usb5/5-2/driver/unbind
 *           ^- busnum-devnum = "bus id"
 **/
static gboolean
pk_device_unbind (const gchar *filename, const gchar *bus_id, GError **error)
{
	gchar *path;
	gboolean ret = TRUE;
	GError *error_local = NULL;

	/* create a path to unbind the driver */
	path = g_build_filename (filename, "driver", "unbind", NULL);

	/* debug */
	if (verbose)
		g_debug ("UNBIND: %s > %s", bus_id, path);

	/* don't actually write */
	if (simulate)
		goto out;

	/* write to file */
	ret = pk_device_rebind_set_contents (path, bus_id, &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to release the current driver */
		*error = g_error_new (1, 0, "%s: %s", _("Failed to write to device"), error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (path);
	return ret;
}

/**
 * pk_device_bind:
 *
 * echo -n "5-2" > /sys/bus/usb/drivers/usb/bind
 *                          ^-bus       ^-driver
 **/
static gboolean
pk_device_bind (const gchar *bus_id, const gchar *subsystem, const gchar *driver, GError **error)
{
	gchar *path;
	gboolean ret = TRUE;
	GError *error_local = NULL;

	/* create a path to bind the driver */
	path = g_build_filename ("/sys", "bus", subsystem, "drivers", driver, "bind", NULL);

	/* debug */
	if (verbose)
		g_debug ("BIND: %s > %s", bus_id, path);

	/* don't actually write */
	if (simulate)
		goto out;

	/* write to file */
	ret = pk_device_rebind_set_contents (path, bus_id, &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to release the current driver */
		*error = g_error_new (1, 0, "%s: %s", _("Failed to write to device"), error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (path);
	return ret;
}

/**
 * pk_device_rebind:
 **/
static gboolean
pk_device_rebind (GUdevClient *client, const gchar *path, GError **error)
{
	GUdevDevice *device;
	gint busnum;
	gint devnum;
	gboolean ret = FALSE;
	const gchar *driver;
	const gchar *subsystem;
	gchar *bus_id = NULL;
	GError *error_local = NULL;

	/* get device */
	device = g_udev_client_query_by_sysfs_path (client, path);
	if (device == NULL) {
		/* TRANSLATORS: the device could not be found in sysfs */
		*error = g_error_new (1, 0, "%s: %s\n", _("Device could not be found"), path);
		goto out;
	}

	/* get properties about the device */
	driver = g_udev_device_get_driver (device);
	subsystem = g_udev_device_get_subsystem (device);
	busnum = g_udev_device_get_sysfs_attr_as_int (device, "busnum");
	devnum = g_udev_device_get_sysfs_attr_as_int (device, "devnum");

	/* debug */
	if (verbose) {
		g_debug ("DEVICE: driver:%s, subsystem:%s, busnum:%i, devnum:%i",
			 driver, subsystem, busnum, devnum);
	}

	/* form the bus id as recognised by the kernel */
	bus_id = g_path_get_basename (path);

	/* FIXME: sometimes the busnum is incorrect */
	if (bus_id == NULL)
		bus_id = g_strdup_printf ("%i-%i", busnum, devnum);

	/* unbind device */
	ret = pk_device_unbind (path, bus_id, &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to release the current driver */
		*error = g_error_new (1, 0, "%s: %s", _("Failed to unregister driver"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* bind device */
	ret = pk_device_bind (bus_id, subsystem, driver, &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to bind the old driver */
		*error = g_error_new (1, 0, "%s: %s", _("Failed to register driver"), error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (bus_id);
	if (device != NULL)
		g_object_unref (device);
	return ret;
}

/**
 * pk_device_rebind_verify_path:
 **/
static gboolean
pk_device_rebind_verify_path (const gchar *filename)
{
	gboolean ret;

	/* don't let the user escape /sys */
	ret = (strstr (filename, "..") == NULL);
	if (!ret)
		goto out;

	/* don't let the user use quoting */
	ret = (strstr (filename, "\\") == NULL);
	if (!ret)
		goto out;

	/* linux specific */
	ret = g_str_has_prefix (filename, "/sys/");
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_device_rebind_verify:
 **/
static gboolean
pk_device_rebind_verify (const gchar *filename, GError **error)
{
	gboolean ret;

	/* does directory exist */
	ret = g_file_test (filename, G_FILE_TEST_IS_DIR);
	if (!ret) {
		/* TRANSLATORS: user did not specify a device sysfs path that exists */
		*error = g_error_new (1, 0, "%s: %s\n", _("Device path not found"), filename);
		goto out;
	}

	/* don't let the user escape /sys */
	ret = pk_device_rebind_verify_path (filename);
	if (!ret) {
		/* TRANSLATORS: user did not specify a valid device sysfs path */
		*error = g_error_new (1, 0, "%s: %s\n", _("Incorrect device path specified"), filename);
		goto out;
	}
out:
	return ret;
}

/**
 * main:
 **/
gint
main (gint argc, gchar *argv[])
{
	gboolean ret;
	gchar **paths = NULL;
	GError *error = NULL;
	GOptionContext *context;
	guint i;
	gint uid;
	gint euid;
	guint retval = 0;
	GUdevClient *client = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "simulate", 's', 0, G_OPTION_ARG_NONE, &simulate,
		   /* command line argument, simulate what would be done, but don't actually do it */
		  _("Don't actually touch the hardware, only simulate what would be done"), NULL },
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &paths,
		  /* TRANSLATORS: command line option: a list of files to install */
		  _("Device paths"), NULL },
		{ NULL}
	};

	/* setup translations */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* setup type system */
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the device needs reloading after installing firmware */
	g_option_context_set_summary (context, _("PackageKit Device Reloader"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* no input */
	if (paths == NULL) {
		/* TRANSLATORS: user did not specify a valid device sysfs path */
		g_print ("%s\n", _("You need to specify at least one valid device path"));
		retval = PK_DEVICE_REBIND_EXIT_CODE_ARGUMENTS_INVALID;
		goto out;
	}

	/* get calling process */
	uid = getuid ();
	euid = geteuid ();
	if (uid != 0 || euid != 0) {
		/* TRANSLATORS: user did not specify a valid device sysfs path */
		g_print ("%s\n", _("This script can only be used by the root user"));
		retval = PK_DEVICE_REBIND_EXIT_CODE_ARGUMENTS_INVALID;
		goto out;
	}

	/* we're running as root, be paranoid and check them for sanity */
	for (i=0; paths[i] != NULL; i++) {
		if (verbose) {
			/* TRANSLATORS: we're going to verify the path first */
			g_print ("%s: %s\n", _("Verifying device path"), paths[i]);
		}
		ret = pk_device_rebind_verify (paths[i], &error);
		if (!ret) {
			/* TRANSLATORS: user did not specify a device sysfs path that exists */
			g_print ("%s: %s\n", _("Failed to verify device path"), error->message);
			g_error_free (error);
			retval = PK_DEVICE_REBIND_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}
	}

	/* use GUdev to find properties */
	client = g_udev_client_new (NULL);

	/* unbind and then bind all the devices */
	for (i=0; paths[i] != NULL; i++) {
		if (verbose) {
			/* TRANSLATORS: we're going to try */
			g_print ("%s: %s\n", _("Attempting to rebind device"), paths[i]);
		}
		ret = pk_device_rebind (client, paths[i], &error);
		if (!ret) {
			/* TRANSLATORS: we failed to release the current driver */
			g_print ("%s: %s\n", _("Failed to rebind device"), error->message);
			g_error_free (error);
			retval = PK_DEVICE_REBIND_EXIT_CODE_FAILED_TO_WRITE;
			goto out;
		}
	}

	/* success */
	retval = PK_DEVICE_REBIND_EXIT_CODE_SUCCESS;
out:
	if (client != NULL)
		g_object_unref (client);
	g_strfreev (paths);
	return retval;
}

