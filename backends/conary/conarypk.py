#!/usr/bin/python
### compatible with conary 2.0.35
###  greets mkj
### zodman@foresightlinux.org under the WTFPL http://sam.zoy.org/wtfpl/

import copy
import itertools
import os

from conary import conarycfg, conaryclient
from conary import errors
from conary.conaryclient import ConaryClient, cmdline
from conary.conaryclient import cml, systemmodel, modelupdate
from conary.conaryclient.update import NoNewTrovesError
from conary.versions import Label
from conary.deps import deps

from conary.lib import sha1helper
from conary.lib import util

from pkConaryLog import log

from packagekit.backend import PackageKitBaseBackend
from packagekit.enums import ERROR_NO_NETWORK

def get_arch(flavor):
    '''Turn a Flavor into a string describing the arch

    Return value can be x86, x86_64 or None.
    '''
    ret = deps.getMajorArch(flavor)
    if ret is None:
        ret = ''
    return ret

def parse_jobs(updJob, excludes=[], show_components=True):
    '''Split the install/erase/update jobs from an UpdateJob

    @updJob: an UpdateJob
    @excludes: packages to be ignored
    @show_components: if set to False, discard jobs on component troves.

    Return a (list, list, list) tuple.
    '''
    def _filter_pkg(j):
        return (show_components or ':' not in j[0]) and j[0] not in excludes

    jobs_list = updJob.getJobs()
    jobs = filter(_filter_pkg, itertools.chain(*jobs_list))
    install_jobs, erase_jobs, update_jobs = [], [], []
    for job in jobs:
        job = job[:3]
        (name, (oldVer, oldFla), (newVer, newFla)) = job
        if not oldVer:
            install_jobs.append(job)
        elif not newVer:
            erase_jobs.append(job)
        else:
            update_jobs.append(job)
    return (install_jobs, erase_jobs, update_jobs)

def _model_build_update_job(cfg, model, modelFile, callback):
    '''Build an UpdateJob based on the system model
    '''
    # Copied from conary/cmds/updatecmd.py:_updateTroves(), with slight
    # modification

    client = conaryclient.ConaryClient(cfg, modelFile=modelFile)
    client.setUpdateCallback(callback)

    updJob = client.newUpdateJob()
    try:
        tc = modelupdate.CMLTroveCache(client.getDatabase(),
                                               client.getRepos(),
                                               callback = callback)
        tcPath = cfg.root + cfg.dbPath + '/modelcache'
        if os.path.exists(tcPath):
            callback.loadingModelCache()
            tc.load(tcPath)
        ts = client.cmlGraph(model)
        try:
            suggMap = client._updateFromTroveSetGraph(updJob, ts, tc,
                                        callback = callback)
        except errors.TroveSpecsNotFound:
            callback.done()
            client.close()
            return updJob, {}

        finalModel = copy.deepcopy(model)
        if model.suggestSimplifications(tc, ts.g):
            ts2 = client.cmlGraph(model)
            updJob2 = client.newUpdateJob()
            try:
                suggMap2 = client._updateFromTroveSetGraph(updJob2, ts2, tc)
            except errors.TroveNotFound:
                pass
            else:
                if (suggMap == suggMap2 and
                    updJob.getJobs() == updJob2.getJobs()):
                    ts = ts2
                    finalModel = model
                    updJob = updJob2
                    suggMap = suggMap2

        model = finalModel
        modelFile.model = finalModel

        if tc.cacheModified():
            callback.savingModelCache()
            tc.save(tcPath)
            callback.done()
    except:
        callback.done()
        client.close()
        raise

    return updJob, suggMap

def _model_apply_update_job(updJob, cfg, modelFile, callback):
    if not updJob.getJobs():
        return
    client = conaryclient.ConaryClient(cfg, modelFile=modelFile)
    client.setUpdateCallback(callback)

    client.checkWriteableRoot()
    client.applyUpdateJob(updJob, noRestart=True)
    modelFile.closeSnapshot()

def _model_do_conary_update(cfg, op, args, callback, dry_run=False):
    '''Perform a conary update action

    op can be 'install'/'erase'.
    args is a list of packages.

    If dry_run is True, return (UpdateJob, suggMap) which contains information
    about the install.
    '''
    # Copy of conary/cmds/conarycmd.py:_UpdateCommand and
    # conary/cmds/updatecmd.py:doModelUpdate

    model = cml.CML(cfg)
    modelFile = systemmodel.SystemModelFile(model)

    model.appendOpByName(op, args)

    ret = _model_build_update_job(cfg, model, modelFile, callback)
    if not dry_run:
        _model_apply_update_job(ret[0], cfg, modelFile, callback)
    return ret

def _model_do_conary_updateall(cfg, callback, dry_run=False):
    '''Perform a conary updatell

    If dry_run is True, return (UpdateJob, suggMap) which contains information
    about the install.

    '''
    model = cml.CML(cfg)
    modelFile = systemmodel.SystemModelFile(model)

    model.refreshVersionSnapshots()
    ret = _model_build_update_job(cfg, model, modelFile, callback)

    if not dry_run:
        _model_apply_update_job(ret[0], cfg, modelFile, callback)
    return updJob

class UpdateJobCache:
    '''A cache to store (freeze) conary UpdateJobs.

    The key is an applyList which can be used to build UpdateJobs.
    '''

    def __init__(self, jobPath='/var/cache/conary/jobs/', createJobPath=True):
        if createJobPath and not os.path.isdir(jobPath):
            os.mkdir(jobPath)
        self._jobPath = jobPath

    def _getJobCachePath(self, applyList):
        applyStr = '\0'.join(['%s=%s[%s]--%s[%s]%s' % (
            x[0], x[1][0], x[1][1], x[2][0], x[2][1], x[3]) for x in applyList])
        return '%s/%s' % (self._jobPath,
                sha1helper.sha1ToString(sha1helper.sha1String(applyStr)))

    def getCachedUpdateJob(self, applyList):
        '''Retrieve a previously cached job
        '''
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            return jobPath

    def cacheUpdateJob(self, applyList, updJob):
        '''Cache a conary UpdateJob
        '''
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            util.rmtree(jobPath)
        os.mkdir(jobPath)
        updJob.freeze(jobPath)

    def clearCache(self):
        '''Clear all cached jobs
        '''
        util.rmtree('%s/*' % self._jobPath)

class ConaryPk:
    def __init__(self):
        # get configs from /etc/conary
        cfg = conarycfg.ConaryConfiguration( readConfigFiles = True)
        # get if the machine its x86 or x86_64
        cfg.initializeFlavors()
        self.cfg = cfg

        # Don't use threaded mode
        self.cfg.configLine('threaded False')

        cli = ConaryClient(cfg)

        # labels enable on /etc/conary/config.d/
        self.default_label = self.cfg.installLabelPath

        # get if x86 or x86_64
        self.flavors = self.cfg.flavor
        self.flavor = self.cfg.flavor[0]
        # for client
        self.cli = cli
        # for query on system (database)
        self.db = cli.db
        # for request query on repository (repos)
        self.repos = cli.repos

        self.job_cache = UpdateJobCache()

    def _exist_network(self):
        if not os.environ.get("NETWORK"):
            Pk = PackageKitBaseBackend("")
            Pk.error(ERROR_NO_NETWORK,"Not exist network conection")

    def clear_job_cache(self):
        self.job_cache.clearCache()

    def _using_system_model(self):
        # Directly check the default location. Ignore conary configuration.
        # Change me if this proves to be problematic.
        return os.path.isfile('/etc/conary/system-model')

    def _get_repos(self):
        """ get repos for do request query """
        return self.repos

    def label(self, installLabel = None):
        """ get label from config or custom installLabel """
        if installLabel:
            return Label(installLabel)
        return self.default_label
    def get_labels_from_config(self):
        labels = []
        for i in self.default_label:
            #if "foresight.rpath.org" or "conary.rpath.com" in i.asString():
            labels.append(i.asString())
        return labels

    def search_path(self,path_file ):
        labels = self.get_labels_from_config()
        where = self._get_repos()
        for label in self.default_label:
            trove = where.getTroveLeavesByPath([path_file], label)
            if trove.get(path_file):
                for ( name,version,flavor) in trove[path_file]:
                    return name

    def query(self, name):
        """ do a conary query """
        if name is None or name == "":
            return []
        try:
            troves = self.db.findTrove( None ,(name , None, None ))
            return troves
        except errors.TroveNotFound:
            return []

    def repo_query(self, name, installLabel = None):
        """ Do a conary request query """
        self._exist_network()
        label = self.label( installLabel )
        repos = self._get_repos()
        try:
            troves = repos.findTrove( label ,( name, None ,self.flavor ) )
            #return repos.getTroves(troves)
            return troves
        except errors.TroveNotFound:
            return []

    def get_metadata( self, name , installLabel = None):
        pass

    def remove(self, name):
        return self.update(name, remove = True )
    def update(self, name, installLabel= None, remove  = False ):
        cli = self.cli
        #get a trove
        troves = self.repo_query(name, installLabel)
        for trove in troves:
            trovespec =  self.trove_to_spec( trove, remove )
        try:
            # create a Job
            job = cli.newUpdateJob()
            # Add Update task to Job
            cli.prepareUpdateJob(job, cmdline.parseChangeList(trovespec))
            # Apply the Job
            cli.applyUpdateJob(job)
            # im rulz
            return "Update Success of %s" %  trovespec
        except NoNewTrovesError:
            return "no new Troves Found by %s " % trovespec

    def trove_to_spec(self, trove, remove = False ):
        # add a -app=blah.rpath.org@rpl:devel for remove packages
        if remove:
            tmp = '-'
        else:
            tmp = ""
        return tmp + cmdline.toTroveSpec( trove[0], str(trove[1]), None)

    def _classic_build_update_job(self, applyList, cache=True):
        '''Build an UpdateJob from applyList
        '''
        updJob = self.cli.newUpdateJob()
        suggMap = {}
        jobPath = self.job_cache.getCachedUpdateJob(applyList)
        if cache and jobPath:
            try:
                updJob.thaw(jobPath)
            except IOError, err:
                updJob = None
        else:
            try:
                suggMap = self.cli.prepareUpdateJob(updJob, applyList)
                if cache:
                    self.job_cache.cacheUpdateJob(applyList, updJob)
            except conaryclient.NoNewTrovesError:
                suggMap = {}

        return updJob, suggMap

    def _classic_get_package_update(self, pkg_list, callback, dry_run=False):
        self.cli.setUpdateCallback(callback)
        applyList = cmdline.parseChangeList(pkg_list, keepExisting=False,
                                            updateByDefault=True,
                                            allowChangeSets=False)
        ret = self._classic_build_update_job(applyList)
        if not dry_run:
            self.cli.applyUpdateJob(ret[0])
        return ret

    def _classic_get_updateall_job(self, callback, dry_run):
        self.cli.setUpdateCallback(callback)

        updateItems = self.cli.fullUpdateItemList()
        applyList = [(x[0], (None, None), x[1:], True) for x in updateItems]

        ret = self._classic_build_update_job(applyList)
        if not dry_run:
            self.cli.applyUpdateJob(ret[0])
        return ret

    def install(self, pkglist, callback, dry_run=False):
        '''Equivalent to 'conary install'

        pkglist is a list of package specs.

        Returns a (UpdateJob, suggMap) tuple.
        '''
        if self._using_system_model():
            ret = _model_do_conary_update(self.cfg, 'install', pkglist,
                    callback, dry_run)
        else:
            ret = self._classic_get_package_update(pkglist, callback, dry_run)
        return ret

    def erase(self, pkglist, callback, dry_run=False):
        '''Equivalent to 'conary erase'

        pkglist is a list of package specs.

        Returns a (UpdateJob, suggMap) tuple.
        '''
        if self._using_system_model():
            ret = _model_do_conary_update(self.cfg, 'erase', pkglist,
                    callback, dry_run)
        else:
            # Append '-' for erasing
            pkglist = ['-%s' % x for x in pkglist]
            ret = self._classic_get_package_update(pkglist, callback, dry_run)
        return ret

    def updateall(self, callback, dry_run=False):
        '''Build an UpdateJob for updateall

        Returns a (UpdateJob, suggMap) tuple.
        '''
        if self._using_system_model():
            ret = _model_do_conary_updateall(self.cfg, callback, dry_run)
        else:
            ret = self._classic_get_updateall_job(callback, dry_run)
        return ret

if __name__ == "__main__":
    conary = ConaryPk()
    print conary.search_path("/usr/bin/vim")
    #print conary.query("gimpasdas")
    #print conary.repo_query("dpaster",'zodyrepo.rpath.org@rpl:devel')
    #print conary.repo_query("gimp")
    #print conary.repo_query("gimpasdasd")
    #print conary.update("amsn")
    #print conary.remove("amsn")

