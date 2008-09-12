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
# get-details
# get-distro-upgrades
# get-files
# get-packages
# get-requires
# get-update-detail
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
# urpmi-dispatched-backend
#

use strict;

use lib;
use File::Basename;

use URPM;
use urpm;
use urpm::media;
use urpm::args;
use urpm::select;

use urpmi_backend::open_db;
use urpmi_backend::tools;
use urpmi_backend::filters;

use perl_packagekit::enums;
use perl_packagekit::prints;

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

sub search_name {

  my ($urpm, $filters, $search_term) = @_;

  my @filterstab = split(/;/, $filters);
  
  my $basename_option = FILTER_BASENAME;
  $basename_option = grep(/$basename_option/, @filterstab);

  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  $db->close();
  
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
