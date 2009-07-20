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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <glib/gprintf.h>
#include <glib/gi18n-lib.h>

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

	g_free(buffer);
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

	g_free(buffer);
}

/**
 * pk_main_refresh_cb:
 **/
static void
pk_main_refresh_cb (PkPlugin *plugin_, NPP instance)
{
	NPRect rect;
	guint width;
	guint height;

	pk_debug ("pk_main_refresh_cb [%p]", instance);

	/* invalid */
	if (plugin_ == NULL) {
		pk_warning ("NULL plugin");
		return;
	}

	/* get parameters */
	g_object_get (plugin_,
		      "width", &width,
		      "height", &height,
		      NULL);

	/* Coordinates here are relative to the plugin's origin (x,y) */
	rect.left = 0;
	rect.right =  width;
	rect.top = 0;
	rect.bottom = height;

	pk_debug ("invalidating rect %ix%i to %ix%i", rect.left, rect.top, rect.right, rect.bottom);

	npnfuncs->invalidaterect (instance, &rect);
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
	case NPPVpluginScriptableIID:
	case NPPVpluginScriptableInstance:
                /* XPCOM scripting, obsolete */
                err = NPERR_GENERIC_ERROR;
		break;
	case NPPVpluginScriptableNPObject:
		err = NPERR_INVALID_PLUGIN_ERROR;
		break;
	case NPPVpluginNeedsXEmbed:
		* ((PRBool *)value) = PR_TRUE;
		break;
	default:
		pk_warning ("Unhandled variable %d instance %p", variable, instance);
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
		    g_strcmp0 (argn[i], "packagenames") == 0)
			pk_plugin_set_data (plugin, argn[i], argv[i]);
	}

	/* add to list */
	instance->pdata = plugin;

	npnfuncs->setvalue (instance, NPPVpluginWindowBool, (void *) FALSE);

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
	g_object_unref (plugin);

	/* free content instance */
	g_signal_handlers_disconnect_by_func (plugin, G_CALLBACK (pk_main_refresh_cb), instance);
	g_object_unref (plugin);

	return NPERR_NO_ERROR;
}

/**
 * pk_main_handle_event:
 **/
static NPError
pk_main_handle_event (NPP instance, void *event)
{
	XEvent *xev = (XEvent *)event;
	cairo_surface_t *surface;
	cairo_t *cr;
	XButtonEvent *xbe;
	XGraphicsExposeEvent *xge;
	XMotionEvent *xme;
	XCrossingEvent *xce;
	guint x;
	guint y;
	guint width;
	guint height;
	Display *display;
	Visual *visual;
	PkPlugin *plugin;

	pk_debug ("pk_main_handle_event [%p]", instance);

	/* find plugin */
	plugin = PK_PLUGIN (instance->pdata);
	if (plugin == NULL)
		return NPERR_GENERIC_ERROR;

	switch (xev->xany.type) {
	case GraphicsExpose:
		xge = (XGraphicsExposeEvent *)event;

		/* get parameters */
		g_object_get (plugin,
			      "x", &x,
			      "y", &y,
			      "width", &width,
			      "height", &height,
			      "display", &display,
			      "visual", &visual,
			      NULL);

		pk_debug ("creating surface on display %i size %ix%i on drawable %i with visual %p",
			 (int)display, x + width, y + height, (gint)xge->drawable, visual);

		surface = cairo_xlib_surface_create (display, xge->drawable, visual, x + width, y + height);

		width = cairo_xlib_surface_get_width (surface);
		height = cairo_xlib_surface_get_height (surface);
		if (width <= 0 || height <= 0)
			pk_warning ("did not create surface: %ix%i", width, height);

		cr = cairo_create (surface);
		pk_plugin_draw (plugin, cr);
		cairo_destroy (cr);
		cairo_surface_destroy (surface);
		return 1;
	case ButtonPress:
		xbe = (XButtonEvent *)event;
		pk_plugin_button_press (plugin, xbe->x, xbe->y, xbe->time);
		return 1;
	case ButtonRelease:
		xbe = (XButtonEvent *)event;
		pk_plugin_button_release (plugin, xbe->x, xbe->y, xbe->time);
		return 1;
	case MotionNotify:
		xme = (XMotionEvent *)event;
		pk_plugin_motion (plugin, xme->x, xme->y);
		return 1;
	case EnterNotify:
		xce = (XCrossingEvent *)event;
		pk_plugin_enter (plugin, xce->x, xce->y);
		return 1;
	case LeaveNotify:
		xce = (XCrossingEvent *)event;
		pk_plugin_leave (plugin, xce->x, xce->y);
		return 1;
	}
	return NPERR_NO_ERROR;
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

	pk_debug ("pk_main_set_window [%p]", instance);

	/* shutdown */
	if (pNPWindow == NULL) {
		pk_warning ("NULL window");
		return NPERR_GENERIC_ERROR;
	}

	/* find plugin */
	plugin = PK_PLUGIN (instance->pdata);
	if (plugin == NULL)
		return NPERR_GENERIC_ERROR;

	/* type */
	pk_debug ("type=%i (NPWindowTypeWindow=%i, NPWindowTypeDrawable=%i)",
		  pNPWindow->type, NPWindowTypeWindow, NPWindowTypeDrawable);

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
		      "x", pNPWindow->x,
		      "y", pNPWindow->y,
		      "width", pNPWindow->width,
		      "height", pNPWindow->height,
		      "display", ws_info->display,
		      "visual", ws_info->visual,
		      NULL);

	pk_debug ("x=%i, y=%i, width=%i, height=%i, display=%p, visual=%p",
		 pNPWindow->x, pNPWindow->y, pNPWindow->width, pNPWindow->height,
		 ws_info->display, ws_info->visual);

	/* is already started */
	g_object_get (plugin,
		      "started", &started,
		      NULL);
	if (started) {
		pk_debug ("already started, so skipping");
		goto out;
	}

	/* start plugin */
	ret = pk_plugin_start (plugin);
	if (!ret)
		pk_warning ("failed to start plugin");
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
	if (!dladdr((void *)NP_GetMIMEDescription, &info)) {
		g_warning("Can't find filename for module");
		return;
	}

	/* now reopen it to get our own handle */
	module_handle = dlopen(info.dli_fname, RTLD_NOW);
	if (!module_handle) {
		g_warning("Can't permanently open module %s", dlerror());
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
	nppfuncs->event = pk_main_handle_event;
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
char *
NP_GetMIMEDescription (void)
{
	pk_debug ("NP_GetMIMEDescription");

	return (gchar*) "application/x-packagekit-plugin:bsc:PackageKit Plugin";
}

/**
 * NP_GetValue:
 **/
NPError
NP_GetValue (void *npp, NPPVariable variable, void *value)
{
	return pk_main_get_value ((NPP)npp, variable, value);
}

