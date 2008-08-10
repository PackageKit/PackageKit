/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Red Hat, Inc.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#define MOZ_X11

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>

#include <cairo-xlib.h>
#include <dlfcn.h>

#include "plugin.h"

#define MIME_TYPES_HANDLED  "application/x-packagekit-plugin"
#define PLUGIN_NAME         "Plugin for Installing Applications"
#define MIME_TYPES_DESCRIPTION  MIME_TYPES_HANDLED":bsc:"PLUGIN_NAME
#define PLUGIN_DESCRIPTION  PLUGIN_NAME

char* NPP_GetMIMEDescription(void)
{
    return (char *)(MIME_TYPES_DESCRIPTION);
}

static void *module_handle = 0;

/////////////////////////////////////
// general initialization and shutdown
//

/* If our dependent libraries like libpackagekit get unloaded, bad stuff
 * happens (they may have registered GLib types and so forth) so we need
 * to keep them around. The (GNU extension) RTLD_NODELETE seems useful
 * but isn't so much, since it only refers to a specific library and not
 * its dependent libraries, so we'd have to identify specifically each
 * of our dependencies that is not safe to unload and that is most of
 * the GTK+ stack.
 */
static void
make_module_resident()
{
    Dl_info info;

    /* Get the (absolute) filename of this module */
    if (!dladdr((void *)NPP_GetMIMEDescription, &info)) {
        g_warning("Can't find filename for module");
        return;
    }

    /* Now reopen it to get our own handle */
    module_handle = dlopen(info.dli_fname, RTLD_NOW);
    if (!module_handle) {
        g_warning("Can't permanently open module %s", dlerror());
        return;
    }

    /* the module will never be closed */
}

NPError NS_PluginInitialize()
{
    if (module_handle != 0) /* Already initialized */
        return NPERR_NO_ERROR;

    make_module_resident();

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

    return NPERR_NO_ERROR;
}

void NS_PluginShutdown()
{
}

// get values per plugin
NPError NS_PluginGetValue(NPPVariable aVariable, void *aValue)
{
    NPError err = NPERR_NO_ERROR;
    switch (aVariable) {
    case NPPVpluginNameString:
        *((char **)aValue) = (char *)PLUGIN_NAME;
        break;
    case NPPVpluginDescriptionString:
        *((char **)aValue) = (char *)PLUGIN_DESCRIPTION;
        break;
    default:
        err = NPERR_INVALID_PARAM;
        break;
    }
    return err;
}

/////////////////////////////////////////////////////////////
//
// construction and destruction of our plugin instance object
//
nsPluginInstanceBase * NS_NewPluginInstance(nsPluginCreateData * aCreateDataStruct)
{
    const char *displayName = "";
    const char *packageNames = NULL;
    const char *desktopNames = NULL;

    if(!aCreateDataStruct)
        return NULL;

    for (int i = 0; i < aCreateDataStruct->argc; i++) {
        if (strcmp(aCreateDataStruct->argn[i], "displayname") == 0)
            displayName = aCreateDataStruct->argv[i];
        else if (strcmp(aCreateDataStruct->argn[i], "packagenames") == 0)
            packageNames = aCreateDataStruct->argv[i];
        else if (strcmp(aCreateDataStruct->argn[i], "desktopnames") == 0)
            desktopNames = aCreateDataStruct->argv[i];
    }

    PkpPluginInstance * plugin = new PkpPluginInstance(aCreateDataStruct->instance, displayName, packageNames, desktopNames);

    NPN_SetValue(aCreateDataStruct->instance,
                 NPPVpluginWindowBool, (void *)FALSE);

    return plugin;
}

void NS_DestroyPluginInstance(nsPluginInstanceBase * aPlugin)
{
    if(aPlugin)
        delete (PkpPluginInstance *)aPlugin;
}

////////////////////////////////////////
//
// nsPluginInstance class implementation
//

PkpPluginInstance::PkpPluginInstance(NPP         aInstance,
                                     const char *displayName,
                                     const char *packageNames,
                                     const char *desktopNames) :
    nsPluginInstanceBase(),
    mInstance(aInstance),
    mInitialized(FALSE),
    mContents(displayName, packageNames, desktopNames),
    mWindow(0)
{
    mContents.setPlugin(this);
}

PkpPluginInstance::~PkpPluginInstance()
{
}

NPBool PkpPluginInstance::init(NPWindow* aWindow)
{
    if(aWindow == NULL)
        return FALSE;

    if (SetWindow(aWindow))
        mInitialized = TRUE;
	
    return mInitialized;
}

void PkpPluginInstance::shut()
{
    mInitialized = FALSE;
}

NPError PkpPluginInstance::GetValue(NPPVariable aVariable, void *aValue)
{
    NPError err = NPERR_NO_ERROR;
    switch (aVariable) {
    case NPPVpluginNameString:
    case NPPVpluginDescriptionString:
        return NS_PluginGetValue(aVariable, aValue) ;
        break;
    default:
        err = NPERR_INVALID_PARAM;
        break;
    }
    return err;

}

NPError PkpPluginInstance::SetWindow(NPWindow* aWindow)
{
    if (aWindow == NULL)
        return FALSE;

    mX = aWindow->x;
    mY = aWindow->y;
    mWidth = aWindow->width;
    mHeight = aWindow->height;

    mWindow = (Window) aWindow->window;
    NPSetWindowCallbackStruct *ws_info = (NPSetWindowCallbackStruct *)aWindow->ws_info;
    mDisplay = ws_info->display;
    mVisual = ws_info->visual;
    mDepth = ws_info->depth;
    mColormap = ws_info->colormap;

    return NPERR_NO_ERROR;
}

void
PkpPluginInstance::refresh()
{
    NPRect rect;

    /* Coordinates here are relative to the plugin's origin (mX,mY) */

    rect.left = 0;
    rect.right =  mWidth;
    rect.top = 0;
    rect.bottom = mHeight;

    NPN_InvalidateRect(mInstance, &rect);
}

uint16
PkpPluginInstance::HandleEvent(void *event)
{
    XEvent *xev = (XEvent *)event;

    switch (xev->xany.type) {
    case GraphicsExpose:
        {
            XGraphicsExposeEvent *xge = (XGraphicsExposeEvent *)event;

            cairo_surface_t *surface = cairo_xlib_surface_create (mDisplay, xge->drawable, mVisual, mX + mWidth, mY + mHeight);
            cairo_t *cr = cairo_create(surface);

            cairo_rectangle(cr, xge->x, xge->y, xge->width, xge->height);
            cairo_clip(cr);

            mContents.draw(cr);

            cairo_destroy(cr);
            cairo_surface_destroy(surface);

            return 1;
        }
    case ButtonPress:
        {
            XButtonEvent *xbe = (XButtonEvent *)event;
            mContents.buttonPress(xbe->x, xbe->y, xbe->time);
            return 1;
        }
    case ButtonRelease:
        {
            XButtonEvent *xbe = (XButtonEvent *)event;
            mContents.buttonRelease(xbe->x, xbe->y, xbe->time);
            return 1;
        }
    case MotionNotify:
        {
            XMotionEvent *xme = (XMotionEvent *)event;
            mContents.motion(xme->x, xme->y);
            return 1;
        }
    case EnterNotify:
        {
            XCrossingEvent *xce = (XCrossingEvent *)event;
            mContents.enter(xce->x, xce->y);
            return 1;
        }
    case LeaveNotify:
        {
            XCrossingEvent *xce = (XCrossingEvent *)event;
            mContents.leave(xce->x, xce->y);
            return 1;
        }
    }

    return 0;
}
