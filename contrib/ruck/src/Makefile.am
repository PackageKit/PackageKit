#ruckexecdir = $(bindir)
#ruckexec_DATA = ruck

rucksupportdir = $(datadir)/ruck
rucksupport_DATA = 			\
	i18n.py				\
	ruckcommand.py			\
	ruckmain.py			\
	ruckformat.py			\
	ruckyum.py    			\
	rucktalk.py

ruckcmdsdir = $(rucksupportdir)/commands
ruckcmds_DATA = 			\
	ruckcachecmds.py		\
	rucktransactcmds.py		\
	ruckpackagecmds.py

ruckexecdir = $(bindir)
ruckexec_in_files = ruck.in
ruckexec_DATA = $(ruckexec_in_files:.in=)
$(ruckexec_DATA): $(ruckexec_in_files) Makefile
	@sed -e "s|\@DDIR\@|$(datadir)|" -e "s|\@VERSION\@|$(VERSION)|" $< > $@

EXTRA_DIST = $(rucksupport_DATA) $(ruckcmds_DATA) $(ruckexec_in_files)

install-exec-hook:
	chmod 0755 $(DESTDIR)$(ruckexecdir)/$(ruckexec_DATA)

DISTCLEANFILES =			\
	$(ruckexec_DATA)
