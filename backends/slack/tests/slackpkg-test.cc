#include "slackpkg.h"

using namespace slack;

static void
slack_test_slackpkg_construct()
{
	auto slackpkg = new Slackpkg ("some", "mirror", 1, NULL, NULL);

	g_assert_cmpstr(slackpkg->get_name (), ==, "some");
	g_assert_cmpstr(slackpkg->get_mirror (), ==, "mirror");
	g_assert_cmpuint(slackpkg->get_order (), ==, 1);
	g_assert_false (slackpkg->is_blacklisted ("pattern"));

	delete slackpkg;
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/slack/slackpkg/construct", slack_test_slackpkg_construct);

	return g_test_run();
}
