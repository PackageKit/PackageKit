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

use urpmi_backend::actions;
use urpmi_backend::tools;

# One or more arguments (Package ids)
exit if($#ARGV < 0);

my @packageidstab = split(/\|/, $ARGV[0]);

my @names;
foreach(@packageidstab) {
  my @pkg_id = (split(/;/, $_));
  push @names, $pkg_id[0];
}

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my %requested;

urpm::select::search_packages($urpm, \%requested, \@names, 
  fuzzy => 0, 
  caseinsensitive => 0,
  all => 0);

eval {
  perform_installation($urpm, \%requested);
};
pk_print_status(PK_STATUS_ENUM_FINISHED);
