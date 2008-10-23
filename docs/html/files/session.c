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
	guint32 xid = 0;
	guint32 timestamp = 0;

	/* init the types system */
	g_type_init ();

	/* get a session bus connection */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);

	/* connect to PackageKit */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit");

	/* execute sync method */
	ret = dbus_g_proxy_call (proxy, "InstallPackageName", &error,
				 G_TYPE_UINT, xid, /* window xid, 0 for none */
				 G_TYPE_UINT, timestamp, /* action timestamp,, 0 for unknown */
				 G_TYPE_STRING, "openoffice-clipart",
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
	}
	return 0;
}

