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
import misc

# the following two variables are used by the target "waf dist"
VERSION='0.1.3'
APPNAME='PackageKit'

# these variables are mandatory ('/' are converted automatically)
srcdir = '.'
blddir = 'build'

def set_options(opt):
	opt.add_option('--wall', action="store_true", help="stop on compile warnings", dest="wall", default=True)
	opt.add_option('--packagekit-user', type='string', help="User for running the PackageKit daemon", dest="user", default='root')
	opt.add_option('--default-backend', type='string', help="Default backend to use alpm,apt,box,conary,dummy,smart,yum,pisi", dest="default_backend", default='dummy')
	opt.add_option('--enable-tests', action="store_true", help="enable unit test code", dest="tests", default=True)
	opt.add_option('--enable-gcov', action="store_true", help="compile with gcov support (gcc only)", dest="gcov", default=False)
	opt.add_option('--enable-gprof', action="store_true", help="compile with gprof support (gcc only)", dest="gprof", default=False)
	pass

def configure(conf):
	conf.check_tool('gcc gnome misc')

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
	else:
		print "*******************************************************************"
		print "** YOU ARE NOT USING A SECURE DAEMON. ALL USERS CAN DO ANYTHING! **"
		print "*******************************************************************"

	#optional deps
	if conf.check_pkg('libnm_glib', destvar='NM_GLIB', vnum='0.6.4'):
		conf.add_define('PK_BUILD_NETWORKMANAGER', 1)

	if conf.find_program('docbook2man', var='DOCBOOK2MAN'):
		conf.env['HAVE_DOCBOOK2MAN'] = 1

	if conf.find_program('xmlto', var='XMLTO'):
		conf.env['DOCBOOK_DOCS_ENABLED'] = 1

	if not Params.g_options.default_backend:
		if conf.find_program('yum'):
			with_default_backend = 'yum'
		elif conf.check_library2('alpm', mandatory=0):
			with_default_backend = 'alpm'
		elif conf.find_program('apt-get'):
			with_default_backend = 'apt-get'
		elif conf.find_program('conary'):
			with_default_backend = 'conary'
		elif conf.find_program('box-repos'):
			with_default_backend = 'box'
		elif conf.find_program('smart'):
			with_default_backend = 'smart'
		elif conf.find_program('pisi'):
			with_default_backend = 'pisi'
		else:
			with_default_backend = 'dummy'

	#TODO
	#if Params.g_options.default_backend is apt then CHECK_MOD apt_pkg

	#the box backend needs another module
	if Params.g_options.default_backend is 'box':
		conf.check_pkg('libbox', destvar='BOX')
		#TODO: fail if not present

	#the alpm backend needs a header file
	if Params.g_options.default_backend is 'alpm':
		conf.check_header('alpm.h')
		#TODO: fail if not present

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

	#TODO: expand these into PREFIX and something recognised by waf
	conf.add_define('PK_CONF_DIR', '$(sysconfdir)/PackageKit')
	conf.add_define('PK_DB_DIR', '$(localstatedir)/lib/PackageKit')
	conf.add_define('PK_PLUGIN_DIR', '$(libdir)/packagekit-backend')

	#TODO: can we define these here?
	#AC_SUBST(PK_PLUGIN_CFLAGS, "-I\$(top_srcdir)/src -I\$(top_srcdir)/libpackagekit $GLIB_CFLAGS $DBUS_CFLAGS $GMODULE_CFLAGS")
	#AC_SUBST(PK_PLUGIN_LIBS, "$GLIB_LIBS $DBUS_LIBS $GMODULE_LIBS")

	conf.env.append_value('CCFLAGS', '-DHAVE_CONFIG_H')
	conf.write_config_header('config.h')

def build(bld):
	# process subfolders from here
	# Pending dirs:
	# data docs etc libgbus libselftest man po policy python backends
	#TODO: process packagekit.pc.in and install packagekit.pc into $(libdir)/pkgconfig

        bld.add_subdirs('libpackagekit client libgbus libselftest')

	#set the user in packagekit.pc.in and install
#	obj=bld.create_obj('subst')
#	obj.source = 'packagekit.pc.in'
#	obj.target = 'packagekit.pc'
#	obj.dict = {'VERSION': VERSION}
#	obj.fun = misc.subst_func
#	obj.destvar = 'PREFIX'
#	obj.subdir  = 'usr/lib/pkgconfig'

	#set the user in org.freedesktop.PackageKit.conf.in and install
	obj=bld.create_obj('subst')
	obj.source = 'org.freedesktop.PackageKit.conf.in'
	obj.target = 'org.freedesktop.PackageKit.conf'
	obj.dict = {'PACKAGEKIT_USER': Params.g_options.user}
	obj.fun = misc.subst_func
	obj.destvar = 'PREFIX'
	obj.subdir  = 'etc/dbus-1/system.d'

def shutdown():
	# this piece of code may be move right after the pixmap or documentation installation
	pass

