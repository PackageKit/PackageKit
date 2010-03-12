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

class UpdateSystemCallback(callbacks.UpdateCallback):
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
        self.progress.set_steps([ 30,60 ]  )
        self.disablepercent = False
        self.dostep = True
    #1
    #3
    def requestingChangeSet(self):
        log.info("Callback UpdateSystem........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_REQUEST)
#        self.backend.percentage(self.progress.percent)
 #       log.info(self.progress.percent)
    #2
    def downloadingChangeSet(self, got, need):
        self.backend.status(STATUS_DOWNLOAD)
        log.info("Callback UpdateSystem........ STATUS_DOWNLOAD  Changeset %.2f percent %.2f/%.2f Mbytes" % ( got*100/float(need), got/MEGA,need/MEGA) )
        self.progress.set_subpercent( got*100 / float(need) )
        self.backend.percentage( self.progress.percent )
        log.info( "%s percent" % self.progress.percent)
        if got == need:
            log.info("Do a step ========0")
            self.progress.step()

    #4
    def resolvingDependencies(self):
        log.info("Callback UpdateSystem........ STATUS_DEP_RESOLVE ")
        self.backend.percentage(self.progress.percent)
        self.backend.status(STATUS_DEP_RESOLVE)

    #5  >> request> download
    def setChangesetHunk(self, num, total):
        log.info("callback. .......... set Changeset HUnk %s/%s" % (num, total ) )
        if total > 0:
            p = num*100/float(total)
        else:
            p = 0
        log.info("Do a supercent ========sub")
        self.progress.set_subpercent(p)
        self.disablepercent = True
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        if num == total:
            self.dostep =True
    #6 
    def setUpdateHunk(self, hunk, hunkCount):
        log.info("callback. .......... set update HUnk %s/%s" % ( hunk, hunkCount))
        if self.dostep:
            self.disablepercent = True
            self.dostep = False


        if hunk < hunkCount:
            p = hunk*100/float(hunkCount)
            log.info("Do a supercent ========sub")
            self.progress.set_subpercent( p )
        else:
            self.smallUpdate = True

        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    def setUpdateJob(self, job):
        self.currentJob = job
        self.backend.percentage(self.progress.percent)
        log.info("callback UpdateSystem. setUpdateJob")
        log.info(self.progress.percent)
        

    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        self.backend.status(STATUS_ROLLBACK)
        self.backend.percentage(self.progress.percent)
        log.info("callback updateSystem. creatingRollback")
        log.info(self.progress.percent)
        log.info(self.progress.percent)


    def preparingUpdate(self, troveNum, troveCount, add=0):
        #self.progress.step()
        log.info("callback updateSystem. preparingUpdate")
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return


        self.backend.percentage(self.progress.percent)
        if troveNum > 0:
            troveNum -= 1
        job = self.currentJob[troveNum]
        name = job[0]
        oldVersion, oldFlavor = job[1]
        newVersion, newFlavor = job[2]
        #log.info("JOB>>>>>>>> %s " % str(job) )
        if oldVersion and newVersion:
            self.backend.status(STATUS_UPDATE)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_UPDATING, '')
        elif oldVersion and not newVersion:
            self.backend.status(STATUS_REMOVE)
            package_id = self.backend.get_package_id(name, oldVersion, oldFlavor)
            self.backend.package(package_id, INFO_REMOVING, '')
        elif not oldVersion and newVersion:
            self.backend.status(STATUS_INSTALL)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_INSTALLING, '')
        log.info(self.progress.percent)

    def creatingDatabaseTransaction(self, troveNum, troveCount):
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.backend.status(STATUS_COMMIT)
        #self.preparingUpdate(troveNum, troveCount, add=troveCount)
        log.info("Callback UpdateSystem........ CreateingDatabaseTransaction %.2f / %.2f " % ( troveNum, troveCount ) )
        #self.progress.set_subpercent( got*100 / float(need) )

    def committingTransaction(self):
        log.info('callback UpdateSystem......Committing Transaction')

        self.backend.status(STATUS_COMMIT)
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    def updateDone(self):
        log.info("callback. ..........  update done")
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
 
    def warning(self, msg, *args, **kwargs):
        e = msg %args
        log.warning(e)
        
    def tagHandlerOutput(self, tag, msg, stderr = False):
        pass

    def troveScriptOutput(self, typ, msg):
        pass

class GetUpdateCallback(callbacks.UpdateCallback):
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
        self.progress.set_steps([1,50, 100 ]  )
    # 1 >> downloadingChangeSet
    # 2 >> downloadingChangeSet
    def requestingChangeSet(self):
        log.info("Callback getUpdates ........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_REQUEST)
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.progress.step()

    def downloadingChangeSet(self, got, need):
       # log.info("Callback getUpdates. Changeset %s percent of %.2f/%.2f bytes" % ( int( got*100/float(need)), got/MEGA,need/MEGA) )
        self.backend.status(STATUS_DOWNLOAD)
        self.progress.set_subpercent( got*100 / float(need) )
        self.backend.percentage( self.progress.percent )
       # log.info( "%s percent" % self.progress.percent)
    # 3
    def resolvingDependencies(self):
        #self.backend.status('Resolving Dependencies')
        self.backend.status(STATUS_DEP_RESOLVE)
        self.progress.step()
        log.info("Callback getUpdates........ STATUS_DEP_RESOLVE ")
        self.backend.percentage(self.progress.percent)
        log.info("do a step>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>")

        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
  
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
        self.progress.set_steps([ 
            1, # requestingChangeSet 1
            50, # resolveDeps2
            51, # SetChangesetHunk3
            52, # requestingChangeSet4
            80,# setUpdateHUnk5
            81,# setUpdateJob6
            ]  )
    # 1 >> download
    # 4 >> download
    def requestingChangeSet(self):
        log.info("Callback ........ STATUS_REQUEST changeset ")
        self.backend.status(STATUS_DOWNLOAD)
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.progress.step()

    def downloadingChangeSet(self, got, need):
        log.info("Callback ........ STATUS_DOWNLOAD  Changeset %s percent %.2f/%.2f bytes" % ( got*100/float(need), got/MEGA,need/MEGA) )
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
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)

    #7 >> preparing update
    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        log.info("Callback ........ STATUS_ROLLBACK  ")
        self.backend.status(STATUS_ROLLBACK)
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
            log.info("pU.. status Update")
            self.backend.status(STATUS_UPDATE)
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_UPDATING, '')
        elif oldVersion and not newVersion:
            self.backend.status(STATUS_REMOVE)
            log.info("pU.. status remove")
            package_id = self.backend.get_package_id(name, oldVersion, oldFlavor)
            self.backend.package(package_id, INFO_REMOVING, '')
        elif not oldVersion and newVersion:
            #self.backend.status(STATUS_INSTALL)
            log.info("pU.. status install")
            package_id = self.backend.get_package_id(name, newVersion, newFlavor)
            self.backend.package(package_id, INFO_INSTALLING, '')
        log.info(self.progress.percent)
    #8
    def creatingDatabaseTransaction(self, troveNum, troveCount):
        log.info("callback. .......... creating Database Transactions")
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.backend.status(STATUS_COMMIT)
     #   self.preparingUpdate(troveNum, troveCount, add=troveCount)

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
 
    def warning(self, msg, *args, **kwargs):
        e = msg %args
	log.warning(e)
        
    def tagHandlerOutput(self, tag, msg, stderr = False):
        pass

    def troveScriptOutput(self, typ, msg):
        pass

    def troveScriptFailure(self, typ, errcode):
        pass

class RemoveCallback(callbacks.UpdateCallback):
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
        self.progress.set_steps([ 2,5,7,8,90,100 ]  )
     # 1
    def resolvingDependencies(self):
        #self.backend.status('Resolving Dependencies')
        log.info("Callback ........ STATUS_DEP_RESOLVE ")
        self.backend.percentage(self.progress.percent)
        self.backend.status(STATUS_DEP_RESOLVE)
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    #2
    def setChangesetHunk(self, num, total):
        log.info("callback. .......... set Changeset HUnk %s/%s" % (num, total ) )
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    #3
    def setUpdateHunk(self, hunk, hunkCount):
        log.info("callback. .......... set update HUnk %s/%s" % ( hunk, hunkCount))
        if hunk < hunkCount:
            p = hunk / float(hunkCount) * 100.0
            self.progress.set_subpercent(p)
        else:
            self.smallUpdate = True

        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    #4
    def setUpdateJob(self, job):
        log.info("callback. .......... set update Job")
        self.currentJob = job
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    #5
    def creatingRollback(self):
        #self.backend.status('Creating Rollback')
        log.info("Callback ........ STATUS_ROLLBACK  ")
        self.backend.status(STATUS_ROLLBACK)
        self.progress.step()
        #self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
     #6
    def removeFiles(self, filenum, total):
        log.info("Callback ........ STATUS_REMOVE %s percent %s/%s files" %(filenum*100/float(total), filenum, total) )
        self.progress.set_subpercent( filenum*100/float(total) )
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        self.backend.status(STATUS_REMOVE)
        self.preparingUpdate(filenum, total, add=total)

    def preparingUpdate(self, troveNum, troveCount, add=0):
        log.info("callback ....... preparing Update  trove %s/%s" % (troveNum, troveCount) )
        #self.progress.step()
        if not self.currentJob or len(self.currentJob) == 0 or troveNum > troveCount:
            return

        log.info("currentJob %s" % troveNum)
        log.info("job %s" % self.currentJob)
        if len(self.currentJob) > troveNum:
            job = self.currentJob[troveNum]
        else:
            return
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

    def creatingDatabaseTransaction(self, troveNum, troveCount):
        log.info("callback. .......... creating Database Transactions")
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
        #self.preparingUpdate(troveNum, troveCount, add=troveCount)
    #8
    def committingTransaction(self):
        #self.backend.status('Committing Transaction')
        log.info("Callback ........ STATUS_COMMIT  transactions ")

        self.backend.status(STATUS_COMMIT)
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        log.info(self.progress.percent)
    #9
    def updateDone(self):
        log.info("callback. ..........  update done")
        self.progress.step()
        self.backend.percentage(self.progress.percent)
        self.currentJob = None
        log.info(self.progress.percent)
  
    def done(self):
    #    self.progress.step()
        log.info("Some Problem ...............>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>")
        log.info(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>")
 
    def warning(self, msg, *args, **kwargs):
        e = msg %args
        log.warning(e)
        
 
