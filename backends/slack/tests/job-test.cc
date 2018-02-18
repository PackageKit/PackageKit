#include "job.h"

using namespace slack;

static void
test_filter_package_installed ()
{
	PkBitfield filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	g_assert_true (filter_package (filters, true));
	g_assert_false (filter_package (filters, false));
}

static void
test_filter_package_not_installed ()
{
	PkBitfield filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	g_assert_true (filter_package (filters, false));
	g_assert_false (filter_package (filters, true));
}

static void
test_filter_package_none ()
{
	PkBitfield filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	g_assert_true (filter_package (filters, false));
	g_assert_true (filter_package (filters, true));
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/slack/filter_package_installed", test_filter_package_installed);
	g_test_add_func ("/slack/filter_package_not_installed", test_filter_package_not_installed);
	g_test_add_func ("/slack/filter_package_none", test_filter_package_none);

	return g_test_run ();
}
