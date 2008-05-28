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

# do not pay the urpm::media::configure() heavy cost:
sub fast_open_urpmi_db() {
    my $urpm = urpm->new;
    $urpm->get_global_options;
    my $error_happened;
    #$urpm->{options}{wait_lock} = $::rpmdrake_options{'wait-lock'};
    #$urpm->{options}{'verify-rpm'} = !$::rpmdrake_options{'no-verify-rpm'} if defined $::rpmdrake_options{'no-verify-rpm'};
    #$urpm->{options}{auto} = $::rpmdrake_options{auto} if defined $::rpmdrake_options{auto};
    #urpm::set_files($urpm, $::rpmdrake_options{'urpmi-root'}[0]) if $::rpmdrake_options{'urpmi-root'}[0];
    #urpm::args::set_root($urpm, $::rpmdrake_options{'rpm-root'}[0]) if $::rpmdrake_options{'rpm-root'}[0];

    #$urpm::args::rpmdrake_options{justdb} = $::rpmdrake_options{justdb};

    #$urpm->{fatal} = sub {
    #    $error_happened = 1;
    #    interactive_msg(N("Fatal error"),
    #                     N("A fatal error occurred: %s.", $_[1]));
    #};

    urpm::media::read_config($urpm);
    # FIXME: seems uneeded with newer urpmi:
    #if ($error_happened) {
    #    touch('/etc/urpmi/urpmi.cfg');
    #    exec('edit-urpm-sources.pl');
    #}
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

sub open_rpm_db {
#    my ($o_force) = @_;
#    my $host;
#    log::explanations("opening the RPM database");
#    if ($::rpmdrake_options{parallel} && ((undef, $host) = @{$::rpmdrake_options{parallel}})) {
#        state $done;
#        my $dblocation = "/var/cache/urpmi/distantdb/$host";
#        if (!$done || $o_force) {
#            print "syncing db from $host to $dblocation...";
#            mkdir_p "$dblocation/var/lib/rpm";
#            system "rsync -Sauz -e ssh $host:/var/lib/rpm/ $dblocation/var/lib/rpm";
#            $? == 0 or die "Couldn't sync db from $host to $dblocation";
#            $done = 1;
#            print "done.\n";
#        }
#        URPM::DB::open($dblocation) or die "Couldn't open RPM DB";
#    } else {
        URPM::DB::open() or die "Couldn't open RPM DB";
#    }
}

