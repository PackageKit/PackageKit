SUBDIRS = test

if BACKEND_TYPE_ALPM
SUBDIRS += alpm
endif

if BACKEND_TYPE_APT
SUBDIRS += apt
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

if BACKEND_TYPE_OPKG
SUBDIRS += opkg
endif

if BACKEND_TYPE_SMART
SUBDIRS += smart
endif

if BACKEND_TYPE_YUM
SUBDIRS += yum
endif

if BACKEND_TYPE_YUM2
SUBDIRS += yum2
endif

if BACKEND_TYPE_PISI
SUBDIRS += pisi
endif

if BACKEND_TYPE_ZYPP
SUBDIRS += zypp
endif

clean-local :
	rm -f *~

