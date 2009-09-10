export G_DEBUG=fatal_criticals
sudo touch /etc/PackageKit/PackageKit.conf
sudo G_DEBUG=fatal_criticals gdb --args .libs/lt-packagekitd --verbose --backend=dummy --disable-timer

