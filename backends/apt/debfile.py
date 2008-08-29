# Copyright (c) 2005-2007 Canonical
#
# AUTHOR:
# Michael Vogt <mvo@ubuntu.com>
#
# This file is part of GDebi
#
# GDebi is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as published
# by the Free Software Foundation; either version 2 of the License, or (at
# your option) any later version.
#
# GDebi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GDebi; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

import warnings
warnings.filterwarnings("ignore", "apt API not stable yet", FutureWarning)
import apt_inst, apt_pkg
import sys
import os
from gettext import gettext as _

# Constants for comparing the local package file with the version in the cache
(VERSION_NONE,
 VERSION_OUTDATED,
 VERSION_SAME,
 VERSION_NEWER) = range(4)
 
class NoDebArchiveException(IOError):
    pass

class DebPackage(object):

    _supported_data_members = ("data.tar.gz", "data.tar.bz2", "data.tar.lzma")

    debug = 0

    def __init__(self, filename=None, cache=None):
        self._cache = cache
        self.file = filename
        self._needPkgs = []
        self._sections = {}
        self._installedConflicts = set()
        self._failureString = ""
        if filename:
            self.open(filename)

    def open(self, filename):
        " open given debfile "
        self.filename = filename
        if not apt_inst.arCheckMember(open(self.filename), "debian-binary"):
            raise NoDebArchiveException, _("This is not a valid DEB archive, missing '%s' member" % "debian-binary")
        control = apt_inst.debExtractControl(open(self.filename))
        self._sections = apt_pkg.ParseSection(control)
        self.pkgname = self._sections["Package"]

    def __getitem__(self, key):
        return self._sections[key]
        
    def filelist(self):
        """ return the list of files in the deb """
        files = []
        def extract_cb(What,Name,Link,Mode,UID,GID,Size,MTime,Major,Minor):
            #print "%s '%s','%s',%u,%u,%u,%u,%u,%u,%u"\
            #      % (What,Name,Link,Mode,UID,GID,Size, MTime, Major, Minor)
            files.append(Name)
        for member in self._supported_data_members:
            if apt_inst.arCheckMember(open(self.filename), member):
                try:
                    apt_inst.debExtract(open(self.filename), extract_cb, member)
                    break
                except SystemError, e:
                    return [_("List of files for '%s'could not be read" % self.filename)]
        return files
    filelist = property(filelist)

    def _isOrGroupSatisfied(self, or_group):
        """ this function gets a 'or_group' and analyzes if
            at least one dependency of this group is already satisfied """
        self._dbg(2,"_checkOrGroup(): %s " % (or_group))

        for dep in or_group:
            depname = dep[0]
            ver = dep[1]
            oper = dep[2]

            # check for virtual pkgs
            if not self._cache.has_key(depname):
                if self._cache.isVirtualPackage(depname):
                    self._dbg(3,"_isOrGroupSatisfied(): %s is virtual dep" % depname)
                    for pkg in self._cache.getProvidingPackages(depname):
                        if pkg.isInstalled:
                            return True
                continue

            inst = self._cache[depname]
            instver = inst.installedVersion
            if instver != None and apt_pkg.CheckDep(instver,oper,ver) == True:
                return True
        return False
            

    def _satisfyOrGroup(self, or_group):
        """ try to satisfy the or_group """

        or_found = False
        virtual_pkg = None

        for dep in or_group:
            depname = dep[0]
            ver = dep[1]
            oper = dep[2]

            # if we don't have it in the cache, it may be virtual
            if not self._cache.has_key(depname):
                if not self._cache.isVirtualPackage(depname):
                    continue
                providers = self._cache.getProvidingPackages(depname)
                # if a package just has a single virtual provider, we
                # just pick that (just like apt)
                if len(providers) != 1:
                    continue
                depname = providers[0].name
                
            # now check if we can satisfy the deps with the candidate(s)
            # in the cache
            cand = self._cache[depname]
            candver = self._cache._depcache.GetCandidateVer(cand._pkg)
            if not candver:
                continue
            if not apt_pkg.CheckDep(candver.VerStr,oper,ver):
                continue

            # check if we need to install it
            self._dbg(2,"Need to get: %s" % depname)
            self._needPkgs.append(depname)
            return True

        # if we reach this point, we failed
        or_str = ""
        for dep in or_group:
            or_str += dep[0]
            if dep != or_group[len(or_group)-1]:
                or_str += "|"
        self._failureString += _("Dependency is not satisfiable: %s\n" % or_str)
        return False

    def _checkSinglePkgConflict(self, pkgname, ver, oper):
        """ returns true if a pkg conflicts with a real installed/marked
            pkg """
        # FIXME: deal with conflicts against its own provides
        #        (e.g. Provides: ftp-server, Conflicts: ftp-server)
        self._dbg(3, "_checkSinglePkgConflict() pkg='%s' ver='%s' oper='%s'" % (pkgname, ver, oper))
        pkgver = None
        cand = self._cache[pkgname]
        if cand.isInstalled:
            pkgver = cand.installedVersion
        elif cand.markedInstall:
            pkgver = cand.candidateVersion
        #print "pkg: %s" % pkgname
        #print "ver: %s" % ver
        #print "pkgver: %s " % pkgver
        #print "oper: %s " % oper
        if (pkgver and apt_pkg.CheckDep(pkgver,oper,ver) and 
            not self.replacesRealPkg(pkgname, oper, ver)):
            self._failureString += _("Conflicts with the installed package '%s'" % cand.name)
            return True
        return False

    def _checkConflictsOrGroup(self, or_group):
        """ check the or-group for conflicts with installed pkgs """
        self._dbg(2,"_checkConflictsOrGroup(): %s " % (or_group))

        or_found = False
        virtual_pkg = None

        for dep in or_group:
            depname = dep[0]
            ver = dep[1]
            oper = dep[2]

            # check conflicts with virtual pkgs
            if not self._cache.has_key(depname):
                # FIXME: we have to check for virtual replaces here as 
                #        well (to pass tests/gdebi-test8.deb)
                if self._cache.isVirtualPackage(depname):
                    for pkg in self._cache.getProvidingPackages(depname):
                        self._dbg(3, "conflicts virtual check: %s" % pkg.name)
                        # P/C/R on virtal pkg, e.g. ftpd
                        if self.pkgName == pkg.name:
                            self._dbg(3, "conflict on self, ignoring")
                            continue
                        if self._checkSinglePkgConflict(pkg.name,ver,oper):
                            self._installedConflicts.add(pkg.name)
                continue
            if self._checkSinglePkgConflict(depname,ver,oper):
                self._installedConflicts.add(depname)
        return len(self._installedConflicts) != 0

    def getConflicts(self):
        """
        Return list of package names conflicting with this package.

        WARNING: This method will is deprecated. Please use the 
        attribute DebPackage.depends instead.
        """
        return self.conflicts

    def conflicts(self):
        """
        List of package names conflicting with this package
        """
        conflicts = []
        key = "Conflicts"
        if self._sections.has_key(key):
            conflicts = apt_pkg.ParseDepends(self._sections[key])
        return conflicts
    conflicts = property(conflicts)

    def getDepends(self):
        """
        Return list of package names on which this package depends on.

        WARNING: This method will is deprecated. Please use the 
        attribute DebPackage.depends instead.
        """
        return self.depends

    def depends(self):
        """
        List of package names on which this package depends on
        """
        depends = []
        # find depends
        for key in ["Depends","PreDepends"]:
            if self._sections.has_key(key):
                depends.extend(apt_pkg.ParseDepends(self._sections[key]))
        return depends
    depends = property(depends)

    def getProvides(self):
        """
        Return list of virtual packages which are provided by this package.

        WARNING: This method will is deprecated. Please use the 
        attribute DebPackage.provides instead.
        """
        return self.provides

    def provides(self):
        """
        List of virtual packages which are provided by this package
        """
        provides = []
        key = "Provides"
        if self._sections.has_key(key):
            provides = apt_pkg.ParseDepends(self._sections[key])
        return provides
    provides = property(provides)

    def getReplaces(self):
        """
        Return list of packages which are replaced by this package.

        WARNING: This method will is deprecated. Please use the 
        attribute DebPackage.replaces instead.
        """
        return self.replaces

    def replaces(self):
        """
        List of packages which are replaced by this package
        """
        replaces = []
        key = "Replaces"
        if self._sections.has_key(key):
            replaces = apt_pkg.ParseDepends(self._sections[key])
        return replaces
    replaces = property(replaces)

    def replacesRealPkg(self, pkgname, oper, ver):
        """ 
        return True if the deb packages replaces a real (not virtual)
        packages named pkgname, oper, ver 
        """
        self._dbg(3, "replacesPkg() %s %s %s" % (pkgname,oper,ver))
        pkgver = None
        cand = self._cache[pkgname]
        if cand.isInstalled:
            pkgver = cand.installedVersion
        elif cand.markedInstall:
            pkgver = cand.candidateVersion
        for or_group in self.getReplaces():
            for (name, ver, oper) in or_group:
                if (name == pkgname and 
                    apt_pkg.CheckDep(pkgver,oper,ver)):
                    self._dbg(3, "we have a replaces in our package for the conflict against '%s'" % (pkgname))
                    return True
        return False

    def checkConflicts(self):
        """ check if the pkg conflicts with a existing or to be installed
            package. Return True if the pkg is ok """
        res = True
        for or_group in self.getConflicts():
            if self._checkConflictsOrGroup(or_group):
                #print "Conflicts with a exisiting pkg!"
                #self._failureString = "Conflicts with a exisiting pkg!"
                res = False
        return res

   
    def compareToVersionInCache(self, useInstalled=True):
        """ checks if the pkg is already installed or availabe in the cache
            and if so in what version, returns if the version of the deb
            is not available,older,same,newer
        """
        self._dbg(3,"compareToVersionInCache")
        pkgname = self._sections["Package"]
        debver = self._sections["Version"]
        self._dbg(1,"debver: %s" % debver)
        if self._cache.has_key(pkgname):
            if useInstalled:
                cachever = self._cache[pkgname].installedVersion
            else:
                cachever = self._cache[pkgname].candidateVersion
            if cachever != None:
                cmp = apt_pkg.VersionCompare(cachever,debver)
                self._dbg(1, "CompareVersion(debver,instver): %s" % cmp)
                if cmp == 0:
                    return VERSION_SAME
                elif cmp < 0:
                    return VERSION_NEWER
                elif cmp > 0:
                    return VERSION_OUTDATED
        return VERSION_NONE

    def checkDeb(self):
        self._dbg(3,"checkDepends")

        # check arch
        arch = self._sections["Architecture"]
        if  arch != "all" and arch != apt_pkg.Config.Find("APT::Architecture"):
            self._dbg(1,"ERROR: Wrong architecture dude!")
            self._failureString = _("Wrong architecture '%s'" % arch)
            return False

        # check version
        res = self.compareToVersionInCache()
        if res == VERSION_OUTDATED: # the deb is older than the installed
            self._failureString = _("A later version is already installed")
            return False

        # FIXME: this sort of error handling sux
        self._failureString = ""

        # check conflicts
        if not self.checkConflicts():
            return False

        # try to satisfy the dependencies
        res = self._satisfyDepends(self.getDepends())
        if not res:
            return False

        # check for conflicts again (this time with the packages that are
        # makeed for install)
        if not self.checkConflicts():
            return False

        if self._cache._depcache.BrokenCount > 0:
            self._failureString = _("Failed to satisfy all dependencies (broken cache)")
            # clean the cache again
            self._cache.clear()
            return False
        return True

    def satisfyDependsStr(self, dependsstr):
        return self._satisfyDepends(apt_pkg.ParseDepends(dependsstr))

    def _satisfyDepends(self, depends):
        # turn off MarkAndSweep via a action group (if available)
        try:
            _actiongroup = apt_pkg.GetPkgActionGroup(self._cache._depcache)
        except AttributeError, e:
            pass
        # check depends
        for or_group in depends:
            #print "or_group: %s" % or_group
            #print "or_group satified: %s" % self._isOrGroupSatisfied(or_group)
            if not self._isOrGroupSatisfied(or_group):
                if not self._satisfyOrGroup(or_group):
                    return False
        # now try it out in the cache
            for pkg in self._needPkgs:
                try:
                    self._cache[pkg].markInstall(fromUser=False)
                except SystemError, e:
                    self._failureString = _("Cannot install '%s'" % pkg)
                    self._cache.clear()
                    return False
        return True

    def missingDeps(self):
        self._dbg(1, "Installing: %s" % self._needPkgs)
        if self._needPkgs == None:
            self.checkDeb()
        return self._needPkgs
    missingDeps = property(missingDeps)

    def requiredChanges(self):
        """ gets the required changes to satisfy the depends.
            returns a tuple with (install, remove, unauthenticated)
        """
        install = []
        remove = []
        unauthenticated = []
        for pkg in self._cache:
            if pkg.markedInstall or pkg.markedUpgrade:
                install.append(pkg.name)
                # check authentication, one authenticated origin is enough
                # libapt will skip non-authenticated origins then
                authenticated = False
                for origin in pkg.candidateOrigin:
                    authenticated |= origin.trusted
                if not authenticated:
                    unauthenticated.append(pkg.name)
            if pkg.markedDelete:
                remove.append(pkg.name)
        return (install,remove, unauthenticated)
    requiredChanges = property(requiredChanges)

    def _dbg(self, level, msg):
        """Write debugging output to sys.stderr.
        """
        if level <= self.debug:
            print >> sys.stderr, msg

    def install(self, installProgress=None):
        """ Install the package """
        if installProgress == None:
            res = os.system("/usr/sbin/dpkg -i %s" % self.filename)
        else:
            installProgress.startUpdate()
            res = installProgress.run(self.filename)
            installProgress.finishUpdate()
        return res

class DscSrcPackage(DebPackage):
    def __init__(self, filename=None, cache=None):
        DebPackage.__init__(self, filename, cache)
        self.depends = []
        self.conflicts = []
        self.binaries = []
        if filename != None:
            self.open(filename)
    def getConflicts(self):
        return self.conflicts
    def getDepends(self):
        return self.depends
    def open(self, file):
        depends_tags = ["Build-Depends:", "Build-Depends-Indep:"]
        conflicts_tags = ["Build-Conflicts:", "Build-Conflicts-Indep:"]
        for line in open(file):
            # check b-d and b-c
            for tag in depends_tags:
                if line.startswith(tag):
                    key = line[len(tag):].strip()
                    self.depends.extend(apt_pkg.ParseSrcDepends(key))
            for tag in conflicts_tags:
                if line.startswith(tag):
                    key = line[len(tag):].strip()
                    self.conflicts.extend(apt_pkg.ParseSrcDepends(key))
            # check binary and source and version
            if line.startswith("Source:"):
                self.pkgName = line[len("Source:"):].strip()
            if line.startswith("Binary:"):
                self.binaries = [pkg.strip() for pkg in line[len("Binary:"):].split(",")]
            if line.startswith("Version:"):
                self._sections["Version"] = line[len("Version:"):].strip()
            # we are at the end 
            if line.startswith("-----BEGIN PGP SIGNATURE-"):
                break
        s = _("Install Build-Dependencies for "
              "source package '%s' that builds %s\n"
              ) % (self.pkgName, " ".join(self.binaries))
        self._sections["Description"] = s
        
    def checkDeb(self):
        if not self.checkConflicts():
            for pkgname in self._installedConflicts:
                if self._cache[pkgname]._pkg.Essential:
                    raise Exception, _("A essential package would be removed")
                self._cache[pkgname].markDelete()
        # FIXME: a additional run of the checkConflicts()
        #        after _satisfyDepends() should probably be done
        return self._satisfyDepends(self.depends)

if __name__ == "__main__":
    from cache import Cache
    from progress import DpkgInstallProgress

    cache = Cache()

    vp = "www-browser"
    print "%s virtual: %s" % (vp,cache.isVirtualPackage(vp))
    providers = cache.getProvidingPackages(vp)
    print "Providers for %s :" % vp
    for pkg in providers:
        print " %s" % pkg.name
    
    d = DebPackage(sys.argv[1], cache)
    print "Deb: %s" % d.pkgname
    if not d.checkDeb():
        print "can't be satified"
        print d._failureString
    print "missing deps: %s" % d.missingDeps
    print d.requiredChanges

    print "Installing ..."
    ret = d.install(DpkgInstallProgress())
    print ret

    #s = DscSrcPackage(cache, "../tests/3ddesktop_0.2.9-6.dsc")
    #s.checkDep()
    #print "Missing deps: ",s.missingDeps
    #print "Print required changes: ", s.requiredChanges

    s = DscSrcPackage(cache=cache)
    d = "libc6 (>= 2.3.2), libaio (>= 0.3.96) | libaio1 (>= 0.3.96)"
    print s._satisfyDepends(apt_pkg.ParseDepends(d))
