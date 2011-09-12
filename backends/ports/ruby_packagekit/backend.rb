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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Copyright (C) 2009 Anders F Bjorklund <afb@users.sourceforge.net>
#
# This file contain the base classes to implement a PackageKit ruby backend
#

PACKAGE_IDS_DELIM = '&'
FILENAME_DELIM = '|'

class PackageKitBaseBackend

  def dispatch_command(cmd, args)
    case
    when cmd == 'download-packages'
        directory = args[0]
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        download_packages(directory, package_ids)
        finished()
    when cmd == 'get-packages'
        filters = args[0]
        get_packages(filters)
        finished()
    when cmd == 'get-repo-list'
        filters = args[0]
        get_repo_list(filters)
        finished()
    when cmd == 'resolve'
        filters = args[0]
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        resolve(filters, package_ids)
        finished()
    when cmd == 'search-details'
        options = args[0]
        searchterms = args[1]
        search_details(options, searchterms)
        finished()
    when cmd == 'search-file'
        options = args[0]
        searchterms = args[1]
        search_file(options, searchterms)
        finished()
    when cmd == 'search-group'
        options = args[0]
        searchterms = args[1]
        search_group(options, searchterms)
        finished()
    when cmd == 'search-name'
        options = args[0]
        searchterms = args[1]
        search_name(options, searchterms)
        finished()
    when cmd == 'get-depends'
        filters = args[0]
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        recursive = to_b(args[2])
        get_depends(filters, package_ids, recursive)
        finished()
    when cmd == 'get-details'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_details(package_ids)
        finished()
    when cmd == 'get-files'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_files(package_ids)
        finished()
    when cmd == 'get-requires'
        filters = args[0]
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        recursive = to_b(args[2])
        get_requires(filters, package_ids, recursive)
        finished()
    when cmd == 'get-update-detail'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_update_detail(package_ids)
        finished()
    when cmd == 'get-updates'
        filters = args[0]
        get_updates(filters)
        finished()
    when cmd == 'install-files'
        only_trusted = to_b(args[0])
        files_to_inst = args[1].split(FILENAME_DELIM)
        install_files(only_trusted, files_to_inst)
        finished()
    when cmd == 'install-packages'
        only_trusted = to_b(args[0])
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        install_packages(only_trusted, package_ids)
        finished()
    when cmd == 'refresh-cache'
        force = to_b(args[0])
        refresh_cache(force)
        finished()
    when cmd == 'remove-packages'
        allowdeps = to_b(args[0])
        autoremove = to_b(args[1])
        package_ids = args[2].split(PACKAGE_IDS_DELIM)
        remove_packages(allowdeps, autoremove, package_ids)
        finished()
    when cmd == 'update-system'
        only_trusted = to_b(args[0])
        update_system(only_trusted)
        finished()
    else
        errmsg = "command '#{cmd}' is not known"
        error(ERROR_INTERNAL_ERROR, errmsg, exit=false)
        finished()
    end
  end

  def dispatcher(args)
    if args.size > 0
      dispatch_command(args[0], args[1..-1])
    else
      $stdin.each_line do |line|
        args = line.chomp.split('\t')
        dispatch_command(args[0], args[1..-1])
      end
    end
  end

end

