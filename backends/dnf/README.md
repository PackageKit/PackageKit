DNF PackageKit Backend
----------------------

It uses the following libraries:

 * libdnf : for tieing all the libraries below together
 * librepo : checking and downloading repository metadata
 * rpm : for actually installing the packages on the system

These are some key file locations:

* /var/cache/PackageKit/$releasever/metadata/ : Used to store the repository metadata
* /var/cache/PackageKit/$releasever/metadata/*/packages : Used for cached packages
* /etc/yum.repos.d/ : the hardcoded location for .repo files
* /etc/pki/rpm-gpg : the hardcoded location for the GPG signatures
* $libdir/packagekit-backend/ : location of PackageKit backend objects

Things we haven't yet decided:

* How to access comps data
