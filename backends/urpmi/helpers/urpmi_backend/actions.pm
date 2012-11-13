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

package urpmi_backend::actions;

use strict;

use urpm;
use urpm::args;
use urpm::msg;
use urpm::main_loop;
use urpm::lock;
use urpmi_backend::tools;
use urpmi_backend::open_db;
use MDK::Common;
use perl_packagekit::enums;
use perl_packagekit::prints;

use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
perform_installation 
perform_file_search 
perform_requires_search
);

sub perform_installation {
  my ($urpm, $requested, %options) = @_;
  my $state = {};
  my $restart;
  my $no_remove = 0;

  # Here we lock urpmi & rpm databases
  # In third argument we can specified if the script must wait until urpmi or rpm
  # databases are locked
  my $lock = urpm::lock::urpmi_db($urpm, undef, wait => 0);
  my $rpm_lock = urpm::lock::rpm_db($urpm, 'exclusive');

  pk_print_status(PK_STATUS_ENUM_DEP_RESOLVE);

  $restart = urpm::select::resolve_dependencies($urpm, $state, $requested, auto_select => $options{auto_select});
  my %selected = %{$state->{selected} || {}};

  print "Dependencies = \n\t";
  print join("\n\t", map(@{$urpm->{depslist}}[$_]->name, keys %selected)), "\n";

  # Here we have packages which cannot be installed because of dependencies
  my @unselected_uninstalled = @{$state->{unselected_uninstalled} || []};
  if(@unselected_uninstalled) {
    my $list = join "\n", map { $_->name . '-' . $_->version . '-' . $_->release  } @unselected_uninstalled;
  }
  # Fix me !
  # Display warning (With pk enum?) which warning the user
  # that the following packages can't be installed because they depend
  # on packages that are older than the installed ones (copy/paste from
  # the diplayed message in urpmi)

  # Here we have packages which cannot be installed
  my @ask_unselect = urpm::select::unselected_packages($urpm, $state);
  if (@ask_unselect) {
    my $list = urpm::select::translate_why_unselected($urpm, $state, @ask_unselect);
  }
  # Fix me !
  # Display warning (With pk enum?) which warning the user
  # that the following packages can't be installed (copy/paste from
  # the diplayed message in urpmi)

  my @ask_remove = urpm::select::removed_packages($urpm, $state);
  if(@ask_remove) {
    my $db = urpm::db_open_or_die($urpm, $urpm->{root});
    urpm::select::find_removed_from_basesystem($urpm, $db, $state, sub {
        my $urpm = shift @_;
        foreach (@_) {
          # Fix me 
          # Someting like that. With a clean pk error enum.
          # printf ("removing package %s will break your system", $_);
        }
        @_ and $no_remove = 1;
      });
    my $list = urpm::select::translate_why_removed($urpm, $state, @ask_remove);
    if($no_remove) {
      # Fix me
      # Display message to prevent that the installation cannot continue because some
      # packages has to be removed for others to be upgraded.
      die;
    }
    # Else, it's ok.
    # Here we can display $list, which describe packages which has to be removed for
    # others to be upgraded.
    printf("Following package(s) will be removed for others to be upgraded:\n%s\n", $list);
  }

  # sorted by medium for format_selected_packages
  my @to_install = @{$urpm->{depslist}}[sort { $a <=> $b } keys %{$state->{selected}}]; 
  my ($src, $binary) = partition { $_->arch eq 'src' } @to_install;
  # With packagekit, we will never install src packages.
  @to_install = @$binary;

  print "\@to_install debug : \n\t";
  print join("\n\t", map(urpm_name($_), @to_install)), "\n";

  my $nb_to_install = $#to_install + 1;
  my $percentage = 0;

  $urpm->{nb_install} = @to_install;
  # For debug issue, we will display sizes
  my ($size, $filesize) = $urpm->selected_size_filesize($state);
  printf("%s of additional disk space will be used.\n", formatXiB($size));
  printf("%s of packages will be retrieved.\n", formatXiB($filesize));

  my $callback_inst = sub {
    my ($urpm, $type, $id, $subtype, $amount, $total) = @_;
    my $pkg = defined $id ? $urpm->{depslist}[$id] : undef;
    if ($subtype eq 'start') {
      if ($type eq 'trans') {
        print "Preparing packages installation ...\n";
        pk_print_status(PK_STATUS_ENUM_INSTALL);
      } 
      elsif (defined $pkg) {
        printf("Installing package %s ...\n", $pkg->name);
        pk_print_package(INFO_INSTALLING, get_package_id($pkg), $pkg->summary);
      }
    } 
    elsif ($subtype eq 'progress') {
      print "($type) Progress : total = ", $total, " ; amount/total = ", $amount/$total, " ; amount = ", $amount, "\n";
      if($type eq "inst") {
        pk_print_percentage($percentage + ($amount/$total)*(100/$nb_to_install));
        if(($amount/$total) == 1) {
          $percentage = $percentage + ($amount/$total)*(100/$nb_to_install);
        }
      }
    }
  };

  # Now, the script will call the urpmi main loop to make installation
  my $exit_code = urpm::main_loop::run($urpm, $state, undef, \@ask_unselect, $requested, {
      inst => $callback_inst,
      trans => $callback_inst,
      trans_log => sub {
        my ($mode, $file, $percent, $total, $eta, $speed) = @_;
        # Transfer log need to be improved.
        if($mode eq "progress") {
          pk_print_status(PK_STATUS_ENUM_DOWNLOAD);
        }
        elsif($mode eq "error") {
          pk_print_error(PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, "Please refresh your package list");
        }
        print "Install current mode = ", $mode, "\n";
      },
      bad_signature => sub {
        if($options{only_trusted} eq "yes") {
          pk_print_error(PK_ERROR_ENUM_GPG_FAILURE, "Bad or missing GPG signatures");
          undef $lock;
          undef $rpm_lock;
          die;
        }
      },
      ask_yes_or_no => sub {
        # Return 1 = Return Yes
        return 1;
      },
      need_restart => sub {
        my ($need_restart_formatted) = @_;
        print "$_\n" foreach values %$need_restart_formatted;
      },
      completed => sub {
        undef $lock;
        undef $rpm_lock;
      },
      post_download => sub {
        # Fix me !
        # At this point, we need to refuse cancel action
      },
    }
  );
}

sub perform_file_search {
  my ($urpm, $requested, $search_term, %options) = @_;
  my $db = open_rpm_db();
  $urpm->compute_installed_flags($db);

  my $xml_info = 'files';
  my %result_hash;

  # - For each medium, we browse the xml info file,
  # while looking for files which matched with the
  # search term given in argument. We store results 
  # in a hash.
  foreach my $medium (urpm::media::non_ignored_media($urpm)) {
    my $xml_info_file = urpm::media::any_xml_info($urpm, $medium, ( "files", "summary" ), undef, undef);
    $xml_info_file or next;
    require urpm::xml_info;
    require urpm::xml_info_pkg;
    my $F = urpm::xml_info::open_lzma($xml_info_file);
    my $fn;
    local $_;
    while (<$F>) {
      if (m!^<!) {
        ($fn) = /fn="(.*)"/;
      } 
      elsif ( (!$options{'fuzzy'} && $_ =~ /^$search_term$/)
        || ($options{'fuzzy'} && $_ =~ /$search_term/) ) {
        # Fix me : Replace with pk error enum.
        # $fn or $urpm->{fatal}("fast algorithm is broken, please report a bug");
        my $pkg = urpm::xml_info_pkg->new({ fn => $fn });
        $result_hash{$pkg->name} = $pkg;
      }
    }
  }

  # - In order to get package summaries, we need to
  # use the search package method from perl-URPM 
  # which return Package type on which we can call
  # methods to create the printing output.
  # (It's about the same code as search-name.pl)
  my @names = keys %result_hash;

  urpm::select::search_packages($urpm, $requested, \@names, 
    fuzzy => 0,
    caseinsensitive => 0,
    all => 0,);
}

sub perform_requires_search {

  my ($urpm, $pkgnames, $recursive_option) = @_;

  my (@properties, %requires, %properties, $dep);
  my %requested;
  urpm::select::search_packages($urpm, 
    \%requested, $pkgnames, 
    use_provides => 0,
    fuzzy => 0,
    all => 0
  );
  @properties = keys %requested;
  my $state = {};

  foreach my $pkg (@{$urpm->{depslist}}) {
    foreach ($pkg->requires_nosense) {
      $requires{$_}{$pkg->id} = undef;
    }
  }

  while (defined ($dep = shift @properties)) {
    my $packages = $urpm->find_candidate_packages($dep);
    foreach (values %$packages) {
      my ($best_requested, $best);
      foreach (@$_) {
        if ($best_requested || exists $requested{$_->id}) {
          if ($best_requested && $best_requested != $_) {
            $_->compare_pkg($best_requested) > 0 and $best_requested = $_;
          } else {
            $best_requested = $_;
          }
        } elsif ($best && $best != $_) {
          $_->compare_pkg($best) > 0 and $best = $_;
        } else {
          $best = $_;
        }
      }

      my $pkg = $best_requested || $best or next;
      exists $state->{selected}{$pkg->id} and next;
      $state->{selected}{$pkg->id} = undef;

      next if !$requested{$dep} && !$recursive_option;

      #- for all provides of package, look up what is requiring them.
      foreach ($pkg->provides) {
        if (my ($n, $s) = /^([^\s\[]*)(?:\[\*\])?\[?([^\s\]]*\s*[^\s\]]*)/) {
          if (my @l = grep { $_ ne $pkg->name } map { $_->name } $urpm->packages_providing($n)) {
            #- If another package provides this requirement,
            #- then don't bother finding stuff that needs it as it will be invalid
            # $urpm->{log}(sprintf "skipping package(s) requiring %s via %s, since %s is also provided by %s", $pkg->name, $n, $n, join(' ', @l));
            next;
          }

          foreach (map { $urpm->{depslist}[$_] }
            grep { ! exists $state->{selected}{$_} && ! exists $properties{$_} }
            keys %{$requires{$n} || {}}) {
            if (grep { URPM::ranges_overlap("$n $s", $_) } $_->requires) {
              push @properties, $_->id;
              # $urpm->{debug} and $urpm->{debug}(sprintf "adding package %s (requires %s%s)", $_->name, $pkg->name, $n eq $pkg->name ? '' : " via $n");
              $properties{$_->id} = undef;
            }
          }
        }
      }
    }
  }

  my @depslist = @{$urpm->{depslist}};
  my @requires = ();
  foreach(@depslist) {
    my $pkgid = $_->id;
    if(grep { /^$pkgid$/ } keys %{$state->{selected}}) {
      push @requires, $_;
    }
  }

  @requires;

}
