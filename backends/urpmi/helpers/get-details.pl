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
use urpmi_backend::tools;
use MDK::Common;

use perl_packagekit::prints;

# Only one argument authorized
# (The Package ID)
exit if($#ARGV != 0);

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

my $pkg = get_package_by_package_id($urpm, $ARGV[0]);

my $medium = pkg2medium($pkg, $urpm);
my $xml_info = 'info';
my $xml_info_file = urpm::media::any_xml_info($urpm, $medium, $xml_info, undef, undef);
require urpm::xml_info;
require urpm::xml_info_pkg;
my $name = urpm_name($pkg);
my %nodes = eval { urpm::xml_info::get_nodes($xml_info, $xml_info_file, [ $name ]) };
my %xml_info_pkgs;
put_in_hash($xml_info_pkgs{$name} ||= {}, $nodes{$name});
my $description = $xml_info_pkgs{$name}{description};
$description =~ s/\n/;/g;

pk_print_details(get_package_id($pkg), "N/A", $pkg->group, $description, "N/A", $pkg->size);

