#include <packagekit-glib/packagekit.h>

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	guint i;
	guint length;
	gboolean ret;
	GError *error = NULL;
	const PkPackageObj *obj;
	PkPackageList *list = NULL;
	PkControl *control = NULL;
	PkClient *client = NULL;
	PkBitfield roles;

	/* find out if we can do GetUpdates */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATES)) {
		g_warning ("Backend does not support GetUpdates()");
		goto out;
	}

	/* create a new client instance */
	client = pk_client_new ();

	/* save all the results as we are not using an async callback */
	pk_client_set_use_buffer (client, TRUE, NULL);

	/* block for the results, does not require gtk_main() or anything */
	pk_client_set_synchronous (client, TRUE, NULL);

	/* get the update list (but only return the newest updates) */
	ret = pk_client_get_updates (client, PK_FILTER_ENUM_NEWEST, &error);
	if (!ret) {
		g_warning ("failed: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the buffered package list */
	list = pk_client_get_package_list (client);
	if (list == NULL) {
		g_warning ("failed to get buffered list");
		goto out;
	}

	length = pk_package_list_get_size (list);
	for (i=0; i<length; i++) {
		/* get each package to be updated, and print to the screen */
		obj = pk_package_list_get_obj (list, i);
		g_print ("%i. %s-%s.%s\t%s\n", i, obj->id->name, obj->id->version, obj->id->arch, obj->summary);
	}

out:
	/* free any GObjects we used */
	if (list != NULL)
		g_object_unref (list);
	if (control != NULL)
		g_object_unref (control);
	if (client != NULL)
		g_object_unref (client);
	return 0;
}

