#!/usr/bin/perl

use strict;

use perl_packagekit::prints;
use perl_packagekit::enums;

pk_print_status(PK_STATUS_ENUM_QUERY);

open(PRODUCT_FILE, "/etc/product.id");

my %product_id;
%product_id = parse_line(<PRODUCT_FILE>);
close(PRODUCT_FILE);

my $distribfile_path = "/tmp/distrib.list";
download_distrib_file($distribfile_path, \%product_id);

-f $distribfile_path or finished(1);

my @distribs;
open(DISTRIB_FILE, $distribfile_path);
while(<DISTRIB_FILE>) {
  my %distrib = parse_line($_);
  push(@distribs, \%distrib);
}

my $distrib;
foreach (@distribs) {
  if($_->{version} == $product_id{version}) {
    $distrib = $_;
  }
}

$distrib or finished(0);
@distribs = sort { $b->{release_date} <=> $a->{release_date} } @distribs;

my $newer_version = get_newer_distrib($distrib->{version}, \@distribs);
$newer_version or finished(0);
pk_print_distro_upgrade(PK_DISTRO_UPGRADE_ENUM_STABLE, join(" ", "Mandriva", $product_id{product}, $newer_version->{version}), "");

unlink($distribfile_path);
pk_print_status(PK_STATUS_ENUM_FINISHED);

sub parse_line {
  my ($line) = @_;
  my %hash;
  my @affects = split(/,/, $line);
  foreach my $affect (@affects) {
    my ($variable, $value) = split(/=/, $affect);
    chomp($variable);
    chomp($value);
    $hash{$variable} = $value;
  }
  return %hash;
}

sub download_distrib_file {

  my ($outfile, $product_id) = @_;
  
  -x "/usr/bin/wget" or die "wget is missing\n";
  
  my $api_url = sprintf("http://api.mandriva.com/distributions/%s.%s.list?product=%s",
                  lc($product_id->{type}),
                  lc($product_id->{arch}),
                  lc($product_id->{product}));
  
  my $wget_command = join(" ", 
                          "/usr/bin/wget",
                          "--quiet",
                          "--output-document", $outfile,
                          $api_url);
  
  my $wget_pid = open(my $wget, "$wget_command |");
  close($wget);
}

sub get_newer_distrib {

  my ($installed_version, $distrib_list) = @_;
  my $installed_distrib;
  foreach (@$distrib_list) {
    if($_->{version} == $installed_version) {
      $installed_distrib = $_;
    }
  }
  $installed_distrib or return;
  foreach (@$distrib_list) {
    if($installed_distrib->{release_date} < $_->{release_date}) {
      return $_;
    }
  }
}

sub finished {
  my ($exit_status) = @_;
  pk_print_status(PK_STATUS_ENUM_FINISHED);
  exit $exit_status;
}
