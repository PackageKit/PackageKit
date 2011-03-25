from conarypk import ConaryPk

from conary import versions
from conary.conarycfg import CfgFlavor
from conary.deps import deps
from conary.lib.cfg import ConfigFile
from conary.lib.cfgtypes import CfgInt, CfgPath
import cElementTree
from pkConaryLog import log

def getPackagesFromLabel(cfg, cli, label):
    '''
    Return a set of (name, version, flavor) triples representing each
    package on the specified I{label}.
    '''
    repos = cli.getRepos()
    label = versions.Label(label)
    #searchFlavor =  (CfgFlavor, deps.parseFlavor('is: x86(~i486,~i586,~i686,~sse,~sse2)'))
    searchFlavor =   deps.parseFlavor('is: x86(~i486,~i586,~i686,~sse,~sse2)')
    mapping = repos.getTroveLatestByLabel({'': {label: None}})

    # Get a list of troves on that label
    # {name: {version: [flavor, ...], ...}, ...}
    ret = set()
    for name, trove_versions in mapping.iteritems():
        if ':' in name:
            # Skip components
            continue
        latestVersion = max(trove_versions.iterkeys())
        flavors = trove_versions[latestVersion]
        for flavor in flavors:
            #if flavor.satisfies(searchFlavor):
            ret.add((name, latestVersion, flavor))
            break
    return ret


def generate_xml( troves, label):
    document = cElementTree.Element("Packages", label = label)
    for trove in troves:
        name = trove.getName()
        version= trove.getVersion().trailingRevision().asString()
        meta = trove.getMetadata()

        package = cElementTree.Element("Package")

        node_name = cElementTree.Element("name")
        node_name.text = name
        node_version = cElementTree.Element("version")
        node_version.text = version

        for i in [ node_name, node_version ]:
            package.append(i)

        for key,value in meta.items():
            if value is not None and value != "None":
                if key == "categories":
                    for cat in value:
                        cat_node = cElementTree.Element("category", lang = "en")
                        cat_node.text = cat
                        package.append(node)
                else:
                    node = cElementTree.Element(key, lang = "en")
                    node.text = value
                    package.append(node)

        document.append(package)
    return document

def init(label, fileoutput, conarypk=None):

    if not conarypk:
        conarypk = ConaryPk()

    cli = conarypk.cli
    cfg = conarypk.cfg
    log.info("Attempting to retrieve repository data for %s" %  label)
    try:
        pkgs = getPackagesFromLabel(cfg,cli,label)
        troves = conarypk.repos.getTroves(pkgs,withFiles=False)
        nodes = generate_xml(troves,label)
        cElementTree.ElementTree(nodes).write(fileoutput)
        log.info("Successfully wrote XML data for label %s into file %s" % (label, fileoutput))
    except:
        log.error("Failed to gather data from the repository")

if __name__ == "__main__":
    init('zodyrepo.rpath.org@rpl:devel','tmp.xml')
