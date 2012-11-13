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

package perl_packagekit::prints;

use Exporter;

our @ISA = qw(Exporter);
our @EXPORT = qw(
  pk_print_package
  pk_print_status
  pk_print_details
  pk_print_files
  pk_print_update_detail
  pk_print_require_restart
  pk_print_error
  pk_print_percentage
  pk_print_sub_percentage
  pk_print_distro_upgrade
  pk_print_repo_details
  );

sub pk_print_package {
  # send 'package' signal
  # @param info: the enumerated INFO_* string
  # @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
  # @param summary: The package Summary
  my ($info, $id, $summary) = @_;
  printf("package\t%s\t%s\t%s\n", $info, $id, $summary);
}

sub pk_print_status {
  # send 'status' signal
  # @param state: STATUS_*
  my ($status) = @_;
  printf("status\t%s\n", $status);
}

sub pk_print_details {
  # Send 'details' signal
  # @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
  # @param license: The license of the package
  # @param group: The enumerated group
  # @param desc: The multi line package description
  # @param url: The upstream project homepage
  # @param bytes: The size of the package, in bytes
  my ($id, $license, $group, $desc, $url, $bytes) = @_;
  printf("details\t%s\t%s\t%s\t%s\t%s\t%ld\n", $id, $license, $group, $desc, $url, $bytes);
}

sub pk_print_files {
  # Send 'files' signal
  # @param file_list: List of the files in the package, separated by ';'
  my ($id, $file_list) = @_;
  printf("files\t%s\t%s\n", $id, $file_list);
}
    
sub pk_print_update_detail {
  # Send 'updatedetail' signal
  # @param id: The package ID name, e.g. openoffice-clipart;2.6.22;ppc64;fedora
  # @param updates:
  # @param obsoletes:
  # @param vendor_url:
  # @param bugzilla_url:
  # @param cve_url:
  # @param restart:
  # @param update_text:
  my ($id, $updates, $obsoletes, $vendor_url, $bugzilla_url, $cve_url, $restart, $update_text) = @_;
  printf("updatedetail\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", $id, $updates, $obsoletes, $vendor_url, $bugzilla_url, $cve_url, $restart, $update_text, '', '', '', '');
}
    
sub pk_print_require_restart {
  # Send 'requirerestart' signal
  # @param restart_type: RESTART_SYSTEM, RESTART_APPLICATION,RESTART_SESSION
  # @param details: Optional details about the restart
  my ($restart_type, $details) = @_;
  printf("requirerestart\t%s\t%s\n", $restart_type, $details);
}
    
sub pk_print_error {
  # send 'error'
  # @param err: Error Type ERROR_*
  # @param description: Error description
  # @param exit: exit application with rc=1, if true
  my ($err, $description) = @_;
  printf("error\t%s\t%s\n", $err, $description);
  exit() if $exit; # FIXME: undeclared variable $exit
}

sub pk_print_percentage {
  my ($percentage) = @_;
  printf("percentage\t%i\n", $percentage);
}

sub pk_print_sub_percentage {
  my ($sub_percentage) = @_;
  printf("subpercentage\t%i\n", $sub_percentage);
}

sub pk_print_distro_upgrade {
  my ($dtype, $name, $summary) = @_;
  printf("distro-upgrade\t%s\t%s\t%s\n", $dtype, $name, $summary);
}

sub pk_print_repo_details {
  my ($id, $description, $enabled) = @_;
  printf("repo-detail\t%s\t%s\t%s\n", $id, $description, $enabled);
}

1;
