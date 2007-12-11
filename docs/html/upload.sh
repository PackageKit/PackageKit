USER="hughsie"
SERVER="packagekit.org"
LOCATION="/srv/www/html"

scp img/*.png $USER@$SERVER:/$LOCATION/img/
scp *.html $USER@$SERVER:/$LOCATION/
scp *.css $USER@$SERVER:/$LOCATION/
scp ../docs/pk-reference.html $USER@$SERVER:/$LOCATION/
scp ../docs/pk-*.png $USER@$SERVER:/$LOCATION/

