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
import Object
import misc
import shutil
import subprocess

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
		conf.env['SECURITY_TYPE_POLKIT'] = True
	else:
		print "*******************************************************************"
		print "** YOU ARE NOT USING A SECURE DAEMON. ALL USERS CAN DO ANYTHING! **"
		print "*******************************************************************"
		conf.env['SECURITY_TYPE_DUMMY'] = True

	#optional deps
	if conf.check_pkg('libnm_glib', destvar='NM_GLIB', vnum='0.6.4'):
		conf.env['HAVE_NETWORKMANAGER'] = True

	if conf.find_program('docbook2man', var='DOCBOOK2MAN'):
		conf.env['HAVE_DOCBOOK2MAN'] = True

	if conf.find_program('xmlto', var='XMLTO'):
		conf.env['DOCBOOK_DOCS_ENABLED'] = True

	# Check what backend to use
	conf.sub_config('backends')

	#do we build the self tests?
	if Params.g_options.tests:
		conf.add_define('PK_BUILD_TESTS', 1)
		conf.env['HAVE_TESTS'] = True

	conf.add_define('VERSION', VERSION)
	conf.add_define('GETTEXT_PACKAGE', 'PackageKit')
	conf.add_define('PACKAGE', 'PackageKit')

	assert conf.env['SYSCONFDIR'], "You have too old WAF; please update to trunk"

	conf.add_define('PK_DB_DIR', os.path.join(conf.env['DATADIR'], 'lib', 'PackageKit'))
	conf.add_define('PK_PLUGIN_DIR', os.path.join(conf.env['LIBDIR'], 'packagekit-backend'))

	conf.env['CCDEFINES'] += ['HAVE_CONFIG_H']
	conf.write_config_header('config.h')


	# We want these last as they might confligt with configuration checks.
	if Params.g_options.wall:
		conf.env.append_value('CPPFLAGS', '-Wall -Werror -Wcast-align -Wno-uninitialized')
	if Params.g_options.gcov:
		conf.env['HAVE_GCOV'] = True
		conf.env.append_value('CCFLAGS', '-fprofile-arcs')
		conf.env.append_value('CCFLAGS', '-ftest-coverage')
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
	env = Params.g_build.env()
	if env['HAVE_GCOV']:
		gcov_report()

def gcov_report():
	env = Params.g_build.env()
	variant = env.variant()

	rootdir = os.getcwd()

	os.chdir(blddir)

	for test in ['test-libpackagekit', 'test-packagekitd']:
		cleanup = []
		sources = []

		obj = Object.name_to_obj(test)

		testdir = obj.path.bldpath(env)
		if not os.path.isdir(testdir):
			continue

		file = os.path.join(testdir, obj.name)
		if not os.path.isfile(file):
			continue

		# Waf currently doesn't name libraries until install. :(
		#
		# This should properly link all local libraries in use
		# to the directory of the test.
		for uselib in obj.to_list(obj.uselib_local):
			lib = Object.name_to_obj(uselib)

			lib_path = lib.path.bldpath(env)
			lib_name = lib.name + '.so'

			vnum_lst = lib.vnum.split('.')

			for x in range(len(vnum_lst) + 1):
				suffix = '.'.join(vnum_lst[:x])
				if suffix:
					suffix = '.' + suffix

				tgt_name = lib_name + suffix
				tgt_path = os.path.join(rootdir, blddir, testdir, tgt_name)
				src_path = os.path.join(rootdir, blddir, lib_path, lib_name)

				if not os.path.exists(tgt_path):
					os.symlink(src_path, tgt_path)
					cleanup.append(tgt_path)

		# Gather sources from objects
		sources += obj.to_list(obj.source)
		for x in obj.to_list(obj.add_objects):
			o = Object.name_to_obj(x)
			sources += o.to_list(o.source)

		d = obj.path.bldpath(env)

		command = 'LD_LIBRARY_PATH=%s %s/%s' % (testdir, testdir, test)
		proc = subprocess.Popen(command, shell=True,
								stdout=subprocess.PIPE,
								stderr=subprocess.PIPE)
		if proc.wait():
			print 'Unable to run %s/%s!' % (testdir, test)
			print proc.stderr.read()
			continue

		print proc.stdout.read()

		# Ignore these
		ignore = """
			pk-main.c
			pk-marshal.c
			pk-security-dummy.c
			pk-backend-python.c
		""".split()

		sources = [x for x in sources if x not in ignore]

		total_loc = 0
		total_covered = 0
		total_stmts = 0

		print '================================================================================'
		print ' Test coverage for module packagekit:'
		print '================================================================================'

		srcdir = obj.path.srcpath(env)

		for src in sources:
			srcpath = os.path.join(srcdir, src)

			command = 'gcov -o %s %s' % (testdir, srcpath)
			proc = subprocess.Popen(command, shell=True,
			                        stdout=subprocess.PIPE,
			                        stderr=subprocess.PIPE)
			if proc.wait():
				print 'gcov failed when processing %s' % srcpath
				raise SystemExit(1)

			covpath = src + '.gcov'
			if not os.path.exists(covpath):
				continue

			cleanup.append(covpath)

			basename, _ = os.path.splitext(src)
			gcdaname = os.path.join(testdir, basename + '.gcda')
			if os.path.exists(gcdaname):
				cleanup.append(gcdaname)

			not_covered = 0
			covered = 0
			loc = 0

			f = open(covpath, 'rb')
			for line in f.readlines():
				if line.startswith('    #####:'):
					not_covered += 1
				elif not line.startswith('        -:'):
					covered += 1
				loc += 1
			f.close()

			if (covered + not_covered) > 0:
				percent = 100.0 * covered / (covered + not_covered)
			else:
				percent = 0

			print '%30s: %7.2f%% (%d of %d)' % (src, percent, covered, not_covered+covered)

			total_loc += loc
			total_covered += covered
			total_stmts += covered + not_covered

		if (total_stmts) > 0:
			percent = 100.0 * total_covered / (total_stmts)
		else:
			percent = 0

		print '================================================================================'
		print ' Source lines          : %d' % total_loc
		print ' Actual statements     : %d' % total_stmts
		print ' Executed statements   : %d' % total_covered
		print ' Test coverage         : %3.2f%%' % percent
		print '================================================================================'

		for item in cleanup:
			os.unlink(item)


	os.chdir(rootdir)
