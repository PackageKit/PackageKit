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
#
# Copyright (C) 2007 Elliot Peele <elliot@bentlogic.net>
# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>

from conary import callbacks
from packagekit.backend import *
from pkConaryLog import log

class UpdateCallback(callbacks.UpdateCallback):
    def resolvingDependencies(self):
        #self.backend.status('Resolving Dependencies')
        self.backend.status(STATUS_DEP_RESOLVE)

    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        log.info("Callback ........ STATUS_ROLLBACK ")
        self.backend.status(STATUS_ROLLBACK)

    def committingTransaction(self):
        #self.backend.status('Committing Transaction')
        log.info("Callback ........ STATUS_COMMIT ")

        self.backend.status(STATUS_COMMIT)

    def downloadingFileContents(self, got, need):
        #self.backend.status('Downloading files for changeset')
        log.info("Callback ........ STATUS_DOWNLOAD ")
        self.backend.status(STATUS_DOWNLOAD)

    def downloadingChangeSet(self, got, need):
        log.info("Callback ........ STATUS_DOWNLOAD  changeset")
        self.backend.status(STATUS_DOWNLOAD)

    def requestingFileContents(self):
        #self.backend.status('Requesting File Contents')
        log.info("Callback ........ STATUS_REQUEST ")
        self.backend.status(STATUS_REQUEST)

    def requestingChangeSet(self):
        #self.backend.status('Requesting Changeset')
        log.info("Callback ........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_REQUEST)

    def removeFiles(self, filenum, total):
        log.info("Callback ........ STATUS_REMOVE")
        self.backend.status(STATUS_REMOVE)
        self.preparingUpdate(filenum, total, add=total)
    def done(self):
        #self.backend.status('Done')

        log.info("Callback ........ done! ")
        pass

    def preparingUpdate(self, troveNum, troveCount, add=0):
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return

        if troveNum > 0 and troveCount > 0:
            sub_percent = (add + troveNum) / (2 * float(troveCount)) * 100
            self.backend.sub_percentage(sub_percent)

            if self.smallUpdate:
                self.backend.percentage(sub_percent)

        if troveNum != 0:
            troveNum -= 1

        job = self.currentJob[troveNum]
        name = job[0]
        oldVersion, oldFlavor = job[1]
        newVersion, newFlavor = job[2]
        log.info((oldVersion, newVersion))
        if oldVersion and newVersion:
            log.info("Callback ........ STATUS_UPDATE ")
            self.backend.status(STATUS_UPDATE)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_UPDATING, '')
        elif oldVersion and not newVersion:
            log.info("Callback ........ STATUS_REMOVE ")
            self.backend.status(STATUS_REMOVE)
            package_id = self.backend.get_package_id(name, oldVersion, oldFlavor)
            self.backend.package(package_id, INFO_REMOVING, '')
        elif not oldVersion and newVersion:
            log.info("Callback ........ STATUS_INSTALL ")
            self.backend.status(STATUS_INSTALL)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_INSTALLING, '')

    def creatingDatabaseTransaction(self, troveNum, troveCount):
        self.preparingUpdate(troveNum, troveCount, add=troveCount)

    def setChangesetHunk(self, num, total):
        pass

    def setUpdateHunk(self, hunk, hunkCount):
        if hunk > 0 and hunkCount > 0:
            percentage = hunk / float(hunkCount) * 100.0
            self.backend.percentage(percentage)
        else:
            self.smallUpdate = True

    def setUpdateJob(self, job):
        self.currentJob = job

    def updateDone(self):
        self.currentJob = None

    def tagHandlerOutput(self, tag, msg, stderr = False):
        pass

    def troveScriptOutput(self, typ, msg):
        pass

    def troveScriptFailure(self, typ, errcode):
        pass

    def __init__(self, backend, cfg=None):
        callbacks.UpdateCallback.__init__(self)
        log.info("==== callback ==== ")
        if cfg:
            self.setTrustThreshold(cfg.trustThreshold)

        self.backend = backend
        self.currentJob = None
        self.smallUpdate = False
