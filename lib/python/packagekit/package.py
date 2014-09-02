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
#
# Copyright (C) 2008
#    Richard Hughes <richard@hughsie.com>

from packagekit.backend import PackageKitEnum

class PackagekitPackage:

    def get_package_id(self, name, version, arch, data):
        return "%s;%s;%s;%s" % (name, version, arch, data)

    def get_package_from_id(self, package_id):
        ''' split up a package id name;ver;arch;data into a tuple
            containing (name, ver, arch, data)
        '''
        return tuple(package_id.split(';', 4))

    def check_license_field(self, license_field):
        '''
        Check the string license_field for free licenses, indicated by
        their short names as documented at
        http://fedoraproject.org/wiki/Licensing

        Licenses can be grouped by " or " to indicate that the package
        can be redistributed under any of the licenses in the group.
        For instance: GPLv2+ or Artistic or FooLicense.

        Groups of licenses can be grouped with " and " to indicate
        that parts of the package are distributed under one group of
        licenses, while other parts of the package are distributed
        under another group.  Groups may be wrapped in parenthesis.
        For instance:
          (GPLv2+ or Artistic) and (GPL+ or Artistic) and FooLicense.

        At least one license in each group must be free for the
        package to be considered Free Software.  If the license_field
        is empty, the package is considered non-free.
        '''

        groups = license_field.split(" and ")
        f = open('/usr/share/PackageKit/helpers/hif/licenses.txt', 'r')
        free_licenses = f.readlines()

        if len(groups) == 0:
            return False

        one_free_group = False

        for group in groups:
            group = group.replace("(", "")
            group = group.replace(")", "")
            licenses = group.split(" or ")

            group_is_free = False

            for license in licenses:
                license = license.strip()

                if len(license) < 1:
                    continue

                if license in free_licenses:
                    one_free_group = True
                    group_is_free = True
                    break

            if group_is_free == False:
                return False

        if one_free_group == False:
            return False

        return True

