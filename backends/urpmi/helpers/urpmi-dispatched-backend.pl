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
# get-updates
# install-packages
# refresh-cache
# remove-packages
# resolve
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
  if($command eq "search-name") {
    search_name($urpm, @args);
  }
  elsif($command eq "get-depends") {
    get_depends($urpm, @args);
  }
  elsif($command eq "get-details") {
    get_details($urpm, @args);
  }
  elsif($command eq "get-files") {
    get_files($urpm, @args);
  }
  elsif($command eq "get-packages") {
    get_packages($urpm, @args);
  }
  elsif($command eq "get-requires") {
    get_requires($urpm, @args);
  }
  elsif($command eq "get-update-detail") {
    get_update_detail($urpm, @args);
  }
}



sub get_depends {

  my ($urpm, $filters, $packageids, $recursive_option) = @_;
  
  my @filterstab = split(/;/, $filters);
  my @packageidstab = split(/\|/, $packageids);
  $recursive_option = 1;
  
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

  my ($urpm, $packageids) = @_;
  
  my @packageidstab = split(/\|/, $packageids);
  pk_print_status(PK_STATUS_ENUM_QUERY);

  foreach (@packageidstab) {
    _print_package_details($urpm, $_);
  }
  _finished();
}

sub get_files {
  
  my ($urpm, $packageids) = @_;
  
  my @packageidstab = split(/\|/, $packageids);
  pk_print_status(PK_STATUS_ENUM_QUERY);
  
  foreach (@packageidstab) {
    _print_package_files($urpm, $_);
  }
  _finished();
}

sub get_packages {

  my ($urpm, $filters) = @_;
  my @filterstab = split(/;/, $filters);
  
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
  
  my ($urpm, $filters, $packageids, $recursive_option) = @_;
  
  my @filterstab = split(/;/, $filters);
  my @packageidstab = split(/\|/, $packageids);
  my $recursive = $recursive_option eq "yes" ? 1 : 0;
  
  my @pkgnames;
  foreach (@packageidstab) {
    my $pkg = get_package_by_package_id($urpm, $_);
    $pkg and push(@pkgnames, $pkg->name);
  }
  
  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);
  my @requires = perform_requires_search($urpm, \@pkgnames, $recursive);
  
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

  my ($urpm, $packageids) = @_;
  
  pk_print_status(PK_STATUS_ENUM_QUERY);
  my @packageidstab = split(/\|/, $packageids);
  
  foreach (@packageidstab) {
    _print_package_update_details($urpm, $_);
  }
  _finished();
}

sub search_name {

  my ($urpm, $filters, $search_term) = @_;
  
  pk_print_status(PK_STATUS_ENUM_QUERY);

  my @filterstab = split(/;/, $filters);
  
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
