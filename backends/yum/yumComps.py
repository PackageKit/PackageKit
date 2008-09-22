#!/usr/bin/python
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>

from packagekit.enums import *
import sqlite3 as sqlite

groupMap = {
'desktops;gnome-desktop'                      : GROUP_DESKTOP_GNOME,
'desktops;window-managers'                    : GROUP_DESKTOP_OTHER,
'desktops;sugar-desktop'                      : GROUP_DESKTOP_OTHER,
'desktops;kde-desktop'                        : GROUP_DESKTOP_KDE,
'desktops;xfce-desktop'                       : GROUP_DESKTOP_XFCE,
'apps;authoring-and-publishing'               : GROUP_PUBLISHING,
'apps;office'                                 : GROUP_OFFICE,
'apps;sound-and-video'                        : GROUP_MULTIMEDIA,
'apps;editors'                                : GROUP_OFFICE,
'apps;engineering-and-scientific'             : GROUP_OTHER,
'apps;games'                                  : GROUP_GAMES,
'apps;graphics'                               : GROUP_GRAPHICS,
'apps;text-internet'                          : GROUP_INTERNET,
'apps;graphical-internet'                     : GROUP_INTERNET,
'apps;education'                              : GROUP_EDUCATION,
'development;kde-software-development'        : GROUP_PROGRAMMING,
'development;gnome-software-development'      : GROUP_PROGRAMMING,
'development;development-tools'               : GROUP_PROGRAMMING,
'development;eclipse'                         : GROUP_PROGRAMMING,
'development;development-libs'                : GROUP_PROGRAMMING,
'development;x-software-development'          : GROUP_PROGRAMMING,
'development;web-development'                 : GROUP_PROGRAMMING,
'development;legacy-software-development'     : GROUP_PROGRAMMING,
'development;ruby'                            : GROUP_PROGRAMMING,
'development;java-development'                : GROUP_PROGRAMMING,
'development;xfce-software-development'       : GROUP_PROGRAMMING,
'development;fedora-packager'                 : GROUP_PROGRAMMING,
'servers;clustering'                          : GROUP_SERVERS,
'servers;dns-server'                          : GROUP_SERVERS,
'servers;server-cfg'                          : GROUP_SERVERS,
'servers;news-server'                         : GROUP_SERVERS,
'servers;web-server'                          : GROUP_SERVERS,
'servers;smb-server'                          : GROUP_SERVERS,
'servers;sql-server'                          : GROUP_SERVERS,
'servers;ftp-server'                          : GROUP_SERVERS,
'servers;printing'                            : GROUP_SERVERS,
'servers;mysql'                               : GROUP_SERVERS,
'servers;mail-server'                         : GROUP_SERVERS,
'servers;network-server'                      : GROUP_SERVERS,
'servers;legacy-network-server'               : GROUP_SERVERS,
'base-system;java'                            : GROUP_SYSTEM,
'base-system;base-x'                          : GROUP_SYSTEM,
'base-system;system-tools'                    : GROUP_ADMIN_TOOLS,
'base-system;fonts'                           : GROUP_FONTS,
'base-system;hardware-support'                : GROUP_SYSTEM,
'base-system;dial-up'                         : GROUP_SYSTEM,
'base-system;admin-tools'                     : GROUP_ADMIN_TOOLS,
'base-system;legacy-software-support'         : GROUP_LEGACY,
'base-system;base'                            : GROUP_SYSTEM,
'base-system;virtualization'                  : GROUP_VIRTUALIZATION,
'base-system;legacy-fonts'                    : GROUP_FONTS,
'language-support;khmer-support'              : GROUP_LOCALIZATION,
'language-support;persian-support'            : GROUP_LOCALIZATION,
'language-support;georgian-support'           : GROUP_LOCALIZATION,
'language-support;malay-support'              : GROUP_LOCALIZATION,
'language-support;tonga-support'              : GROUP_LOCALIZATION,
'language-support;portuguese-support'         : GROUP_LOCALIZATION,
'language-support;japanese-support'           : GROUP_LOCALIZATION,
'language-support;hungarian-support'          : GROUP_LOCALIZATION,
'language-support;somali-support'             : GROUP_LOCALIZATION,
'language-support;punjabi-support'            : GROUP_LOCALIZATION,
'language-support;bhutanese-support'          : GROUP_LOCALIZATION,
'language-support;british-support'            : GROUP_LOCALIZATION,
'language-support;korean-support'             : GROUP_LOCALIZATION,
'language-support;lao-support'                : GROUP_LOCALIZATION,
'language-support;inuktitut-support'          : GROUP_LOCALIZATION,
'language-support;german-support'             : GROUP_LOCALIZATION,
'language-support;hindi-support'              : GROUP_LOCALIZATION,
'language-support;faeroese-support'           : GROUP_LOCALIZATION,
'language-support;swedish-support'            : GROUP_LOCALIZATION,
'language-support;tsonga-support'             : GROUP_LOCALIZATION,
'language-support;russian-support'            : GROUP_LOCALIZATION,
'language-support;serbian-support'            : GROUP_LOCALIZATION,
'language-support;latvian-support'            : GROUP_LOCALIZATION,
'language-support;samoan-support'             : GROUP_LOCALIZATION,
'language-support;sinhala-support'            : GROUP_LOCALIZATION,
'language-support;catalan-support'            : GROUP_LOCALIZATION,
'language-support;lithuanian-support'         : GROUP_LOCALIZATION,
'language-support;turkish-support'            : GROUP_LOCALIZATION,
'language-support;arabic-support'             : GROUP_LOCALIZATION,
'language-support;vietnamese-support'         : GROUP_LOCALIZATION,
'language-support;mongolian-support'          : GROUP_LOCALIZATION,
'language-support;tswana-support'             : GROUP_LOCALIZATION,
'language-support;irish-support'              : GROUP_LOCALIZATION,
'language-support;italian-support'            : GROUP_LOCALIZATION,
'language-support;slovak-support'             : GROUP_LOCALIZATION,
'language-support;slovenian-support'          : GROUP_LOCALIZATION,
'language-support;belarusian-support'         : GROUP_LOCALIZATION,
'language-support;northern-sotho-support'     : GROUP_LOCALIZATION,
'language-support;kannada-support'            : GROUP_LOCALIZATION,
'language-support;malayalam-support'          : GROUP_LOCALIZATION,
'language-support;swati-support'              : GROUP_LOCALIZATION,
'language-support;breton-support'             : GROUP_LOCALIZATION,
'language-support;romanian-support'           : GROUP_LOCALIZATION,
'language-support;greek-support'              : GROUP_LOCALIZATION,
'language-support;tagalog-support'            : GROUP_LOCALIZATION,
'language-support;zulu-support'               : GROUP_LOCALIZATION,
'language-support;tibetan-support'            : GROUP_LOCALIZATION,
'language-support;danish-support'             : GROUP_LOCALIZATION,
'language-support;afrikaans-support'          : GROUP_LOCALIZATION,
'language-support;southern-sotho-support'     : GROUP_LOCALIZATION,
'language-support;bosnian-support'            : GROUP_LOCALIZATION,
'language-support;brazilian-support'          : GROUP_LOCALIZATION,
'language-support;basque-support'             : GROUP_LOCALIZATION,
'language-support;welsh-support'              : GROUP_LOCALIZATION,
'language-support;thai-support'               : GROUP_LOCALIZATION,
'language-support;telugu-support'             : GROUP_LOCALIZATION,
'language-support;low-saxon-support'          : GROUP_LOCALIZATION,
'language-support;urdu-support'               : GROUP_LOCALIZATION,
'language-support;tamil-support'              : GROUP_LOCALIZATION,
'language-support;indonesian-support'         : GROUP_LOCALIZATION,
'language-support;gujarati-support'           : GROUP_LOCALIZATION,
'language-support;xhosa-support'              : GROUP_LOCALIZATION,
'language-support;chinese-support'            : GROUP_LOCALIZATION,
'language-support;czech-support'              : GROUP_LOCALIZATION,
'language-support;venda-support'              : GROUP_LOCALIZATION,
'language-support;bulgarian-support'          : GROUP_LOCALIZATION,
'language-support;albanian-support'           : GROUP_LOCALIZATION,
'language-support;galician-support'           : GROUP_LOCALIZATION,
'language-support;armenian-support'           : GROUP_LOCALIZATION,
'language-support;dutch-support'              : GROUP_LOCALIZATION,
'language-support;oriya-support'              : GROUP_LOCALIZATION,
'language-support;maori-support'              : GROUP_LOCALIZATION,
'language-support;nepali-support'             : GROUP_LOCALIZATION,
'language-support;icelandic-support'          : GROUP_LOCALIZATION,
'language-support;ukrainian-support'          : GROUP_LOCALIZATION,
'language-support;assamese-support'           : GROUP_LOCALIZATION,
'language-support;bengali-support'            : GROUP_LOCALIZATION,
'language-support;spanish-support'            : GROUP_LOCALIZATION,
'language-support;hebrew-support'             : GROUP_LOCALIZATION,
'language-support;estonian-support'           : GROUP_LOCALIZATION,
'language-support;french-support'             : GROUP_LOCALIZATION,
'language-support;croatian-support'           : GROUP_LOCALIZATION,
'language-support;filipino-support'           : GROUP_LOCALIZATION,
'language-support;finnish-support'            : GROUP_LOCALIZATION,
'language-support;norwegian-support'          : GROUP_LOCALIZATION,
'language-support;southern-ndebele-support'   : GROUP_LOCALIZATION,
'language-support;polish-support'             : GROUP_LOCALIZATION,
'language-support;gaelic-support'             : GROUP_LOCALIZATION,
'language-support;marathi-support'            : GROUP_LOCALIZATION,
'language-support;ethiopic-support'           : GROUP_LOCALIZATION,
'language-support;esperanto-support'          : GROUP_LOCALIZATION,
'language-support;northern-sami-support'      : GROUP_LOCALIZATION,
'language-support;macedonian-support'         : GROUP_LOCALIZATION,
'language-support;walloon-support'            : GROUP_LOCALIZATION,
'language-support;kashubian-support'          : GROUP_LOCALIZATION,
'rpmfusion_free;kde-desktop'                  : GROUP_DESKTOP_KDE,
'rpmfusion_free;misc-libs'                    : GROUP_OTHER,
'rpmfusion_free;games'                        : GROUP_GAMES,
'rpmfusion_free;misc-tools'                   : GROUP_LOCALIZATION,
'rpmfusion_free;hardware-support'             : GROUP_ADMIN_TOOLS,
'rpmfusion_free;sound-and-video'              : GROUP_MULTIMEDIA,
'rpmfusion_free;base'                         : GROUP_SYSTEM,
'rpmfusion_free;gnome-desktop'                : GROUP_DESKTOP_GNOME,
'rpmfusion_free;internet'                     : GROUP_INTERNET,
'rpmfusion_free;system-tools'                 : GROUP_SYSTEM,
'rpmfusion_nonfree;games'                     : GROUP_GAMES,
'rpmfusion_nonfree;hardware-support'          : GROUP_SYSTEM,
'rpmfusion_nonfree;base'                      : GROUP_SYSTEM
}

class yumComps:

    def __init__(self,yumbase,db = None):
        self.yumbase = yumbase
        self.cursor = None
        self.connection = None
        if not db:
            db = '/var/cache/yum/packagekit-groups-V2.sqlite'
        self.db = db

    def connect(self):
        ''' connect to database '''
        try:
            # will be created if it does not exist
            self.connection = sqlite.connect(self.db)
            self.cursor = self.connection.cursor()
        except Exception, e:
            print 'cannot connect to database %s: %s' % (self.db,str(e))
            return False

        # test if we can get a group for a common package, create if fail
        try:
            self.cursor.execute('SELECT group_enum FROM groups WHERE name = ?;',['hal'])
        except Exception, e:
            self.cursor.execute('CREATE TABLE groups (name TEXT,category TEXT,groupid TEXT,group_enum TEXT,pkgtype Text);')
            self.refresh()

        return True

    def _add_db(self,name,category,groupid,pkgroup,pkgtype):
        self.cursor.execute('INSERT INTO groups values(?,?,?,?,?);',(name,category,groupid,pkgroup,pkgtype))

    def refresh(self,force=False):
        ''' get the data from yum (slow, REALLY SLOW) '''

        cats = self.yumbase.comps.categories
        if self.yumbase.comps.compscount == 0:
            return False

        # delete old data else we get multiple entries
        self.cursor.execute('DELETE FROM groups;')

        # store to sqlite
        for category in cats:
            grps = map(lambda x: self.yumbase.comps.return_group(x),
               filter(lambda x: self.yumbase.comps.has_group(x),category.groups))
            for group in grps:

                # strip out rpmfusion from the group name
                group_name = group.groupid
                group_name = group_name.replace('rpmfusion_nonfree-','')
                group_name = group_name.replace('rpmfusion_free-','')
                group_id = "%s;%s" % (category.categoryid,group_name)

                group_enum = GROUP_OTHER
                if groupMap.has_key(group_id):
                    group_enum = groupMap[group_id]
                else:
                    print 'unknown group enum',group_id

                for package in group.mandatory_packages:
                    self._add_db(package,category.categoryid,group_name,group_enum,'mandatory')
                for package in group.default_packages:
                    self._add_db(package,category.categoryid,group_name,group_enum,'default')
                for package in group.optional_packages:
                    self._add_db(package,category.categoryid,group_name,group_enum,'optional')

        # write to disk
        self.connection.commit()
        return True

    def get_package_list(self,group_key):
        ''' for a PK group, get the packagelist for this group '''
        all_packages = [];
        self.cursor.execute('SELECT name FROM groups WHERE group_enum = ?;',[group_key])
        for row in self.cursor:
            all_packages.append(row[0])
        return all_packages

    def get_group(self,pkgname):
        ''' return the PackageKit group enum for the package '''
        self.cursor.execute('SELECT group_enum FROM groups WHERE name = ?;',[pkgname])
        group = GROUP_OTHER
        for row in self.cursor:
            group = row[0]

        return group

    def get_meta_packages(self):
        metapkgs = set()
        self.cursor.execute('SELECT groupid FROM groups')
        for row in self.cursor:
            metapkgs.add(row[0])
        return list(metapkgs)



    def get_meta_package_list(self,groupid):
        ''' for a comps group, get the packagelist for this group (mandatory,default)'''
        all_packages = [];
        self.cursor.execute('SELECT name FROM groups WHERE groupid = ? AND ( pkgtype = "mandatory" OR pkgtype = "default");',[groupid])
        for row in self.cursor:
            all_packages.append(row[0])
        return all_packages

    def get_category(self,groupid):
        ''' for a comps group, get the category for a group '''
        category = None
        self.cursor.execute('SELECT category FROM groups WHERE groupid = ?;',[groupid])
        for row in self.cursor:
            category = row[0]
            break
        return category

if __name__ == "__main__":
    import yum
    import os
    yb = yum.YumBase()
    db = "packagekit-groupsV2.sqlite"
    comps = yumComps(yb,db)
    comps.connect()
    comps.refresh()
    print "pk group system"
    print 40 * "="
    pkgs = comps.get_package_list('system')
    print pkgs
    print "comps group games"
    print 40 * "="
    pkgs = comps.get_meta_package_list('games')
    print pkgs
    print "comps group kde-desktop"
    print 40 * "="
    pkgs = comps.get_meta_package_list('kde-desktop')
    print pkgs
    os.unlink(db) # kill the db
