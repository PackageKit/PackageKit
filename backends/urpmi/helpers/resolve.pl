#!/usr/bin/perl

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
exit if ($#ARGV != 1);
my @filters = split(/;/, $ARGV[0]);
my $search_term = $ARGV[1];

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my @names = ( $search_term );
my %requested;
my $result = urpm::select::search_packages($urpm, \%requested, \@names, 
  fuzzy => 0, 
  caseinsensitive => 0,
  all => 0
);

if($result) {
  my @requested_keys = keys %requested;
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  my $pkg = @{$urpm->{depslist}}[$requested_keys[0]];

  # We exit the script if found package does not match with
  # specified filters
  if(!filter($pkg, \@filters, {FILTER_DEVELOPMENT => 1, FILTER_GUI => 1})) {
    exit;
  }
  if($pkg->version."-".$pkg->release eq find_installed_version($pkg)) {
    if(grep(/^${\FILTER_NOT_INSTALLED}$/, @filters)) {
      exit;
    }
    pk_print_package(INFO_INSTALLED, get_package_id($pkg), $pkg->summary);
  }
  else {
    if(grep(/^${\FILTER_INSTALLED}$/, @filters)) {
      exit;
    }
    pk_print_package(INFO_AVAILABLE, get_package_id($pkg), $pkg->summary);
  }
}
else {
  pk_print_error(PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Can't find any package for the specified name");
}

