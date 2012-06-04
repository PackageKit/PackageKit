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
