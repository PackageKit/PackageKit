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
use urpmi_backend::actions;
use urpmi_backend::filters;
use perl_packagekit::enums;
use perl_packagekit::prints;

# 3 arguments
# (filter, packageid, and recursive option)
exit if($#ARGV != 2);

my @filters = split(/;/, $ARGV[0]);
my $pkgid = $ARGV[1];
my $recursive_option = 0;
$recursive_option = 1 if($ARGV[2] eq "yes");

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my $pkg = get_package_by_package_id($urpm, $pkgid);
if(!$pkg) {
  pk_print_error(PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Requested package wasn't found");
  exit;
}

pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);
my @requires = perform_requires_search($urpm, $pkg, $recursive_option);

foreach(@requires) {
  if(filter($_, \@filters, { FILTER_GUI => 1, FILTER_DEVELOPMENT => 1 })) {
    if(package_version_is_installed($_)) {
      !grep(/^${\FILTER_NOT_INSTALLED}$/, @filters) and pk_print_package(INFO_INSTALLED, get_package_id($_), $_->summary);
    }
    else {
      !grep(/^${\FILTER_INSTALLED}$/, @filters) and pk_print_package(INFO_AVAILABLE, get_package_id($_), $_->summary);
    }
  }
}
pk_print_status(PK_STATUS_ENUM_FINISHED);

