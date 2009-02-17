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
        self.progress.set_steps([ 0,5,10,15,20,60,75,80,90,100 ]  )
    # 1
    # 4
    def requestingChangeSet(self):
        log.info("Callback ........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_DOWNLOAD)
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.progress.step()

    def downloadingChangeSet(self, got, need):
        log.info("Callback ........ STATUS_DOWNLOAD  Changeset %s percent %s/%s bytes" % ( got*100/float(need), got,need) )
        self.progress.set_subpercent( got*100 / float(need) )
        self.backend.percentage( self.progress.percent )
        log.info( "%s percent" % self.progress.percent)



    # 2 
    def resolvingDependencies(self):
        #self.backend.status('Resolving Dependencies')
        log.info("Callback ........ STATUS_DEP_RESOLVE ")
        self.backend.percentage(self.progress.percent)
        self.backend.status(STATUS_DEP_RESOLVE)
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    # 3 
    def setChangesetHunk(self, num, total):
        log.info("callback. .......... set Changeset HUnk %s/%s" % (num, total ) )
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

   # 5
    def setUpdateHunk(self, hunk, hunkCount):
        log.info("callback. .......... set update HUnk %s/%s" % ( hunk, hunkCount))
        self.progress.step()

        if hunk < hunkCount:
            p = hunk / float(hunkCount) * 100.0
            self.progress.set_subpercent(p)
        else:
            self.smallUpdate = True

        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    # 6
    def setUpdateJob(self, job):
        log.info("callback. .......... set update Job")
        self.currentJob = job
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    #7
    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        log.info("Callback ........ STATUS_ROLLBACK  ")
        self.backend.status(STATUS_ROLLBACK)
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)


    def preparingUpdate(self, troveNum, troveCount, add=0):
        log.info("callback ....... preparing Update  trove %s/%s" % (troveNum, troveCount) )
        #self.progress.step()
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return

        if troveNum > 0 and troveCount > 0:
            sub_percent = (add + troveNum) / (2 * float(troveCount)) * 100
            self.progress.set_subpercent(sub_percent)

        self.backend.percentage(self.progress.percent)
        if troveNum > 0:
            troveNum -= 1
        log.info("currentJob")
        log.info(self.currentJob[troveNum])
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
        log.info(self.progress.percent)
    #8
    def creatingDatabaseTransaction(self, troveNum, troveCount):
        log.info("callback. .......... creating Database Transactions")
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.preparingUpdate(troveNum, troveCount, add=troveCount)

    # 9
    def committingTransaction(self):
        #self.backend.status('Committing Transaction')
        log.info("Callback ........ STATUS_COMMIT  transactions ")

        self.backend.status(STATUS_COMMIT)
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    #10
    def updateDone(self):
        log.info("callback. ..........  update done")
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        self.currentJob = None
        log.info(self.progress.percent)



    def downloadingFileContents(self, got, need):
        #self.backend.status('Downloading files for changeset')
        log.info("Callback ........ STATUS_DOWNLOAD  FIle Contents %s " %  str( got*100/need  ))
        self.backend.status(STATUS_DOWNLOAD)
        #self.backend.sub_percentage(got*100/need)

    def requestingFileContents(self):
        #self.backend.status('Requesting File Contents')
        log.info("Callback ........ STATUS_REQUEST request File contents ")
        self.backend.status(STATUS_REQUEST)

    def removeFiles(self, filenum, total):
        log.info("Callback ........ STATUS_REMOVE %s/%sfiles" %( filenum, total) )
        self.backend.status(STATUS_REMOVE)
        self.preparingUpdate(filenum, total, add=total)
    
    def done(self):
        #self.backend.status('Done')
    #    self.progress.step()
        log.info("DONEEEEEEEEEEEE")
        """
        e = ""
        for i in self.error:
            e = e + i
            log.error(i)
        if self.error:
            self.backend.error(ERROR_DEP_RESOLUTION_FAILED, e)
        """
 
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

  
