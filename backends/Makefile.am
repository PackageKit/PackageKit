SUBDIRS = dummy test

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

if BACKEND_TYPE_YUM
SUBDIRS += yum
endif

clean-local :
	rm -f *~

