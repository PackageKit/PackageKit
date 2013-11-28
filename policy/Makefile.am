
polkit_rulesdir = $(datadir)/polkit-1/rules.d
dist_polkit_rules_DATA =					\
	org.freedesktop.packagekit.rules

polkit_policydir = $(datadir)/polkit-1/actions
dist_polkit_policy_DATA =					\
	org.freedesktop.packagekit.policy

# You will need a recent intltool or the patch from this bug
# http://bugzilla.gnome.org/show_bug.cgi?id=462312
@INTLTOOL_POLICY_RULE@

EXTRA_DIST = org.freedesktop.packagekit.policy.in
DISTCLEANFILES = org.freedesktop.packagekit.policy

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
