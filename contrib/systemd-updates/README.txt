           OFFLINE UPDATES USING PACKAGEKIT AND SYSTEMD

This functionality adds the offline updates feature to PackageKit as
described in http://freedesktop.org/wiki/Software/systemd/OSUpgrade

What do do if you're a backend:

 * If the transaction_flags contain PREPARE then download all the
   metadata and packages for the transaction, but don't actually apply
   the transaction.

What to do if you're a frontend:

 * If the /var/lib/PackageKit/prepared-update file exists then offer
   the user to:
    - _Install updates and restart
    - _Suspend
   rather than just:
    - _Suspend
 * If the user clicks "Install updates and restart" then execute
   pkexec /usr/libexec/pk-trigger-offline-update and then restart the
   computer the usual way.

The next reboot will happen, the updates will be installed, and the
computer will be restarted again.

The /var/lib/PackageKit/prepared-update file will be automatically
removed if the offline update succeeded, but the offline update will
only be attempted once.

The /var/lib/PackageKit/offline-update-competed file will be created
once the pk-offline-update program has been run by systemd. For failure,
the file will contain the following:

"""
[PackageKit Offline Update Results]
Success=false
ErrorCode=missing-gpg-signature
ErrorDetails="The GPG signature 0xDEADBEEF is not installed"
"""

For success, the file will contain the following:

"""
[PackageKit Offline Update Results]
Success=true
Packages=upower;0.9.16-1.fc17;x86_64;updates,zif;0.3.0-1.fc17;x86_64;updates
"""

________________________________________________________________________

                    TESTING OFFLINE UPDATES

 *  Ensure powertop and colorhug-client are installed and the system is
    othewise up to date. Ensure PackageKit 0.8.1-3 or later is installed.
 *  Downgrade the powertop and colorhug-client packages on your system so
    that you have exactly two updates when you do "pkcon get-updates"
 *  Download the new powertop update with:
    pkcon --only-download update powertop
 *  Observe there are no PolicyKit dialogs shown as the command completes
 *  Ensure /var/lib/PackageKit/prepared-update contains just the powertop
    package on a single line
    [You can also use 'pkcon offline-get-prepared' to do the same thing]
 *  Download the new colorhug-client update with:
    pkcon --only-download update colorhug-client
 *  Ensure /var/lib/PackageKit/prepared-update contains both powertop and
    colorhug-client on two lines
 *  Run pkexec /usr/libexec/pk-trigger-offline-update and confirm that
    the system-update symlink exists on '/'.
    [You can also use 'pkcon offline-trigger' to do the same thing]
 *  Observe that pkexec ran without showing a PolicyKit dialog
 *  Run sudo PK_OFFLINE_UPDATE_TEST=1 /usr/libexec/pk-offline-update and
    observe that the two updates are applied
 *  Confirm that /var/lib/PackageKit/prepared-update has been deleted
 *  Confirm that /var/lib/PackageKit/offline-update-competed exists
    and no error messages have been logged.
    [You can also use 'pkcon offline-status' to do the same thing]
 *  Downgrade powertop and colorhug-client again, and do:
    pkcon --only-download update powertop colorhug-client
    pkexec /usr/libexec/pk-trigger-offline-update
 *  Reboot, and observe that the updates are applied in the special
    system-update.service with plymouth going from 0% to 100%
 *  Confirm that the system is rebooted into a running system that has
    has the updates applied.
