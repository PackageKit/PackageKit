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
use urpm::args;
use urpmi_backend::actions;

# No arguments
exit if($#ARGV != -1);

#my $urpm = urpm->new_parse_cmdline;
my $urpm = urpm->new_parse_cmdline;
my $urpmi_lock = urpm::lock::urpmi_db($urpm, 'exclusive', wait => 0);
urpm::media::read_config($urpm);

my @entries = map { $_->{name} } @{$urpm->{media}};
@entries == 0 and die N("nothing to update (use urpmi.addmedia to add a media)\n");

my %options = ( all => 1 );

my $ok = urpm::media::update_media($urpm, %options, 
  quiet => 0);
exit($ok ? 0 : 1);
