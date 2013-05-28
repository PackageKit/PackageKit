
NULL =

if SECURITY_TYPE_POLKIT
polkit_rulesdir = $(datadir)/polkit-1/rules.d
dist_polkit_rules_DATA =					\
	org.freedesktop.packagekit.rules			\
	$(NULL)

polkit_policydir = $(datadir)/polkit-1/actions
dist_polkit_policy_DATA =					\
	org.freedesktop.packagekit.policy			\
	$(NULL)

# You will need a recent intltool or the patch from this bug
# http://bugzilla.gnome.org/show_bug.cgi?id=462312
@INTLTOOL_POLICY_RULE@

EXTRA_DIST = org.freedesktop.packagekit.policy.in
DISTCLEANFILES = org.freedesktop.packagekit.policy
endif

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
