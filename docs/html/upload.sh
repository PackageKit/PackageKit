USER="hughsie"
SERVER="packagekit.org"
LOCATION="/srv/www/html"

scp img/*.png $USER@$SERVER:/$LOCATION/img/
scp *.html $USER@$SERVER:/$LOCATION/
scp *.css $USER@$SERVER:/$LOCATION/
scp ../spec/pk-reference.html $USER@$SERVER:/$LOCATION/
scp ../spec/pk-*.png $USER@$SERVER:/$LOCATION/
scp ../api/html/PackageKit-pk-client.html $USER@$SERVER:/$LOCATION/gtk-doc/
scp ../api/html/style.css $USER@$SERVER:/$LOCATION/gtk-doc/


