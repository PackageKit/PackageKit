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

#define MOZ_X11

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>

#include <cairo-xlib.h>
#include <dlfcn.h>
#include <pango/pangocairo.h>
#include <packagekit/pk-package-id.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <libgnome/gnome-desktop-item.h>
#include <libgnomevfs/gnome-vfs.h>

#include "plugin.h"

#define APPLICATION_DIR "/usr/share/applications"

#define MARGIN 5

////////////////////////////////////////
//
// PkpContents class implementation
//

static std::vector<std::string>
splitString(const char *str)
{
    std::vector<std::string> v;

    if (str) {
        char **split = g_strsplit(str, " ", -1);
        for (char **s = split; *s; s++) {
            char *stripped = strdup(*s);
            g_strstrip(stripped);
            v.push_back(stripped);
            g_free(stripped);
        }

        g_strfreev(split);
    }

    return v;
}

PkpContents::PkpContents(const char *displayName,
                         const char *packageNames,
                         const char *desktopNames) :
    mStatus(IN_PROGRESS),
    mDisplayName(displayName),
    mPackageNames(splitString(packageNames)),
    mDesktopNames(splitString(desktopNames)),
    mLayout(0),
    mInstallPackageHandle(0)
{
    recheck();
}

PkpContents::~PkpContents()
{
    clearLayout();

    if (mInstallPackageHandle != 0)
        pkp_execute_command_async_cancel(mInstallPackageHandle);

    while (!mClients.empty())
        removeClient(mClients.front());
}

void PkpContents::recheck()
{
    mStatus = IN_PROGRESS;
    mAvailableVersion = "";
    mAvailablePackageName = "";
    
    for (std::vector<std::string>::iterator i = mPackageNames.begin(); i != mPackageNames.end(); i++) {
        GError *error = NULL;
        PkClient *client = pk_client_new();
        if (!pk_client_resolve(client, "none", i->c_str(), &error)) {
            g_warning("%s", error->message);
            g_clear_error(&error);
            g_object_unref(client);
        } else {
            g_signal_connect(client, "package", G_CALLBACK(onClientPackage), this);
            g_signal_connect(client, "error-code", G_CALLBACK(onClientErrorCode), this);
            g_signal_connect(client, "finished", G_CALLBACK(onClientFinished), this);
            mClients.push_back(client);
        }
    }

    findDesktopFile();

    if (mClients.empty() && getStatus() == IN_PROGRESS)
        setStatus(UNAVAILABLE);
}

void PkpContents::removeClient(PkClient *client)
{
    for (std::vector<PkClient *>::iterator i = mClients.begin(); i != mClients.end(); i++) {
        if (*i == client) {
            mClients.erase(i);
            g_signal_handlers_disconnect_by_func(client, (void *)onClientPackage, this);
            g_signal_handlers_disconnect_by_func(client, (void *)onClientErrorCode, this);
            g_signal_handlers_disconnect_by_func(client, (void *)onClientFinished, this);
            g_object_unref(client);
            break;
        }
    }

    if (mClients.empty()) {
        if (getStatus() == IN_PROGRESS)
            setStatus(UNAVAILABLE);
    }
}

void
PkpContents::setStatus(PackageStatus status)
{
    if (mStatus != status) {
        mStatus = status;
        clearLayout();
        refresh();
    }
}

void
PkpContents::setAvailableVersion(const char *version)
{
    mAvailableVersion = version;
    clearLayout();
    refresh();
}

void
PkpContents::setAvailablePackageName(const char *name)
{
    mAvailablePackageName = name;
}

void
PkpContents::setInstalledVersion(const char *version)
{
    mInstalledVersion = version;
    clearLayout();
    refresh();
}

void
PkpContents::clearLayout()
{
    if (mLayout) {
        g_object_unref(mLayout);
        mLayout = 0;
    }
}

static void
append_markup(GString *str, const char *format, ...)
{
    va_list vap;
    
    va_start(vap, format);
    char *tmp = g_markup_vprintf_escaped(format, vap);
    va_end(vap);

    g_string_append(str, tmp);
    g_free(tmp);
}

static guint32
rgba_from_gdk_color(GdkColor *color)
{
    return (((color->red   >> 8) << 24) |
            ((color->green >> 8) << 16) |
            ((color->blue  >> 8) << 8) |
            0xff);
}

static void
set_source_from_rgba(cairo_t *cr,
                     guint32  rgba)
{
    cairo_set_source_rgba(cr,
                          ((rgba & 0xff000000) >> 24) / 255.,
                          ((rgba & 0x00ff0000) >> 16) / 255.,
                          ((rgba & 0x0000ff00) >> 8) / 255.,
                          (rgba & 0x000000ff) / 255.);
                          
}

/* Retrieve the system colors and fonts.
 * This looks incredibly expensive .... to create a GtkWindow for
 * every expose ... but actually it's only moderately expensive;
 * Creating a GtkWindow is just normal GObject creation overhead --
 * the extra expense beyond that will come when we actually create
 * the window.
 */
static void
get_style(PangoFontDescription **font_desc,
          guint32               *foreground,
          guint32               *background,
          guint32               *link)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_widget_ensure_style(window);

    *foreground = rgba_from_gdk_color(&window->style->text[GTK_STATE_NORMAL]);
    *background = rgba_from_gdk_color(&window->style->base[GTK_STATE_NORMAL]);

    GdkColor link_color = { 0, 0, 0, 0xeeee };
    GdkColor *tmp = NULL;

    gtk_widget_style_get (GTK_WIDGET (window),
                          "link-color", &tmp, NULL);
    if (tmp != NULL) {
        link_color = *tmp;
        gdk_color_free(tmp);
    }

    *link = rgba_from_gdk_color(&link_color);

    *font_desc = pango_font_description_copy(window->style->font_desc);
   
    gtk_widget_destroy(window);
}

void
PkpContents::ensureLayout(cairo_t              *cr,
                               PangoFontDescription *font_desc,
                               guint32               link_color)
{
    GString *markup = g_string_new(NULL);
    
    if (mLayout)
        return;
    
    mLayout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(mLayout, font_desc);

    switch (mStatus) {
    case IN_PROGRESS:
        append_markup(markup, _("Getting package information..."));
        break;
    case INSTALLED:
        if (!mDesktopFile.empty())
            append_markup(markup, _("<span color='#%06x' underline='single' size='larger'>Run %s</span>"),
                          link_color >> 8,
                          mDisplayName.c_str());
        else
            append_markup(markup, _("<big>%s</big>"), mDisplayName.c_str());
        if (!mInstalledVersion.empty())
            append_markup(markup, _("\n<small>Installed version: %s</small>"), mInstalledVersion.c_str());
        break;
    case AVAILABLE:
        append_markup(markup, _("<span color='#%06x' underline='single' size='larger'>Install %s Now</span>"),
                      link_color >> 8,
                      mDisplayName.c_str());
        append_markup(markup, _("\n<small>Version: %s</small>"), mAvailableVersion.c_str());
        break;
    case UNAVAILABLE:
        append_markup(markup, _("<big>%s</big>"), mDisplayName.c_str());
        append_markup(markup, _("\n<small>No packages found for your system</small>"));
        break;
    case INSTALLING:
        append_markup(markup, _("<big>%s</big>"), mDisplayName.c_str());
        append_markup(markup, _("\n<small>Installing...</small>"));
        break;
    }

    pango_layout_set_markup(mLayout, markup->str, -1);
    g_string_free(markup, TRUE);
}

void
PkpContents::refresh()
{
    mPlugin->refresh();
}
                               
void
PkpContents::setPlugin(PkpPluginInstance *plugin)
{
    mPlugin = plugin;
}

void
PkpContents::draw(cairo_t *cr)
{
    guint32 foreground, background, link;
    PangoFontDescription *font_desc;
    
    get_style(&font_desc, &foreground, &background, &link);

    set_source_from_rgba(cr, background);
    cairo_rectangle(cr, mPlugin->getX(), mPlugin->getY(), mPlugin->getWidth(), mPlugin->getHeight());
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_rectangle(cr, mPlugin->getX() + 0.5, mPlugin->getY() + 0.5, mPlugin->getWidth() - 1, mPlugin->getHeight() - 1);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    ensureLayout(cr, font_desc, link);
    int width, height;
    pango_layout_get_pixel_size(mLayout, &width, &height);

    cairo_move_to(cr, mPlugin->getX() + MARGIN, mPlugin->getY() + MARGIN);
    set_source_from_rgba(cr, foreground);
    pango_cairo_show_layout(cr, mLayout);
}

void
PkpContents::buttonPress(int x, int y, Time time)
{
}

void
PkpContents::buttonRelease(int x, int y, Time time)
{
    if (!mDesktopFile.empty())
        runApplication();
    else if (!mAvailablePackageName.empty())
        installPackage();
}

void
PkpContents::motion(int x, int y)
{
}

void
PkpContents::enter(int x, int y)
{
}

void
PkpContents::leave(int x, int y)
{
}

static guint32
get_server_timestamp()
{
    GtkWidget *invisible = gtk_invisible_new();
    gtk_widget_realize(invisible);
    return gdk_x11_get_server_time(invisible->window);
    gtk_widget_destroy(invisible);
}

static gboolean
validate_name(const char *name)
{
    const char *p;
    
    for (p = name; *p; p++) {
        char c = *p;
        
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              (c == '.') ||
              (c == '_') ||
              (c == '-')))
            return FALSE;
    }

    return TRUE;
}

void
PkpContents::findDesktopFile()
{
    for (std::vector<std::string>::iterator i = mDesktopNames.begin(); i != mDesktopNames.end(); i++) {
        if (!validate_name(i->c_str())) {
            g_warning("Bad desktop name: '%s'", i->c_str());
            continue;
        }
        
        char *filename = g_strconcat(i->c_str(), ".desktop", NULL);
        char *path = g_build_filename(APPLICATION_DIR, filename, NULL);
        g_free(filename);

        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            mDesktopFile = path;
            break;
        }

        g_free(path);
    }

    if (!mDesktopFile.empty())
        setStatus(INSTALLED);
}
                  
void
PkpContents::runApplication (void)
{
    GError *error = NULL;
    
    /* This is idempotent and fairly cheap, so do it here to avoid initializing
     * gnome-vfs on plugin startup
     */
    gnome_vfs_init();

    if (mDesktopFile.c_str() == 0) {
        g_warning("Didn't find application to launch");
        return;
    }

    GnomeDesktopItem *item = gnome_desktop_item_new_from_file(mDesktopFile.c_str(), GNOME_DESKTOP_ITEM_LOAD_NO_TRANSLATIONS, &error);
    if (!item) {
        g_warning("%s\n", error->message);
        g_clear_error(&error);
        gnome_desktop_item_unref(item);
        return;
    }

    guint32 launch_time = gtk_get_current_event_time();
    if (launch_time == GDK_CURRENT_TIME)
        launch_time = get_server_timestamp();

    if (!gnome_desktop_item_launch(item, NULL, (GnomeDesktopItemLaunchFlags)0, &error)) {
        g_warning("%s\n", error->message);
        g_clear_error(&error);
        gnome_desktop_item_unref(item);
        return;
    }
}

void
PkpContents::installPackage (void)
{
    if (mAvailablePackageName.empty()) {
        g_warning("No available package to install");
        return;
    }

    if (mInstallPackageHandle != 0) {
        g_warning("Already installing package");
        return;
    }

    char *argv[3];
    argv[0] = (char *)"gpk-install-package";
    argv[1] = (char *)mAvailablePackageName.c_str();
    argv[2] = 0;

    mInstallPackageHandle = pkp_execute_command_async(argv, onInstallFinished, this);
    setStatus(INSTALLING);
}

void
PkpContents::onClientPackage(PkClient	  *client,
                                  PkInfoEnum	   info,
                                  const gchar	   *package_id,
                                  const gchar	   *summary,
                                  PkpContents *contents)
{
    fprintf(stderr, "package: %d %s %s\n", info, package_id, summary);

    PkPackageId *id = pk_package_id_new_from_string(package_id);
    
    if (info == PK_INFO_ENUM_AVAILABLE) {
        if (contents->getStatus() != INSTALLED)
            contents->setStatus(AVAILABLE);
        contents->setAvailableVersion(id->version);
        contents->setAvailablePackageName(id->name);
    } else if (info == PK_INFO_ENUM_INSTALLED) {
        contents->setStatus(INSTALLED);
        contents->setInstalledVersion(id->version);
    }
    
    pk_package_id_free(id);
}

void
PkpContents::onClientErrorCode(PkClient	     *client,
                                    PkErrorCodeEnum   code,
                                    const gchar	     *details,
                                    PkpContents *contents)
{
    fprintf(stderr, "error code: %d %s\n", code, details);
    contents->removeClient(client);
}

void
PkpContents::onClientFinished(PkClient	    *client,
                              PkExitEnum     exit,
                              guint	     runtime,
                              PkpContents   *contents)
    
{
    fprintf(stderr, "finished: %d\n", exit);
    contents->removeClient(client);
}
    
void
PkpContents::onInstallFinished(GError        *error,
                               int            status,
                               const char    *output,
                               void          *callback_data)
{
    PkpContents *contents = (PkpContents *)callback_data;

    contents->mInstallPackageHandle = 0;
    
    if (error) {
        g_warning("Error occurred during install: %s", error->message);
    }

    if (status != 0) {
        g_warning("gpk-install-command exited with non-zero status %d", status);
    }

    contents->recheck();
}
