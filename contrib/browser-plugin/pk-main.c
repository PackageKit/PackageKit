/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
#include <glib/gprintf.h>
#include <glib/gi18n-lib.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <npapi.h>
#include <npfunctions.h>
#include <npruntime.h>

#define __USE_GNU
#include <dlfcn.h>

#include "pk-main.h"
#include "pk-plugin.h"
#include "pk-plugin-install.h"

static NPNetscapeFuncs *npnfuncs = NULL;
static void *module_handle = NULL;

#ifndef HIBYTE
#define HIBYTE(x) ((((uint32_t)(x)) & 0xff00) >> 8)
#endif

static void pk_main_draw_window (PkPlugin *plugin);
static void pk_main_event_handler (PkPlugin *plugin, XEvent *event);

/**
 * pk_debug_real:
 **/
void
pk_debug_real (const gchar *func, const gchar *file, const int line, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;

	if (g_getenv ("PK_DEBUG") == NULL)
		return;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	g_print ("FN:%s FC:%s LN:%i\n\t%s\n", file, func, line, buffer);

	g_free (buffer);
}

/**
 * pk_warning_real:
 **/
void
pk_warning_real (const gchar *func, const gchar *file, const int line, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	g_print ("FN:%s FC:%s LN:%i\n!!\t%s\n", file, func, line, buffer);

	g_free (buffer);
}

/**
 * pk_main_refresh_cb:
 **/
static void
pk_main_refresh_cb (PkPlugin *plugin_, NPP instance)
{
	pk_debug ("pk_main_refresh_cb [%p]", instance);

	/* invalid */
	if (plugin_ == NULL) {
		pk_warning ("NULL plugin");
		return;
	}

	pk_main_draw_window (plugin_);
}

/**
 * pk_main_get_value:
 **/
static NPError
pk_main_get_value (NPP instance, NPPVariable variable, void *value)
{
	NPError err = NPERR_NO_ERROR;
	switch (variable) {
	case NPPVpluginNameString:
		* ((const gchar **)value) = "PackageKit";
		break;
	case NPPVpluginDescriptionString:
		* ((const gchar **)value) = "Plugin for Installing Applications (new)";
		break;
	default:
		err = NPERR_INVALID_PARAM;
	}
	return err;
}

/**
 * pk_main_newp:
 **/
static NPError
pk_main_newp (NPMIMEType pluginType, NPP instance, uint16_t mode, int16_t argc, char *argn[], char *argv[], NPSavedData *saved)
{
	gint i;
	PkPlugin *plugin;

	pk_debug ("new [%p]", instance);

	/* create new content instance */
	plugin = PK_PLUGIN (pk_plugin_install_new ());
	g_signal_connect (plugin, "refresh", G_CALLBACK (pk_main_refresh_cb), instance);

	/* set data */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (argn[i], "displayname") == 0 ||
		    g_strcmp0 (argn[i], "packagenames") == 0 ||
                    g_strcmp0 (argn[i], "radius") == 0 ||
                    g_strcmp0 (argn[i], "color") == 0)
			pk_plugin_set_data (plugin, argn[i], argv[i]);
	}

	/* add to list */
	instance->pdata = plugin;

	return NPERR_NO_ERROR;
}

/**
 * pk_main_destroy:
 **/
static NPError
pk_main_destroy (NPP instance, NPSavedData **save)
{
	PkPlugin *plugin = PK_PLUGIN (instance->pdata);

	pk_debug ("pk_main_destroy [%p]", instance);

	/* free content instance */
	g_signal_handlers_disconnect_by_func (plugin, G_CALLBACK (pk_main_refresh_cb), instance);
	g_object_unref (plugin);

	return NPERR_NO_ERROR;
}

/**
 * pk_main_plugin_x11_filter_event:
 **/
static GdkFilterReturn
pk_main_plugin_x11_filter_event (GdkXEvent *gdkxevent, GdkEvent *unused, gpointer plugin)
{
	pk_main_event_handler (plugin, gdkxevent);
	return GDK_FILTER_REMOVE;
}

/**
 * pk_main_create_window:
 **/
static void
pk_main_create_window (PkPlugin *plugin)
{
	gint width;
	gint height;
	Window  xwindow;
	Display *xdisplay;
	GdkWindow *gdk_window;

	/* get parameters */
	g_object_get (plugin,
		      "width", &width,
		      "height", &height,
		      "display", &xdisplay,
		      "window", &xwindow,
		      "gdk-window", &gdk_window,
		      NULL);


	if (gdk_window == NULL) {
		GdkWindowAttr attr;
		GdkWindow *parent;
		GdkDisplay *gdk_display;

		// TODO - is it correct? Do we want to translate xdisplay -> GdkDisplay?
		gdk_display = gdk_display_get_default ();
		if (gdk_display == NULL) {
			pk_debug ("invalid display returned by gdk_display_get_default ()\n");
			return;
		}

		/* get parent */
		parent = gdk_window_foreign_new_for_display (gdk_display, xwindow);
		if (parent == NULL) {
			pk_debug ("invalid window given for setup (id %lu)\n", xwindow);
			return;
		}

		attr.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK |
				  GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK |
				  GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
				  GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_KEY_PRESS_MASK |
				  GDK_KEY_RELEASE_MASK;
		attr.x = 0;
		attr.y = 0;
		attr.width = width;
		attr.height = height;
		attr.window_type = GDK_WINDOW_CHILD;
		attr.wclass = GDK_INPUT_OUTPUT;
		gdk_window = gdk_window_new (parent, &attr, GDK_WA_X | GDK_WA_Y);
		gdk_window_add_filter (gdk_window, pk_main_plugin_x11_filter_event, plugin);

		/* show window */
		gdk_window_show (gdk_window);
		g_object_set (plugin, "gdk-window", gdk_window, NULL);

	} else {
		gdk_window_move_resize (gdk_window, 0, 0, width, height);
	}
}

/**
 * pk_main_delete_window:
 **/
static void
pk_main_delete_window (PkPlugin *plugin)
{
	GdkWindow *gdk_window;

	/* get drawing window */
	g_object_get (plugin, "gdk-window", &gdk_window, NULL);

	if (gdk_window)
		gdk_window_remove_filter (gdk_window, pk_main_plugin_x11_filter_event, plugin);

	/* Clear parameters */
	g_object_set (plugin,
		      "x", 0,
		      "y", 0,
		      "width", 0,
		      "height", 0,
		      "display", NULL,
		      "visual", NULL,
		      "window", 0,
		      "gdk-window", NULL,
		      NULL);
}

/**
 * pk_main_draw_window:
 **/
static void
pk_main_draw_window (PkPlugin *plugin)
{
	cairo_t *cr;
	GdkWindow *gdk_window;

	/* get drawing window */
	g_object_get (plugin, "gdk-window", &gdk_window, NULL);

	if (gdk_window == NULL) {
		pk_debug ("gdk_window is NULL!");
		return;
	}

	cr = gdk_cairo_create (gdk_window);
	pk_plugin_draw (plugin, cr);
	cairo_destroy (cr);
}

/**
 * pk_main_handle_event:
 **/
static void
pk_main_event_handler (PkPlugin *plugin, XEvent *event)
{
	XButtonEvent *xbe;
	XMotionEvent *xme;
	XCrossingEvent *xce;

	pk_debug ("pk_main_handle_event [%p]", plugin);

	/* find plugin */
	if (plugin == NULL)
		return;

	switch (event->xany.type) {
	case VisibilityNotify:
	case Expose:
		{
		Display *display;
		Window  window;

		pk_debug ("Expose [%p]", plugin);

		/* get parameters */
		g_object_get (plugin, "display", &display, "window", &window, NULL);

		/* get rid of all other exposure events */
		while (XCheckTypedWindowEvent (display, window, Expose, event));
			pk_main_draw_window (plugin);
		return;
		}
	case ButtonPress:
		xbe = (XButtonEvent *)event;
		pk_plugin_button_press (plugin, xbe->x, xbe->y, xbe->time);
		return;
	case ButtonRelease:
		xbe = (XButtonEvent *)event;
		pk_plugin_button_release (plugin, xbe->x, xbe->y, xbe->time);
		return;
	case MotionNotify:
		xme = (XMotionEvent *)event;
		pk_plugin_motion (plugin, xme->x, xme->y);
		return;
	case EnterNotify:
		xce = (XCrossingEvent *)event;
		pk_plugin_enter (plugin, xce->x, xce->y);
		return;
	case LeaveNotify:
		xce = (XCrossingEvent *)event;
		pk_plugin_leave (plugin, xce->x, xce->y);
		return;
	}
}

/**
 * pk_main_set_window:
 **/
static NPError
pk_main_set_window (NPP instance, NPWindow* pNPWindow)
{
	gboolean ret;
	gboolean started;
	PkPlugin *plugin;
	NPSetWindowCallbackStruct *ws_info;
	Window window;

	pk_debug ("pk_main_set_window [%p]", instance);

	/* find plugin */
	plugin = PK_PLUGIN (instance->pdata);
	if (plugin == NULL)
		return NPERR_GENERIC_ERROR;

	/* shutdown */
	if (pNPWindow == NULL) {
		pk_main_delete_window (plugin);
		return NPERR_NO_ERROR;
	}

	/* type */
	pk_debug ("type=%i (NPWindowTypeWindow=%i, NPWindowTypeDrawable=%i)",
		  pNPWindow->type, NPWindowTypeWindow, NPWindowTypeDrawable);

	g_object_get (plugin,
		      "window", &window,
		      NULL);

	/*
	 * The page with the plugin is being resized.
	 * Save any UI information because the next time
	 * around expect a SetWindow with a new window
	 * id.
	 */
	if ((Window) (pNPWindow->window) == window) {
		pk_debug ("resize event will come");
		goto out;
	}

	/* do we have a callback struct (WebKit doesn't send this) */
	ws_info = (NPSetWindowCallbackStruct *) pNPWindow->ws_info;
	if (ws_info == NULL) {
		pk_debug ("no callback struct");
		goto out;
	}

	/* no visual yet */
	if (ws_info->visual == NULL) {
		pk_debug ("no visual, so skipping");
		goto out;
	}

	/* set parameters */
	g_object_set (plugin,
		      "x", 0,
		      "y", 0,
		      "width", pNPWindow->width,
		      "height", pNPWindow->height,
		      "display", ws_info->display,
		      "visual", ws_info->visual,
		      "window", pNPWindow->window,
		      NULL);

	pk_debug ("x=%i, y=%i, width=%i, height=%i, display=%p, visual=%p, window=%ld",
		 pNPWindow->x, pNPWindow->y, pNPWindow->width, pNPWindow->height,
		 ws_info->display, ws_info->visual, (Window)pNPWindow->window);

	/* is already started */
	g_object_get (plugin,
		      "started", &started,
		      NULL);
	if (!started) {
		/* start plugin */
		ret = pk_plugin_start (plugin);
		if (!ret)
			pk_warning ("failed to start plugin");
	}

	/* Set-up drawing window */
	pk_main_create_window (plugin);

	/* draw plugin */
	pk_main_draw_window (plugin);

out:
	return NPERR_NO_ERROR;
}

/**
 * pk_main_make_module_resident:
 *
 * If our dependent libraries like libpackagekit get unloaded, bad stuff
 * happens (they may have registered GLib types and so forth) so we need
 * to keep them around. The (GNU extension) RTLD_NODELETE seems useful
 * but isn't so much, since it only refers to a specific library and not
 * its dependent libraries, so we'd have to identify specifically each
 * of our dependencies that is not safe to unload and that is most of
 * the GTK+ stack.
 **/
static void
pk_main_make_module_resident (void)
{
	Dl_info info;

	/* get the absolute filename of this module */
	if (!dladdr ((void *)NP_GetMIMEDescription, &info)) {
		g_warning ("Can't find filename for module");
		return;
	}

	/* now reopen it to get our own handle */
	module_handle = dlopen (info.dli_fname, RTLD_NOW);
	if (!module_handle) {
		g_warning ("Can't permanently open module %s", dlerror ());
		return;
	}
}

NPError NP_GetEntryPoints (NPPluginFuncs *nppfuncs);

/**
 * NP_GetEntryPoints:
 **/
NPError
NP_GetEntryPoints (NPPluginFuncs *nppfuncs)
{
	pk_debug ("NP_GetEntryPoints");

	nppfuncs->version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
	nppfuncs->newp = pk_main_newp;
	nppfuncs->destroy = pk_main_destroy;
	nppfuncs->getvalue = pk_main_get_value;
	nppfuncs->setwindow = pk_main_set_window;

	return NPERR_NO_ERROR;
}

/**
 * NP_Initialize:
 **/
NPError
NP_Initialize (NPNetscapeFuncs *npnf, NPPluginFuncs *nppfuncs)
{
	pk_debug ("NP_Initialize");

	if (npnf == NULL)
		return NPERR_INVALID_FUNCTABLE_ERROR;

	if (HIBYTE (npnf->version) > NP_VERSION_MAJOR)
		return NPERR_INCOMPATIBLE_VERSION_ERROR;

	/* already initialized */
	if (module_handle != NULL)
		return NPERR_NO_ERROR;

	/* if libpackagekit get unloaded, bad stuff happens */
	pk_main_make_module_resident ();

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

	npnfuncs = npnf;
	NP_GetEntryPoints (nppfuncs);
	return NPERR_NO_ERROR;
}

/**
 * NP_Shutdown:
 **/
NPError
NP_Shutdown ()
{
	pk_debug ("NP_Shutdown");
	return NPERR_NO_ERROR;
}

/**
 * NP_GetMIMEDescription:
 **/
const char *
NP_GetMIMEDescription (void)
{
	g_debug ("NP_GetMIMEDescription");
	return (const gchar*) "application/x-packagekit-plugin:bsc:PackageKit Plugin";
}

/**
 * NP_GetValue:
 **/
NPError
NP_GetValue (void *npp, NPPVariable variable, void *value)
{
	return pk_main_get_value ((NPP)npp, variable, value);
}

