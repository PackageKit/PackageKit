Hawkey PackageKit Backend
----------------------------------------

This backend is designed to *replace* the yum backend in PackageKit.

It uses the following libraries:

 * librepo : checking and downloading repository metadata
 * hawkey : for depsolving
 * rpm : for actually installing the packages on the system

It also uses a lot of internal glue to hold all the pieces together. These have
mostly been reused from the Zif project, hence all the Hif prefixes everywhere.

These are some key file locations:

* /var/cache/PackageKit/metadata/ : Used to store the repository metadata
* /var/cache/PackageKit/metadata/*/packages : Used for cached packages
* /etc/yum.repos.d/ : the hardcoded location for .repo files
* /etc/pki/rpm-gpg : the hardcoded location for the GPG signatures
* /etc/PackageKit/Hawkey.conf : the hardcoded PackageKit-hawkey config file
* $libdir/packagekit-backend/ : location of PackageKit backend objects

Things we haven't yet decided:

* How to access comps data
