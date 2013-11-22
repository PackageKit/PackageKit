SUBDIRS = test

if BACKEND_TYPE_ALPM
SUBDIRS += alpm
endif

if BACKEND_TYPE_APT
SUBDIRS += apt
endif

if BACKEND_TYPE_APTCC
SUBDIRS += aptcc
endif

if BACKEND_TYPE_BOX
SUBDIRS += box
endif

if BACKEND_TYPE_CONARY
SUBDIRS += conary
endif

if BACKEND_TYPE_DUMMY
SUBDIRS += dummy
endif

if BACKEND_TYPE_ENTROPY
SUBDIRS += entropy
endif

if BACKEND_TYPE_HAWKEY
SUBDIRS += hawkey
endif

if BACKEND_TYPE_OPKG
SUBDIRS += opkg
endif

if BACKEND_TYPE_SLAPT
SUBDIRS += slapt
endif

if BACKEND_TYPE_SMART
SUBDIRS += smart
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

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
