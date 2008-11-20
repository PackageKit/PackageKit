#!/bin/sh

echo "Upload!"
#ssh packagekit.org "rm /srv/www/html/packages/*.rpm"
rm -Rf /tmp/repo
mkdir /tmp/repo
cp /home/hughsie/rpmbuild/REPOS/fedora/10/i386/PackageKit-* /tmp/repo
cp /home/hughsie/rpmbuild/REPOS/fedora/10/SRPMS/PackageKit-* /tmp/repo
cp /home/hughsie/rpmbuild/REPOS/fedora/10/i386/gnome-packagekit-* /tmp/repo
cp /home/hughsie/rpmbuild/REPOS/fedora/10/SRPMS/gnome-packagekit-* /tmp/repo
cd /tmp/repo
createrepo -d .
#scp -r * packagekit.org:/srv/www/html/packages/
rsync -avz -e ssh --human-readable --fuzzy --delete-after . hughsie@packagekit.org:/srv/www/html/packages
cd -

