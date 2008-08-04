#!/usr/bin/perl

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

my @names;
foreach(@ARGV) {
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

perform_installation($urpm, \%requested);
