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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include <X11/Xlib.h>
#include <pango/pango.h>
#include <packagekit/pk-client.h>
#include <cairo.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "pluginbase.h"
#include "util.h"

enum PackageStatus { IN_PROGRESS, INSTALLED, AVAILABLE, UNAVAILABLE, INSTALLING };

class nsPluginInstance : public nsPluginInstanceBase
{
public:
    nsPluginInstance(NPP aInstance, const char *displayName, const char *packageNames, const char *desktopNames);
    virtual ~nsPluginInstance();

    NPBool init(NPWindow* aWindow);
    void shut();
    NPBool isInitialized() {return mInitialized;}
    NPError GetValue(NPPVariable variable, void *value);
    NPError SetWindow(NPWindow* aWindow);
    uint16 HandleEvent(void *event);

    void setStatus(PackageStatus status);
    PackageStatus getStatus() { return mStatus; }
    void setAvailableVersion(const char *version);
    void setAvailablePackageName(const char *name);
    void setInstalledVersion(const char *version);

private:
    void recheck();
    void findDesktopFile();
    void runApplication();
    void installPackage();
    
    void ensureLayout(cairo_t *cr,
                      PangoFontDescription *font_desc,
                      guint32 link_color);
    void clearLayout();
    void refresh();
    
    void handleGraphicsExpose(XGraphicsExposeEvent *xev);
    void handleButtonPress(XButtonEvent *xev);
    void handleButtonRelease(XButtonEvent *xev);
    void handleMotionNotify(XMotionEvent *xev);
    void handleEnterNotify(XCrossingEvent *xev);
    void handleLeaveNotify(XCrossingEvent *xev);

    void removeClient(PkClient *client);
    
    static void onClientPackage(PkClient 	   *client,
                                PkInfoEnum	    info,
                                const gchar	   *package_id,
                                const gchar	   *summary,
                                nsPluginInstance   *instance);
    static void onClientErrorCode(PkClient	   *client,
                                  PkErrorCodeEnum  code,
                                  const gchar	   *details,
                                  nsPluginInstance *instance);
    static void onClientFinished(PkClient	  *client,
                                 PkExitEnum	   exit,
                                 guint		   runtime,
                                 nsPluginInstance *instance);
    static void onInstallFinished(GError        *error,
                                  int            status,
                                  const char    *output,
                                  void          *callback_data);
    
    NPP mInstance;
    NPBool mInitialized;
    PackageStatus mStatus;
    std::string mAvailableVersion;
    std::string mAvailablePackageName;
    std::string mInstalledVersion;
    std::string mDesktopFile;

    std::string mDisplayName;
    std::vector<std::string> mPackageNames;
    std::vector<std::string> mDesktopNames;
    
    Window mWindow;
    Display *mDisplay;
    int mX, mY;
    int mWidth, mHeight;
    Visual* mVisual;
    Colormap mColormap;
    unsigned int mDepth;
  
    PangoLayout *mLayout;
    int mLinkStart;
    int mLinkEnd;

    std::vector<PkClient *> mClients;

    PkpExecuteCommandAsyncHandle *mInstallPackageHandle;
};

#endif // __PLUGIN_H__
