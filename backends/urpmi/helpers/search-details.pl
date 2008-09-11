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
use urpm::media;
use urpm::args;

use urpmi_backend::open_db;
use urpmi_backend::tools;
use urpmi_backend::filters;

use perl_packagekit::enums;
use perl_packagekit::prints;

# Two arguments (filterand search term)
exit if ($#ARGV != 1);
my @filters = split(/;/, $ARGV[0]);
my $search_term = $ARGV[1];

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my $db = open_rpm_db();
$urpm->compute_installed_flags($db);

# Here we display installed packages
if(not grep(/^${\FILTER_NOT_INSTALLED}$/, @filters)) {
  $db->traverse(sub {
      my ($pkg) = @_;
      if(filter($pkg, \@filters, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
        if($pkg->name =~ /$search_term/ || $pkg->summary =~ /$search_term/ || $pkg->url =~ /$search_term/) {
          pk_print_package(INFO_INSTALLED, get_package_id($pkg), ensure_utf8($pkg->summary));
        }
      }
    });
}

# Here are package which can be installed
if(not grep(/^${\FILTER_INSTALLED}$/, @filters)) {
  foreach my $pkg(@{$urpm->{depslist}}) {
    if($pkg->flag_upgrade) {
      if(filter($pkg, \@filters, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
        if($pkg->name =~ /$search_term/ || $pkg->summary =~ /$search_term/ || $pkg->url =~ /$search_term/) {
          pk_print_package(INFO_AVAILABLE, get_package_id($pkg), ensure_utf8($pkg->summary));
        }
      }
    }  
  }
}
