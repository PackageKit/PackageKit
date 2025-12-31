DNF5 PackageKit Backend
----------------------

It uses the following libraries:

 * libdnf5 : for the actual package management functions
 * rpm : for actually installing the packages on the system

For AppStream data, the libdnf5 AppStream plugin is used.

These are some key file locations:

* /var/cache/PackageKit/$releasever/metadata/ : Used to store the repository metadata
* /var/cache/PackageKit/$releasever/metadata/*/packages : Used for cached packages
* $libdir/packagekit-backend/ : location of PackageKit backend objects

Things we haven't yet decided:

* How to access comps data
