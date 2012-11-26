#
# Copyright (C) 2008 Aurelien Lefebvre <alkh@mandriva.org>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#

package urpmi_backend::tools;

use strict;

use MDK::Common;
use URPM;
use urpmi_backend::open_db;
use urpm::msg;

use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
  get_update_medias 
  rpm_description 
  urpm_name 
  find_installed_fullname 
  is_mageia
  is_package_installed
  get_package_id 
  ensure_utf8 
  pkg2medium 
  fullname_to_package_id
  get_package_by_package_id
  get_package_upgrade
  get_installed_fullname
  get_installed_fullname_pkid
);

sub get_update_medias {
  my ($urpm) = @_;
  grep { !$_->{ignore} && $_->{update} } @{$urpm->{media}};
}

sub rpm_description {
    my ($description) = @_;
    ensure_utf8($description);
    my ($t, $tmp);
    foreach (split "\n", $description) {
  s/^\s*//;
        if (/^$/ || /^\s*(-|\*|\+|o)\s/) {
            $t || $tmp and $t .= "$tmp\n";
            $tmp = $_;
        } else {
            $tmp = ($tmp ? "$tmp " : ($t && "\n") . $tmp) . $_;
        }
    }
    "$t$tmp\n";
}

sub urpm_name {
    return '?-?-?.?' unless ref $_[0];
    my ($name, $version, $release, $arch) = $_[0]->fullname;
    "$name-$version-$release.$arch";
}

# from rpmtools:
sub ensure_utf8 {
    if (utf8::is_utf8($_[0])) {
	utf8::valid($_[0]) and return;

	utf8::encode($_[0]); #- disable utf8 flag
	utf8::upgrade($_[0]);
    } else {
	utf8::decode($_[0]); #- try to set utf8 flag
	utf8::valid($_[0]) and return;
	warn "do not know what to with $_[0]\n";
    }
}

sub find_installed_fullname {
  my ($p) = @_;
  my @fullname;
  URPM::DB::open()->traverse_tag('name', [ $p->name ], sub { push @fullname, scalar($_[0]->fullname) });
  @fullname ? join(',', sort @fullname) : "";
}

sub is_package_installed {
    my ($pkg) = @_;
    return URPM::DB::open()->is_package_installed($pkg);
}

sub is_mageia() {
    cat_('/etc/release') =~ /Mageia/;
}

sub vendor() {
    is_mageia() ? "mageia" : "mandriva";
}

sub get_package_id {
  my ($pkg) = @_;
  return join(';', $pkg->name, $pkg->version . "-" . $pkg->release, $pkg->arch, vendor());
}

sub pkg2medium {
  my ($p, $urpm) = @_;
  return if !ref $p;
  return { name => N("None (installed)") } if !$p->id; # if installed
  URPM::pkg2media($urpm->{media}, $p) || undef;
}

sub fullname_to_package_id {
  # fullname, ie 'xeyes-1.0.1-5mdv2008.1.i586'
  my ($pkg_string) = @_;
  chomp($pkg_string);
  if ($pkg_string =~ /^(.*)-([^-]*)-([^-]*)\.([^\.]*)$/) {
      return join(';', $1, $2, $3, $4, vendor());
  }
}

sub get_package_by_package_id {
  my ($urpm, $package_id) = @_;
  my @depslist = @{$urpm->{depslist}};
  foreach (@depslist) {
    if (get_package_id($_) eq $package_id) {
      return $_;
    }
  }
  return;
}

sub get_package_upgrade {
  my ($urpm, $pkg) = @_;
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);
  my @depslist = @{$urpm->{depslist}};
  my $pkgname = $pkg->name;
  foreach (@depslist) {
    if ($_->name =~ /^$pkgname$/ && $_->flag_upgrade) {
      return $_;
    }
  }
}

sub get_installed_fullname {
  my ($urpm, $pkg) = @_;
  my @depslist = @{$urpm->{depslist}};
  my $pkgname = $pkg->name;
  foreach my $pkg (@depslist) {
    if ($pkg->name =~ /^$pkgname$/ && is_package_installed($pkg)) {
      return $pkg;
    }
  }
  return;
}

sub get_installed_fullname_pkid {
  my ($pkg) = @_;
  my $pkgname = $pkg->name;
  my $db = open_rpm_db();
  my $installed_pkid;
  $db->traverse(sub {
      my ($pkg) = @_;
      if ($pkg->name =~ /^$pkgname$/) {
        $installed_pkid = get_package_id($pkg);
      }
    });
  return $installed_pkid;
}
