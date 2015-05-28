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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>

from packagekit.enums import *
import sqlite3
import os
import yum

__DB_VER__ = '1'
class yumComps:

    def __init__(self, yumbase, db = None):
        self.yumbase = yumbase
        self.cursor = None
        self.connection = None
        if not db:
            db = '/var/cache/PackageKit/groups.sqlite'
        self.db = db

        # ensure the directory exists
        dirname = os.path.dirname(db)
        if not os.path.isdir(dirname):
            os.makedirs(dirname)

        # load the group map
        self.groupMap = {}
        mapping = open('/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf', 'r')
        lines = mapping.readlines()
        for line in lines:
            line = line.replace('\n', '')

            # blank line
            if len(line) == 0:
                continue

            # fonts=base-system;fonts,base-system;legacy-fonts
            split = line.split('=')
            if len(split) < 2:
                continue

            entries = split[1].split(',')
            for entry in entries:
                self.groupMap[entry] = split[0]

    def connect(self):
        ''' connect to database '''
        try:
            # will be created if it does not exist
            self.connection = sqlite3.connect(self.db)
            self.cursor = self.connection.cursor()
        except Exception, e:
            print 'cannot connect to database %s: %s' % (self.db, str(e))
            return False

        try:
            version = None
            # Get the current database version
            self.cursor.execute('SELECT version FROM version')
            for row in self.cursor:
                version = str(row[0])
                break
            # Check if we have the right DB version
            if not version or version != __DB_VER__:
                print "Wrong database versions : %s needs %s " % (version, __DB_VER__)
                self._make_database_tables()
        except Exception, e:
            # We couldn't get the version, so create a new database
            self._make_database_tables()

        return True

    def _make_database_tables(self):
        ''' Setup a database for yum category and group information'''
        try: # kill the old db
            self.connection.close()
            os.unlink(self.db) # kill the db
            self.connection = sqlite3.connect(self.db)
            self.cursor = self.connection.cursor()
        except Exception, e:
            print e
        else:
            self.cursor.execute('CREATE TABLE groups (name TEXT, category TEXT, groupid TEXT, group_enum TEXT, pkgtype Text);')
            self.cursor.execute('CREATE TABLE version (version TEXT);')
            self.cursor.execute('INSERT INTO version values(?);', __DB_VER__)
            self.connection.commit()
            self.refresh()

    def _add_db(self, name, category, groupid, pkgroup, pkgtype):
        ''' add an item into the database '''
        self.cursor.execute('INSERT INTO groups values(?, ?, ?, ?, ?);', (name, category, groupid, pkgroup, pkgtype))

    def refresh(self, force=False):
        ''' get the data from yum (slow, REALLY SLOW) '''
        try:
            cats = self.yumbase.comps.categories
        except yum.Errors.RepoError, e:
            return False
        except Exception, e:
            return False
        if self.yumbase.comps.compscount == 0:
            return False

        # delete old data else we get multiple entries
        self.cursor.execute('DELETE FROM groups;')

        # store to sqlite
        for category in cats:
            grps = map(lambda x: self.yumbase.comps.return_group(x),
               filter(lambda x: self.yumbase.comps.has_group(x), category.groups))
            self._add_groups_to_db(grps, category.categoryid)

        # write to disk
        self.connection.commit()
        print "Non Categorized groups"
        self._add_non_catagorized_groups()
        self.connection.commit()
        return True

    def _add_groups_to_db(self, grps, cat_id):
        for group in grps:
            # strip out rpmfusion from the group name
            group_name = group.groupid
            group_name = group_name.replace('rpmfusion_nonfree-', '')
            group_name = group_name.replace('rpmfusion_free-', '')
            group_id = "%s;%s" % (cat_id, group_name)

            group_enum = GROUP_OTHER
            if self.groupMap.has_key(group_id):
                group_enum = self.groupMap[group_id]
            else:
                print 'unknown group enum', group_id

            for package in group.mandatory_packages:
                self._add_db(package, cat_id, group_name, group_enum, 'mandatory')
            for package in group.default_packages:
                self._add_db(package, cat_id, group_name, group_enum, 'default')
            for package in group.optional_packages:
                self._add_db(package, cat_id, group_name, group_enum, 'optional')


    def _add_non_catagorized_groups(self):
        to_add = []
        # Go through all the groups
        for grp in self.yumbase.comps.groups:
            # check if it is already added to the db
            if self.get_category(grp.groupid):
                continue
            elif grp.user_visible:
                to_add.append(grp)
        self._add_groups_to_db(to_add, "other")


    def get_package_list(self, group_key):
        ''' for a PK group, get the packagelist for this group '''
        all_packages = []
        self.cursor.execute('SELECT name FROM groups WHERE group_enum = ?;', [group_key])
        for row in self.cursor:
            all_packages.append(row[0])
        return all_packages

    def get_group(self, pkgname):
        ''' return the PackageKit group enum for the package '''
        self.cursor.execute('SELECT group_enum FROM groups WHERE name = ?;', [pkgname])
        group = GROUP_OTHER
        for row in self.cursor:
            group = row[0]

        return group

    def get_meta_packages(self):
        ''' return all the group_id's '''
        metapkgs = set()
        self.cursor.execute('SELECT groupid FROM groups')
        for row in self.cursor:
            metapkgs.add(row[0])
        return list(metapkgs)

    def get_meta_package_list(self, groupid):
        ''' for a comps group, get the packagelist for this group (mandatory, default)'''
        all_packages = []
        self.cursor.execute('SELECT name FROM groups WHERE groupid = ? ;', [groupid])
        for row in self.cursor:
            all_packages.append(row[0])
        return all_packages

    def get_category(self, groupid):
        ''' for a comps group, get the category for a group '''
        category = None
        self.cursor.execute('SELECT category FROM groups WHERE groupid = ?;', [groupid])
        for row in self.cursor:
            category = row[0]
            break
        return category

    def get_groups(self, cat_id):
        grps = set()
        self.cursor.execute('SELECT groupid FROM groups WHERE category = ?', [cat_id])
        for row in self.cursor:
            grps.add(row[0])
        return list(grps)

