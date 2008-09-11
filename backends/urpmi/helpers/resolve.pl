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
use urpmi_backend::open_db;
use urpmi_backend::tools;
use urpmi_backend::filters;
use perl_packagekit::enums;
use perl_packagekit::prints;

# Two arguments (filter and package name)
$#ARGV == 1 or exit 1;
my @filters = split(/;/, $ARGV[0]);
my @patterns = split(/\|/, $ARGV[1]);

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

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

