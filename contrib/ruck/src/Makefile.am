ruckexecdir = $(bindir)
ruckexec_DATA = ruck

rucksupportdir = $(datadir)/ruck
rucksupport_DATA = 		\
	i18n.py	            \
	ruckcommand.py		\
	ruckmain.py		    \
	ruckformat.py		\
	rucklocks.py 		\
	ruckyum.py    		\
	rucktalk.py

ruckcmdsdir = $(rucksupportdir)/commands
ruckcmds_DATA = 			\
	ruckcachecmds.py	    \
	rucktransactcmds.py	\
	ruckpackagecmds.py

EXTRA_DIST = $(rucksupport_DATA) $(ruckcmds_DATA)

install-exec-hook:
	chmod 0755 $(DESTDIR)$(ruckexecdir)/$(ruckexec_DATA)
