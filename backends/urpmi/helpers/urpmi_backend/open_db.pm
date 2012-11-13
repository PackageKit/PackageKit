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

package urpmi_backend::open_db;

use strict;

use MDK::Common;

use urpm;
use urpm::media;
use urpm::select;

use URPM;

use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(fast_open_urpmi_db open_urpmi_db open_rpm_db);

# Note that most part of this perl module
# is extracted from Rpmdrake

sub fast_open_urpmi_db() {
    my $urpm = urpm->new;
    $urpm->get_global_options;
    urpm::media::read_config($urpm);
    $urpm;
}

sub open_urpmi_db {
    my (%urpmi_options) = @_;
    my $urpm = fast_open_urpmi_db();
    my $media = ''; # See Rpmdrake source code for more information.

    my $searchmedia = $urpmi_options{update} ? undef : join(',', get_inactive_backport_media($urpm));
    $urpm->{lock} = urpm::lock::urpmi_db($urpm, undef, wait => $urpm->{options}{wait_lock});
    my $previous = ''; # Same as $media above.
    urpm::select::set_priority_upgrade_option($urpm, (ref $previous ? join(',', @$previous) : ()));
    urpm::media::configure($urpm, media => $media, if_($searchmedia, searchmedia => $searchmedia), %urpmi_options);
    $urpm;
}

sub get_inactive_backport_media {
    my ($urpm) = @_;
    map { $_->{name} } grep { $_->{ignore} && $_->{name} =~ /backport/i } @{$urpm->{media}};
}

sub open_rpm_db() {
  URPM::DB::open() or die "Couldn't open RPM DB";
}

