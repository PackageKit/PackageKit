echo "start test..."

#package_id="gnome-power-manager;2.19.7-0.72.20070828svn.fc7.hughsie;i386;installed"
package_id="BasiliskII;1.0-0.20060501.1.fc7;x86_64;freshrpms"

backend="yum"

echo "get deps $package_id"
./$backend/helpers/get-deps.py $package_id 
echo "exitcode=$?"

echo "get description $package_id"
./$backend/helpers/get-details.py $package_id
echo "exitcode=$?"

echo "get updates"
./$backend/helpers/get-updates.py
echo "exitcode=$?"

echo "refresh cache"
./$backend/helpers/refresh-cache.py
echo "exitcode=$?"

echo "remove $package_id"
./$backend/helpers/remove.py no $package_id
echo "exitcode=$?"

echo "remove $package_id (already removed)"
./$backend/helpers/remove.py no $package_id
echo "exitcode=$?"

echo "install $package_id"
./$backend/helpers/install.py $package_id
echo "exitcode=$?"

echo "install $package_id (already installed)"
./$backend/helpers/install.py $package_id
echo "exitcode=$?"

echo "search details lm_sensors"
./$backend/helpers/search-details.py none lm_sensors
echo "exitcode=$?"

echo "search file gpm-prefs.glade"
./$backend/helpers/search-file.py none gpm-prefs.glade
echo "exitcode=$?"

echo "search group system"
./$backend/helpers/search-group.py none system
echo "exitcode=$?"

echo "search name power"
./$backend/helpers/search-name.py none power
echo "exitcode=$?"

echo "update $package_id"
./$backend/helpers/update.py $package_id
echo "exitcode=$?"

echo "update system"
./$backend/helpers/update-system.py
echo "exitcode=$?"
