## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

TEST_FILES =						\
	test.catalog					\
	pk-client-helper-test.py			\
	pk-spawn-test.sh				\
	pk-spawn-proxy.sh				\
	pk-spawn-test-sigquit.sh			\
	pk-spawn-test-sigquit.py			\
	pk-spawn-test-profiling.sh			\
	pk-spawn-dispatcher.py				\
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


-include $(top_srcdir)/git.mk
