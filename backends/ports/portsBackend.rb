#!/usr/local/bin/ruby
# -*- ruby -*-

# Copyright (C) 2009 Anders F Bjorklund <afb@users.sourceforge.net>
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

require 'pkgtools'
require 'open3' # ignores exitcodes

PROGRAM_DIR=File.dirname(File.expand_path($PROGRAM_NAME))
$LOAD_PATH.unshift PROGRAM_DIR

require 'ruby_packagekit/enums'

PACKAGE_IDS_DELIM = '&'
FILENAME_DELIM = '|'

# maps Ports group to PackageKit group
GROUPS = {
"accessibility" => GROUP_ACCESSIBILITY,
"arabic" => GROUP_LOCALIZATION,
"archivers" => GROUP_UNKNOWN,
"astro" => GROUP_UNKNOWN,
"audio" => GROUP_MULTIMEDIA, # ???
"benchmarks" => GROUP_UNKNOWN,
"biology" => GROUP_SCIENCE,
"cad" => GROUP_UNKNOWN,
"chinese" => GROUP_LOCALIZATION,
"comms" => GROUP_COMMUNICATION,
"converters" => GROUP_UNKNOWN,
"databases" => GROUP_SERVERS,
"deskutils" => GROUP_ACCESSORIES,
"devel" => GROUP_PROGRAMMING,
"dns" => GROUP_INTERNET,
"editors" => GROUP_UNKNOWN,
"emulators" => GROUP_VIRTUALIZATION, # ???
"finance" => GROUP_UNKNOWN,
"french" => GROUP_LOCALIZATION,
"ftp" => GROUP_INTERNET,
"games" => GROUP_GAMES,
"german" => GROUP_LOCALIZATION,
"graphics" => GROUP_GRAPHICS,
"hebrew" => GROUP_LOCALIZATION,
"hungarian" => GROUP_LOCALIZATION,
"irc" => GROUP_INTERNET,
"japanese" => GROUP_LOCALIZATION,
"java" => GROUP_UNKNOWN,
"korean" => GROUP_LOCALIZATION,
"lang" => GROUP_UNKNOWN,
"mail" => GROUP_INTERNET,
"math" => GROUP_SCIENCE,
"mbone" => GROUP_UNKNOWN,
"misc" => GROUP_OTHER,
"multimedia" => GROUP_MULTIMEDIA,
"net" => GROUP_NETWORK,
"net-im" => GROUP_NETWORK,
"net-mgmt" => GROUP_NETWORK,
"net-p2p" => GROUP_NETWORK,
"news" => GROUP_INTERNET,
"palm" => GROUP_UNKNOWN,
"polish" => GROUP_LOCALIZATION,
"ports-mgmt" => GROUP_ADMIN_TOOLS,
"portuguese" => GROUP_LOCALIZATION,
"print" => GROUP_PUBLISHING,
"russian" => GROUP_LOCALIZATION,
"science" => GROUP_SCIENCE,
"security" => GROUP_SECURITY,
"shells" => GROUP_SYSTEM, # ???
"spanish" => GROUP_LOCALIZATION,
"sysutils" => GROUP_SYSTEM,
"textproc" => GROUP_UNKNOWN,
"ukrainian" => GROUP_LOCALIZATION,
"vietnamese" => GROUP_LOCALIZATION,
"www" => GROUP_INTERNET,
"x11" => GROUP_DESKTOP_OTHER,
"x11-clocks" => GROUP_DESKTOP_OTHER,
"x11-drivers" => GROUP_DESKTOP_OTHER,
"x11-fm" => GROUP_DESKTOP_OTHER,
"x11-fonts" => GROUP_FONTS,
"x11-servers" => GROUP_DESKTOP_OTHER,
"x11-themes" => GROUP_DESKTOP_OTHER,
"x11-toolkits" => GROUP_DESKTOP_OTHER,
"x11-wm" => GROUP_DESKTOP_OTHER,
### virtual categories
"afterstep" => GROUP_DESKTOP_OTHER,
"docs" => GROUP_DOCUMENTATION,
"elisp" => GROUP_UNKNOWN,
"geography" => GROUP_UNKNOWN,
"gnome" => GROUP_DESKTOP_GNOME,
"gnustep" => GROUP_DESKTOP_OTHER,
"hamradio" => GROUP_COMMUNICATION,
"haskell" => GROUP_UNKNOWN,
"ipv6" => GROUP_NETWORK,
"kde" => GROUP_DESKTOP_KDE,
"kld" => GROUP_SYSTEM,
"linux" => GROUP_VIRTUALIZATION, # ???
"lisp" => GROUP_UNKNOWN,
"parallel" => GROUP_UNKNOWN,
"pear" => GROUP_UNKNOWN,
"perl5" => GROUP_UNKNOWN,
"plan9" => GROUP_UNKNOWN,
"python" => GROUP_UNKNOWN,
"ruby" => GROUP_UNKNOWN,
"rubygems" => GROUP_UNKNOWN,
"scheme" => GROUP_UNKNOWN,
"tcl" => GROUP_UNKNOWN,
"tk" => GROUP_UNKNOWN,
"windowmaker" => GROUP_UNKNOWN,
"xfce" => GROUP_DESKTOP_XFCE,
"zope" => GROUP_UNKNOWN,
}

def init_global
    $pkg_arch = PkgConfig::OS_PLATFORM
end

def get_packages(filters)
    status(STATUS_QUERY)
    filterlist = filters.split(';')
    begin
      $portsdb.each do |portinfo|
        port = PortInfo.new(portinfo)
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filterlist.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filterlist.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = port.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n\n/, ';')
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
    rescue => e
       STDERR.puts e.message
       exit 1
    end
rescue PortsDB::IndexFileError
  error(ERROR_INTERNAL_ERROR, "Error reading the ports INDEX.", false)
rescue PortsDB::DBError
  error(ERROR_INTERNAL_ERROR, "Error reading the ports database.", false)
end

def get_repo_list(filters)
    status(STATUS_INFO)
    repo_detail("ports", "FreeBSD Ports", enabled=true)
end

def resolve(filters, packages)
    status(STATUS_QUERY)
    _resolve(filters, packages)
end

def _resolve(filters, packages)
    filterlist = filters.split(';')
    packages.each do |package|
      portnames = $portsdb.glob(package)
      if portnames
      portnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filterlist.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filterlist.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = port.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n/, ';')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

def search_group(filters, key)
    status(STATUS_QUERY)
    filterlist = filters.split(';')
    category = GROUPS.invert[key] || GROUP_UNKNOWN
    begin
      $portsdb.each(category) do |portinfo|
        port = PortInfo.new(portinfo)
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filterlist.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filterlist.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = port.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n\n/, ';')
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
    end
end

def search_name(filters, key)
    status(STATUS_QUERY)
    filterlist = filters.split(';')
    name = key
    begin
      $portsdb.glob("*#{name}*").each do |port|
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = port.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
    end
end

def search_details(filters, key)
    status(STATUS_QUERY)
    filterlist = filters.split(';')
    begin
      $portsdb.each do |portinfo|
        port = PortInfo.new(portinfo)
        pkg = PkgInfo.new(port.pkgname)
        if port.comment and port.comment.match(key)
        installed = pkg.installed?
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = pkg.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
        end
      end
    end
end

def search_file(filters, key)
    status(STATUS_QUERY)
    filterlist = filters.split(';')
    if filterlist.include? FILTER_NOT_INSTALLED
      error(ERROR_CANNOT_GET_FILELIST, "Only available for installed packages", false)
      return
    end
    if key[0,1] == '/':
      packages = $pkgdb.which_m(key)
      if packages
      packages.each do |pkgname|
        pkg = $pkgdb.pkg(pkgname)
        installed = true
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = pkg.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
      end
    else
        $pkgdb.each do |pkg|
        match = false
        pkg.files.each do |file|
          match = true if file.match(key)
        end
        next unless match
        installed = true
        data = installed ? "installed" : "ports"
        package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
        status = installed ? INFO_INSTALLED : INFO_AVAILABLE
        summary = pkg.comment
        if summary
            summary.chomp.chomp
            summary = summary.gsub(/\n/, ' ')
            summary = summary.gsub(/\t/, ' ')
        end
        package(package_id, status, summary)
      end
    end
end

def get_depends(filters, package_ids, recursive)
    status(STATUS_INFO)
    filterlist = filters.split(';')
    package_ids.each do |package|
      name, version, arch, data = package.split(';')

      pkgnames = $portsdb.glob(name)
      if pkgnames
       pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        next if pkg.version != version

        if pkg.pkgdep
          pkg.pkgdep.each do |dep|
            _resolve(filters, dep)
          end
        elsif port.all_depends
          port.all_depends.each do |dep|
            _resolve(filters, dep)
          end
        end
       end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

def get_details(package_ids)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = package.split(';')

      pkgnames = $portsdb.glob(name)
      if pkgnames
        pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        next if pkg.version != version

        license = LICENSE_UNKNOWN
        portgroup = port.category()
        group = GROUPS[portgroup] || GROUP_UNKNOWN
        descr_file = File.join($portsdb.ports_dir, port.descr_file)
        www = ""
        if descr_file
            desc = IO.read(descr_file)
            desc.chomp.chomp
            www = $~[1] if desc =~ /WWW:\s+(.*)/
            desc = desc.sub(/WWW:\s+(.*)/, "")
            license = $~[1] if desc =~ /LICENSE:\s+(.*)/
            license = license.gsub(/\s/, ' ')
            desc = desc.gsub(/\n/, ';')
            desc = desc.gsub(/\t/, ' ')
        end
        size = pkg.totalsize || 0
        details(package, license, group, desc, www, size)
      end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

def get_files(package_ids)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = package.split(';')

      pkgnames = $portsdb.glob(name)
      if pkgnames
        pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        next if pkg.version != version
        if !pkg.installed?
            error(ERROR_CANNOT_GET_FILELIST, "Only available for installed packages", false)
        end
        file_list = pkg.files.join(';')
        files(package, file_list)
      end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

def get_requires(filters, package_ids, recursive)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = package.split(';')

     pkgnames = $portsdb.glob(name)
      if pkgnames
       pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        next if pkg.version != version

        if pkg.required_by
        pkg.required_by.each do |dep|
            resolve(FILTER_INSTALLED, dep)
        end
        end
       end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

# (ports-mgmt/portaudit)
PORTAUDIT="#{PREFIX}/sbin/portaudit"

def refresh_cache(force)
    percentage(0)
    status(STATUS_DOWNLOAD_PACKAGELIST)
    $portsdb.update(fetch=true)
    if File.exist?(PORTAUDIT)
      status(STATUS_DOWNLOAD_UPDATEINFO)
      system(PORTAUDIT, '-q', '-F')
    end
    percentage(50)
    status(STATUS_REFRESH_CACHE)
    $portsdb.update_db(force)
    percentage(100)
end

def get_updates(filters)
    status(STATUS_DEP_RESOLVE)
    filterlist = filters.split(';')
    list = []
    $pkgdb.glob.each do |pkgname|
        list |= $pkgdb.recurse(pkgname)
    end
    status(STATUS_INFO)
    list.each do |pkg|
        pkgname = pkg.fullname
        if origin = pkg.origin
            if portinfo = $portsdb[origin]
              newpkg = portinfo.pkgname
            elsif $portsdb.exist?(origin, quick = true)
              pkgname = $portsdb.exist?(origin) or next
              newpkg = PkgInfo.new(pkgname)
            else
              # pkg's port is not in portsdb
              next
            end
            if newpkg.version > pkg.version
              data = "ports"
              package_id = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
              status = INFO_NORMAL
              if File.exist?(PORTAUDIT)
                system("PATH=/sbin:$PATH #{PORTAUDIT} -q '#{pkg.fullname}'") # /sbin/md5
                status = INFO_SECURITY if ($? != 0)
              end
              summary = pkg.comment
              if summary
                summary.chomp.chomp
                summary = summary.gsub(/\n/, ' ')
                summary = summary.gsub(/\t/, ' ')
              end
              package(package_id, status, summary)
            end
        end
    end
end

def get_update_detail(package_ids)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = package.split(';')

     pkgnames = $portsdb.glob(name)
      if pkgnames
       pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)

        updates = ''
        obsoletes = ''

        oldpkg = $pkgdb.glob(port.origin).first
        next if pkg == oldpkg
        if oldpkg
          next if oldpkg.version != version
          data = 'ports'
          package = sprintf "%s;%s;%s;%s", pkg.name, pkg.version, $pkg_arch, data
          data = oldpkg.installed? ? 'installed' : 'ports'
          updates = sprintf "%s;%s;%s;%s", oldpkg.name, oldpkg.version, $pkg_arch, data
        else
          pkgnames = $portsdb.glob(name)
          pkgnames.each do |oldport|
            oldpkg = PkgInfo.new(oldport.pkgname)
            next if oldpkg.version != version
          end
          data = oldpkg.installed? ? 'installed' : 'ports'
          obsoletes = sprintf "%s;%s;%s;%s", oldpkg.name, oldpkg.version, $pkg_arch, data
        end

        state = UPDATE_STATE_STABLE

        vendor_urls = []
        bugzilla_urls = []
        cve_urls = []

        description = ''

        issued = ''
        updated = ''

        # TODO: http://www.vuxml.org

        vendor_urls = vendor_urls.join(';')
        bugzilla_urls = bugzilla_urls.join(';')
        cve_urls = cve_urls.join(';')

        reboot = 'none'
        changelog = ''

        update_detail(package,
                updates, obsoletes, vendor_urls, bugzilla_urls, cve_urls,
                reboot, description, changelog, state, issued, updated)
        break
       end
      else
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{package} was not found")
      end
    end
end

# (ports-mgmt/portupgrade)
PORTUPGRADE="#{PREFIX}/sbin/portupgrade"

# use a non-interactive (default) dialog program
DIALOG="#{PROGRAM_DIR}/helpers/default-dialog.rb"

USE_PKG = true # use packages
BIN_PKG = true # build packages

# Here are the extra subphases used, when:
# ---> Using the port instead of a package

# ---> Building '#{portdir}'
# ===> Cleaning for #{pkgname}
# [fetch distfiles]
# ===> Extracting for #{pkgname}
# [fetch patchfiles]
# ===> Patching for #{pkgname}
# ===> Configuring for #{pkgname}
# ===> Building for #{pkgname}
# ===> Installing for #{pkgname}
# ===> Building package for #{pkgname}
# ===> Cleaning for #{pkgname}

def update_system(only_trusted)
    if only_trusted
        error(ERROR_MISSING_GPG_SIGNATURE, "Trusted packages not available.")
        return
    end
    args = ['-M', 'DIALOG='+DIALOG]
    args << '-P' if USE_PKG
    args << '-p' if BIN_PKG
    args << '-a' # all installed
    status(STATUS_DEP_RESOLVE)
    stdin, stdout, stderr = Open3.popen3(PORTUPGRADE, *args)
    stdout.each_line do |line|
        if line.match(/^\=+\>/)
            message(MESSAGE_UNKNOWN, line.chomp)
        elsif line.match(/^\-\-\-\>/)
           if line.match(/Upgrading '(.*)\-(.*)' to '(.*)\-(.*)'/)
                status(STATUS_UPDATE)
                _resolve(FILTER_NONE, $1)
            elsif line.match(/Fetching (.*)\-(.*)/)
                status(STATUS_DOWNLOAD)
                _resolve(FILTER_NONE, $1)
            elsif line.match(/SECURITY REPORT/)
                # important safety tip
            end
            message(MESSAGE_UNKNOWN, line.chomp)
        end
    end
    stderr.each_line do |line|
        if line.match(/\[Updating the pkgdb.*\]/)
          status(STATUS_WAIT)
        elsif line.match(/^\*\* Command failed \[exit code (\d)\]: (.*)/)
          error(ERROR_TRANSACTION_ERROR, $2)
        elsif not line.match(/\[Gathering depends.*\]/) \
          and not line.match(/^\*\* Could not find the latest version/)
          message(MESSAGE_BACKEND_ERROR, line.chomp)
        end
    end
end

def install_packages(only_trusted, package_ids)
    if only_trusted
        error(ERROR_MISSING_GPG_SIGNATURE, "Trusted packages not available.")
        return
    end
    pkgnames = []
    package_ids.each do |package|
      name, version, arch, data = package.split(';')
      if not $portsdb.glob(name)
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{name} was not found", exit=false)
        next
      end
      pkgname = "#{name}-#{version}"
      pkg = PkgInfo.new(pkgname)
      if pkg.installed?
        error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package #{pkgname} is already installed")
      else
        pkgnames << pkgname
      end
    end
    return if pkgnames.empty?
    args = ['-M', 'DIALOG='+DIALOG]
    args << '-P' if USE_PKG
    args << '-p' if BIN_PKG
    args.concat pkgnames
    status(STATUS_DEP_RESOLVE)
    stdin, stdout, stderr = Open3.popen3(PkgDB::command(:portinstall), *args)
    stdout.each_line do |line|
        if line.match(/^\=+\>/)
            message(MESSAGE_UNKNOWN, line.chomp)
        elsif line.match(/^\-\-\-\>/)
            if line.match(/Installing '(.*)\-(.*)'/)
                status(STATUS_INSTALL)
                _resolve(FILTER_NONE, $1)
            elsif line.match(/Fetching (.*)\-(.*)/)
                status(STATUS_DOWNLOAD)
                _resolve(FILTER_NONE, $1)
            elsif line.match(/SECURITY REPORT/)
                # important safety tip
            end
            message(MESSAGE_UNKNOWN, line.chomp)
        end
     end
    stderr.each_line do |line|
        if line.match(/\[Updating the pkgdb.*\]/)
          status(STATUS_WAIT)
        elsif line.match(/^\*\* Command failed \[exit code (\d)\]: (.*)/)
          error(ERROR_TRANSACTION_ERROR, $2)
        elsif not line.match(/\[Gathering depends.*\]/) \
          and not line.match(/^\*\* Could not find the latest version/)
          message(MESSAGE_BACKEND_ERROR, line.chomp)
        end
    end
    package_ids.each do |package|
      name, version, arch, data = package.split(';')
      pkgname = "#{name}-#{version}"
      _resolve(FILTER_INSTALLED, pkgname)
    end
end

def remove_packages(allowdep, autoremove, package_ids)
    if autoremove
        error(ERROR_NOT_SUPPORTED, "Automatic removal not available.", exit=false)
    end
    pkgnames = []
    package_ids.each do |package|
      name, version, arch, data = package.split(';')
      if not $portsdb.glob(name)
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{name} was not found", exit=false)
        next
      end
      pkgname = "#{name}-#{version}"
      pkg = PkgInfo.new(pkgname)
      if not pkg.installed?
        error(ERROR_PACKAGE_NOT_INSTALLED, "The package #{pkgname} is not installed")
      else
        pkgnames << pkgname
      end
    end
    return if pkgnames.empty?
    status(STATUS_DEP_RESOLVE)
    args = []
    args << '--recursive' if allowdep
    args.concat pkgnames
    stdin, stdout, stderr = Open3.popen3(PkgDB::command(:pkg_deinstall), *args)
    stdout.each_line do |line|
        if line.match(/^\=+\>/)
            message(MESSAGE_UNKNOWN, line.chomp)
        elsif line.match(/^\-\-\-\>/)
            if line.match(/Deinstalling '(.*)\-(.*)'/)
                status(STATUS_REMOVE)
                _resolve(FILTER_NONE, $1)
            end
            message(MESSAGE_UNKNOWN, line.chomp)
        end
    end
    stderr.each_line do |line|
        if line.match(/\[Updating the pkgdb.*\]/)
           status(STATUS_WAIT)
        elsif line.match(/^\*\* Command failed \[exit code (\d)\]: (.*)/)
          error(ERROR_TRANSACTION_ERROR, $2)
        elsif not line.match(/\[Gathering depends.*\]/) \
          and not line.match(/^\*\* Could not find the latest version/)
           message(MESSAGE_BACKEND_ERROR, line.chomp)
        end
    end
    package_ids.each do |package|
      name, version, arch, data = package.split(';')
      pkgname = "#{name}-#{version}"
      _resolve(FILTER_NOT_INSTALLED, pkgname)
    end
end

#######################################################################

def message(typ, msg)
   $stdout.printf "message\t%s\t%s\n", typ, msg
   $stdout.flush
end

def package(package_id, status, summary)
   $stdout.printf "package\t%s\t%s\t%s\n", status, package_id, summary
   $stdout.flush
end

def repo_detail(repoid, name, state)
   $stdout.printf "repo-detail\t%s\t%s\t%s\n", repoid, name, state
   $stdout.flush
end

def update_detail(package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated)
   $stdout.printf "updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text, changelog, state, issued, updated
   $stdout.flush
end

def details(package_id, package_license, group, desc, url, bytes)
   $stdout.printf "details\t%s\t%s\t%s\t%s\t%s\t%d\n", package_id, package_license, group, desc, url, bytes
   $stdout.flush
end

def files(package_id, file_list)
   $stdout.printf "files\t%s\t%s\n", package_id, file_list
   $stdout.flush
end

def status(state)
   $stdout.printf "status\t%s\n", state
   $stdout.flush
end

def error(err, description, exit=true)
   $stdout.printf "error\t%s\t%s\n", err, description
   $stdout.flush
   if exit
      finished
      exit(1)
   end
end

def percentage(percent=nil)
   if percent==nil
      $stdout.printf "finished\n"
   else percent == 0 or percent > $percentage_old
      $stdout.printf "percentage\t%i\n", percent
      $percentage_old = percent
   end
   $stdout.flush
end

def finished
   $stdout.printf "finished\n"
   $stdout.flush
end

#######################################################################

def to_b(string)
    return true if string == true || string =~ /^true$/i
    return false if string == false || string.nil? || string =~ /^false$/i
    return true if string == "yes"
    return false if string == "no"
    raise ArgumentError.new("invalid value for bool: \"#{string}\"")
end

def dispatch_command(cmd, args)
    case
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

#######################################################################

def main(argv)
    init_global
    init_pkgtools_global
    dispatcher(argv)
    0
end

if $0 == __FILE__
  set_signal_handlers

  exit(main(ARGV) || 1)
end
