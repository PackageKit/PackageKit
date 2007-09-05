#!/bin/sh

#autobuild.sh all PolicyKit
#sudo auto_refresh_from_repo.sh
#autobuild.sh all PolicyKit-gnome
#sudo auto_refresh_from_repo.sh
autobuild.sh all PackageKit force
sudo auto_refresh_from_repo.sh
autobuild.sh all gnome-packagekit force
sudo auto_refresh_from_repo.sh

