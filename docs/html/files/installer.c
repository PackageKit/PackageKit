#include <dbus/dbus-glib.h>

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *connection;
	DBusGProxy *proxy;
	GError *error = NULL;
	gboolean ret;

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit");

	/* execute sync method */
	ret = dbus_g_proxy_call (proxy, "InstallPackageName", &error,
				 G_TYPE_STRING, "openoffice-clipart",
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
	}
	return 0;
}

