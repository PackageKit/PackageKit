#!/usr/bin/python
#
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
# (c) 2008
#     Tim Lauridsen <timlau@fedoraproject.org>

# Misc classes and funtions
import sys

def _isunicode(obj):
    if sys.hexversion >= 0x3000000:
        return isinstance(obj, str)
    else:
        return isinstance(obj, unicode)

def _israwstring(obj):
    if sys.hexversion >= 0x3000000:
        return isinstance(obj, bytes)
    else:
        return isinstance(obj, str)

def _to_unicode(obj, encoding="utf-8"):
    # string/unicode support
    if _isunicode(obj):
        return obj
    if not _israwstring(obj):
        # don't touch non-str, non-unicode objects
        return obj

    if hasattr(obj, 'decode'):
        return obj.decode(encoding, errors="replace")
    else:
        if sys.hexversion >= 0x3000000:
            return str(obj, encoding, errors="replace")
        else:
            return unicode(obj, encoding, errors="replace")

def _to_rawstring(obj, from_encoding="utf-8"):
    if _israwstring(obj):
        return obj
    return obj.encode(from_encoding, errors="replace")


class PackageKitPackage:
    '''
    container class from values from the Package signal
    '''
    def __init__(self, info, package_id, summary):
        self.installed = (info == 'installed')
        self.id = _to_rawstring(package_id)
        self.summary = _to_unicode(summary)
        n,v,a,r = self.id.split(';')
        self.name = n
        self.ver = v
        self.arch = a
        self.repoid = r
        self.summary = _to_unicode(summary)
        self.info = _to_rawstring(info)

    def __str__(self):
        (name, ver, arch, repo) = tuple(self.id.split(";"))
        p =  "%s-%s.%s" % (name, ver, arch)
        return p

class PackageKitDistroUpgrade:
    '''
    container class from values from the DistroUpgrade signal
    '''
    def __init__(self, upgrade_type, name, summary):
        self.upgrade_type = upgrade_type
        self.name = _to_rawstring(name)
        self.summary = _to_unicode(summary)

    def __str__(self):
        return " type : %s, name : %s, summary : %s " % (
                self.upgrade_type, self.name, self.summary)

class PackageKitDetails:
    '''
    container class from values from the Detail signal
    '''
    def __init__(self, package_id, package_license, group, detail, url, size):
        self.id = _to_rawstring(package_id)
        self.license = _to_rawstring(package_license)
        self.group = _to_rawstring(group)
        self.detail = _to_unicode(detail)
        self.url = _to_rawstring(url)
        self.size = int(size)

class PackageKitUpdateDetails:
    '''
    container class from values from the UpdateDetail signal
    '''
    def __init__(self, package_id, updates, obsoletes, vendor_url, bugzilla_url, \
                 cve_url, restart, update_text, changelog, state, \
                 issued, updated):
        self.id = _to_rawstring(package_id)
        self.updates = _to_rawstring(updates)
        self.obsoletes = _to_rawstring(obsoletes)
        self.vendor_url = _to_rawstring(vendor_url)
        self.bugzilla_url = _to_rawstring(bugzilla_url)
        self.cve_url = _to_rawstring(cve_url)
        self.restart = (restart == 'yes')
        self.update_text = _to_unicode(update_text)
        self.changelog = _to_unicode(changelog)
        self.state = _to_rawstring(state)
        self.issued = _to_rawstring(issued)
        self.updated = _to_rawstring(updated)

class PackageKitRepos:
    '''
    container class from values from the Repos signal
    '''
    def __init__(self, repo_id, description, enabled):
        self.id = _to_rawstring(repo_id)
        self.description = _to_unicode(description)
        self.enabled = (enabled == 'yes')

class PackageKitFiles:
    '''
    container class from values from the Files signal
    '''
    def __init__(self, package_id, files):
        self.id = _to_rawstring(package_id)
        self.files = _to_rawstring(files)

class PackageKitCategory:
    '''
    container class from values from the Category signal
    '''
    def __init__(self, parent_id, cat_id, name, summary, icon):
        self.parent_id = _to_rawstring(parent_id)
        self.cat_id = _to_rawstring(cat_id)
        self.name = _to_unicode(name)
        self.summary = _to_unicode(summary)
        self.icon = _to_rawstring(icon)

class PackageKitMessage:
    '''container class from values from the Message signal'''
    def __init__(self, code, details):
        self.code = code
        self.details = details
