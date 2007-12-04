#
# vim: ts=4 et sts=4
#
# Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
# Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import sys
import os
import re

from packagekit.backend import *
import apt_pkg

import warnings
warnings.filterwarnings(action='ignore', category=FutureWarning)
import apt

_HYPHEN_PATTERN = re.compile(r'(\s|_)+')

class Package(object):
    def __init__(self, pkg, backend):
        self._pkg = pkg

    @property
    def id(self):
        return self._pkg.id

    @property
    def name(self):
        return self._pkg.name

    @property
    def summary(self):
        return self._pkg.summary

    @property
    def description(self):
        return self._pkg.description

    @property
    def architecture(self):
        return self._pkg.architecture

    @property
    def section(self):
        return self._pkg.section

    @property
    def group(self):
        section = self.section.split('/')[-1].lower()
        #if section in ():
        #    return GROUP_ACCESSIBILITY
        if section in ('utils',):
            return "accessories"
        #if section in ():
        #    return GROUP_EDUCATION
        if section in ('games',):
            return "games"
        if section in ('graphics',):
            return "graphics"
        if section in ('net', 'news', 'web', 'comm'):
            return "internet"
        if section in ('editors', 'tex'):
            return "office"
        if section in ('misc',):
            return "other"
        if section in ('devel', 'libdevel', 'interpreters', 'perl', 'python'):
            return "programming"
        if section in ('sound',):
            return "multimedia"
        if section in ('base', 'admin'):
            return "system"
        return "unknown"

    @property
    def installed_version(self):
        return self._pkg.installedVersion

    @property
    def candidate_version(self):
        return self._pkg.candidateVersion

    @property
    def is_installed(self):
        return self._pkg.isInstalled

    @property
    def is_upgradable(self):
        return self._pkg.isUpgradable

    @property
    def is_development(self):
        name = self.name.lower()
        section = self.section.split('/')[-1].lower()
        return name.endswith('-dev') or name.endswith('-dbg') or \
                section in ('devel', 'libdevel')

    @property
    def is_gui(self):
        section = self.section.split('/')[-1].lower()
        return section in ('x11', 'gnome', 'kde')

    def match_name(self, name):
        needle = name.strip().lower()
        haystack = self.name.lower()
        needle = _HYPHEN_PATTERN.sub('-', needle)
        haystack = _HYPHEN_PATTERN.sub('-', haystack)
        if haystack.find(needle) >= 0:
            return True
        return False

    def match_details(self, details):
        if self.match_name(details):
            return True
        needle = details.strip().lower()
        haystack = self.description.lower()
        if haystack.find(needle) >= 0:
            return True
        return False

    def match_group(self, name):
        needle = name.strip().lower()
        haystack = self.group
        if haystack.startswith(needle):
            return True
        return False

class PackageKitProgress(apt.progress.OpProgress, apt.progress.FetchProgress):
    def __init__(self, backend):
        self._backend = backend
        apt.progress.OpProgress.__init__(self)
        apt.progress.FetchProgress.__init__(self)

    # OpProgress callbacks
    def update(self, percent):
        pass

    def done(self):
        pass

    # FetchProgress callbacks
    def pulse(self):
        apt.progress.FetchProgress.pulse(self)
        self._backend.percentage(self.percent)
        return True

    def stop(self):
        self._backend.percentage(100)

    def mediaChange(self, medium, drive):
        self._backend.error(ERROR_INTERNAL_ERROR,
                "Medium change needed")


class PackageKitAptBackend(PackageKitBaseBackend):
    def __init__(self, args):
        PackageKitBaseBackend.__init__(self, args)
        self._apt_cache = apt.Cache(PackageKitProgress(self))

    def search_name(self, filters, key):
        '''
        Implement the {backend}-search-name functionality
        '''
        self.allow_interrupt(True)
        for package in self._do_search(filters,
                lambda pkg: pkg.match_name(key)):
            self._emit_package(package)

    def search_details(self, filters, key):
        '''
        Implement the {backend}-search-details functionality
        '''
        self.allow_interrupt(True)
        for package in self._do_search(filters,
                lambda pkg: pkg.match_details(key)):
            self._emit_package(package)

    def search_group(self, filters, key):
        '''
        Implement the {backend}-search-group functionality
        '''
        self.allow_interrupt(True)
        for package in self._do_search(filters,
                lambda pkg: pkg.match_group(key)):
            self._emit_package(package)

    def search_file(self, filters, key):
        '''
        Implement the {backend}-search-file functionality
        '''
        self.allow_interrupt(True)
        self.percentage(None)

        self.error(ERROR_NOT_SUPPORTED,
                "This function is not implemented in this backend")

    def refresh_cache(self):
        '''
        Implement the {backend}-refresh_cache functionality
        '''
        try:
            res = self._apt_cache.update(PackageKitProgress(self))
        except Exception, error_message:
             self.error(ERROR_INTERNAL_ERROR,
                        "Failed to fetch the following items:\n%s" % error_message)
        return res

    def get_description(self, package):
        '''
        Implement the {backend}-get-description functionality
        '''
        name, version, arch, data = self.get_package_from_id(package)
        pkg = Package(self._apt_cache[name], self)
        description = re.sub('\s+', ' ', pkg.description).strip()
        self.description(package, 'unknown', pkg.group, description, '', 0, '')

    def resolve(self, name):
        '''
        Implement the {backend}-resolve functionality
        '''
        pkg = Package(self._apt_cache[name], self)
        self._emit_package(pkg)

    def get_depends(self,package):
        '''
        Implement the {backend}-get-depends functionality
        '''
        name, version, arch, data = self.get_package_from_id(package)
        pkg = Package(self._apt_cache[name], self)
        print pkg._pkg._pkg.VersionList
        print "ok",self._apt_cache._depcache.SetCandidateVer(pkg._pkg._pkg,pkg._pkg._pkg.VersionList[0])
        print self._apt_cache._depcache.GetCandidateVer(pkg._pkg._pkg),pkg._pkg._pkg.VersionList[0]
        print "mi",pkg._pkg.markInstall(autoFix=False)
        print pkg._pkg._pkg.VersionList
        print "other",self._apt_cache._depcache.SetCandidateVer(pkg._pkg._pkg,pkg._pkg._pkg.VersionList[0])
        print self._apt_cache._depcache.GetCandidateVer(pkg._pkg._pkg),pkg._pkg._pkg.VersionList[0]
        print pkg.candidate_version
        for d in pkg._pkg.candidateDependencies:
            for o in d.or_dependencies:
                dep = Package(self._apt_cache[o.name],self)
        print "changes"
        for x in self._apt_cache.getChanges():
            print x.name,x.candidateVersion
        raise Exception

  ### Helpers ###
    def _emit_package(self, package):
        id = self.get_package_id(package.name,
                package.installed_version or package.candidate_version,
                package.architecture,
                "")
        if package.is_installed:
            status = INFO_INSTALLED
        else:
            status = INFO_AVAILABLE
        summary = package.summary
        self.package(id, status, summary)

    def _do_search(self, filters, condition):
        filters = filters.split(';')
        size = len(self._apt_cache)
        percentage = 0
        for i, pkg in enumerate(self._apt_cache):
            new_percentage = i / float(size) * 100
            if new_percentage - percentage >= 5:
                percentage = new_percentage
                self.percentage(percentage)
            package = Package(pkg, self)
            if package.installed_version is None and \
                    package.candidate_version is None:
                continue
            if not condition(package):
                continue
            if not self._do_filtering(package, filters):
                continue
            yield package
        self.percentage(100)


    def _do_filtering(self, package, filters):
        if len(filters) == 0 or filters == ['none']:
            return True
        if (FILTER_INSTALLED in filters) and (not package.is_installed):
            return False
        if (FILTER_NOT_INSTALLED in filters) and package.is_installed:
            return False
        if (FILTER_GUI in filters) and (not package.is_gui):
            return False
        if (FILTER_NOT_GUI in filters) and package.is_gui:
            return False
        if (FILTER_DEVELOPMENT in filters) and (not package.is_development):
            return False
        if (FILTER_NOT_DEVELOPMENT in filters) and package.is_development:
            return False
        return True

