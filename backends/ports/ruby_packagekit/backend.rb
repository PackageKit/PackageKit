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

# Copyright (C) 2009, 2013 Anders F Bjorklund <afb@users.sourceforge.net>
#
# This file contain the base classes to implement a PackageKit ruby backend
#

PACKAGE_IDS_DELIM = '&'
FILENAME_DELIM = '|'
FLAGS_DELIM = ';'

class PackageKitBaseBackend
  @locked = false

  def do_lock()
    # Generic locking, override and extend in subclass
    @locked = true
  end

  def un_lock()
    # Generic locking, override and extend in subclass
    @locked = false
  end

  def locked?
    return @locked
  end

  def error(err, description, exit=true)
    if exit and locked?:
      un_lock()
    end
    error_description(err, description)
    if exit
      # Paradoxically, we don't want to print "finished" to stdout here.
      #
      # Leave PackageKit to clean up for us in this case.
      exit(254)
    end
  end

  def dispatch_command(cmd, args)
    case
    when cmd == 'download-packages'
        directory = args[0]
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        download_packages(directory, package_ids)
        finished()
    when cmd == 'get-packages'
        filters = args[0].split(FLAGS_DELIM)
        get_packages(filters)
        finished()
    when cmd == 'get-repo-list'
        filters = args[0].split(FLAGS_DELIM)
        get_repo_list(filters)
        finished()
    when cmd == 'resolve'
        filters = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        resolve(filters, package_ids)
        finished()
    when cmd == 'search-details'
        options = args[0].split(FLAGS_DELIM)
        searchterms = args[1].split(PACKAGE_IDS_DELIM)
        search_details(options, searchterms)
        finished()
    when cmd == 'search-file'
        options = args[0].split(FLAGS_DELIM)
        searchterms = args[1].split(PACKAGE_IDS_DELIM)
        search_file(options, searchterms)
        finished()
    when cmd == 'search-group'
        options = args[0].split(FLAGS_DELIM)
        searchterms = args[1].split(PACKAGE_IDS_DELIM)
        search_group(options, searchterms)
        finished()
    when cmd == 'search-name'
        options = args[0].split(FLAGS_DELIM)
        searchterms = args[1].split(PACKAGE_IDS_DELIM)
        search_name(options, searchterms)
        finished()
    when cmd == 'depends-on'
        filters = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        recursive = to_b(args[2])
        depends_on(filters, package_ids, recursive)
        finished()
    when cmd == 'get-details'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_details(package_ids)
        finished()
    when cmd == 'get-files'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_files(package_ids)
        finished()
    when cmd == 'required-by'
        filters = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        recursive = to_b(args[2])
        required_by(filters, package_ids, recursive)
        finished()
    when cmd == 'get-update-detail'
        package_ids = args[0].split(PACKAGE_IDS_DELIM)
        get_update_detail(package_ids)
        finished()
    when cmd == 'get-updates'
        filters = args[0].split(FLAGS_DELIM)
        get_updates(filters)
        finished()
    when cmd == 'install-files'
        transaction_flags = args[0].split(FLAGS_DELIM)
        files_to_inst = args[1].split(FILENAME_DELIM)
        install_files(transaction_flags, files_to_inst)
        finished()
    when cmd == 'install-packages'
        transaction_flags = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        install_packages(transaction_flags, package_ids)
        finished()
    when cmd == 'update-packages'
        transaction_flags = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        update_packages(transaction_flags, package_ids)
        finished()
    when cmd == 'refresh-cache'
        force = to_b(args[0])
        refresh_cache(force)
        finished()
    when cmd == 'remove-packages'
        transaction_flags = args[0].split(FLAGS_DELIM)
        package_ids = args[1].split(PACKAGE_IDS_DELIM)
        allowdeps = to_b(args[2])
        autoremove = to_b(args[3])
        remove_packages(transaction_flags, package_ids, allowdeps, autoremove)
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

