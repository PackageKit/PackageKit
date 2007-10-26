#!/usr/bin/python

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Library General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) 2007 Tom Parker <palfrey@tevp.net>

from packagekit.backend import *
import apt

class PackageKitProgress(apt.progress.OpProgress, apt.progress.FetchProgress):
	def __init__(self, backend):
		self.backend = backend

	# OpProgress callbacks
	def update(self, percent):
		self.backend.percentage(percent)

	def done(self):
		self.backend.percentage(50.0)

	# FetchProgress callbacks
	def pulse(self):
		apt.progress.FetchProgress.pulse(self)
		self.backend.percentage(self.percent)

	def stop(self):
		print "self.inc (stop)"
		self.backend.percentage(100)

	def mediaChange(self, medium, drive):
		self.backend.error(ERROR_INTERNAL_ERROR, "Needed to do a medium change!")

class PackageKitAptBackend(PackageKitBaseBackend):
	def refresh_cache(self):
		'''
		Implement the {backend}-refresh_cache functionality
		'''
		self.percentage(0)
		pkp = PackageKitProgress(self)
		cache = apt.Cache(pkp)
		if cache.update(pkp) == False:
			self.error(ERROR_INTERNAL_ERROR,"Fetch failure")

