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
use urpm::select;
use MDK::Common;
use urpmi_backend::tools;
use perl_packagekit::enums;
use perl_packagekit::prints;

# On argument (filter)
exit if($#ARGV != 0);
# Fix me
# Filter are to be implemented.

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my $state = {};
my %requested;
my $restart = urpm::select::resolve_dependencies($urpm, $state, \%requested,
  auto_select => 1);

my %selected = %{$state->{selected} || {}};
my @ask_unselect = urpm::select::unselected_packages($urpm, $state);
my @to_remove = urpm::select::removed_packages($urpm, $state);
my @to_install = @{$urpm->{depslist}}[sort { $a <=> $b } keys %{$state->{selected}}]; 
my ($src, $binary) = partition { $_->arch eq 'src' } @to_install;
@to_install = @$binary;
my $updates_descr = urpm::get_updates_description($urpm);

foreach(@to_install) {
  my $updesc = $updates_descr->{URPM::pkg2media($urpm->{media}, $_)->{name}}{$_->name};
  if($updesc->{importance} eq "bugfix") {
    pk_print_package(INFO_BUGFIX, get_package_id($_), $_->summary);
  }
  elsif($updesc->{importance} eq "security") {
    pk_print_package(INFO_SECURITY, get_package_id($_), $_->summary);
  }
  else {
    pk_print_package(INFO_NORMAL, get_package_id($_), $_->summary);
  }
}
