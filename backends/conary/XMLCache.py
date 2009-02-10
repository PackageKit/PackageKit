import os
import sys
from xml.dom.minidom import parse
import urllib as url

from pkConaryLog import log
from conarypk import ConaryPk
class XMLRepo:
    xml_path = ""
    repository = ""
    def __init__(self, repo, path ):
        self.xml_path = path
        self._setRepo(repo)

    def resolve(self, search_trove):
        """ resolve its a search with name """
        trove =  self._getPackage(search_trove)
        if trove:
            return trove
        else:
            return None
        
    def search(self, search):
        return self._searchPackage(search)

    def _setRepo(self,repo):  
        self.repo = repo
        doc = self._open()
        self.label = str( doc.childNodes[0].getAttribute("label") )

    def _open(self):
        try:
            return self._repo
        except AttributeError:
            self._repo =   parse( open( self.xml_path + self.repo) )
            return self._repo

    def _generatePackage(self, package_node ): 
        """ convert from package_node to dictionary """
        pkg = {}
        cat = []
        for node in package_node.childNodes:
            if pkg.has_key('categorie'):
                cat.append(str(node.childNodes[0].nodeValue))
            else:
                pkg[str(node.nodeName)] = str(node.childNodes[0].nodeValue)
        pkg["categorie"] = cat
        return pkg

    def _getPackage(self, name):
        doc = self._open()
        results = []
        for packages in doc.childNodes:
            for package in packages.childNodes:
                pkg = self._generatePackage(package)
                pkg["label"] = self.label
                if name == pkg["name"]:
                    return pkg
        return None
                    
    def _searchPackage(self, name):
        doc = self._open()
        results = []
        for packages in doc.childNodes:
            for package in packages.childNodes:
                pkg = self._generatePackage(package)
                pkg["label"] = self.label
                for i in pkg.keys():
                    if i  == "label":
                        continue
                    if i =='categorie':
                        for j in pkg[i]:
                            if name.lower() in j.lower():
                                results.append(pkg)
                    if name.lower() in pkg[i]:
                        results.append(pkg)
        return results

        

class XMLCache:
    #xml_files = ["foresight.rpath.org@fl:2"]
    xml_files = []
    server = "http://packages.foresightlinux.org/cache/"
    repos = []
    dbPath = '/var/cache/conary/'
    jobPath = dbPath + 'jobs'
    xml_path =  dbPath + "xmlrepo/"

    def __init__(self):
        con = ConaryPk()
        labels = con.get_labels_from_config()

        if not os.path.isdir(self.dbPath):
            os.makedirs(self.dbPath)
        if not os.path.isdir(self.jobPath):
            os.mkdir(self.jobPath)
        if not os.path.isdir( self.xml_path ):
            os.makedirs(self.xml_path )
 
        for xml_file in labels:
           if not os.path.exists( self.xml_path + xml_file + ".xml"  ):
                self._fetchXML()
        for xml_file in labels :
            self.repos.append(XMLRepo( xml_file + ".xml", self.xml_path ))

    def _getJobCachePath(self, applyList):
        from conary.lib import sha1helper
        applyStr = '\0'.join(['%s=%s[%s]--%s[%s]%s' % (x[0], x[1][0], x[1][1], x[2][0], x[2][1], x[3]) for x in applyList])
        return self.jobPath + '/' + sha1helper.sha1ToString(sha1helper.sha1String(applyStr))

    def checkCachedUpdateJob(self, applyList):
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            return jobPath
    
    def cacheUpdateJob(self, applyList, updJob):
        jobPath = self._getJobCachePath(applyList)
        if os.path.exists(jobPath):
            from conary.lib import util
            util.rmtree(jobPath)
        os.mkdir(jobPath)
        updJob.freeze(jobPath)

    def getTroves(self):
        pass
    def searchByGroups(self, groups):
        pass
    def refresh(self):
        self._fetchXML()
    def resolve(self, name ):
        for repo in self.repos:
            r =  repo.resolve(name)
            if r:
                return r
        else:
            return None

    def search(self, search ):
        repositories_result = []
        for repo in self.repos:
            results = repo.search(search)
            for i in results:
                repositories_result.append(i)
        return repositories_result

    def _fetchXML(self ):
        con = ConaryPk()
        labels = con.get_labels_from_config()
        for i in labels:
            label = i + '.xml'
            filename = self.xml_path + label
            wwwfile = self.server + label
            wget = url.urlopen( wwwfile )
            openfile = open( filename ,'w')
            openfile.writelines(wget.readlines())
            openfile.close()
    def _getCategorieBase(self, mapDict, categorieList ):
        if not categorieList:
            return None

        tempDict = {}
        for cat in categorieList:

            if mapDict.has_key(cat):
                map = mapDict[cat]
            else:
                continue

            if tempDict.has_key(map):
                tempDict[map] = tempDict[map] + 1
            else:
                tempDict[map] = 1
        tmp = 0
        t_key = ""
        for key, value in tempDict.items():
            if value > tmp:
                t_key =  key
                tmp  = value
        return t_key


if __name__ == '__main__':
    from conaryBackend import groupMap
    XMLCache()._getCategorieBase( groupMap,  ['GTK', 'Graphics', 'Photography', 'Viewer'])

