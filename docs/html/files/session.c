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
	const gchar *packages[] = {"openoffice-clipart", "openoffice-clipart-extras", NULL};

	/* init the types system */
	g_type_init ();

	/* get a session bus connection */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);

	/* connect to PackageKit */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Modify");

	/* execute sync method */
	ret = dbus_g_proxy_call (proxy, "InstallPackageNames", &error,
				 G_TYPE_UINT, xid, /* window xid, 0 for none */
				 G_TYPE_STRV, packages,
				 G_TYPE_STRING, "show-confirm-search,hide-finished",
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
	}
	return 0;
}

