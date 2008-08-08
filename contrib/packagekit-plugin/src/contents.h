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
 * The Original Code is packagekit-plugin code.
 *
 * The Initial Developer of the Original Code is
 * Red Hat, Inc.
 * Portions created by the Initial Developer are Copyright (C) 2008
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


#ifndef __CONTENTS_H__
#define __CONTENTS_H__

#include <X11/Xlib.h>
#include <gio/gio.h>
#include <pango/pango.h>
#include <packagekit/pk-client.h>
#include <cairo.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

class PkpPluginInstance;

enum PackageStatus {
    IN_PROGRESS, /* Looking up package information */
    INSTALLED,   /* Package installed */
    UPGRADABLE,  /* Package installed, newer version available */
    AVAILABLE,   /* Package not installed, version available */
    UNAVAILABLE, /* Package not installed or available */
    INSTALLING   /* Currently installing a new version */
};

class PkpContents
{
public:
    PkpContents(const char *displayName, const char *packageNames, const char *desktopNames);
    virtual ~PkpContents();

    void setPlugin(PkpPluginInstance *plugin);

    void draw(cairo_t *cr);
    void buttonPress(int x, int y, Time time);
    void buttonRelease(int x, int y, Time time);
    void motion(int x, int y);
    void enter(int x, int y);
    void leave(int x, int y);

private:
    void recheck();
    void findAppInfo();
    void runApplication(Time time);
    void installPackage(Time time);

    int getLinkIndex(int x, int y);

    void setStatus(PackageStatus status);
    PackageStatus getStatus() { return mStatus; }
    void setAvailableVersion(const char *version);
    void setAvailablePackageName(const char *name);
    void setInstalledVersion(const char *version);

    void ensureLayout(cairo_t *cr,
                      PangoFontDescription *font_desc,
                      guint32 link_color);
    void clearLayout();
    void refresh();

    void removeClient(PkClient *client);

    static void onClientPackage(PkClient           *client,
                                const PkPackageObj *obj,
                                PkpContents        *contents);
    static void onClientErrorCode(PkClient	   *client,
                                  PkErrorCodeEnum  code,
                                  const gchar	   *details,
                                  PkpContents *contents);
    static void onClientFinished(PkClient	  *client,
                                 PkExitEnum	   exit,
                                 guint		   runtime,
                                 PkpContents      *contents);

    static void onInstallPackageFinished(DBusGProxy     *proxy,
                                         DBusGProxyCall *call,
                                         void           *user_data);

    PkpPluginInstance *mPlugin;
    PackageStatus mStatus;
    std::string mAvailableVersion;
    std::string mAvailablePackageName;
    std::string mInstalledVersion;
    GAppInfo *mAppInfo;

    std::string mDisplayName;
    std::vector<std::string> mPackageNames;
    std::vector<std::string> mDesktopNames;

    PangoLayout *mLayout;

    std::vector<PkClient *> mClients;

    DBusGProxy *mInstallPackageProxy;
    DBusGProxyCall *mInstallPackageCall;
};

#endif // __CONTENTS_H__
