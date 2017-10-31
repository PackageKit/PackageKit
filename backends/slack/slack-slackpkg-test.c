#include "slack-slackpkg.h"

static void
slack_test_slackpkg_construct()
{
	SlackSlackpkg *slackpkg = slack_slackpkg_new("some", "mirror", 1, NULL, NULL);
	GValue value = G_VALUE_INIT;

	g_value_init(&value, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(slackpkg), "name", &value);
	g_assert_cmpstr(g_value_get_string(&value), ==, "some");
	g_value_unset(&value);

	g_value_init(&value, G_TYPE_STRING);
	g_object_get_property(G_OBJECT(slackpkg), "mirror", &value);
	g_assert_cmpstr(g_value_get_string(&value), ==, "mirror");
	g_value_unset(&value);

	g_value_init(&value, G_TYPE_UINT);
	g_object_get_property(G_OBJECT(slackpkg), "order", &value);
	g_assert_cmpuint(g_value_get_uint(&value), ==, 1);
	g_value_unset(&value);

	g_value_init(&value, G_TYPE_REGEX);
	g_object_get_property(G_OBJECT(slackpkg), "blacklist", &value);
	g_assert_null(g_value_get_boxed(&value));
	g_value_unset(&value);

	g_object_unref(slackpkg);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/slack/slackpkg/construct", slack_test_slackpkg_construct);

	return g_test_run();
}
