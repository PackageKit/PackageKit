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
from packagekit.progress import PackagekitProgress
from pkConaryLog import log

class UpdateCallback(callbacks.UpdateCallback):
    # 3P  >>> ( prepare Update end )
    def resolvingDependencies(self):
        #self.backend.status('Resolving Dependencies')
        log.info("Callback ........ STATUS_DEP_RESOLVE ")
        self.backend.status(STATUS_DEP_RESOLVE)
        self.progress.step()
    #5A >> status_install  preparing Update
    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        log.info("Callback ........ STATUS_ROLLBACK  ")
        self.backend.status(STATUS_ROLLBACK)
    # 7A >> update done
    def committingTransaction(self):
        #self.backend.status('Committing Transaction')
        log.info("Callback ........ STATUS_COMMIT  transactions ")

        self.backend.status(STATUS_COMMIT)

    def downloadingFileContents(self, got, need):
        #self.backend.status('Downloading files for changeset')
        log.info("Callback ........ STATUS_DOWNLOAD  FIle Contents %s " %  str( got*100/need  ))
        self.backend.status(STATUS_DOWNLOAD)
        #self.backend.sub_percentage(got*100/need)
    # 2P >> dep_resolve
    # 2 A >>> set Update Hunk
    def downloadingChangeSet(self, got, need):
        self.backend.status(STATUS_DOWNLOAD)
        self.progress.set_subpercent( got*100 / float(need) )

        p = self.progress.percent
        self.backend.sub_percentage(p)
        log.info("Callback ........ STATUS_DOWNLOAD  Changeset %s" % p )

    def requestingFileContents(self):
        #self.backend.status('Requesting File Contents')
        log.info("Callback ........ STATUS_REQUEST request File contents ")
        self.backend.status(STATUS_REQUEST)
    # 1(P)repare  >> download a change set
    # 1(A)pply >> donwload a changeset
    def requestingChangeSet(self):
        log.info("Callback ........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_REQUEST)
        self.progress.step()

    def removeFiles(self, filenum, total):
        log.info("Callback ........ STATUS_REMOVE %s/%sfiles" %( filenum, total) )
        self.backend.status(STATUS_REMOVE)
        self.preparingUpdate(filenum, total, add=total)

    def done(self):
        #self.backend.status('Done')
        log.info("DONEEEEEEEEEEEE")
        """
        e = ""
        for i in self.error:
            e = e + i
            log.error(i)
        if self.error:
            self.backend.error(ERROR_DEP_RESOLUTION_FAILED, e)
        """
    # 6 A >>> transactions
    def preparingUpdate(self, troveNum, troveCount, add=0):
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return

        if troveNum > 0 and troveCount > 0:
            sub_percent = (add + troveNum) / (2 * float(troveCount)) * 100
            self.progress.set_subpercent(sub_percent)
            p = self.progress.percent
            self.backend.sub_percentage(p)

            if self.smallUpdate:
                self.backend.percentage(self.progress.percent)

        if troveNum != 0:
            troveNum -= 1

        job = self.currentJob[troveNum]
        name = job[0]
        oldVersion, oldFlavor = job[1]
        newVersion, newFlavor = job[2]
        #log.info("JOB>>>>>>>> %s " % str(job) )
        if oldVersion and newVersion:
            log.info("Callback ........ STATUS_UPDATE preparing Update ")
            self.backend.status(STATUS_UPDATE)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_UPDATING, '')
        elif oldVersion and not newVersion:
            log.info("Callback ........ STATUS_REMOVE preparing Update ")
            self.backend.status(STATUS_REMOVE)
            package_id = self.backend.get_package_id(name, oldVersion, oldFlavor)
            self.backend.package(package_id, INFO_REMOVING, '')
        elif not oldVersion and newVersion:
            log.info("Callback ........ STATUS_INSTALL preparing Update")
            self.backend.status(STATUS_INSTALL)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_INSTALLING, '')

    def creatingDatabaseTransaction(self, troveNum, troveCount):
        log.info("callback. .......... creating Database Transactions")
        self.preparingUpdate(troveNum, troveCount, add=troveCount)

    def setChangesetHunk(self, num, total):
        log.info("callback. .......... set Changeset HUnk")
        pass
    # 3A >> set update Job
    def setUpdateHunk(self, hunk, hunkCount):
        log.info("callback. .......... set update HUnk")
        self.progress.step()
        if hunk > 0 and hunkCount > 0:
            percentage = hunk / float(hunkCount) * 100.0
            self.progress.set_subpercent(percentage)
            p = self.progress.percent
            self.backend.sub_percentage(p)
        else:
            self.smallUpdate = True
    # 4A >> Status rollback
    def setUpdateJob(self, job):
        log.info("callback. .......... set update Job")
        self.currentJob = job
        self.progress.step()
    # 8 A >> termina
    def updateDone(self):
        log.info("callback. ..........  update done")
        self.currentJob = None
        self.progress.step()
    def warning(self, msg, *args, **kwargs):
        e = msg %args
        log.error("<<<<<<<<<<<<<<<<<<<<<<<<<<<<")
        log.error(e)
        self.backend.error(ERROR_DEP_RESOLUTION_FAILED, e, False )
        
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
        self.error = []
        self.progress = PackagekitProgress()
        self.progress.set_steps( range( 1, 100, 9) )
