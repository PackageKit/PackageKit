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
use urpm::install;
use urpmi_backend::tools;
use perl_packagekit::enums;
use perl_packagekit::prints;

my $notfound = 0;
my @breaking_pkgs = ();
my $allowdeps_option = 0;
my @pkgid;
my $state = {};
my $notfound_callback = sub {
  $notfound = 1;
};

# At least two arguments (allow deps & a package id list)
exit if($#ARGV < 1);

my $urpm = urpm->new_parse_cmdline;
my $urpmi_lock = urpm::lock::urpmi_db($urpm, 'exclusive', wait => 1);
urpm::media::configure($urpm);

$allowdeps_option = shift @ARGV;

my @names;
foreach(@ARGV) {
  my @pkg_id = (split(/;/, $_));
  push @names, $pkg_id[0];
}

pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);

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
  pk_print_error(PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "Selected package isn't installed on your system");
}
elsif(@breaking_pkgs) {
  pk_print_error(PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE, "Removing selected packages will break your system");
}
else {
  if(!$allowdeps_option && $#to_remove > 1) {
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
}

$urpmi_lock->unlock;

pk_print_status(PK_STATUS_ENUM_FINISHED);
