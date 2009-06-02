#!/usr/bin/python
#
# Copyright (C) 2009 Mounir Lamouri (volkmar) <mounir.lamouri@gmail.com>
#
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

# packagekit imports
from packagekit.backend import *
from packagekit.progress import *
from packagekit.package import PackagekitPackage

# portage imports
# TODO: why some python app are adding try / catch around this ?
import portage
import _emerge

# misc imports
import sys
import signal
import re
from itertools import izip

# NOTES:
#
# Package IDs description:
# CAT/PN;PV;KEYWORD;[REPOSITORY|installed]
# Last field must be "installed" if installed. Otherwise it's the repo name
# TODO: KEYWORD ? (arch or ~arch) with update, it will work ?
#
# Naming convention:
# cpv: category package version, the standard representation of what packagekit
# 	names a package (an ebuild for portage)

# TODO:
# print only found package or every ebuilds ?

def sigquit(signum, frame):
	sys.exit(1)

def id_to_cpv(pkgid):
	'''
	Transform the package id (packagekit) to a cpv (portage)
	'''
	# TODO: raise error if ret[0] doesn't contain a '/'
	ret = split_package_id(pkgid)

	if len(ret) < 4:
		raise "id_to_cpv: package id not valid"

	return ret[0] + "-" + ret[1]

def cpv_to_id(cpv):
	'''
	Transform the cpv (portage) to a package id (packagekit)
	'''
	# TODO: how to get KEYWORDS ?
	# TODO: repository should be "installed" when installed
	# TODO: => move to class
	package, version, rev = portage.pkgsplit(cpv)
	keywords, repo = portage.portdb.aux_get(cpv, ["KEYWORDS", "repository"])

	if rev != "r0":
		version = version + "-" + rev

	return get_package_id(package, version, "KEYWORD", repo)

class PackageKitPortageBackend(PackageKitBaseBackend, PackagekitPackage):

	def __init__(self, args, lock=True):
		signal.signal(signal.SIGQUIT, sigquit)
		PackageKitBaseBackend.__init__(self, args)

		self.portage_settings = portage.config()
		self.vardb = portage.db[portage.settings["ROOT"]]["vartree"].dbapi
		#self.portdb = portage.db[portage.settings["ROOT"]]["porttree"].dbapi

		if lock:
			self.doLock()

	def package(self, cpv):
		desc = portage.portdb.aux_get(cpv, ["DESCRIPTION"])
		if self.vardb.cpv_exists(cpv):
			info = INFO_INSTALLED
		else:
			info = INFO_AVAILABLE
		PackageKitBaseBackend.package(self, cpv_to_id(cpv), info, desc[0])

	def download_packages(self, directory, pkgids):
		# TODO: what is directory for ?
		# TODO: remove wget output
		# TODO: percentage
		self.status(STATUS_DOWNLOAD)
		self.allow_cancel(True)
		percentage = 0

		for pkgid in pkgids:
			cpv = id_to_cpv(pkgid)

			# is cpv valid
			if not portage.portdb.cpv_exists(cpv):
				# self.warning ? self.error ?
				self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find the package %s" % pkgid)
				continue

			# TODO: FEATURES=-fetch ?

			try:
				uris = portage.portdb.getFetchMap(cpv)

				if not portage.fetch(uris, self.portage_settings, fetchonly=1, try_mirrors=1):
					self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))
			except Exception, e:
				self.error(ERROR_INTERNAL_ERROR, _format_str(traceback.format_exc()))

	def get_depends(self, filters, pkgids, recursive):
		# TODO: manage filters
		# TODO: optimize by using vardb for installed packages ?
		self.status(STATUS_INFO)
		self.allow_cancel(True)
		self.percentage(None)

		recursive = text_to_bool(recursive)

		for pkgid in pkgids:
			cpv = id_to_cpv(pkgid)

			# is cpv valid
			if not portage.portdb.cpv_exists(cpv):
				# self.warning ? self.error ?
				self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find the package %s" % pkgid)
				continue

			myopts = "--emptytree"
			spinner = ""
			settings, trees, mtimedb = _emerge.load_emerge_config()
			myparams = _emerge.create_depgraph_params(myopts, "")
			spinner = _emerge.stdout_spinner()
			depgraph = _emerge.depgraph(settings, trees, myopts, myparams, spinner)
			retval, fav = depgraph.select_files(["="+cpv])
			if not retval:
				self.error(ERROR_INTERNAL_ERROR, "Wasn't able to get dependency graph")
				continue

			if recursive:
				# printing the whole tree
				pkgs = depgraph.altlist(reversed=1)
				for pkg in pkgs:
					self.package(pkg[2])
			else: # !recursive
				# only printing child of the root node
				# actually, we have "=cpv" -> "cpv" -> children
				root_node = depgraph.digraph.root_nodes()[0] # =cpv
				root_node = depgraph.digraph.child_nodes(root_node)[0] # cpv
				children = depgraph.digraph.child_nodes(root_node)
				for child in children:
					self.package(child[2])

	def get_details(self, pkgids):
		self.status(STATUS_INFO)
		self.allow_cancel(True)
		self.percentage(None)

		for pkgid in pkgids:
			cpv = id_to_cpv(pkgid)

			# is cpv valid
			if not portage.portdb.cpv_exists(cpv):
				# self.warning ? self.error ?
				self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Could not find the package %s" % pkgid)
				continue

			homepage, desc, license = portage.portdb.aux_get(cpv, ["HOMEPAGE", "DESCRIPTION", "LICENSE"])
			# get size
			ebuild = portage.portdb.findname(cpv)
			if ebuild:
				dir = os.path.dirname(ebuild)
				manifest = portage.manifest.Manifest(dir, portage.settings["DISTDIR"])
				uris = portage.portdb.getFetchMap(cpv)
				size = manifest.getDistfilesSize(uris)

			self.details(cpv_to_id(cpv), license, "GROUP?", desc, homepage, size)

	def get_files(self, pkgids):
		self.status(STATUS_INFO)
		self.allow_cancel(True)
		self.percentage(None)

		for pkgid in pkgids:
			cpv = id_to_cpv(pkgid)

			# is cpv valid
			if not portage.portdb.cpv_exists(cpv):
				self.error(ERROR_PACKAGE_NOT_FOUND, "Package %s was not found" % pkgid)
				continue

			if not self.vardb.cpv_exists(cpv):
				self.message(MESSAGE_COULD_NOT_FIND_PACKAGE, "Package %s is not installed" % pkgid)
				continue

			cat, pv = portage.catsplit(cpv)
			db = portage.dblink(cat, pv, portage.settings["ROOT"], self.portage_settings,
					treetype="vartree", vartree=self.vardb)
			files = db.getcontents().keys()
			files = sorted(files)
			files = ";".join(files)

			self.files(pkgid, files)

	def get_packages(self, filters):
		# TODO: filters
		self.status(STATUS_QUERY)
		self.allow_cancel(True)
		self.percentage(None)

		for cp in portage.portdb.cp_all():
			for cpv in portage.portdb.match(cp):
				self.package(cpv)

	def install_packages(self, pkgs):
		self.status(STATUS_RUNNING)
		self.allow_cancel(True) # TODO: sure ?
		self.percentage(None)

		myopts = {} # TODO: --nodepends ?
		spinner = ""
		favorites = []
		settings, trees, mtimedb = _emerge.load_emerge_config()
		spinner = _emerge.stdout_spinner()
		rootconfig = _emerge.RootConfig(self.portage_settings, trees["/"], portage._sets.load_default_config(self.portage_settings, trees["/"]))
		# setconfig ?
		if "resume" not in mtimedb:
			mtimedb["resume"] = mtimedb["resume_backup"]
			del mtimedb["resume_backup"]

		for pkg in pkgs:
			# check for installed is not mandatory as there are a lot of reason
			# to re-install a package (USE/LDFLAGS/CFLAGS change for example) (or live)
			# TODO: keep a final position
			cpv = id_to_cpv(pkg)
			db_keys = list(portage.portdb._aux_cache_keys)
			metadata = izip(db_keys, portage.portdb.aux_get(cpv, db_keys))
			package = _emerge.Package(type_name="ebuild", root_config=rootconfig, cpv=cpv, metadata=metadata)

			mergetask = _emerge.Scheduler(settings, trees, mtimedb, myopts, spinner, [package], favorites, package)
			mergetask.merge()


	def resolve(self, filters, pkgs):
		# TODO: filters
		self.status(STATUS_QUERY)
		self.allow_cancel(True)
		self.percentage(None)

		for pkg in pkgs:
			searchre = re.compile(pkg, re.IGNORECASE)

			# TODO: optim with filter = installed
			for cp in portage.portdb.cp_all():
				if searchre.search(cp):
					#print self.vardb.dep_bestmatch(cp)
					self.package(portage.portdb.xmatch("bestmatch-visible", cp))
					

	def search_file(self, filters, key):
		# TODO: manage filters, error if ~installed ?
		# TODO: search for exact file name
		self.status(STATUS_QUERY)
		self.allow_cancel(True)
		self.percentage(None)

		searchre = re.compile(key, re.IGNORECASE)
		cpvlist = []

		for cpv in self.vardb.cpv_all():
			cat, pv = portage.catsplit(cpv)
			db = portage.dblink(cat, pv, portage.settings["ROOT"], self.portage_settings,
					treetype="vartree", vartree=self.vardb)
			contents = db.getcontents()
			if not contents:
				continue
			for file in contents.keys():
				if searchre.search(file):
					cpvlist.append(cpv)
					break

		for cpv in cpvlist:
			self.package(cpv)

	def search_name(self, filters, key):
		# TODO: manage filters
		# TODO: collections ?
		self.status(STATUS_QUERY)
		self.allow_cancel(True)
		self.percentage(None)

		searchre = re.compile(key, re.IGNORECASE)

		for cp in portage.portdb.cp_all():
			if searchre.search(cp):
				for cpv in portage.portdb.match(cp): #TODO: cp_list(cp) ?
					self.package(cpv)

def main():
	backend = PackageKitPortageBackend("") #'', lock=True)
	backend.dispatcher(sys.argv[1:])

if __name__ == "__main__":
	main()
