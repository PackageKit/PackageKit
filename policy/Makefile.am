
NULL =

polkit_policydir = $(datadir)/PolicyKit/policy
dist_polkit_policy_DATA =					\
	org.freedesktop.packagekit.policy			\
	$(NULL)

# You will need a recent intltool or the patch from this bug
# http://bugzilla.gnome.org/show_bug.cgi?id=462312
@INTLTOOL_POLICY_RULE@

if SECURITY_TYPE_POLKIT
check:
	$(POLKIT_POLICY_FILE_VALIDATE) $(top_srcdir)/policy/$(dist_polkit_policy_DATA)
endif

EXTRA_DIST = org.freedesktop.packagekit.policy.in
DISTCLEANFILES = org.freedesktop.packagekit.policy

clean-local :
	rm -f *~
