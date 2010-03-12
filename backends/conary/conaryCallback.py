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
#
# Copyright (C) 2007 Elliot Peele <elliot@bentlogic.net>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
from conary import callbacks
from packagekit.backend import *
from conaryProgress import PackagekitProgress
from pkConaryLog import log

MEGA = 1048576.0

class BasePKConaryCallback(callbacks.UpdateCallback):
    def __init__(self, backend, cfg=None):
        callbacks.UpdateCallback.__init__(self)
        log.info("==== callback ==== ")
        if cfg:
            self.setTrustThreshold(cfg.trustThreshold)

        self.backend = backend
        self.currentJob = None
        self.smallUpdate = False
        self.error = []
        self.progress = PackagekitProgress()
        self.disablepercent = False
        self.dostep = True

    def requestingChangeSet(self):
        self.backend.status(STATUS_REQUEST)
        log.info("[STATUS_REQUEST] Requesting ChangeSet")

    def downloadingChangeSet(self, got, need):
        self.backend.status(STATUS_DOWNLOAD)
        log.info("[STATUS_DOWNLOAD] Downloading ChangeSet (%.2f%% %.2f out of %.2f MiB)" \
                % ( got*100/float(need), got/MEGA,need/MEGA) )
        self.progress.set_subpercent( got*100 / float(need) )
        self.backend.percentage( self.progress.percent )
        if got >= need:
            self.dostep = True

    def resolvingDependencies(self):
        self.backend.status(STATUS_DEP_RESOLVE)
        log.info("[STATUS_DEP_RESOLVE] Resolving Dependencies")
        self.backend.percentage(self.progress.percent)

    def setChangesetHunk(self, num, total):
        log.info("Changeset Hunk %d out of %d" % (num, total) )
        if total > 0:
            p = num*100/float(total)
        else:
            p = 0
        self.progress.set_subpercent(p)
        self.disablepercent = True
        self.backend.percentage(self.progress.percent)
        if num == total:
            self.dostep = True

    def setUpdateHunk(self, num, total):
        log.info("Update Hunk %d out of %d" % (num, total ) )
        if self.dostep:
            self.disablepercent = True
            self.dostep = False

        if num < total:
            p = num*100/float(total)
            self.progress.set_subpercent(p)
        else:
            self.smallUpdate = True
        self.backend.percentage(self.progress.percent)

    def setUpdateJob(self, job):
        self.currentJob = job
        self.backend.percentage(self.progress.percent)
        log.info("callback UpdateSystem. setUpdateJob")
        log.info(self.progress.percent)

    def creatingRollback(self):
        # Don't do anything unless we actually rollback
        pass

    def preparingUpdate(self, troveNum, troveCount, add=0):
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return

        self.backend.percentage(self.progress.percent)
        job = self.currentJob[troveNum-1]
        name = job[0]
        oldVersion, oldFlavor = job[1]
        newVersion, newFlavor = job[2]
        if oldVersion and newVersion:
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            log.info("Preparing Update %d out of %d: %s" % (troveNum, troveCount, package_id))
            self.backend.status(STATUS_UPDATE)
            self.backend.package(package_id, INFO_UPDATING, '')
        elif oldVersion and not newVersion:
            package_id = self.backend.get_package_id(name, oldVersion, oldFlavor)
            log.info("Preparing Remove %d out of %d: %s" % (troveNum, troveCount, package_id))
            self.backend.status(STATUS_REMOVE)
            self.backend.package(package_id, INFO_REMOVING, '')
        elif not oldVersion and newVersion:
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            log.info("Preparing Install %d out of %d: %s" % (troveNum, troveCount, package_id))
            self.backend.status(STATUS_INSTALL)
            self.backend.package(package_id, INFO_INSTALLING, '')

    def creatingDatabaseTransaction(self, troveNum, troveCount):
        log.info("Creating Database Transaction %d out of %d" % (troveNum, troveCount))
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    def committingTransaction(self):
        log.info('[STATUS_COMMIT] Committing Transaction')
        self.backend.status(STATUS_COMMIT)
        self.backend.percentage(self.progress.percent)

    def updateDone(self):
        log.info("Update Done")
        self.backend.percentage(self.progress.percent)
        self.currentJob = None
        log.info(self.progress.percent)

    def downloadingFileContents(self, got, need):
        log.info("[STATUS_DOWNLOAD] Downloading File Contents (%.2f%% %.2f out of %.2f MiB)" \
                % ( got*100/float(need), got/MEGA,need/MEGA) )
        self.backend.status(STATUS_DOWNLOAD)
        #self.backend.sub_percentage(got*100/need)

    def requestingFileContents(self):
        log.info("[STATUS_REQUEST] Requesting File Contents")
        self.backend.status(STATUS_REQUEST)

    def removeFiles(self, filenum, total):
        log.info("[STATUS_REMOVE] %s out of %s file(s)" %( filenum, total) )
        self.backend.status(STATUS_REMOVE)

    def done(self):
        log.info("Done.")
 
    def warning(self, msg, *args, **kwargs):
        e = msg %args
        log.warning(e)

    def tagHandlerOutput(self, tag, msg, stderr = False):
        log.info("Tag Handler Output: [%s] %s" % (tag, msg)) 

    def troveScriptOutput(self, typ, msg):
        log.info("Trove Script Output [%s] %s" % (typ, msg)) 


class UpdateSystemCallback(BasePKConaryCallback):

    def __init__(self, backend, cfg=None):
        BasePKConaryCallback.__init__(self, backend, cfg)
        self.progress.set_steps([ 30,60 ]  )

class GetUpdateCallback(BasePKConaryCallback):

    def __init__(self, backend, cfg=None):
        BasePKConaryCallback.__init__(self, backend, cfg)
        self.progress.set_steps([1,50, 100 ]  )

class UpdateCallback(BasePKConaryCallback):
    def __init__(self, backend, cfg=None):
        BasePKConaryCallback.__init__(self, backend, cfg)
        self.progress.set_steps([ 
            1, # requestingChangeSet 1
            50, # resolveDeps2
            51, # SetChangesetHunk3
            52, # requestingChangeSet4
            80,# setUpdateHUnk5
            81,# setUpdateJob6
            ]  )

class RemoveCallback(BasePKConaryCallback):
    def __init__(self, backend, cfg=None):
        BasePKConaryCallback.__init__(self, backend, cfg)
        self.progress.set_steps([ 2,5,7,8,90,100 ]  )
