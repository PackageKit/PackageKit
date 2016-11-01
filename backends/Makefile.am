SUBDIRS = test

if BACKEND_TYPE_ALPM
SUBDIRS += alpm
endif

if BACKEND_TYPE_APTCC
SUBDIRS += aptcc
endif

if BACKEND_TYPE_DNF
SUBDIRS += dnf
endif

if BACKEND_TYPE_DUMMY
SUBDIRS += dummy
endif

if BACKEND_TYPE_ENTROPY
SUBDIRS += entropy
endif

if BACKEND_TYPE_HIF
SUBDIRS += hif
endif

if BACKEND_TYPE_KATJA
SUBDIRS += katja
endif

if BACKEND_TYPE_URPMI
SUBDIRS += urpmi
endif

if BACKEND_TYPE_YUM
SUBDIRS += yum
endif

if BACKEND_TYPE_PISI
SUBDIRS += pisi
endif

if BACKEND_TYPE_ZYPP
SUBDIRS += zypp
endif

if BACKEND_TYPE_POLDEK
SUBDIRS += poldek
endif

if BACKEND_TYPE_PORTAGE
SUBDIRS += portage
endif

if BACKEND_TYPE_PORTS
SUBDIRS += ports
endif

if BACKEND_TYPE_NIX
SUBDIRS += nix
endif

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
