#!/usr/bin/perl

use strict;

use lib;
use File::Basename;

BEGIN {
  push @INC, dirname($0);
}

use urpm;
use urpm::media;

use urpmi_backend::actions;
use urpmi_backend::filters;
use urpmi_backend::tools;
use perl_packagekit::prints;
use perl_packagekit::enums;

# Two arguments (filter and search term)
exit if ($#ARGV != 1);
my @filters = split(/;/, $ARGV[0]);
my $search_term = $ARGV[1];

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my %requested;

pk_print_status(PK_STATUS_ENUM_QUERY);

perform_file_search($urpm, \%requested, $search_term, fuzzy => 1);

foreach(keys %requested) {
  my $p = @{$urpm->{depslist}}[$_];
  if(filter($p, \@filters, { FILTER_INSTALLED => 1, FILTER_DEVELOPMENT=> 1, FILTER_GUI => 1})) {
    my $version = find_installed_version($p);
    if($version eq $p->version."-".$p->release) {
      pk_print_package(INFO_INSTALLED, get_package_id($p), ensure_utf8($p->summary));
    }
    else {
      pk_print_package(INFO_AVAILABLE, get_package_id($p), ensure_utf8($p->summary));
    }
  }
}

pk_print_status(PK_STATUS_ENUM_FINISHED);
