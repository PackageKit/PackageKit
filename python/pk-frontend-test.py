#!/usr/bin/python
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

# Copyright (C) 2007 Tom Parker <palfrey@tevp.net>

# "pkt" python test program for PackageKit
#
# pkt serves both as a simple PackageKit client, and as an example user of the
# PackageKit python API

from sys import argv,exit
from optparse import OptionParser
from types import FunctionType

from packagekit.frontend import *

class pkt(PackageKit):
	def ProgressChanged(self,jid,percent, elapsed, remaining):
		print "Progress: %.2f%%"%percent, elapsed, remaining

	def JobStatus(self,jid,type):
		print "Job type: %s"%type

	def Package(self,jid,value,name,summary):
		print "Package: %s - %s"%(name,summary)

	def Description(self,jid,package_id,license,group,detail,url,size,file_list):
		print "Package: %s" % package_id
		print "  %s" % url
		print "  %s" % detail
		print "  %s bytes" % size
		print "  Files:"
		for file in file_list.split(";"):
			print "    %s" % file

	def Transaction(self,jid,old_jid,timespec,succeeded,role,duration):
		success = "Failed"
		if succeeded:
			success = "Successful"

		print "Transaction: %s\n" % old_jid,
		print "  %s" % role
		print "  %s" % timespec
		print "  %s seconds" % duration
		print "  %s" % success

try:
	p = pkt()
except PackageKitNotStarted:
	print "PackageKit doesn't appear to be started. You may need to enable dbus autostart"
	exit(1)

def search(*args):
	patt = " ".join(args[0])
	if len(patt)==0:
		print "need something to search for"
		raise PackageKitTransactionFailure
	return p.SearchName(patt)

def desc(*args):
	if len(args)!=1 or len(args[0])!=1:
		print "desc only takes single arg"
		raise PackageKitTransactionFailure
	return p.GetDescription(args[0][0])

def depends(*args):
	if len(args)!=1 or len(args[0])!=1:
		print "depends only takes single arg"
		raise PackageKitTransactionFailure
	return p.GetDepends(args[0][0])

def refresh_cache(*args):
	if len(args)>0 and len(args[0])>0:
		print "refresh_cache doesn't take args"
		raise PackageKitTransactionFailure
	return p.RefreshCache()

def history(*args):
	count = 5
	if len(args)==1 and len(args[0])==1:
		count = args[0][0]

	count = int(count)

	if count < 1:
		print "history takes an integer (how many transactions to show)"

	return p.GetOldTransactions(count)

def get_backend_detail(*args):
	details = p.GetBackendDetail()
	for d in details:
		print "  %s" % d

	# Quit immediately because no job was scheduled
	exit(0)

def usage():
	print "Usage: %s <command> <options>"%argv[0]
	print "Valid commands are:"
	for k in globals().keys():
		if k in ["usage","catchall_signal_handler"]: #ignore
			continue
		g = globals()[k]
		if type(g) == FunctionType:
			print "\t%s"%k
	exit(1)

parser = OptionParser()
(options, args) = parser.parse_args()

if len(args)==0:
	usage()

if not globals().has_key(args[0]):
	print "Don't know operation '%s'"%args[0]
	usage()

try:
	globals()[args[0]](args[1:])
except PackageKitAccessDenied:
	print "You don't have sufficient permissions to access PackageKit (on the org.freedesktop.PackageKit dbus service)"
	exit(1)
except PackageKitTransactionFailure:
	usage()

p.run()
