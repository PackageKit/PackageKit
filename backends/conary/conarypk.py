#!/usr/bin/python
### compatible with conary 2.0.35
###  greets mkj
### zodman@foresightlinux.org under the WTFPL http://sam.zoy.org/wtfpl/

from conary.conaryclient import ConaryClient, cmdline
from conary import conarycfg
from conary.versions import Label
from conary.errors import TroveNotFound
from conary.conaryclient.update import NoNewTrovesError
from conary.deps import deps

from pkConaryLog import log

import os

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
    def _exist_network(self):
        if not os.environ.get("NETWORK"):
            Pk = PackageKitBaseBackend("")
            Pk.error(ERROR_NO_NETWORK,"Not exist network conection")

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
        except TroveNotFound:
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
        except TroveNotFound:
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

if __name__ == "__main__":
    conary = ConaryPk()
    print conary.search_path("/usr/bin/vim")
    #print conary.query("gimpasdas")
    #print conary.repo_query("dpaster",'zodyrepo.rpath.org@rpl:devel')
    #print conary.repo_query("gimp")
    #print conary.repo_query("gimpasdasd")
    #print conary.update("amsn")
    #print conary.remove("amsn")

