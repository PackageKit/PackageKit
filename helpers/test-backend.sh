echo "start test..."

package_id="gnome-power-manager;2.19.7-0.72.20070828svn.fc7.hughsie;i386;installed"
backend="yum"

echo "get deps $package_id"
./$backend-get-deps.py "gnome-power-manager;2.19.7-0.72.20070828svn.fc7.hughsie;i386;installed"
echo "exitcode=$?"

echo "get description $package_id"
./$backend-get-description.py $package_id
echo "exitcode=$?"

echo "get updates"
./$backend-get-updates.py
echo "exitcode=$?"

echo "refresh cache"
./$backend-refresh-cache.py
echo "exitcode=$?"

echo "remove $package_id"
./$backend-remove.py no $package_id
echo "exitcode=$?"

echo "remove $package_id (already removed)"
./$backend-remove.py no $package_id
echo "exitcode=$?"

echo "install $package_id"
./$backend-install.py $package_id
echo "exitcode=$?"

echo "install $package_id (already installed)"
./$backend-install.py $package_id
echo "exitcode=$?"

echo "search details lm_sensors"
./$backend-search-details.py none lm_sensors
echo "exitcode=$?"

echo "search file gpm-prefs.glade"
./$backend-search-file.py none gpm-prefs.glade
echo "exitcode=$?"

echo "search group system"
./$backend-search-group.py none system
echo "exitcode=$?"

echo "search name power"
./$backend-search-name.py none power
echo "exitcode=$?"

echo "update $package_id"
./$backend-update.py $package_id
echo "exitcode=$?"

echo "update system"
./$backend-update-system.py
echo "exitcode=$?"

