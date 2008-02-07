#!/usr/bin/python

# Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import os
import sys

import dbus
import dbus.glib
import dbus.service
import gobject

PACKAGEKIT_DBUS_INTERFACE = 'org.freedesktop.PackageKitTestBackend'
PACKAGEKIT_DBUS_SERVICE = 'org.freedesktop.PackageKitTestBackend'
PACKAGEKIT_DBUS_PATH = '/org/freedesktop/PackageKitTestBackend'

#sudo dbus-send --system --dest=org.freedesktop.PackageKitTestBackend --type=method_call --print-reply /org/freedesktop/PackageKitTestBackend org.freedesktop.PackageKitTestBackend.SearchName string:filter string:search

class PackageKitTestBackendService(dbus.service.Object):
    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Init(self):
        print 'Init!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Lock(self):
        print 'Lock!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Unlock(self):
        print 'Unlock!'

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='', out_signature='')
    def Exit(self):
        sys.exit(0)

    @dbus.service.method(PACKAGEKIT_DBUS_INTERFACE,
                         in_signature='ss', out_signature='')
    def SearchName(self, filterx, search):
        print 'filter'
        self.Finished(0)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def Finished(self, exit):
        print "Finished (%d)" % (exit)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssb')
    def RepoDetail(self, repo_id, description, enabled):
        print "RepoDetail (%s, %s, %i)" % (repo_id, description, enabled)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def StatusChanged(self, status):
        print "StatusChanged (%i)" % (status)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def PercentageChanged(self, percentage):
        print "PercentageChanged (%i)" % (percentage)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='u')
    def SubPercentageChanged(self, percentage):
        print "SubPercentageChanged (%i)" % (percentage)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='')
    def NoPercentageChanged(self):
        print "NoPercentageChanged"

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='uss')
    def Package(self, percentage, package_id, summary):
        print "Package (%s, %s)" % (package_id, summary)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssussu')
    def Description(self, package_id, licence, group, detail, url, size):
        print "Description (%s, %s, %u, %s, %s, %u)" % (package_id, licence, group, detail, url, size)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssussu')
    def Files(self, package_id, file_list):
        print "Files (%s, %s)" % (package_id, file_list)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='sssssssu')
    def UpdateDetail(self, package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text):
        print "UpdateDetail (%s, %s, %s, %s, %s, %s, %s, %u)" % (package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='b')
    def AllowCancel(self, allow_cancel):
        print "AllowCancel (%i)" % (allow_cancel)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='us')
    def ErrorCode(self, code, description):
        print "ErrorCode (%i, %s)" % (code, description)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='us')
    def RequireRestart(self, restart, description):
        print "RequireRestart (%i, %s)" % (restart, description)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='us')
    def Message(self, message, description):
        print "Message (%i, %s)" % (message, description)

    @dbus.service.signal(dbus_interface=PACKAGEKIT_DBUS_INTERFACE,
                         signature='ssssssu')
    def RepoSignatureRequired(self, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type):
        print "RepoSignatureRequired (%s, %s, %s, %s, %s, %s, %i)" % (repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, sig_type)

bus = dbus.SystemBus()
bus_name = dbus.service.BusName(PACKAGEKIT_DBUS_SERVICE, bus=bus)
manager = PackageKitTestBackendService(bus_name, PACKAGEKIT_DBUS_PATH)

mainloop = gobject.MainLoop()
mainloop.run()

