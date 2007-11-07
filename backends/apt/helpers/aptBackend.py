#
# Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
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
        self._cache = backend._apt_cache
        self._depcache = backend._apt_dep_cache
        self._records = backend._apt_records

    @property
    def id(self):
        return self._pkg.ID

    @property
    def name(self):
        return self._pkg.Name

    @property
    def summary(self):
        if not self._seek_records():
            return ""
        ver = self._depcache.GetCandidateVer(self._pkg)
        desc_iter = ver.TranslatedDescription
        self._records.Lookup(desc_iter.FileList.pop(0))
        return self._records.ShortDesc

    @property
    def description(self):
        if not self._seek_records():
            return ""
        # get the translated description
        ver = self._depcache.GetCandidateVer(self._pkg)
        desc_iter = ver.TranslatedDescription
        self._records.Lookup(desc_iter.FileList.pop(0))
        desc = ""
        try:
            s = unicode(self._records.LongDesc,"utf-8")
        except UnicodeDecodeError, e:
            s = _("Invalid unicode in description for '%s' (%s). "
                  "Please report.") % (self.name, e)
        for line in s.splitlines():
                tmp = line.strip()
                if tmp == ".":
                    desc += "\n"
                else:
                    desc += tmp + "\n"
        return desc

    @property
    def architecture(self):
        if not self._seek_records():
            return None
        sec = apt_pkg.ParseSection(self._records.Record)
        if sec.has_key("Architecture"):
            return sec["Architecture"]
        return None

    @property
    def section(self):
        return self._pkg.Section

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
        version = self._pkg.CurrentVer
        if version != None:
            return version.VerStr
        else:
            return None

    @property
    def candidate_version(self):
        version = self._depcache.GetCandidateVer(self._pkg)
        if version != None:
            return version.VerStr
        else:
            return None

    @property
    def is_installed(self):
        return (self._pkg.CurrentVer != None)

    @property
    def is_upgradable(self):
        return self.is_installed and self._depcache.IsUpgradable(self._pkg)

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

    ### Helpers ###
    def _seek_records(self, use_candidate=True):
        if use_candidate:
            version = self._depcache.GetCandidateVer(self._pkg)
        else:
            version = self._pkg.CurrentVer

        # check if we found a version
        if version == None or version.FileList == None:
            return False
        self._records.Lookup(version.FileList.pop(0))
        return True


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
        self._apt_cache = apt_pkg.GetCache(PackageKitProgress(self))
        self._apt_dep_cache = apt_pkg.GetDepCache(self._apt_cache)
        self._apt_records = apt_pkg.GetPkgRecords(self._apt_cache)
        self._apt_list = apt_pkg.GetPkgSourceList()
        self._apt_list.ReadMainList()

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
        lockfile = apt_pkg.Config.FindDir("Dir::State::Lists") + "lock"
        lock = apt_pkg.GetLock(lockfile)
        if lock < 0:
            self.error(ERROR_INTERNAL_ERROR,
                    "Failed to acquire the lock")

        try:
            fetcher = apt_pkg.GetAcquire(PackageKitProgress(self))
            # this can throw a exception
            self._apt_list.GetIndexes(fetcher)
            self._do_fetch(fetcher)
        finally:
            os.close(lock)

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
        size = len(self._apt_cache.Packages)
        percentage = 0
        for i, pkg in enumerate(self._apt_cache.Packages):
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

    def _do_fetch(self, fetcher):
        result = fetcher.Run()
        failed = False
        transient = False
        error_message = ""
        for item in fetcher.Items:
            if item.Status == item.StatDone:
                continue
            if item.StatIdle:
                transient = True
                continue
            error_message += "%s %s\n" % \
                    (item.DescURI, item.ErrorText)
            failed = True

        # we raise a exception if the download failed or it was cancelt
        if failed:
            self.error(ERROR_INTERNAL_ERROR,
                    "Failed to fetch the following items:\n%s" % error_message)
        return (result == fetcher.ResultContinue)

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
        return TRUE

