## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

TEST_FILES =						\
	pk-spawn-test.sh				\
	pk-spawn-test-proxy.sh				\
	pk-spawn-test-sigquit.sh			\
	pk-spawn-test-profiling.sh			\
	$(NULL)

EXTRA_DIST =						\
	$(TEST_FILES)					\
	$(NULL)

clean-local:
	rm -f *~

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

