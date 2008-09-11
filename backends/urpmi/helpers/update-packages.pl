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
use urpm::select;
use urpm::args;
use urpmi_backend::tools;
use urpmi_backend::open_db;
use urpmi_backend::actions;

# This with one or more package ids
exit if($#ARGV < 0);

my @names;
foreach(@ARGV) {
  print "-->", $_, "<--", "\n";
  my @pkgid = split(/;/, $_);
  push @names, $pkgid[0];
}

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my $db = open_rpm_db();
$urpm->compute_installed_flags($db);

my %requested;

my @depslist = @{$urpm->{depslist}};
my $pkg = undef;
foreach my $depslistpkg (@depslist) {
  foreach my $name (@names) {
    if($depslistpkg->name =~ /^$name$/ && $depslistpkg->flag_upgrade) {
      $requested{$depslistpkg->id} = 1;
      goto tonext;
    }
  }
  tonext:
}

perform_installation($urpm, \%requested);

