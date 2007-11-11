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
import shutil

# the following two variables are used by the target "waf dist"
VERSION='0.1.4'
APPNAME='PackageKit'

# these variables are mandatory ('/' are converted automatically)
srcdir = '.'
blddir = '_build_'

def dist_hook():
	shutil.rmtree("wafadmin", True)
	#TODO: why doesn't this delete?
	shutil.rmtree("waf-lightc", True)

def set_options(opt):
	opt.add_option('--wall', action="store_true", help="stop on compile warnings", dest="wall", default=True)
	opt.add_option('--packagekit-user', type='string', help="User for running the PackageKit daemon", dest="user", default='root')
	opt.add_option('--enable-tests', action="store_true", help="enable unit test code", dest="tests", default=True)
	opt.add_option('--enable-gcov', action="store_true", help="compile with gcov support (gcc only)", dest="gcov", default=False)
	opt.add_option('--enable-gprof', action="store_true", help="compile with gprof support (gcc only)", dest="gprof", default=False)

	opt.sub_options('backends')

def configure(conf):
	conf.check_tool('gcc gnome misc')

	conf.check_pkg('glib-2.0', destvar='GLIB', vnum='2.14.0')
	conf.check_pkg('gobject-2.0', destvar='GOBJECT', vnum='2.14.0')
	conf.check_pkg('gmodule-2.0', destvar='GMODULE', vnum='2.14.0')
	conf.check_pkg('gthread-2.0', destvar='GTHREAD', vnum='2.14.0')
	conf.check_pkg('dbus-1', destvar='DBUS', vnum='1.1.1')
	conf.check_pkg('dbus-glib-1', destvar='DBUS_GLIB', vnum='0.60')
	conf.check_pkg('sqlite3', destvar='SQLITE')

	#we need both of these for the server
	ret = conf.check_pkg('polkit-dbus', destvar='POLKIT_DBUS', vnum='0.5')
	if ret:
		ret = conf.check_pkg('polkit-grant', destvar='POLKIT_GRANT', vnum='0.5')
	if ret:
		#we only need the validation tool if we are doing the tests
		if Params.g_options.tests:
			ret = conf.find_program('polkit-config-file-validate', var='POLKIT_POLICY_FILE_VALIDATE')
	if ret:
		conf.add_define('SECURITY_TYPE_POLKIT', 1)
	else:
		print "*******************************************************************"
		print "** YOU ARE NOT USING A SECURE DAEMON. ALL USERS CAN DO ANYTHING! **"
		print "*******************************************************************"
		conf.add_define('SECURITY_TYPE_DUMMY', 1)

	#optional deps
	if conf.check_pkg('libnm_glib', destvar='NM_GLIB', vnum='0.6.4'):
		conf.add_define('PK_BUILD_NETWORKMANAGER', 1)

	if conf.find_program('docbook2man', var='DOCBOOK2MAN'):
		conf.env['HAVE_DOCBOOK2MAN'] = 1

	if conf.find_program('xmlto', var='XMLTO'):
		conf.env['DOCBOOK_DOCS_ENABLED'] = 1

	# Check what backend to use
	conf.sub_config('backends')

	#do we build the self tests?
	if Params.g_options.tests:
		conf.add_define('PK_BUILD_TESTS', 1)

	conf.add_define('VERSION', VERSION)
	conf.add_define('GETTEXT_PACKAGE', 'PackageKit')
	conf.add_define('PACKAGE', 'PackageKit')

        assert conf.env['SYSCONFDIR'], "You have too old WAF; please update to trunk"

	conf.add_define('PK_CONF_DIR', os.path.join(conf.env['SYSCONFDIR'], 'PackageKit'))
	conf.add_define('PK_DB_DIR', os.path.join(conf.env['DATADIR'], 'lib', 'PackageKit'))
	conf.add_define('PK_PLUGIN_DIR', os.path.join(conf.env['LIBDIR'], 'packagekit-backend'))

	conf.env.append_value('CCFLAGS', '-DHAVE_CONFIG_H')
	conf.write_config_header('config.h')


	# We want these last as they shouldn't 
	if Params.g_options.wall:
		conf.env.append_value('CPPFLAGS', '-Wall -Werror -Wcast-align -Wno-uninitialized')
	if Params.g_options.gcov:
		conf.env.append_value('CFLAGS', '-fprofile-arcs')
		conf.env.append_value('CFLAGS', '-ftest-coverage')
		conf.env.append_value('CXXFLAGS', '-fprofile-arcs')
		conf.env.append_value('CXXFLAGS', '-ftest-coverage')
		conf.env.append_value('LINKFLAGS', '-fprofile-arcs')

	if Params.g_options.gprof:
		conf.env.append_value('CFLAGS', '-fprofile-arcs -ftest-coverage')


def build(bld):
	# process subfolders from here
	# Pending dirs:
	#  man python
        bld.add_subdirs('libpackagekit backends client libgbus libselftest etc policy po data src docs')

	#set the user in packagekit.pc.in and install
	obj=bld.create_obj('subst')
	obj.source = 'packagekit.pc.in'
	obj.target = 'packagekit.pc'
	#TODO: set these correctly
	obj.dict = {'VERSION': 'dave', 'prefix':'PREFIX', 'exec_prefix':'PREFIX', 'libdir':'usr/lib', 'includedir':'usr/include'}
	obj.fun = misc.subst_func
	obj.install_var = 'PREFIX'
	obj.install_subdir = 'usr/lib/pkgconfig'

	#set the user in org.freedesktop.PackageKit.conf.in and install
	obj=bld.create_obj('subst')
	obj.source = 'org.freedesktop.PackageKit.conf.in'
	obj.target = 'org.freedesktop.PackageKit.conf'
	obj.dict = {'PACKAGEKIT_USER': Params.g_options.user}
	obj.fun = misc.subst_func
	obj.install_var = 'PREFIX'
	obj.install_subdir = 'etc/dbus-1/system.d'

def shutdown():
	#TODO: why ohh why doesn't this work?
	if Params.g_options.gcov:
		gcov_report()
	pass

def gcov_report():
	env = Params.g_build.env_of_name('default')
	os.chdir(blddir)
	try:
		#from http://code.nsnam.org/ns-3-dev/file/c21093326f8d/wscript
		command = "rm -f gcov.txt"
		if subprocess.Popen(command, shell=True).wait():
			raise SystemExit(1)
		command = "../src/pk-self-test'
		if subprocess.Popen(command, shell=True).wait():
			raise SystemExit(1)
		command = "../tools/create-coverage-report.sh packagekit src/*.c'
		if subprocess.Popen(command, shell=True).wait():
			raise SystemExit(1)
	finally:
		os.chdir("..")

