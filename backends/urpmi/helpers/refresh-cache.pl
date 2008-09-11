#!/usr/bin/perl
#
# Copyright (C) 2008 Aurelien Lefebvre <alefebvre@mandriva.com>
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#

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
use perl_packagekit::enums;
use perl_packagekit::prints;

# No arguments
$#ARGV != -1 and exit 1;

my $urpm = urpm->new_parse_cmdline;
$urpm->{fatal} = sub { 
    pk_print_error(PK_ERROR_ENUM_TRANSACTION_ERROR, $_[1]."\n"); 
    exit($_[0]) 
};
my $urpmi_lock = urpm::lock::urpmi_db($urpm, 'exclusive', wait => 0);
urpm::media::read_config($urpm);

my @entries = map { $_->{name} } @{$urpm->{media}};
@entries == 0 and pk_print_error(PK_ERROR_ENUM_TRANSACTION_ERROR, "nothing to update (use urpmi.addmedia to add a media)\n");

my %options = ( all => 1 );

my $ok = urpm::media::update_media($urpm, %options, quiet => 0);
exit($ok ? 0 : 1);
