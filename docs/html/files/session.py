import sys

import dbus

try:
    bus = dbus.SessionBus()
except dbus.DBusException as e:
    print(f'Unable to connect to dbus: {e}')
    sys.exit(1)
try:
    proxy = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
    iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit.Modify')
    iface.InstallPackageNames(
        dbus.UInt32(0),
        ["openclipart-libreoffice", "openclipart-svg"],
        "show-confirm-search,hide-finished",
    )
except dbus.DBusException as e:
    print(f'Unable to use PackageKit: {e}')
