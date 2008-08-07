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

# imports
from pysqlite2 import dbapi2 as sqlite
import yum

# simple object that we can use when we use sqlite directly
class PackageObjectSimple(object):
    def __init__(self):
        self.name = None
        self.epoch = None
        self.version = None
        self.release = None
        self.arch = None
        self.repo = None
        self.summary = None
        self.sourcerpm = None
        self.requires = []
        self.pkgKey = 0

# use direct access for speed, for more details see
# https://bugzilla.redhat.com/show_bug.cgi?id=453356
class YumDirectSQL(object):
    def __init__(self,yumbase):
        ''' connect to all enabled repos '''
        self.repo_list = []
        self.cursor_list = []
        for repo in yumbase.repos.repos.values():
            if repo.isEnabled():
                database = '/var/cache/yum/' + repo.id + '/primary.sqlite'
                try:
                    connection = sqlite.connect(database)
                    cursor = connection.cursor()
                    self.repo_list.append((repo.id,connection))
                    self.cursor_list.append((cursor,repo.id))
                except Exception, e:
                    print "cannot connect to database %s: %s" % (database,str(e))

    def resolve(self,package):
        ''' resolve a name like "hal" into a list of pkgs '''
        pkgs = []
        for cursor,repoid in self.cursor_list:
            cursor.execute('SELECT name,epoch,version,release,arch,summary,rpm_sourcerpm,pkgKey FROM packages WHERE name = ?',[package])
            for row in cursor:
                pkg = PackageObjectSimple()
                pkg.name = row[0]
                pkg.epoch = row[1]
                pkg.version = row[2]
                pkg.release = row[3]
                pkg.arch = row[4]
                pkg.repo = repoid
                pkg.summary = row[5]
                pkg.sourcerpm = row[6]
                pkg.pkgKey = row[7]

                # get the requires
                cursor.execute('SELECT name FROM requires WHERE pkgKey = ?',[pkg.pkgKey])
                for row in cursor:
                    pkg.requires.append(row[0])

                pkgs.append(pkg)
        return pkgs

    def close(self):
        ''' close all database connections '''
        for repo_id,repo_connection in self.repo_list:
            repo_connection.close()

