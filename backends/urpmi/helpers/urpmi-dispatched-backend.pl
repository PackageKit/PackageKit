#!/usr/bin/perl
#
# Copyright (C) 2008 Aurelien Lefebvre <alefebvre@mandriva.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#

#
# Dispatched backend implementation progress
#
# get-depends                   DONE
# get-details                   DONE
# get-distro-upgrades
# get-files                     DONE
# get-packages                  DONE
# get-requires                  DONE
# get-update-detail             DONE
# get-updates                   DONE
# install-packages		DONE
# refresh-cache                 DONE
# remove-packages               DONE
# resolve                       DONE
# search-details
# search-file
# search-group
# search-name                   DONE
# update-packages
# update-system
#

use strict;

use lib;
use File::Basename;

use URPM;
use urpm;
use urpm::media;
use urpm::args;
use urpm::select;

use urpmi_backend::actions;
use urpmi_backend::open_db;
use urpmi_backend::tools;
use urpmi_backend::filters;

use perl_packagekit::enums;
use perl_packagekit::prints;

use MDK::Common;

BEGIN {
  push @INC, dirname($0);
}

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);
  
while(<STDIN>) {
  chomp($_);
  my @args = split (/ /, $_);
  my $command = shift(@args);
  if($command eq "get-depends") {
    get_depends($urpm, \@args);
  }
  elsif($command eq "get-details") {
    get_details($urpm, \@args);
  }
  elsif($command eq "get-files") {
    get_files($urpm, \@args);
  }
  elsif($command eq "get-packages") {
    get_packages($urpm, \@args);
  }
  elsif($command eq "get-requires") {
    get_requires($urpm, \@args);
  }
  elsif($command eq "get-update-detail") {
    get_update_detail($urpm, \@args);
  }
  elsif($command eq "get-updates") {
    get_updates($urpm, \@args);
  }
  elsif($command eq "install-packages") {
    install_packages($urpm, \@args);
  }
  elsif($command eq "search-name") {
    search_name($urpm, \@args);
  }
  elsif($command eq "refresh-cache") {
    refresh_cache($urpm);
    urpm::media::configure($urpm);
  }
  elsif($command eq "remove-packages") {
    remove_packages($urpm, \@args);
  }
  elsif($command eq "resolve") {
    resolve($urpm, \@args);
  }
}



sub get_depends {

  my ($urpm, $args) = @_;
  
  my @filterstab = split(/;/, @{$args}[0]);
  shift @{$args};
  my $recursive_text = pop @{$args};
  my $recursive_option = $recursive_text eq "yes" ? 1 : 0;
  my @packageidstab = @{$args};
  
  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);
  
  my @pkgnames;
  foreach (@packageidstab) {
    my @pkgid = split(/;/, $_);
    push(@pkgnames, $pkgid[0]);
  }
  
  my %requested;
  my $results = urpm::select::search_packages($urpm, \%requested, \@pkgnames,
    fuzzy => 0,
    caseinsensitive => 0,
    all => 0
  );
  
  $results 
      or (_finished() and return);
  
  my $empty_db = new URPM;
  my $state = {};
  $urpm->resolve_requested($empty_db,
    $state,
    \%requested,
  );
  
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  
  my %selected = %{$state->{selected}};
  my @selected_keys = keys %selected;
  my @depslist = @{$urpm->{depslist}};
  
  foreach(sort {@depslist[$b]->flag_installed <=> @depslist[$a]->flag_installed} @selected_keys) {
    my $pkg = @depslist[$_];
    if($pkg->flag_installed) {
      grep(/^${\FILTER_NOT_INSTALLED}$/, @filterstab) and next;
      pk_print_package(INFO_INSTALLED, get_package_id($pkg), $pkg->summary);
    }
    else {
      grep(/^${\FILTER_INSTALLED}$/, @filterstab) and next;
      pk_print_package(INFO_AVAILABLE, get_package_id($pkg), $pkg->summary);
    }
  }
  _finished();
}

sub get_details {

  my ($urpm, $args) = @_;
  
  my @packageidstab = @{$args};
  pk_print_status(PK_STATUS_ENUM_QUERY);

  foreach (@packageidstab) {
    _print_package_details($urpm, $_);
  }
  _finished();
}

sub get_files {
  
  my ($urpm, $args) = @_;
  
  my @packageidstab = @{$args};
  pk_print_status(PK_STATUS_ENUM_QUERY);
  
  foreach (@packageidstab) {
    _print_package_files($urpm, $_);
  }
  _finished();
}

sub get_packages {

  my ($urpm, $args) = @_;
  my @filterstab = split(/;/, @{$args}[0]);
  
  pk_print_status(PK_STATUS_ENUM_QUERY);
  
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  
  # Here we display installed packages
  if(not grep(/^${\FILTER_NOT_INSTALLED}$/, @filterstab)) {
    $db->traverse(sub {
        my ($pkg) = @_;
        if(filter($pkg, \@filterstab, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
          pk_print_package(INFO_INSTALLED, get_package_id($pkg), ensure_utf8($pkg->summary));
        }
      });
  }
  
  # Here are package which can be installed
  if(not grep(/^${\FILTER_INSTALLED}$/, @filterstab)) {
    foreach my $pkg(@{$urpm->{depslist}}) {
      if($pkg->flag_upgrade) {
        if(filter($pkg, \@filterstab, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
          pk_print_package(INFO_AVAILABLE, get_package_id($pkg), ensure_utf8($pkg->summary));
        }
      }  
    }
  }
  _finished();
}

sub get_requires {
  
  my ($urpm, $args) = @_;
  
  my @filterstab = split(/;/, @{$args}[0]);
  shift @{$args};
  my $recursive_text = pop @{$args};
  my $recursive_option = $recursive_text eq "yes" ? 1 : 0;
  my @packageidstab = @{$args};
  
  my @pkgnames;
  foreach (@packageidstab) {
    my $pkg = get_package_by_package_id($urpm, $_);
    $pkg and push(@pkgnames, $pkg->name);
  }
  
  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);
  my @requires = perform_requires_search($urpm, \@pkgnames, $recursive_option);
  
  foreach(@requires) {
    if(filter($_, \@filterstab, { FILTER_GUI => 1, FILTER_DEVELOPMENT => 1 })) {
      if(package_version_is_installed($_)) {
        grep(/^${\FILTER_NOT_INSTALLED}$/, @filterstab) or pk_print_package(INFO_INSTALLED, get_package_id($_), $_->summary);
      }
      else {
        grep(/^${\FILTER_INSTALLED}$/, @filterstab) or pk_print_package(INFO_AVAILABLE, get_package_id($_), $_->summary);
      }
    }
  }
  _finished();
}

sub get_update_detail {

  my ($urpm, $args) = @_;
  
  pk_print_status(PK_STATUS_ENUM_QUERY);
  my @packageidstab = @{$args};
  
  foreach (@packageidstab) {
    _print_package_update_details($urpm, $_);
  }
  _finished();
}

sub get_updates {

  my ($urpm, $args) = @_;
  # Fix me
  # Filter are to be implemented.
  my $filters = @{$args}[0];
  
  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);

  my $state = {};
  my %requested;
  my $restart = urpm::select::resolve_dependencies($urpm, $state, \%requested,
    auto_select => 1);
  
  my %selected = %{$state->{selected} || {}};
  my @ask_unselect = urpm::select::unselected_packages($urpm, $state);
  my @to_remove = urpm::select::removed_packages($urpm, $state);
  my @to_install = @{$urpm->{depslist}}[sort { $a <=> $b } keys %{$state->{selected}}]; 
  @to_install = grep { $_->arch ne 'src' } @to_install;
  my $updates_descr = urpm::get_updates_description($urpm);
  
  foreach(@to_install) {
    my $updesc = $updates_descr->{URPM::pkg2media($urpm->{media}, $_)->{name}}{$_->name};
    pk_print_package($updesc->{importance} eq "bugfix" ? INFO_BUGFIX :
                        $updesc->{importance} eq "security" ? INFO_SECURITY :
                        INFO_NORMAL, get_package_id($_), $_->summary);
  }
  _finished();
}

sub install_packages {

  my ($urpm, $args) = @_;

  my @packageidstab = @{$args};
  
  my @names;
  foreach(@packageidstab) {
    my @pkg_id = (split(/;/, $_));
    push @names, $pkg_id[0];
  }
  
  my %requested;
  
  urpm::select::search_packages($urpm, \%requested, \@names, 
    fuzzy => 0, 
    caseinsensitive => 0,
    all => 0);
  eval {
    perform_installation($urpm, \%requested);
  };
  _finished();
}

sub search_name {

  my ($urpm, $args) = @_;
  
  pk_print_status(PK_STATUS_ENUM_QUERY);

  my @filterstab = split(/;/, @{$args}[0]);
  my $search_term = @{$args}[1];
  
  my $basename_option = FILTER_BASENAME;
  $basename_option = grep(/$basename_option/, @filterstab);

  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  
  # Here we display installed packages
  if(not grep(/^${\FILTER_NOT_INSTALLED}$/, @filterstab)) {
    $db->traverse(sub {
        my ($pkg) = @_;
        if(filter($pkg, \@filterstab, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
          if( (!$basename_option && $pkg->name =~ /$search_term/)
            || $pkg->name =~ /^$search_term$/ ) {
            pk_print_package(INFO_INSTALLED, get_package_id($pkg), ensure_utf8($pkg->summary));
          }
        }
      });
  }
  
  # Here are packages which can be installed
  grep(/^${\FILTER_INSTALLED}$/, @filterstab) 
    and _finished()
    and return;
  
  foreach my $pkg(@{$urpm->{depslist}}) {
    if($pkg->flag_upgrade && filter($pkg, \@filterstab, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
      if( (!$basename_option && $pkg->name =~ /$search_term/)
        || $pkg->name =~ /^$search_term$/ ) {
        pk_print_package(INFO_AVAILABLE, get_package_id($pkg), ensure_utf8($pkg->summary));
      }
    }
  }

  _finished();
}

sub refresh_cache {

  my ($urpm) = @_;

  $urpm->{fatal} = sub { 
    pk_print_error(PK_ERROR_ENUM_TRANSACTION_ERROR, $_[1]."\n"); 
    die;
  };
  my $urpmi_lock = urpm::lock::urpmi_db($urpm, 'exclusive', wait => 0);
  urpm::media::read_config($urpm);

  my @entries = map { $_->{name} } @{$urpm->{media}};
  @entries == 0 and pk_print_error(PK_ERROR_ENUM_TRANSACTION_ERROR, "nothing to update (use urpmi.addmedia to add a media)\n");

  my %options = ( all => 1 );
  
  eval {
    my $ok = urpm::media::update_media($urpm, %options, quiet => 0);
  };
  _finished();

}

sub remove_packages {

  my ($urpm, $args) = @_;

  my $notfound = 0;
  my $notfound_callback = sub {
    $notfound = 1;
  };

  my $urpmi_lock = urpm::lock::urpmi_db($urpm, 'exclusive', wait => 1);

  my $allowdeps_text = shift @{$args};
  my $allowdeps_option = $allowdeps_text eq "yes" ? 1 : 0;
  my @packageidstab = @{$args};

  my @names;
  foreach(@packageidstab) {
    my @pkg_id = (split(/;/, $_));
    push @names, $pkg_id[0];
  }

  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);

  my $state = {};
  my @breaking_pkgs = ();
  my @to_remove = urpm::select::find_packages_to_remove($urpm,
    $state,
    \@names,
    callback_notfound => $notfound_callback,
    callback_fuzzy => $notfound_callback,
    callback_base => sub {
      my $urpm = shift @_;
      push @breaking_pkgs, @_;
    }
  );

  if($notfound) {
    pk_print_error(PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "Some selected packages are not installed on your system");
  }
  elsif(@breaking_pkgs) {
    pk_print_error(PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, "Removing selected packages will break your system");
  }
  elsif(!$allowdeps_option && scalar(@to_remove) != scalar(@names)) {
    pk_print_error(PK_ERROR_ENUM_TRANSACTION_ERROR, "Packages can't be removed because dependencies remove is forbidden");
  }
  else {
    pk_print_status(PK_STATUS_ENUM_REMOVE);
    urpm::install::install($urpm,
      \@to_remove, {}, {},
      callback_report_uninst => sub {
        my @return = split(/ /, $_[0]);
        pk_print_package(INFO_REMOVING, fullname_to_package_id($return[$#return]), "");
      }
    );
  }

  $urpmi_lock->unlock;
  _finished();
}

sub resolve {

  my ($urpm, $args) = @_;

  my @filters = split(/;/, @{$args}[0]);
  shift @{$args};
  my @patterns = @{$args};

  pk_print_status(PK_STATUS_ENUM_QUERY);

  my %requested;
  urpm::select::search_packages($urpm, \%requested, \@patterns, 
    fuzzy => 0, 
    caseinsensitive => 0,
    all => 0
  );

  my @requested_keys = keys %requested;
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);

  foreach (@requested_keys) {
    my $pkg = @{$urpm->{depslist}}[$_];
    ($_ && $pkg) or next;

    # We exit the script if found package does not match with specified filters
    filter($pkg, \@filters, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1}) or next;

    if($pkg->version."-".$pkg->release eq find_installed_version($pkg)) {
      grep(/^${\FILTER_NOT_INSTALLED}$/, @filters) and next;
      pk_print_package(INFO_INSTALLED, get_package_id($pkg), $pkg->summary);
    }
    else {
      grep(/^${\FILTER_INSTALLED}$/, @filters) and next;
      pk_print_package(INFO_AVAILABLE, get_package_id($pkg), $pkg->summary);
    }
  }
  _finished();
}

sub _finished {
  pk_print_status(PK_STATUS_ENUM_FINISHED);
}

sub _print_package_details {

  my ($urpm, $pkgid) = @_;
  
  my $pkg = get_package_by_package_id($urpm, $pkgid);
  $pkg or return;

  my $medium = pkg2medium($pkg, $urpm);
  my $xml_info = 'info';
  my $xml_info_file = urpm::media::any_xml_info($urpm, $medium, $xml_info, undef, undef);
  
  if(!$xml_info_file) {
    pk_print_details(get_package_id($pkg), "N/A", $pkg->group, "N/A", "N/A", 0);
    return;
  }
  
  require urpm::xml_info;
  require urpm::xml_info_pkg;
  my $name = urpm_name($pkg);
  my %nodes = eval { urpm::xml_info::get_nodes($xml_info, $xml_info_file, [ $name ]) };
  my %xml_info_pkgs;
  put_in_hash($xml_info_pkgs{$name} ||= {}, $nodes{$name});
  my $description = $xml_info_pkgs{$name}{description};
  $description =~ s/\n/;/g;
  $description =~ s/\t/ /g;
  
  pk_print_details(get_package_id($pkg), "N/A", $pkg->group, ensure_utf8($description), "N/A", $pkg->size);
}

sub _print_package_files {

  my ($urpm, $pkgid) = @_;

  my $pkg = get_package_by_package_id($urpm, $pkgid);
  $pkg or return;
  
  my $medium = pkg2medium($pkg, $urpm);
  my $xml_info = 'files';
  my $xml_info_file = urpm::media::any_xml_info($urpm, $medium, $xml_info, undef, undef);
  require urpm::xml_info;
  require urpm::xml_info_pkg;
  my $name = urpm_name($pkg);
  my %nodes = eval { urpm::xml_info::get_nodes($xml_info, $xml_info_file, [ $name ]) };
  my %xml_info_pkgs;
  put_in_hash($xml_info_pkgs{$name} ||= {}, $nodes{$name});
  my @files = map { chomp_($_) } split("\n", $xml_info_pkgs{$name}{files});
  
  pk_print_files(get_package_id($pkg), join(';', @files));
}

sub _print_package_update_details {

  my ($urpm, $pkgid) = @_;
  my $pkg = get_package_by_package_id($urpm, $pkgid);
  $pkg or return;

  my %requested;
  $requested{$pkg->id} = 1;
  my $state = {};
  my $restart = urpm::select::resolve_dependencies($urpm, $state, \%requested);
  my @ask_unselect = urpm::select::unselected_packages($urpm, $state);
  my @to_remove = urpm::select::removed_packages($urpm, $state);
  my @to_install = @{$urpm->{depslist}}[sort { $a <=> $b } keys %{$state->{selected}}]; 
  my ($src, $binary) = partition { $_->arch eq 'src' } @to_install;
  @to_install = @$binary;
  my $updates_descr = urpm::get_updates_description($urpm);
  my $updesc = $updates_descr->{URPM::pkg2media($urpm->{media}, $pkg)->{name}}{$pkg->name};
  my $desc;
  if($updesc) {
    $desc = $updesc->{pre};
    $desc =~ s/\n/;/g;
  }
  
  my @to_upgrade_pkids;
  foreach(@to_install) {
    my $pkid = get_installed_version_pkid($_);
    push @to_upgrade_pkids, $pkid if $pkid;
  }
  
  pk_print_update_detail(get_package_id($pkg),
    join("^", @to_upgrade_pkids),
    join("^", map(fullname_to_package_id($_), @to_remove)),
    "http://qa.mandriva.com",
    "http://qa.mandriva.com",
    "http://qa.mandriva.com",
    $restart ? PK_RESTART_ENUM_SYSTEM : PK_RESTART_ENUM_APPLICATION,
    $desc);
}
