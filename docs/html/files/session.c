#include <gio/gio.h>

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	const gchar *packages[] = {"openoffice-clipart",
				   "openoffice-clipart-extras",
				   NULL};
	GDBusProxy *proxy = NULL;
	GError *error = NULL;
	guint32 xid = 0;
	GVariant *retval = NULL;

	/* init the types system */
	g_type_init ();

	/* get a session bus proxy */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_NONE, NULL,
					       "org.freedesktop.PackageKit",
					       "/org/freedesktop/PackageKit",
					       "org.freedesktop.PackageKit.Modify",
					       NULL, &error);
	if (proxy == NULL) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the window ID, or use 0 for non-modal */
	//xid = GDK_WINDOW_XID (gtk_widget_get_window (dialog));

	/* issue the sync request */
	retval = g_dbus_proxy_call_sync (proxy,
					 "InstallPackageNames",
					 g_variant_new ("(u^a&ss)",
							xid,
							packages,
							"hide-finished"),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1, /* timeout */
					 NULL, /* cancellable */
					 &error);
	if (retval == NULL) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (proxy != NULL)
		g_object_unref (proxy);
	if (retval != NULL)
		g_object_unref (retval);
	return 0;
}

