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

package urpmi_backend::groups;

use strict;

use perl_packagekit::enums;
use Exporter;

our @ISA = qw(Exporter);
our @EXPORT = qw(
  MDV_GROUPS
  get_mdv_groups 
  get_pk_group 
  package_belongs_to_pk_group
);

use constant MDV_GROUPS => {
    'Accessibility' => GROUP_ACCESSIBILITY,
    'Archiving/Backup' => GROUP_OTHER,
    'Archiving/Cd burning' => GROUP_MULTIMEDIA,
    'Archiving/Compression' => GROUP_ACCESSORIES,
    'Archiving/Other' => GROUP_OTHER,
    'Books/Computer books' => GROUP_OTHER,
    'Books/Faqs' => GROUP_OTHER,
    'Books/Howtos' => GROUP_OTHER,
    'Books/Literature' => GROUP_OTHER,
    'Books/Other' => GROUP_OTHER,
    'Communications' => GROUP_COMMUNICATION,
    'Databases' => GROUP_PROGRAMMING,
    'Development/C' => GROUP_PROGRAMMING,
    'Development/C++' => GROUP_PROGRAMMING,
    'Development/Databases' => GROUP_PROGRAMMING,
    'Development/GNOME and GTK+' => GROUP_PROGRAMMING,
    'Development/Java' => GROUP_PROGRAMMING,
    'Development/KDE and Qt' => GROUP_PROGRAMMING,
    'Development/Kernel' => GROUP_PROGRAMMING,
    'Development/Other' => GROUP_PROGRAMMING,
    'Development/Perl' => GROUP_PROGRAMMING,
    'Development/PHP' => GROUP_PROGRAMMING,
    'Development/Python' => GROUP_PROGRAMMING,
    'Development/Ruby' => GROUP_PROGRAMMING,
    'Development/X11' => GROUP_PROGRAMMING,
    'Editors' => GROUP_ACCESSORIES,
    'Education' => GROUP_EDUCATION,
    'Emulators' => GROUP_VIRTUALIZATION,
    'File tools' => GROUP_ACCESSORIES,
    'Games/Adventure' => GROUP_GAMES,
    'Games/Arcade' => GROUP_GAMES,
    'Games/Boards' => GROUP_GAMES,
    'Games/Cards' => GROUP_GAMES,
    'Games/Other' => GROUP_GAMES,
    'Games/Puzzles' => GROUP_GAMES,
    'Games/Sports' => GROUP_GAMES,
    'Games/Strategy' => GROUP_GAMES,
    'Graphical desktop/Enlightenment' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/FVWM based' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/GNOME' => GROUP_DESKTOP_GNOME,
    'Graphical desktop/Icewm' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/KDE' => GROUP_DESKTOP_KDE,
    'Graphical desktop/Other' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/Sawfish' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/WindowMaker' => GROUP_DESKTOP_OTHER,
    'Graphical desktop/Xfce' => GROUP_DESKTOP_XFCE,
    'Graphics' => GROUP_GRAPHICS,
    'Monitoring' => GROUP_NETWORK,
    'Networking/Chat' => GROUP_INTERNET,
    'Networking/File transfer' =>  GROUP_INTERNET,
    'Networking/IRC' => GROUP_INTERNET,
    'Networking/Instant messaging' => GROUP_INTERNET,
    'Networking/Mail' => GROUP_INTERNET,
    'Networking/News' => GROUP_INTERNET,
    'Networking/Other' => GROUP_INTERNET,
    'Networking/Remote access' => GROUP_INTERNET,
    'Networking/WWW' => GROUP_INTERNET,
    'Office' => GROUP_OFFICE,
    'Publishing' => GROUP_PUBLISHING,
    'Sciences/Astronomy' => GROUP_OTHER,
    'Sciences/Biology' => GROUP_OTHER,
    'Sciences/Chemistry' => GROUP_OTHER,
    'Sciences/Computer science' => GROUP_OTHER,
    'Sciences/Geosciences' => GROUP_OTHER,
    'Sciences/Mathematics' => GROUP_OTHER,
    'Sciences/Other' => GROUP_OTHER,
    'Sciences/Physics' => GROUP_OTHER,
    'Shells' => GROUP_SYSTEM,
    'Sound' => GROUP_MULTIMEDIA,
    'System/Base' => GROUP_SYSTEM,
    'System/Cluster' => GROUP_SYSTEM,
    'System/Configuration/Boot and Init' => GROUP_SYSTEM,
    'System/Configuration/Hardware' => GROUP_SYSTEM,
    'System/Configuration/Networking' => GROUP_SYSTEM,
    'System/Configuration/Other' => GROUP_SYSTEM,
    'System/Configuration/Packaging' => GROUP_SYSTEM,
    'System/Configuration/Printing' => GROUP_SYSTEM,
    'System/Fonts/Console' => GROUP_FONTS,
    'System/Fonts/True type' => GROUP_FONTS,
    'System/Fonts/Type1' => GROUP_FONTS,
    'System/Fonts/X11 bitmap' => GROUP_FONTS,
    'System/Internationalization' => GROUP_LOCALIZATION,
    'System/Kernel and hardware' => GROUP_SYSTEM,
    'System/Libraries' => GROUP_SYSTEM,
    'System/Printing' => GROUP_SYSTEM,
    'System/Servers' => GROUP_SYSTEM,
    'System/X11' => GROUP_SYSTEM,
    'Terminals' => GROUP_SYSTEM,
    'Text tools' => GROUP_ACCESSORIES,
    'Toys' => GROUP_GAMES,
    'Video' => GROUP_MULTIMEDIA
  };

sub get_mdv_groups {
  my ($pk_group) = @_;
  my @groups;
  foreach(keys %{(MDV_GROUPS)}) {
    if((MDV_GROUPS)->{$_} eq $pk_group) {
      push @groups, $_;
    }
  }
  return @groups;
}

sub get_pk_group {
  my ($mdv_group) = @_;
  if((MDV_GROUPS)->{$mdv_group} eq "") {
    return GROUP_UNKNOWN;
  }
  return (MDV_GROUPS)->{$mdv_group};
}

sub package_belongs_to_pk_group {
  my ($pkg, $pk_group) = @_;
  my @groups = get_mdv_groups($pk_group);
  my $pkg_group = $pkg->group;
  return grep { /$pkg_group/ } @groups;
}

1;

