
NULL =

polkitdir = $(datadir)/PolicyKit/policy
polkit_DATA =					\
	packagekit.policy			\
	$(NULL)

EXTRA_DIST =					\
	$(polkit_DATA)				\
	$(NULL)

check:
	$(POLKIT_POLICY_FILE_VALIDATE) $(top_srcdir)/policy/$(polkit_DATA)

clean-local :
	rm -f *~
