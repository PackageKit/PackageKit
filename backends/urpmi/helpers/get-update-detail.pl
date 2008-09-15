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

use strict;

use lib;
use File::Basename;

BEGIN {
  push @INC, dirname($0);
}

use urpm;
use urpm::args;
use urpm::media;
use urpm::select;
use urpmi_backend::tools;
use urpmi_backend::open_db;
use perl_packagekit::enums;
use perl_packagekit::prints;
use MDK::Common;

# One argument (package ids)
$#ARGV > -1 or exit 1;

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my @pkgids = @ARGV;

foreach (@pkgids) {
  print_package_update_details($urpm, $_);
}

sub print_package_update_details {

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
