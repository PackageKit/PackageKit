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
use urpmi_backend::tools;
use urpmi_backend::open_db;
use perl_packagekit::enums;
use perl_packagekit::prints;
use MDK::Common;

# One argument (package id)
exit if($#ARGV != 0);

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my ($pkg_name) = split(/;/, @ARGV[0]);

my %requested;

my $result = urpm::select::search_packages($urpm, \%requested, [ $pkg_name ], 
  fuzzy => 0, 
  caseinsensitive => 0,
  all => 0);

if(!$result) {
  exit;
}

my @requested_keys = keys %requested;
my $pkg = @{$urpm->{depslist}}[pop @requested_keys];
my $pkg_upgrade = get_package_upgrade($urpm, $pkg);

if(!find_installed_version($pkg)) {
  pk_print_error(PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "The selected package isn't installed on your system");
}
elsif(!$pkg_upgrade) {
  pk_print_error(PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED, "The selected package is already at the latest version");
}
else {
  my $state = {};
  my $restart = urpm::select::resolve_dependencies($urpm, $state, \%requested);
  my %selected = %{$state->{selected}};
  my @ask_unselect = urpm::select::unselected_packages($urpm, $state);
  my @to_remove = urpm::select::removed_packages($urpm, $state);
  my @to_install = @{$urpm->{depslist}}[sort { $a <=> $b } keys %{$state->{selected}}]; 
  my ($src, $binary) = partition { $_->arch eq 'src' } @to_install;
  @to_install = @$binary;
  my $updates_descr = urpm::get_updates_description($urpm);
  my $updesc = $updates_descr->{URPM::pkg2media($urpm->{media}, $pkg_upgrade)->{name}}{$pkg_upgrade->name};
  my $desc;
  if($updesc) {
    $desc = $updesc->{pre};
    $desc =~ s/\n/;/g;
  }

  if($restart) {
    pk_print_update_detail(get_package_id($pkg),
      join("^", map(get_package_id($_), @to_install)),
      join("^", map(fullname_to_package_id($_), @to_remove)),
      "http://qa.mandriva.com",
      "http://qa.mandriva.com",
      "http://qa.mandriva.com",
      PK_RESTART_ENUM_SYSTEM,
      $desc);
  }
  else {
    pk_print_update_detail(get_package_id($pkg),
      join("^", map(get_package_id($_), @to_install)),
      join("^", map(fullname_to_package_id($_), @to_remove)),
      "http://qa.mandriva.com",
      "http://qa.mandriva.com",
      "http://qa.mandriva.com",
      PK_RESTART_ENUM_APPLICATION,
      $desc);
  }

}
