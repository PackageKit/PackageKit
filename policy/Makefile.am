
NULL =

polkitdir = $(datadir)/PolicyKit/policy
polkit_DATA =					\
	packagekit.policy			\
	$(NULL)

EXTRA_DIST =					\
	$(polkit_DATA)				\
	$(NULL)

if SECURITY_TYPE_POLKIT
check:
	$(POLKIT_POLICY_FILE_VALIDATE) $(top_srcdir)/policy/$(polkit_DATA)
endif

clean-local :
	rm -f *~
