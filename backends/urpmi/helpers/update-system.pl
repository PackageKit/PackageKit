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

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

perform_installation($urpm, {}, auto_select => 1);
