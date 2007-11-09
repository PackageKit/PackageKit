#! /usr/bin/env python
# encoding: utf-8
#
# Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import os
import Params

# the following two variables are used by the target "waf dist"
VERSION='0.1.3'
APPNAME='PackageKit'

# these variables are mandatory ('/' are converted automatically)
srcdir = '.'
blddir = 'build'

def set_options(opt):
	opt.add_option('--wall', action="store_true", help="stop on compile warnings", dest="wall", default=True)
	opt.add_option('--packagekit-user', type='string', help="User for running the PackageKit daemon", dest="user", default='root')
	opt.add_option('--default-backend', type='string', help="Default backend to use alpm,apt,box,conary,dummy,smart,yum,pisi", dest="default_backend", default='root')
	opt.add_option('--enable-tests', action="store_true", help="enable unit test code", dest="tests", default=True)
	opt.add_option('--enable-gcov', action="store_true", help="compile with gcov support (gcc only)", dest="gcov", default=False)
	opt.add_option('--enable-gprof', action="store_true", help="compile with gprof support (gcc only)", dest="gprof", default=False)
	pass

def configure(conf):
	conf.check_tool('gcc gnome')

	conf.check_pkg('glib-2.0', destvar='GLIB', vnum='2.14.0')
	conf.check_pkg('gobject-2.0', destvar='GOBJECT', vnum='2.14.0')
	conf.check_pkg('gmodule-2.0', destvar='GMODULE', vnum='2.14.0')
	conf.check_pkg('gthread-2.0', destvar='GTHREAD', vnum='2.14.0')
	conf.check_pkg('dbus-1', destvar='DBUS', vnum='1.1.1')
	conf.check_pkg('dbus-glib-1', destvar='DBUS_GLIB', vnum='0.60')
	conf.check_pkg('sqlite3', destvar='SQLITE')

	ret = conf.check_pkg('polkit-dbus', destvar='POLKIT_DBUS', vnum='0.5')
	if ret:
		ret = conf.check_pkg('polkit-grant', destvar='POLKIT_GRANT', vnum='0.5')
	if ret:
		conf.add_define('SECURITY_TYPE_POLKIT', 1)

	#optional deps
	if conf.check_pkg('libnm_glib', destvar='NM_GLIB', vnum='0.6.4'):
		conf.add_define('PK_BUILD_NETWORKMANAGER', 1)

	#TODO: check program docbook2man and set HAVE_DOCBOOK2MAN
	#TODO: check program xmlto and set DOCBOOK_DOCS_ENABLED

#TODO
#if Params.g_options.default_backend is empty, then check
#	if test -f /usr/bin/yum ; then
#		with_default_backend=yum
#	elif test -f /usr/lib/libalpm.so; then
#		with_default_backend=alpm
#	elif test -f /usr/bin/apt-get ; then
#		with_default_backend=apt
#	elif test -f /usr/bin/conary ; then
#		with_default_backend=conary
#	elif test -f /usr/bin/box-repos ; then
#		with_default_backend=box
#	elif test -f /usr/bin/smart ; then
#		with_default_backend=smart
#	elif test -f /usr/bin/pisi ; then
#		with_default_backend=pisi
#	else
#		with_default_backend=dummy
#	fi

#TODO
#if Params.g_options.default_backend is apt then CHECK_MOD apt_pkg

#if Params.g_options.default_backend is box then PKG_CHECK_MODULES(BOX, libbox)

	#process options
	if Params.g_options.wall:
		conf.env.append_value('CPPFLAGS', '-Wall -Werror -Wcast-align -Wno-uninitialized')
	if Params.g_options.gcov:
		conf.env.append_value('CFLAGS', '-fprofile-arcs -ftest-coverage')
	if Params.g_options.gprof:
		conf.env.append_value('CFLAGS', '-fprofile-arcs -ftest-coverage')

	#do we build the self tests?
	if Params.g_options.tests:
		conf.add_define('PK_BUILD_TESTS', 1)

	conf.add_define('VERSION', VERSION)
	conf.add_define('GETTEXT_PACKAGE', 'PackageKit')
	conf.add_define('PACKAGE', 'PackageKit')

	conf.env.append_value('CCFLAGS', '-DHAVE_CONFIG_H')
	conf.write_config_header('config.h')

def build(bld):
	# process subfolders from here
	# Pending dirs:
	# libpackagekit client data docs etc html libgbus libselftest man po policy python tools backends
        bld.add_subdirs('')
	print "more"

def shutdown():
	# this piece of code may be move right after the pixmap or documentation installation
	print "done!"
