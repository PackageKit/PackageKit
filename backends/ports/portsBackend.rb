#!/usr/local/bin/ruby
# -*- ruby -*-

# Copyright (C) 2009, 2013 Anders F Bjorklund <afb@users.sourceforge.net>
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

require 'pkgtools'
require 'open3' # ignores exitcodes

PROGRAM_DIR=File.dirname(File.expand_path($PROGRAM_NAME))
$LOAD_PATH.unshift PROGRAM_DIR

require 'ruby_packagekit/backend'
require 'ruby_packagekit/enums'
require 'ruby_packagekit/prints'

def init_global
    $pkg_arch = PkgConfig::OS_PLATFORM
end

class PackageKitPortsBackend < PackageKitBaseBackend

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

  def get_packages(filters)
    status(STATUS_QUERY)
    begin
      $portsdb.each do |portinfo|
        port = PortInfo.new(portinfo)
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filters.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filters.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
    packages.each do |package|
      portnames = $portsdb.glob(package)
      if portnames
      portnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filters.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filters.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
    if key == GROUP_NEWEST
      portmodified = Hash.new do |modified, portinfo|
        modified[portinfo] = File.mtime(File.join(portinfo.portdir, 'Makefile')) if portinfo
      end
      ports = $portsdb.origins.sort { |a, b| portmodified[$portsdb.port(b)] \
                                         <=>  portmodified[$portsdb.port(a)] }
    else
      category = GROUPS.invert[key] || GROUP_UNKNOWN
      ports = $portsdb.origins(category)
    end
    begin
      ports.each do |origin|
        port = $portsdb.port(origin)
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        if filters.include? FILTER_NOT_INSTALLED and installed:
            next
        elsif filters.include? FILTER_INSTALLED and !installed:
            next
        end
        data = installed ? "installed" : "ports"
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
    name = key
    begin
      $portsdb.glob("*#{name}*").each do |port|
        pkg = PkgInfo.new(port.pkgname)
        installed = pkg.installed?
        data = installed ? "installed" : "ports"
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
    begin
      $portsdb.each do |portinfo|
        port = PortInfo.new(portinfo)
        pkg = PkgInfo.new(port.pkgname)
        if port.comment and port.comment.match(key)
        installed = pkg.installed?
        data = installed ? "installed" : "ports"
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
    if filters.include? FILTER_NOT_INSTALLED
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
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
        package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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

  def depends_on(filters, package_ids, recursive)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = split_package_id(package)

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

  def _distsize(origin)
    portdir = $portsdb.portdir(origin)
    file = $portsdb.make_var('MD5_FILE', portdir) # 'distinfo'
    dist_dir = $portsdb.make_var('DISTDIR', $portsdb.my_portdir)
    distsize = 0
    open(file) do |f|
      f.each do |line|
        if /^SIZE \((.*)\) = ([0-9]+)/ =~ line
          if not File.exist?(File.join(dist_dir, $1))
            distsize += $2.to_i
          end
        end
      end
    end
    return distsize
  end

  def get_details(package_ids)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = split_package_id(package)

      pkgnames = $portsdb.glob(name)
      if pkgnames
        pkgnames.each do |port|
        pkg = PkgInfo.new(port.pkgname)
        next if pkg.version != version

        license = 'unknown'
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
        size = pkg.totalsize || _distsize(port.origin)
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
      name, version, arch, data = split_package_id(package)

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

  def required_by(filters, package_ids, recursive)
    status(STATUS_INFO)
    package_ids.each do |package|
      name, version, arch, data = split_package_id(package)

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
    system "cd #{$portsdb.abs_ports_dir} && make update"
    if File.exist?(PORTAUDIT)
      status(STATUS_DOWNLOAD_UPDATEINFO)
      system(PORTAUDIT, '-q', '-F')
    end
    percentage(50)
    status(STATUS_REFRESH_CACHE)
    $portsdb.update_db(force)
    percentage(100)
  end

# (security/vxquery)
VXQUERY = "#{PREFIX}/bin/vxquery"

# http://www.vuxml.org
VULN_XML = 'vuln.xml'

  def _match_range(range, version)
    cmp = PkgVersion.new(version.to_s) <=> PkgVersion.new(range.text)
    return true if range.name == 'lt' && cmp <  0
    return true if range.name == 'le' && cmp <= 0
    return true if range.name == 'eq' && cmp == 0
    return true if range.name == 'ge' && cmp >= 0
    return true if range.name == 'gt' && cmp >  0
    return false
  end

  def _vuxml(name, oldversion=nil, newversion=nil)
    vulnxml = File.join($portsdb.portdir('security/vuxml'), VULN_XML)
    vulns = []
    if File.exist?(VXQUERY) and File.exist?(vulnxml)
      require 'rexml/document'
      vuxml = `#{VXQUERY} -t 'vuxml' #{vulnxml} '#{name}'`
      doc = REXML::Document.new vuxml
      doc.root.each_element('//vuln') do |vuln|
        match = false
        vuln.each_element('affects/package') do |package|
          package.elements['name'].each do |element|
            if element == name
              match = true
              break
            end
          end
          next unless match
          if oldversion and newversion
            match = false
            package.elements['range'].each do |element|
              if _match_range(element, oldversion) and
                 not _match_range(element, newversion)
                match = true
                break
              end
            end
          end
        end
        vulns << vuln if match
      end
    end
    return vulns
  end

  def get_updates(filters)
    status(STATUS_DEP_RESOLVE)
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
              package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
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
      name, version, arch, data = split_package_id(package)

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
          package = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
          data = oldpkg.installed? ? 'installed' : 'ports'
          updates = get_package_id(oldpkg.name, oldpkg.version, $pkg_arch, data)
        else
          pkgnames = $portsdb.glob(name)
          pkgnames.each do |oldport|
            oldpkg = PkgInfo.new(oldport.pkgname)
            next if oldpkg.version != version
          end
          data = oldpkg.installed? ? 'installed' : 'ports'
          obsoletes = get_package_id(oldpkg.name, oldpkg.version, $pkg_arch, data)
        end

        state = UPDATE_STATE_STABLE

        vendor_urls = []
        bugzilla_urls = []
        cve_urls = []

        description = ''

        issued = ''
        updated = ''

        vulns = _vuxml(pkg.name, oldpkg.version, pkg.version)
        vulns.each do |vuln|
          if topic = vuln.elements['topic']
            description += topic.text
          end
          if vid = vuln.attributes["vid"]
            vendor_urls << "http://vuxml.freebsd.org/#{vid}.html"
          end
          vuln.each_element('references/cvename') do |cve|
            cve_urls << "http://cve.mitre.org/cgi-bin/cvename.cgi?name=#{cve.text}"
          end
          vuln.each_element('references/url') do |element|
            url = element.text.chomp
            if url.match(/bugzilla/)
              bugzilla_urls << url
            end
          end
          if date = vuln.elements['dates/entry']
            issued = date.text
          end
          if date = vuln.elements['dates/modified']
            updated = date.text
          end
        end

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
DIALOG="#{PROGRAM_DIR}/helpers/default-dialog"

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

# use packages
USE_PKG = true

# build packages
BIN_PKG = true

  def download_packages(directory, package_ids)
    pkgnames = []
    package_ids.each do |package|
      name, version, arch, data = split_package_id(package)
      if not $portsdb.glob(name)
        error(ERROR_PACKAGE_NOT_FOUND, "Package #{name} was not found", exit=false)
        next
      end
      pkgname = "#{name}-#{version}"
      pkgnames << pkgname
    end

    args = [ '-f' ] # download even if installed
    args.concat pkgnames
    packages = ENV['PACKAGES']
    pkgname = nil
    status(STATUS_DEP_RESOLVE)
    ENV['PACKAGES'] = directory
    stdin, stdout, stderr = Open3.popen3(PkgDB::command(:pkg_fetch), *args)
    stdout.each_line do |line|
        if line.match(/^\-\-\-\>/)
            if line.match(/Fetching (.*)\-(.*)/)
                status(STATUS_DOWNLOAD)
                _resolve(FILTER_NONE, $1)
                pkgname = "#{$1}-#{$2}"
            elsif line.match(/Saved as (.*)/)
                next unless pkgname
                pkg = PkgInfo.new(pkgname)
                file_list = $1
                data = pkg.installed? ? "installed" : "ports"
                package_id = get_package_id(pkg.name, pkg.version, $pkg_arch, data)
                files(package_id, file_list)
                pkgname = nil
            end
            message(MESSAGE_UNKNOWN, line.chomp)
        end
    end
    stderr.each_line do |line|
        if line.match(/\*\* Failed to fetch (.*)\-(.*)/)
          pkgname = "#{$1}-#{$2}"
          next unless pkgnames.include?(pkgname)
          message(ERROR_PACKAGE_DOWNLOAD_FAILED, "Failed to fetch #{pkgname}")
        else
          message(MESSAGE_BACKEND_ERROR, line.chomp)
        end
    end
    ENV['PACKAGES'] = packages
  end

  def install_files(transaction_flags, inst_files)
    simulate = transaction_flags.include? TRANSACTION_FLAG_SIMULATE
    if transaction_flags.include? TRANSACTION_FLAG_ONLY_TRUSTED
        error(ERROR_MISSING_GPG_SIGNATURE, "Trusted packages not available.")
        return
    end
    pkg_path = ENV['PKG_PATH']
    path = []
    pkgnames = []
    inst_files.each do |file|
        begin
            pkg = PkgFileInfo.new(file)
            pkgname = pkg.fullname
            if pkg.installed?
              error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package #{pkgname} is already installed")
            elsif $portsdb.glob(pkgname).empty?
              # portinstall is a little picky about installing packages for mismatched ports
              error(ERROR_PACKAGE_NOT_FOUND, "Port for #{pkgname} was not found", exit=true)
            else
              pkgnames << pkgname
            end
        rescue ArgumentError
            error(ERROR_INVALID_PACKAGE_FILE, "File #{file} is not a package")
            next
        end
        path << File.dirname(file)
    end
    path << $packages_dir # add default dir for depends
    ENV['PKG_PATH'] = path.join(':')
    _install(pkgnames, simulate)
    ENV['PKG_PATH'] = pkg_path
  end

  def install_packages(transaction_flags, package_ids)
    simulate = transaction_flags.include? TRANSACTION_FLAG_SIMULATE
    if transaction_flags.include? TRANSACTION_FLAG_ONLY_TRUSTED
        error(ERROR_MISSING_GPG_SIGNATURE, "Trusted packages not available.")
        return
    end
    pkgnames = []
    package_ids.each do |package|
        name, version, arch, data = split_package_id(package)
        if $portsdb.glob(name)
            pkgname = "#{name}-#{version}"
            pkg = PkgInfo.new(pkgname)
            if pkg.installed?
              error(ERROR_PACKAGE_ALREADY_INSTALLED, "The package #{pkgname} is already installed")
            else
              pkgnames << pkg.fullname
            end
        else
            error(ERROR_PACKAGE_NOT_FOUND, "Package #{name} was not found", exit=false)
            next
        end
    end
    _install(pkgnames, simulate)
  end

  def update_packages(transaction_flags, package_ids)
    simulate = transaction_flags.include? TRANSACTION_FLAG_SIMULATE
    if transaction_flags.include? TRANSACTION_FLAG_ONLY_TRUSTED
        error(ERROR_MISSING_GPG_SIGNATURE, "Trusted packages not available.")
        return
    end
    pkgnames = []
    package_ids.each do |package|
        name, version, arch, data = split_package_id(package)
        if $portsdb.glob(name)
            pkgname = "#{name}-#{version}"
            pkg = PkgInfo.new(pkgname)
            pkgnames << pkg.fullname
        else
            error(ERROR_PACKAGE_NOT_FOUND, "Package #{name} was not found", exit=false)
            next
        end
    end
    _upgrade(pkgnames, simulate)
  end

  def _install(pkgnames, simulate=false)
    _execute(:portinstall, pkgnames, simulate)
  end

  def _upgrade(pkgnames, simulate=false)
    _execute(:portupgrade, pkgnames, simulate)
  end

  def _execute(command, pkgnames, simulate)
    return if pkgnames.empty?
    args = ['-M', 'DIALOG='+DIALOG]
    args << '-P' if USE_PKG
    args << '-p' if BIN_PKG
    args << '-n' if simulate
    args.concat pkgnames
    status(STATUS_DEP_RESOLVE)
    stdin, stdout, stderr = Open3.popen3(PkgDB::command(command), *args)
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
    pkgnames.each do |pkgname|
      _resolve(FILTER_INSTALLED, pkgname)
    end
  end

  def remove_packages(transaction_flags, package_ids, allowdep, autoremove)
    if autoremove
        error(ERROR_NOT_SUPPORTED, "Automatic removal not available.", exit=false)
    end
    pkgnames = []
    package_ids.each do |package|
      name, version, arch, data = split_package_id(package)
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
      name, version, arch, data = split_package_id(package)
      pkgname = "#{name}-#{version}"
      _resolve(FILTER_NOT_INSTALLED, pkgname)
    end
  end

  protected :_resolve, :_match_range, :_vuxml, :_install, :_upgrade, :_execute
end

#######################################################################

def to_b(string)
  return true if string == true || string =~ /^true$/i
  return false if string == false || string.nil? || string =~ /^false$/i
  return true if string == "yes"
  return false if string == "no"
  raise ArgumentError.new("invalid value for bool: \"#{string}\"")
end

# Returns a package id.
def get_package_id(name, version, arch, data)
  return [name, version, arch, data].join(';')
end

# Returns an array with the name, version, arch, data of a package id.
def split_package_id(id)
  return id.split(';', 4)
end

#######################################################################

def main(argv)
  init_global
  init_pkgtools_global
  backend = PackageKitPortsBackend.new
  backend.dispatcher(argv)
  0
end

if $0 == __FILE__
  set_signal_handlers

  exit(main(ARGV) || 1)
end
