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

# No arguments
exit if($#ARGV != -1);

my $urpm = urpm->new_parse_cmdline;
urpm::media::configure($urpm);

perform_installation($urpm, {}, auto_select => 1);
