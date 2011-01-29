import os
import cElementTree
from xml.parsers.expat import ExpatError
import urllib as url


from conary.lib import sha1helper
from conary.lib import util

from packagekit.backend import *
from packagekit.enums import ERROR_NO_CACHE,ERROR_REPO_CONFIGURATION_ERROR, ERROR_NO_NETWORK


from pkConaryLog import log
from conarypk import ConaryPk
from conaryEnums import groupMap
import generateXML

#{{{ FuNCS
def getGroup( categorieList ):
    where = mapGroup( categorieList )
    if where.values():
        return max( where.iteritems())[0]
    else:
        return None



def mapGroup(categorieList):
    where = {}
    if  not categorieList:
        return where
    #log.info(categorieList)
    for cat in categorieList:
        for group,categories in groupMap.items():
            if cat in categories:
                if group in where:
                    where[group] = where[group] +1
                else:
                    where[group] = 1
    return where
#}}}
class XMLRepo:
    xml_path = ""
    repository = ""
    def __init__(self, repo, path, pk):
        self.pk = pk
        self.xml_path = path
        self._setRepo(repo)

    def resolve(self, search_trove):
        """ resolve its a search with name """
        trove =  self._getPackage(search_trove)
        if trove:
            return trove
        else:
            return None

    def resolve_list(self, searchList):
        return self._getPackages(searchList)
        
    def search(self, search, where ):
        if where == "name":
            return self._searchNamePackage(search)
        elif where == "details":
            return self._searchDetailsPackage(search)
        elif where == "group":
            return self._searchGroupPackage(search)
        elif where == "all":
            log.info("searching all .............")
            return self._getAllPackages()
        return []

    def _setRepo(self,repo):  
        self.repo = repo
        doc = self._open()
        self.label = str( doc.get("label") )

    def _open(self):
        try:
            return self._repo
        except AttributeError:
            try:
                r = self.xml_path +self.repo
                self._repo =   cElementTree.parse(r).getroot()
                return self._repo
            except:
                self.pk.error(ERROR_REPO_CONFIGURATION_ERROR," The file %s not parsed submit a issue at http://issues.foresightlinux.org" % self.repo )
       

    def _generatePackage(self, package_node ): 
        """ convert from package_node to dictionary """
        cat = [ cat for cat in package_node.findall("category") ]
        pkg = dict( 
            name= package_node.find("name").text,
            label = self.label,
            version = package_node.find("version").text,
            shortDesc = getattr( package_node.find("shortDesc"), "text", ""),
            longDesc = getattr(package_node.find("longDesc"),"text",""),
            url = getattr( package_node.find("url"),"text","") ,
            category = [ i.text for i in cat ],
            licenses = eval( getattr( package_node.find("licenses"),"text", "str('')") )
        ) 
        return pkg

    def _getPackage(self, name):
        doc = self._open()
        for package in  doc.findall("Package"):
            if package.find("name").text == name:
                return self._generatePackage(package)
        return None

    def _getPackages(self, name_list ):
        doc = self._open()
        r = []
        for package in  doc.findall("Package"):
            if package.find("name").text in name_list:
                pkg = self._generatePackage(package)
                r.append(pkg)
        return r

    def _searchNamePackage(self, searchlist):
        '''Search in package name
        '''
        doc = self._open()
        results = []
        for package in doc.findall("Package"):
            for s in searchlist:
                if s.lower() in str(package.find("name").text).lower():
                    results.append(self._generatePackage(package))
                    break
        return results

    def _searchGroupPackage(self, name):
        doc = self._open()
        results_group = []
        for package in doc.findall("Package"):
            pkg = self._generatePackage(package)
            if pkg.has_key("category"):
                group = getGroup(pkg["category"])
                if name.lower() == group:
                    results_group.append(pkg)
        return results_group


    def _searchDetailsPackage(self, name):
        return self._searchPackage(name)

    def _searchPackage(self, name):
        doc = self._open()
        results = []
        for package in doc.findall("Package"):
            # categoria
            pkg = self._generatePackage(package)
            for i in pkg.keys():
                if i  == "label":
                    continue
                if i =='category':
                    for j in pkg[i]:
                        if name.lower() in j.lower():
                            results.append(pkg)
                
                if type(pkg[i]) == str:
                    check = pkg[i].lower()
                else:
                    check = pkg[i]
                if name.lower() in check:
                    results.append(pkg)
            
        return results
    def _getAllPackages(self):
        doc = self._open()
        results = []
        for package in doc.findall("Package"):
            pkg = self._generatePackage(package)
            results.append(pkg)
        return results


class XMLCache:

    # Let's only get XML data from things that we support.
    # XXX We really should replace this with the Conary
    #     RESTful API real soon now.
    pregenerated_XML_labels = (
        'conary.rpath.com@rpl:2-qa',
        'foresight.rpath.org@fl:2',
        'foresight.rpath.org@fl:2-qa',
        'foresight.rpath.org@fl:2-devel',
        'foresight.rpath.org@fl:2-kernel',
        'foresight.rpath.org@fl:2-qa-kernel',
        'foresight.rpath.org@fl:2-devel-kernel',
    )

    server = "http://packages.foresightlinux.org/cache/"
    repos = []
    dbPath = '/var/cache/conary/'
    jobPath = dbPath + 'jobs'
    xml_path =  dbPath + "xmlrepo/"

    def __init__(self):
        self.conarypk = ConaryPk()
        self.labels = ( x for x in self.conarypk.get_labels_from_config() )
        self.pk = PackageKitBaseBackend("")

        if not os.path.isdir(self.dbPath):
            os.makedirs(self.dbPath)
        if not os.path.isdir(self.jobPath):
            os.mkdir(self.jobPath)
        if not os.path.isdir( self.xml_path ):
            os.makedirs(self.xml_path )

        for label in self.labels:
           if not os.path.exists( self.xml_path + label + ".xml"  ):
                self._fetchXML(label)
           self.repos.append(XMLRepo( label + ".xml", self.xml_path, self.pk ))

    def _getJobCachePath(self, applyList):
        applyStr = '\0'.join(['%s=%s[%s]--%s[%s]%s' % (x[0], x[1][0], x[1][1], x[2][0], x[2][1], x[3]) for x in applyList])
        return self.jobPath + '/' + sha1helper.sha1ToString(sha1helper.sha1String(applyStr))

    def checkCachedUpdateJob(self, applyList):
        jobPath = self._getJobCachePath(applyList)
        log.info("CheckjobPath %s" % jobPath)
        if os.path.exists(jobPath):
            return jobPath
    
    def cacheUpdateJob(self, applyList, updJob):
        jobPath = self._getJobCachePath(applyList)
        log.info("jobPath %s" % jobPath)
        if os.path.exists(jobPath):
            log.info("deleting the JobPath %s "% jobPath)
            util.rmtree(jobPath)
            log.info("end deleting the JobPath %s "% jobPath)
        log.info("making the logPath ")
        os.mkdir(jobPath)
        log.info("freeze JobPath")
        updJob.freeze(jobPath)
        log.info("end freeze JobPath")

    def convertTroveToDict(self, troveTupleList):
        mList = []
        for troveTuple in troveTupleList:
            pkg = {}
            pkg["name"] = troveTuple[0]
            pkg["version"] = troveTuple[1].trailingRevision()
            pkg["label"] = troveTuple[1].trailingLabel() 
            mList.append(pkg)
        return mList
            
    def searchByGroups(self, groups):
        pass

    def refresh(self):
        for label in self.labels:
            self._fetchXML(label)

    def resolve(self, name ):
        for repo in self.repos:
            r =  repo.resolve(name)
            if r:
                return r
        else:
            return None

    def search(self, search, where = "name" ):
        """ 
            @where (string) values = name | details | group |
        """
        repositories_result = []
        for repo in self.repos:
            results = repo.search(search , where )
            for i in results:
                repositories_result.append(i)
        return self.list_set( repositories_result)

    def resolve_list(self, search_list ):
        r = []
        for repo in self.repos:
            res = repo.resolve_list( search_list )
            for i in res:
                r.append( i)
        return self.list_set( r )

    def list_set(self, repositories_result ):
        names = set( [i["name"] for i in repositories_result] )
        #log.info("names>>>>>>>>>>>>>>>>>>>>><")
        #log.info(names)
        results = []
        for i in repositories_result:
           # log.info(i["name"])
            if i["name"] in names:
                results.append(i)
                names.remove(i["name"])
        #log.debug([i["name"] for i in results ] )
        return results

    def _fetchXML(self, label):
        log.info("Updating XMLCache for label %s" % label)
        filename = label + '.xml'
        filepath = self.xml_path + filename
        if label in self.pregenerated_XML_labels:
            wwwfile = self.server + filename
            try:
                wget = url.urlopen( wwwfile )
                openfile = open(filepath,'w')
                openfile.writelines(wget.readlines())
                openfile.close()
            except:
                self.pk.error(ERROR_NO_NETWORK,"%s can not open" % wwwfile)
        else:
            generateXML.init(label,filepath,self.conarypk)

    def getGroup(self,categorieList):
        return getGroup(categorieList)
                
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

    def _getAllCategories(self):
        categories = []
        for i in self.repos:
            pkgs = i._getAllPackages()
            for pkg in pkgs:
                if pkg.has_key('category'):
                    for cat in pkg["category"]:
                        categories.append(cat)
        categories.sort()
        return set( categories )
        

if __name__ == '__main__':
  #  print ">>> name"
    import sys
    #print XMLCache().resolve("gimp")
    l= XMLCache().resolve_list(sys.argv[1:])
    #print ">> details"
    #l= XMLCache().search('Internet', 'group' )

    for v,p in enumerate(l):
        print v,p["name"]
    #print  XMLCache().getGroup(['GTK', 'Graphics', 'Photography', 'Viewer'])
